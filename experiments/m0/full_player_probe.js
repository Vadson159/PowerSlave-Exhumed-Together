/* M0-C1: version-locked experimental Player initialization adapter.
   NOT original engine source. No memcpy of live objects and no writes to engine-owned pointer fields.
   Owned storage is initialized from independently verified constructor stores. Native Reset and Spawn
   perform inventory setup, handle refcounting and owner binding. Storage stays pinned until process exit.
   Runtime constructor witness must pass before this script may be generated/loaded. */
'use strict';
const m=Process.mainModule,base=m.base,local=base.add(0xaf33e0),gi=base.add(0xaf33a0);
if(Process.id!==cfg.pid||m.path.toLowerCase()!==cfg.target.toLowerCase()||!cfg.constructor_witness_passed||
 cfg.binary_sha256!=='b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d')
 throw new Error('C target/witness mismatch');
function prefix(name,rva){const s=cfg.native_prefixes?.[name]||cfg.sites?.[name];
 if(!s||s.rva!==rva||typeof s.prefix!=='string'||s.prefix.length!==32)throw new Error('Missing exact-build prefix: '+name);
 const actual=Array.from(new Uint8Array(base.add(rva).readByteArray(16)),x=>x.toString(16).padStart(2,'0')).join('');
 if(actual!==s.prefix)throw new Error('Native prefix mismatch: '+name);}
[['set_target',0x1d35d0],['set_source_owner',0x1a17c0],['dynamic_light_factory',0x1d50b0],['impact',0x1e6510],['remove',0x1a02b0],
 ['reset',0x1dfa70],['weapon_ready',0x20f9e0],['factory',0x1d4780]].forEach(([n,r])=>prefix(n,r));
if(cfg.death_test||cfg.host_death_test||cfg.inflight_respawn_test){prefix('damage',0x1a1370);prefix('puppet_damage',0x1dbd00);}
if(cfg.projectile_test)prefix('weapon_pickup_touch',0x1d8470);
if(cfg.context_world_exit_test){prefix('actor_destructor',0x19f210);prefix('native_shutdown',0xfcc20);}
if(cfg.physics_test||cfg.inflight_respawn_test)prefix('collision',0x1a4990);
if(cfg.death_test&&(!cfg.controller_test||!cfg.weapon_ready||cfg.enemy_test))throw new Error('Death probe requires controller/weapon-ready and no enemy fixture');
if(cfg.projectile_test&&(!cfg.controller_test||!cfg.weapon_ready||cfg.enemy_test||cfg.death_test))throw new Error('Projectile probe requires its own controller/weapon-ready mode');
// Shipped defs/defines.txt, defs/actors/items.txt and weapon animations; native touch grants the selected slot.
const projectileWeapons={pistol:{name:'pistol',pickup_type:42,slot:1,kind:'hitscan',max_shots:3},
 cobra:{name:'cobra',pickup_type:46,slot:5,kind:'projectile',max_shots:cfg.projectile_target==='wall'?2:1},
 amun:{name:'amun',pickup_type:44,slot:3,kind:'projectile',max_shots:1}};
const projectileWeaponName=cfg.projectile_weapon||'pistol';
const projectileWeapon=cfg.projectile_test&&Object.prototype.hasOwnProperty.call(projectileWeapons,projectileWeaponName)?projectileWeapons[projectileWeaponName]:null;
if(cfg.projectile_test&&!projectileWeapon)throw new Error('Unknown bounded projectile weapon fixture');
const p2storage=Memory.alloc(0x198+32+15),p2=p2storage.add(31).and(ptr('0xfffffffffffffff0')),vt=base.add(0x5137c8);
// Guards and zero/default member initialization are confined to our own allocation.
p2storage.writeByteArray(new Uint8Array(0x198+32+15).fill(0xa5));p2.writeByteArray(new Uint8Array(0x198));
if(!p2.and(15).isNull())throw new Error('Secondary Player alignment failed');
p2.add(0x80).writeU32(0xffffffff);p2.add(0xe0).writePointer(p2);
p2.add(0x160).writePointer(vt);p2.add(0x178).writePointer(vt);
// Separate post-constructor ready bindings: native GI init and C01 ready_interpolants prove these
// exact this-relative references. Their missing initialization caused the recorded m_pRefValue assert.
// Do not register this experimental non-presented context in the singleton render interpolant list.
p2.add(0x168).writePointer(p2.add(0x13c));p2.add(0x180).writePointer(p2.add(0x134));
function native(r,ret,args){return new NativeFunction(base.add(r),ret,args,{abi:'win64',traps:'all'});}
const reset=native(0x1dfa70,'void',['pointer']),spawn=native(0x1d4780,'pointer',['pointer','int','pointer','float','int']);
const remove=native(0x1a02b0,'void',['pointer']);
// DOC34_NATIVE_RELEASE_ONLY: no target/sourceOwner setter calls in this probe.
const lethalDamage=cfg.death_test||cfg.host_death_test?native(0x1a1370,'void',['pointer','pointer','int','int']):null;
// Native non-null SetTarget is used once to assign this experiment's AI fixture;
// it is never used to clear a holder or repair retirement counts.
const assignFixtureTarget=cfg.host_death_test?native(0x1d35d0,'void',['pointer','pointer']):null;
// References are observed until their native holder releases them; pending is a valid state.
let scope=0,created=null,node2=null,original=null,originalNode=null,stage='armed',frames=0,samples=0,updates=0;
let controller=null,weaponEvents={p1:0,p2:0},meleeEvents={p1:0,p2:0},collisionEvents={p1:0,p2:0};
let enemy=null,enemyNode=null,enemyFrames=0,enemyAttackedAt=null,enemyHit=null,lastAim=null,currentMelee=null;
let pickupIdentity=null,pickupNode=null;
let p1Identity=null,p2Identity=null,enemyIdentity=null,cleanupStarted=false,nativeOutcomeUnknown=false,retireFrames=0;
let cycle=1,cycleInitial=null,deathProbe=null,deathPassed=false,recreatePassed=false;
let hostProbe=null,hostInitial=null,hostPuppetTicks=0,hostAiTicks=0;
const nativeCalls={},referenceCandidates=[],ownedControllers=[],completedCycles=[],sourceOwnerReleases=[],retiredPuppets=[],lastNativeDamage={};let referenceOverflow=false;
const projectileProbe=cfg.projectile_test?{weapon:projectileWeapon,phase:'clear',phase_frame:0,shots:0,last_shot:-1000,switch_pulses:0,
 pickup_observed:false,ammo_observed:false,hit_observed:false,p2_projectile_observed:false,ai_projectile_observed:false,
 ownership_witnesses:[],last_witness_frame:0,stop_reason:null}:null;
function event(kind,data){send(Object.assign({kind,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},cfg.death_test?{cycle}:{},data));}
const hostGate=cfg.host_death_test||cfg.manual_respawn_test?createHostDeathGate(base,cfg,event):null;
function simulationThread(){return Process.getCurrentThreadId()===cfg.simulation_tid;}
function gameplayState(){return gi.add(0x24).readS32()===3&&gi.add(0x28).readS32()===0;}
function identity(actor,node,kind){return {actor,node,kind,invalid:false,pool_return_seen:false};}
function nodeView(node){if(!node||node.isNull())return {node:'0x0',payload:'0x0',refs:null};
 try{return {node:node.toString(),payload:node.add(0x28).readPointer().toString(),refs:node.add(0x40).readS32()};}
 catch(e){return {node:node.toString(),read_error:String(e)};}}
function live(rec){if(!rec||rec.invalid||rec.actor.isNull()||rec.node.isNull())return false;
 const n=nodeView(rec.node);return n.payload===rec.actor.toString()&&rec.actor.add(0x10).readPointer().equals(rec.node);}
function playerActor(a){for(const [rec,which,p] of [[p1Identity,'p1',local],[p2Identity,'p2',p2]])
 if(rec&&a.equals(rec.actor)&&live(rec))return {rec,which,player:p};return null;}
function trackedActor(a){const p=playerActor(a);if(p)return p.rec;
 return enemyIdentity&&a.equals(enemyIdentity.actor)&&live(enemyIdentity)?enemyIdentity:null;}
function inventory(p){return {current:p.add(0x88).readS32(),pending:p.add(0x8c).readS32(),
 owned:Array.from({length:8},(_,i)=>p.add(0xcc+i).readU8()),counters:Array.from({length:8},(_,i)=>p.add(0xbc+2*i).readU16())};}
function sameInventory(a,b){return JSON.stringify(a.owned)===JSON.stringify(b.owned)&&JSON.stringify(a.counters)===JSON.stringify(b.counters);}
function sourcePlayer(a){const direct=playerActor(a);if(direct)return {player:direct.player.toString(),via:'direct_player_actor'};
 for(const old of retiredPuppets)if(a.equals(old.actor)&&live(old))return {player:p2.toString(),via:'retired_player_actor',node:old.node.toString(),generation:old.generation};
 const rec=referenceCandidates.find(r=>!r.invalid&&r.actor.equals(a));if(!rec||!live(rec))return null;
 // Actor target+28 may point at a victim. Only its sourceOwner+148 attributes a projectile attacker here.
 const node=(nativeClass(rec.actor)==='projectile'||rec.seen_impact)?rec.actor.add(0x148).readPointer():null;
 if(!node)return null;for(const [r,p] of [[p1Identity,local],[p2Identity,p2]])if(r&&r.node.equals(node)&&live(r))
  return {player:p.toString(),via:'actor_source_owner',node:node.toString(),generation:rec.generation};
 for(const old of retiredPuppets)if(old.node.equals(node)&&live(old))return {player:p2.toString(),via:'retired_player_generation_source_owner',node:node.toString(),player_generation:old.generation,projectile_generation:rec.generation};return null;}
function enemyGraph(){if(!live(enemyIdentity))return null;const a=enemyIdentity.actor;return {actor:a.toString(),node:enemyIdentity.node.toString(),
 health:a.add(0x17a).readS16(),type:a.add(0x170).readPointer().readS32(),vtable:a.readPointer().toString(),
 target_node:a.add(0x28).readPointer().toString(),origin:[0x18,0x1c,0x20].map(o=>a.add(o).readFloat()),yaw:a.add(0x38).readFloat(),sector:a.add(0x3c).readS32()};}
function graph(p){const n=p.add(0xd8).readPointer(),v=nodeView(n),rec=p.equals(local)?p1Identity:p2Identity;
 const valid=!!rec&&rec.node.equals(n)&&live(rec),a=valid?rec.actor:ptr(0);return {player:p.toString(),node:n.toString(),actor:a.toString(),live_identity:valid,
 refs:v.refs,generation:rec?.generation??null,owner:valid?a.add(0x1b8).readPointer().toString():null,
 health:valid?a.add(0x17a).readS16():null,origin:valid?[0x18,0x1c,0x20].map(o=>a.add(o).readFloat()):null,
 yaw:valid?a.add(0x38).readFloat():null,velocity:valid?[0xe0,0xe4,0xe8].map(o=>a.add(o).readFloat()):null,
 sector:valid?a.add(0x3c).readS32():null,puppet_flags:valid?a.add(0x1c0).readU32():null,death_timer:p.add(0xa8).readU16(),
 weapon_animation:p.add(0xe8).readPointer().toString(),weapon_state:p.add(0xf0).readS32(),buttons:p.readU32()};}
function guards(){return [p2.sub(16),p2.add(0x198)].every(a=>Array.from(new Uint8Array(a.readByteArray(16))).every(x=>x===0xa5));}
function nativeKey(name){return cfg.death_test?'cycle'+cycle+'.'+name:name;}
function callOnce(name,fn){name=nativeKey(name);if(!simulationThread()||!gameplayState())throw new Error('Native lifecycle call outside safe gameplay tick');
 if(nativeCalls[name])throw new Error('Refusing repeated native lifecycle operation: '+name);
 nativeCalls[name]={outcome:'attempted',utc:new Date().toISOString()};event('C_native_call_attempt',{operation:name});
 try{const result=fn();nativeCalls[name].outcome='returned';return result;}
 catch(e){nativeCalls[name].outcome='unknown';nativeCalls[name].error=String(e);nativeOutcomeUnknown=true;throw e;}}
const factory=base.add(0x5a10a0);
function prepareController(){const c=createControllerAdapter(base,local,p2,{preserveLocalInput:!cfg.inflight_respawn_test&&(!!cfg.enemy_test||!!cfg.death_test||!!cfg.projectile_test||!!cfg.host_death_test),
 simulationTid:cfg.simulation_tid,prefixes:cfg.native_prefixes,gameState:gi.add(0x24),skipDeadSecondary:!!cfg.death_test||!!cfg.projectile_test||!!cfg.host_death_test});
 ownedControllers.push(c);return c;}
if(cfg.controller_test)controller=prepareController();
Interceptor.attach(base.add(0x20f760),{onEnter(args){if(!simulationThread()||!gameplayState())return;const p=args[0].readPointer();if(!p.equals(local)&&!p.equals(p2))return;
 const which=p.equals(p2)?'p2':'p1';if(++weaponEvents[which]<=12)event('C_weapon_change',{which,weapon_context:args[0].toString(),owner:p.toString(),selector:args[1].toString()});}});
Interceptor.attach(base.add(0x199710),{onEnter(args){if(!simulationThread()||!gameplayState())return;const found=playerActor(args[1]);if(!found)return;
 const which=found.which;meleeEvents[which]++;this.tracked=true;this.savedMelee=currentMelee;currentMelee={which,actor:args[1].toString(),owner:args[1].add(0x1b8).readPointer().toString()};
 event('C_melee_action',{...currentMelee,action:args[0].toString(),enemy:enemyGraph()});
},onLeave(){if(this.tracked)currentMelee=this.savedMelee;}});
Interceptor.attach(base.add(0x1a4990),{onEnter(args){if(!simulationThread()||!gameplayState())return;const found=playerActor(args[1]);if(!found)return;
 const which=found.which;collisionEvents[which]++;
 if(collisionEvents[which]<=2)event('C_collision_call',{which,scratch:args[0].toString(),actor:args[1].toString(),vector:args[2].toString()});}});
Interceptor.attach(base.add(0x1a1370),{onEnter(args){if(!simulationThread()||!gameplayState())return;this.record=trackedActor(args[0]);if(!this.record)return;
 this.victim=args[0];this.before=this.victim.add(0x17a).readS16();this.source=args[1];this.type=args[2].toInt32();this.amount=args[3].toInt32();
 const owner=sourcePlayer(this.source);this.sourceOwner=owner?owner.player:null;this.sourceAttribution=owner;
 this.melee=currentMelee;this.caller=this.returnAddress.toString();
},onLeave(){if(!this.victim||!gameplayState()||!live(this.record))return;const after=this.victim.add(0x17a).readS16(),data={victim:this.victim.toString(),source:this.source.toString(),type:this.type,amount:this.amount,before:this.before,after,
 melee:this.melee,native_caller:this.caller,source_owner:this.sourceOwner,source_attribution:this.sourceAttribution};
 event('C_damage_observed',data);
 lastNativeDamage[data.victim]=data;
 if(this.record===enemyIdentity&&live(p2Identity)&&(this.source.equals(p2Identity.actor)||cfg.projectile_test&&this.sourceOwner===p2.toString())&&after<this.before){
  enemyHit={...data,frame:frames};event('C_enemy_hit',enemyHit);
  if(projectileProbe&&inventory(p2).current===projectileWeapon.slot){projectileProbe.hit_observed=true;event('C_projectile_mode_weapon_hit',{...enemyHit,weapon:projectileWeapon.name});}
 }
}});
if(cfg.death_test)Interceptor.attach(base.add(0x1dbd00),{onEnter(args){
 if(!simulationThread()||!gameplayState()||!live(p2Identity)||!args[0].equals(p2Identity.actor))return;
 this.record=p2Identity;this.before={health:args[0].add(0x17a).readS16(),flags:args[0].add(0x1c0).readU32()};
},onLeave(){if(!this.record||!gameplayState()||!live(this.record))return;const a=this.record.actor;
 event('C_secondary_puppet_on_damage',{actor:a.toString(),native_rva:'0x1dbd00',before:this.before,
  after:{health:a.add(0x17a).readS16(),flags:a.add(0x1c0).readU32()},owner:a.add(0x1b8).readPointer().toString()});
}});
if(cfg.projectile_test)Interceptor.attach(base.add(0x1d8470),{onEnter(args){
 if(!simulationThread()||!gameplayState()||!live(pickupIdentity)||!args[0].equals(pickupIdentity.actor))return;
 const toucher=playerActor(args[1]);if(!toucher)return;this.toucher=toucher.which;this.owner=toucher.player.toString();
 this.before={p1:inventory(local),p2:inventory(p2)};
},onLeave(){if(!this.before||!gameplayState()||!live(p1Identity)||!live(p2Identity))return;
 const after={p1:inventory(local),p2:inventory(p2)},slot=projectileWeapon.slot;
 const granted=after.p2.owned[slot]===1&&(this.before.p2.owned[slot]!==1||after.p2.counters[slot]>this.before.p2.counters[slot]);
 const independent=sameInventory(this.before.p1,after.p1)&&this.before.p1.current===after.p1.current&&this.before.p1.pending===after.p1.pending;
 event('C_weapon_fixture_touch_observed',{native_rva:'0x1d8470',toucher:this.toucher,owner:this.owner,before:this.before,after,granted,primary_inventory_unchanged:independent});
 if(this.toucher==='p2'&&this.owner===p2.toString()&&granted&&independent){projectileProbe.pickup_observed=true;
  event('C_secondary_weapon_pickup_passed',{weapon:projectileWeapon,owned_byte:'0x'+(0xcc+slot).toString(16),counter_offset:'0x'+(0xbc+2*slot).toString(16),before:this.before,after});}
}});
if(cfg.enemy_test)Interceptor.attach(base.add(0x1a78c0),{onEnter(args){if(!simulationThread()||!gameplayState()||!live(p2Identity)||!args[1].equals(p2Identity.actor)||!currentMelee)return;
 event('C_melee_ray',{actor:args[1].toString(),start:[0,4,8].map(o=>args[2].add(o).readFloat()),end:[0,4,8].map(o=>args[3].add(o).readFloat()),enemy:enemyGraph()});}});
if(!factory.readPointer().add(0x18).readPointer().equals(base.add(0x1d4780)))throw new Error('Factory slot mismatch');
// C01/v1 showed JS onLeave resolver callbacks suppressed in our nested NativeFunction call.
// Use a native direct replacement for this five-byte leaf. All control data is adapter-owned.
const resolverState=Memory.alloc(24);resolverState.writeU32(0);resolverState.add(4).writeU32(cfg.simulation_tid);
resolverState.add(8).writePointer(p2);resolverState.add(16).writeU32(0);
const resolverModule=new CModule(`
 typedef unsigned int U32;
 extern unsigned char resolver_state[];
 extern U32 current_tid(void);
 extern void *engine_getplayer(void *instance);
 void *probe_resolver(void *instance) { return engine_getplayer(instance); }
 void *select_player(void *instance) {
   if (*(volatile U32 *)resolver_state && current_tid()==*(U32 *)(resolver_state+4)) {
     (*(U32 *)(resolver_state+16))++;
     return *(void **)(resolver_state+8);
   }
   return (unsigned char *)instance+0x40;
 }
`,{resolver_state:resolverState,current_tid:Module.getGlobalExportByName('GetCurrentThreadId'),engine_getplayer:base.add(0x1b5520)});
// C01/v1-v4: nested invocation suppresses standard Interceptor callbacks/replacement;
// replaceFast cannot relocate this short leaf. Use a verified five-byte rel32 diagnostic redirect
// installed/restored ONLY at the observed simulation-thread safe point. No adjacent bytes touched.
// This is a version-locked in-memory code hook in the disposable copy, not an object pointer write.
const getterEntry=base.add(0x1b5520),getterBytes=getterEntry.readByteArray(5);
if(Array.from(new Uint8Array(getterBytes),x=>x.toString(16).padStart(2,'0')).join('')!=='488d4140c3')
 throw new Error('Getter five-byte leaf mismatch; refusing patch');
const bridge=Memory.alloc(Process.pageSize,{near:getterEntry,maxDistance:0x70000000});
Memory.protect(bridge,Process.pageSize,'rwx');
Memory.patchCode(bridge,32,code=>{const w=new X86Writer(code,{pc:bridge});w.putJmpAddress(resolverModule.select_player);w.flush();});
Memory.protect(bridge,Process.pageSize,'r-x');
const delta=bridge.sub(getterEntry.add(5)).toInt32(),redirect=[0xe9,delta&255,(delta>>>8)&255,(delta>>>16)&255,(delta>>>24)&255];
let redirectInstalled=false;
function installResolver(){if(!simulationThread())throw new Error('Getter install outside simulation thread');
 if(Array.from(new Uint8Array(getterEntry.readByteArray(5))).some((b,i)=>b!==new Uint8Array(getterBytes)[i]))throw new Error('Getter changed before install');
 Memory.patchCode(getterEntry,5,code=>code.writeByteArray(redirect));redirectInstalled=true;}
function restoreResolver(){if(!simulationThread())throw new Error('Getter restore outside simulation thread');
 if(redirectInstalled){const actual=Array.from(new Uint8Array(getterEntry.readByteArray(5)));
  if(!actual.every((b,i)=>b===redirect[i]))throw new Error('Getter redirect changed; refusing to overwrite another hook');
  Memory.patchCode(getterEntry,5,code=>code.writeByteArray(getterBytes));redirectInstalled=false;event('C_resolver_restored',{entry:getterEntry.toString()});}}
const getPlayer=new NativeFunction(resolverModule.probe_resolver,'pointer',['pointer'],{abi:'win64',traps:'all'});
Process.setExceptionHandler(d=>{event('C_native_exception',{type:d.type,address:d.address.toString(),
 memory:d.memory?{operation:d.memory.operation,address:d.memory.address.toString()}:null,
 rip:d.context.rip.toString(),rcx:d.context.rcx.toString(),rdx:d.context.rdx.toString(),r8:d.context.r8.toString(),
 trace:Thread.backtrace(d.context,Backtracer.ACCURATE).slice(0,12).map(x=>({address:x.toString(),symbol:DebugSymbol.fromAddress(x).toString()}))});return false;});
function resolverEnter(){scope++;resolverState.writeU32(scope);}
function resolverLeave(){scope--;resolverState.writeU32(scope);}
event('C_native_resolver_prepared',{entry:base.add(0x1b5520).toString(),replacement:resolverModule.select_player.toString(),bridge:bridge.toString(),
 policy:'Only observed simulation TID during explicit native Spawn; otherwise exact native this+0x40 leaf semantics'});
Interceptor.attach(base.add(0x1db8a0),{onEnter(args){if(simulationThread()&&gameplayState()&&live(p2Identity)&&args[0].equals(p2Identity.actor)){
 updates++;if(updates<4)event('C_secondary_puppet_tick',{ordinal:updates,actor:p2Identity.actor.toString(),owner:p2Identity.actor.add(0x1b8).readPointer().toString()});
}if(cfg.host_death_test&&simulationThread()&&gameplayState()&&live(p1Identity)&&args[0].equals(p1Identity.actor))hostPuppetTicks++;}});
if(cfg.host_death_test){prefix('target_ai_tick',0x194ce0);Interceptor.attach(base.add(0x194ce0),{onEnter(args){
 if(simulationThread()&&gameplayState()&&live(enemyIdentity)&&args[0].equals(enemyIdentity.actor))hostAiTicks++;
}});}
function candidate(actor,kind,type=null){if(actor.isNull())return null;const node=actor.add(0x10).readPointer();
 if(node.isNull()||nodeView(node).payload!==actor.toString())return null;
 const old=referenceCandidates.find(r=>!r.invalid&&r.actor.equals(actor)&&r.node.equals(node));if(old)return old;
 if(referenceCandidates.length>=64){if(!referenceOverflow){referenceOverflow=true;event('C_reference_tracking_limit',{limit:64,coverage:'partial'});}return null;}
 const rec=identity(actor,node,kind);rec.type=type;rec.generation=referenceCandidates.length+1;rec.seen_impact=false;referenceCandidates.push(rec);return rec;}
// STATIC identity correction: 1d50b0 initially binds DynLight; it is not a projectile factory.
const projectileVtable=base.add(cfg.native_classes.projectile_vtable_rva),dynlightVtable=base.add(cfg.native_classes.dynlight_vtable_rva);
function nativeClass(a){const v=a.readPointer();return v.equals(projectileVtable)?'projectile':v.equals(dynlightVtable)?'dynamic_light':null;}
function referenceView(rec){if(!live(rec))return {kind:rec.kind,actor:rec.actor.toString(),invalid:true,node:nodeView(rec.node)};
 const kind=nativeClass(rec.actor),handle=rec.actor.add(0x28).readPointer();
 const actorLayout=kind==='projectile'||rec.seen_impact;
 const source=actorLayout?rec.actor.add(0x148).readPointer():null;
 const owned=[p2Identity,enemyIdentity,...retiredPuppets].filter(Boolean).map(r=>r.node.toString());
 return {kind:rec.kind,native_class:kind,type:rec.type,impact_type:rec.impact_type,impact_definition:rec.impact_definition,impact_vtable:rec.impact_vtable,
  vtable:rec.actor.readPointer().toString(),actor:rec.actor.toString(),node:nodeView(rec.node),
  handle_field:kind==='dynamic_light'?'dynamic_light_source_at_28':'target_node_at_28',handle_node:handle.toString(),
  source_owner_node:source?.toString(),holds_fixture:owned.includes(handle.toString())||!!source&&owned.includes(source.toString())};}
function fixtureHolders(){return referenceCandidates.map(referenceView).filter(r=>r.holds_fixture);}
function observeProjectileReference(rec,view){if(!projectileProbe||!live(rec)||!(view.native_class==='projectile'||rec.seen_impact))return;
 const source=view.source_owner_node;if(!source)return;
 const owner=source===p2Identity?.node.toString()?'p2':source===enemyIdentity?.node.toString()?'fixture_enemy':null;if(!owner)return;
 if(projectileProbe.ownership_witnesses.some(w=>w.generation===rec.generation&&w.owner===owner))return;
 const witness={owner,generation:rec.generation,frame:frames,reference:view,passive:true};projectileProbe.ownership_witnesses.push(witness);projectileProbe.last_witness_frame=frames;
 if(owner==='p2')projectileProbe.p2_projectile_observed=true;else projectileProbe.ai_projectile_observed=true;
 event('C_projectile_ownership_witness',witness);
}
function holderEdges(a,kind){const edges={target_or_source_at_28:a.add(0x28).readPointer().toString()};
 if(kind==='projectile'){edges.source_owner_at_148=a.add(0x148).readPointer().toString();edges.node_at_1c0=a.add(0x1c0).readPointer().toString();}
 return edges;}
function edgeRefs(edges){return Object.fromEntries(Object.entries(edges).map(([name,value])=>[name,value==='0x0'?null:nodeView(ptr(value))]));}
function watchNativeHolderPhase(rva,name,requiredClass){
 prefix(name,rva);
 Interceptor.attach(base.add(rva),{onEnter(args){
  if(!simulationThread()||!gameplayState()||!['alive','retiring','retirement_pending'].includes(stage))return;
  try{
   const kind=nativeClass(args[0]);if(kind!==requiredClass)return;
   const before=holderEdges(args[0],kind),known=[p2Identity,enemyIdentity,...retiredPuppets,...referenceCandidates].filter(Boolean).map(r=>r.node.toString());
   if(!Object.values(before).some(node=>node!=='0x0'&&known.includes(node)))return;
   this.saved={actor:args[0],kind,before,refs:edgeRefs(before),vtable:args[0].readPointer().toString()};
  }catch(e){referenceOverflow=true;event('C_lifetime_observer_read_error',{phase:name,error:String(e),passive:true});}
 },onLeave(){
  if(!this.saved)return;
  try{
   const q=this.saved,after=holderEdges(q.actor,q.kind),afterRefs=edgeRefs(q.before);
   const data={phase:name,native_rva:'0x'+rva.toString(16),actor:q.actor.toString(),native_class:q.kind,entry_vtable:q.vtable,
    before:q.before,after,before_refs:q.refs,after_refs:afterRefs,frame:frames,passive:true,native_calls_in_observer:false};
   event('C_native_holder_phase',data);
   if(q.kind==='projectile'&&q.before.source_owner_at_148!=='0x0'&&after.source_owner_at_148==='0x0'){
    const before=q.refs.source_owner_at_148,now=afterRefs.source_owner_at_148;
    const release={...data,source_node:q.before.source_owner_at_148,ref_delta:before?.refs!==undefined&&now?.refs!==undefined?now.refs-before.refs:null,
     note:'Field cleared by the observed native function; no adapter setter/refcount write. Other native edges may also change inside this function.'};
    sourceOwnerReleases.push(release);if(projectileProbe)projectileProbe.last_witness_frame=frames;event('C_source_owner_natural_release',release);
   }
  }catch(e){referenceOverflow=true;event('C_lifetime_observer_read_error',{phase:name,error:String(e),passive:true});}
 }});
}
if(cfg.enemy_test||cfg.projectile_test){
 if(cfg.reference_policy!=='DOC34_NATIVE_RELEASE_ONLY')throw new Error('Passive reference policy required');
 if(cfg.projectile_test){prefix('projectile_tick',0x1e4b10);Interceptor.attach(base.add(0x1e4b10),{onEnter(args){
  if(!simulationThread()||!gameplayState()||!['alive','retiring','retirement_pending'].includes(stage)||nativeClass(args[0])!=='projectile')return;
  const rec=candidate(args[0],'projectile_tick');if(rec)observeProjectileReference(rec,referenceView(rec));
 }});}
 Interceptor.attach(base.add(0x1d4780),{onEnter(args){this.observe=simulationThread()&&gameplayState()&&['constructing','alive','retiring'].includes(stage);this.type=args[1].toInt32();},
  onLeave(ret){if(this.observe&&gameplayState()){const rec=candidate(ret,'actor_factory',this.type);if(rec&&nativeClass(ret)==='projectile'){const view=referenceView(rec);observeProjectileReference(rec,view);event('C_projectile_actor_factory_observed',{reference:view,passive:true});}}}});
 Interceptor.attach(base.add(0x1d50b0),{onEnter(args){this.observe=simulationThread()&&gameplayState()&&!!trackedActor(args[1]);this.source=args[1].toString();},
  onLeave(ret){if(!this.observe||!gameplayState()||ret.isNull())return;
   if(nativeClass(ret)!=='dynamic_light'){referenceOverflow=true;event('C_native_class_mismatch',{site_rva:'0x1d50b0',actual_vtable:ret.readPointer().toString(),expected:dynlightVtable.toString()});return;}
   const rec=candidate(ret,'dynamic_light');if(rec)event('C_dynamic_light_factory_observed',{source_actor:this.source,reference:referenceView(rec),
    live_constructor:base.add(cfg.native_classes.dynlight_ctor_field_rva).readPointer().toString(),passive:true,not_projectile_evidence:true});}});
 Interceptor.attach(base.add(0x1e6510),{onEnter(args){if(!simulationThread()||!gameplayState()||!['alive','retiring'].includes(stage))return;
  const rec=candidate(args[0],'actor_impact');if(rec&&nativeClass(rec.actor)!=='dynamic_light'&&live(rec)){rec.seen_impact=true;
   const definition=rec.actor.add(0x170).readPointer();rec.impact_definition=definition.toString();
   rec.impact_type=definition.isNull()?null:definition.readS32();rec.impact_vtable=rec.actor.readPointer().toString();
   const view=referenceView(rec);observeProjectileReference(rec,view);if(view.holds_fixture)event('C_fixture_impact_holder',{reference:view,victim:args[1].toString(),passive:true});}}});
 watchNativeHolderPhase(0x1e4a80,'projectile_on_remove','projectile');
 watchNativeHolderPhase(0x1a02f0,'actor_spawn','projectile');
 watchNativeHolderPhase(0x19f210,'actor_destructor','projectile');
 watchNativeHolderPhase(0x195000,'common_on_remove','dynamic_light');
 Interceptor.attach(base.add(0x1a17c0),{onEnter(args){
  if(!simulationThread()||!gameplayState()||!['alive','retiring'].includes(stage))return;
  if(nativeClass(args[0])!=='projectile')return;
  this.actor=args[0];this.source=args[1].toString();this.before=args[0].add(0x148).readPointer().toString();
 },onLeave(){if(!this.actor)return;try{const rec=candidate(this.actor,'projectile_actor');if(!rec)return;
  const view=referenceView(rec);observeProjectileReference(rec,view);if(view.holds_fixture)event('C_source_owner_native_setter_observed',{
   actor:this.actor.toString(),source_argument:this.source,before:this.before,reference:view,passive:true,native_calls_in_observer:false});
 }catch(e){referenceOverflow=true;event('C_lifetime_observer_read_error',{phase:'source_owner_observer',error:String(e),passive:true});}}});
}
Interceptor.attach(base.add(0x1d5000),{onEnter(args){
 const records=[p1Identity,p2Identity,enemyIdentity,pickupIdentity,...retiredPuppets,...referenceCandidates].filter(r=>r&&!r.invalid&&r.actor.equals(args[1]));
 for(const rec of records){rec.pool_return_node=nodeView(rec.node);rec.invalid=true;rec.pool_return_seen=true;}
 if(records.length){event('C_pool_return',{rcx:args[0].toString(),rdx:args[1].toString(),kinds:records.map(r=>r.kind),nodes:records.map(r=>r.pool_return_node),identities_invalidated:true});
  if(p2Identity?.invalid)created=null;if(enemyIdentity?.invalid)enemy=null;if(p1Identity?.invalid)original=null;}
}});
function stopHooks(){if(!simulationThread())throw new Error('Cleanup outside simulation thread');
 try{if(controller)controller.stop();}finally{scope=0;resolverState.writeU32(0);restoreResolver();}
 const restored={};
 for(const name of ['commands','turning','controller_update']){const s=cfg.native_prefixes[name];
  const actual=Array.from(new Uint8Array(base.add(s.rva).readByteArray(16)),b=>b.toString(16).padStart(2,'0')).join('');
  restored[name]={rva:'0x'+s.rva.toString(16),actual,expected:s.prefix,passed:actual===s.prefix};}
 const getter=Array.from(new Uint8Array(getterEntry.readByteArray(5)),b=>b.toString(16).padStart(2,'0')).join('');
 const passed=getter==='488d4140c3'&&Object.values(restored).every(r=>r.passed);
 event('C_hooks_restored',{getter,controller_prefixes:restored,passed});
 if(!passed)throw new Error('Native hooks did not restore exact original bytes');}
function removeOwned(rec,key){if(!live(rec))throw new Error('Owned actor identity lost before '+key);
 if(!['secondary','fixture_enemy','weapon_fixture'].includes(rec.kind)||nativeClass(rec.actor))throw new Error('Refusing explicit Remove on a reference-holder object');
 if(!rec.actor.readPointer().add(0x30).readPointer().equals(base.add(0x1a02b0)))throw new Error('Owned actor uses an unreviewed virtual Remove; keep storage pinned');
 if(rec.actor.add(0x40).readU8()!==0){nativeCalls[nativeKey(key)]={outcome:'already_marked_removed',note:'Observed native removed flag; no duplicate Remove'};return;}
 callOnce(key,()=>remove(rec.actor));}
function beginRetirement(reason){if(cleanupStarted)return;cleanupStarted=true;retireFrames=0;stage='retiring';
 try{stopHooks();if(!gameplayState()){stage='teardown_unknown';event('C_map_teardown_unknown',{reason,note:'No native object calls or actor reads after state change.'});return;}
  event('C_retire_begin',{reason,local:graph(local),secondary:graph(p2),updates});
  if(nativeOutcomeUnknown)throw new Error('A native call outcome is unknown; keeping context and code pinned');
  if(pickupIdentity){if(live(pickupIdentity))removeOwned(pickupIdentity,'weapon_fixture_remove');
   else if(!pickupIdentity.pool_return_seen)throw new Error('Weapon fixture identity lost without observed native pool return');}
  if(enemyIdentity&&live(enemyIdentity)){
   event('C_enemy_target_pending',{target_node:enemyIdentity.actor.add(0x28).readPointer().toString(),policy:'Let the native holder release its own edge; no adapter SetTarget call'});
   event('C_enemy_remove',{enemy:enemyGraph(),hit:enemyHit});removeOwned(enemyIdentity,'enemy_remove');
  }else if(enemyIdentity&&!enemyIdentity.pool_return_seen&&!enemyIdentity.native_destroyed)throw new Error('Fixture enemy identity lost without native retirement evidence');
  if(p2Identity)removeOwned(p2Identity,'secondary_remove');
  const handle=p2.add(0xd8).readPointer();
  if(!handle.isNull()){
   if(!p2Identity||!handle.equals(p2Identity.node)||!live(p2Identity))throw new Error('Secondary handle identity lost before Reset');
   callOnce('secondary_retire_reset',()=>reset(p2));
  }
  event('C_unbound',{local:graph(local),secondary:graph(p2),retired_node:node2?.toString(),node_refs:nodeView(node2).refs,guards:guards(),native_calls:nativeCalls,fixture_holders:fixtureHolders()});
 }catch(e){stage='cleanup_unknown';event('C_cleanup_error',{error:String(e),native_calls:nativeCalls,note:'Do not retry uncertain native calls or unload script; keep storage/code pinned until normal process exit.'});}}
function giSnapshot(){return {state:gi.add(0x24).readS32(),pending:gi.add(0x28).readS32()};}
function samePrimary(before,after){return after.live_identity&&after.node===before.node&&after.actor===before.actor&&after.owner===before.owner&&after.health===before.health;}
function unchangedGi(before){const now=giSnapshot();return now.state===before.state&&now.pending===before.pending;}
function cameraFields(p){return Object.fromEntries([0x130,0x134,0x13c,0x148].map(o=>['at_'+o.toString(16),p.add(o).readFloat()]));}
function hostDeathTick(){let primary=graph(local);const secondary=graph(p2),counts=controller.counts();
 if(primary.node!==hostInitial.primary.node||primary.actor!==hostInitial.primary.actor||primary.owner!==hostInitial.primary.owner||!unchangedGi(hostInitial.gi))throw new Error('Host identity or GI changed during host-death test');
 if(secondary.health<=0||(secondary.puppet_flags&0x200))throw new Error('Secondary did not remain alive during host death');
 if(frames===90){
  hostGate.install();const before={primary,secondary,controller:counts,gi:giSnapshot(),camera:cameraFields(local)};
  callOnce('host_lethal_damage',()=>lethalDamage(p1Identity.actor,p2Identity.actor,0,primary.health));
  const after=graph(local);if(after.health>0||!(after.puppet_flags&0x200)||!unchangedGi(hostInitial.gi))throw new Error('Host native death did not reach the expected dead branch');
  // Use the actual native start location of this run, with native Spawn sector/yaw.
  const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(hostInitial.primary.origin[i]));
  enemy=callOnce('host_ai_fixture_spawn',()=>spawn(factory,1,v,hostInitial.primary.yaw,hostInitial.primary.sector));
  if(enemy.isNull())throw new Error('Host AI fixture Spawn returned null');enemyNode=enemy.add(0x10).readPointer();enemyIdentity=identity(enemy,enemyNode,'fixture_enemy');
  if(!live(enemyIdentity)||enemy.readPointer().toString()!==base.add(0x527610).toString()||!enemy.readPointer().add(0x20).readPointer().equals(base.add(0x194ce0)))throw new Error('Host AI fixture native type/Tick differs');
  const targetBefore=enemy.add(0x28).readPointer().toString(),refsBefore=nodeView(p2Identity.node);
  callOnce('host_ai_fixture_target_p2',()=>assignFixtureTarget(enemy,p2Identity.actor));
  if(!enemy.add(0x28).readPointer().equals(p2Identity.node))throw new Error('Native AI SetTarget did not bind the secondary node');
  event('C_host_ai_native_target_assignment',{enemy:enemyGraph(),target_before:targetBefore,secondary_refs_before:refsBefore,secondary_refs_after:nodeView(p2Identity.node),native_rva:'0x1d35d0',purpose:'One fixture target assignment, no null/release call'});
  hostProbe={frame:frames,controller:controller.counts(),puppetTicks:updates,hostTicks:hostPuppetTicks,aiTicks:hostAiTicks,origin:secondary.origin,maxMovement:0};
  event('C_host_native_death',{before,after:graph(local),secondary:graph(p2),gate:hostGate.snapshot(),direct_native_damage:true,full_m0:false});
  primary=graph(local);
 }
 if(hostProbe){
  const age=frames-hostProbe.frame;hostProbe.maxMovement=Math.max(hostProbe.maxMovement,Math.hypot(...secondary.origin.map((x,i)=>x-hostProbe.origin[i])));
  const view=cameraFields(local),p1ticks=counts.p1_controller-hostProbe.controller.p1_controller,p2ticks=counts.p2_controller-hostProbe.controller.p2_controller;
  const data={age,primary,secondary,gi:giSnapshot(),p1_native_updates:p1ticks,p2_native_updates:p2ticks,p1_puppet_ticks:hostPuppetTicks-hostProbe.hostTicks,
   p2_puppet_ticks:updates-hostProbe.puppetTicks,ai_ticks:hostAiTicks-hostProbe.aiTicks,enemy:enemyGraph(),max_p2_movement:hostProbe.maxMovement,camera:view,gate:hostGate.snapshot()};
  if(primary.health>0||!(primary.puppet_flags&0x200)||!Object.values(view).every(Number.isFinite)||data.gate.unexpected_global_requests)throw new Error('Host death/camera/global-request invariant failed');
  if(age%30===0)event('C_dead_host_sample',data);
  if(age>=90){
   if(primary.death_timer<60||p1ticks<90||p2ticks<90||data.ai_ticks<90||data.p1_puppet_ticks<90||data.p2_puppet_ticks<90||hostProbe.maxMovement<=1)throw new Error('Host observation did not collect independent live world/secondary updates');
   event('C_host_death_bounded_observed',{...data,full_m0:false,visual_camera_hud_acceptance:false,gate_restoration:'Pending native state exit or process-exit boundary; keep script/code pinned'});return true;
  }
 }
 if(frames>=360)throw new Error('Bounded host-death observation deadline');return false;
}
function deathTick(){let secondary=graph(p2);const primary=graph(local),counts=controller.counts();
 if(counts.p2_identity_skips)throw new Error('Secondary controller generation guard rejected an update');
 if(!samePrimary(cycleInitial.primary,primary)||!unchangedGi(cycleInitial.gi))throw new Error('Primary health/identity or GI changed during isolated death/recreate test');
 const distance=Math.hypot(...secondary.origin.map((n,i)=>n-cycleInitial.secondary.origin[i]));
 if(frames===30){
  if(distance<=1)throw new Error('Secondary did not move clear before death/recreate check');
  event('C_death_mode_movement',{distance,secondary,primary,controller:counts});
 }
 if(cycle===2){
  if(secondary.health<=0||(secondary.puppet_flags&0x200)||secondary.death_timer!==0)throw new Error('Recreated secondary is not independently alive');
  if(frames===60){
   if(distance<=1)throw new Error('Recreated secondary movement was not observed');
   recreatePassed=true;event('C_same_context_recreated_alive',{same_player_context:true,secondary,primary,distance,controller:counts,full_m0:false});
  }
  return recreatePassed&&frames>=120;
 }
 if(!deathProbe&&frames<90&&(secondary.health<=0||(secondary.puppet_flags&0x200)))throw new Error('Secondary died before the controlled native damage event');
 if(frames===90){
  if(secondary.health<=0||secondary.death_timer!==0)throw new Error('Secondary is not ready for one controlled death event');
  const before={secondary,primary,gi:giSnapshot(),controller:counts,puppet_updates:updates};
  callOnce('secondary_lethal_damage',()=>lethalDamage(p2Identity.actor,p1Identity.actor,0,secondary.health));
  if(!gameplayState())throw new Error('GI changed during native secondary death');
  const after=graph(p2),primaryAfter=graph(local);
  if(after.health>0||!(after.puppet_flags&0x200)||after.death_timer!==0||!samePrimary(primary,primaryAfter)||!unchangedGi(before.gi))
   throw new Error('Native secondary death did not preserve expected independent state');
  deathProbe={before,frame:frames,controller:controller.counts(),puppet_updates:updates};
  event('C_secondary_native_death',{before,after,primary_after:primaryAfter,gi_after:giSnapshot(),direct_native_damage:true,
   type:0,amount:secondary.health,controller_policy:'Skip dead P2 Player Update; retain normal Puppet ticks and native P1 updates'});
  secondary=after;
 }
 if(deathProbe){
  const primaryTicks=counts.p1_controller-deathProbe.controller.p1_controller,deadSkips=counts.p2_dead_skips-deathProbe.controller.p2_dead_skips;
  const puppetTicks=updates-deathProbe.puppet_updates,secondaryUpdates=counts.p2_controller-deathProbe.controller.p2_controller;
  if(secondary.health>0||!(secondary.puppet_flags&0x200)||secondary.death_timer!==0||secondaryUpdates!==0)throw new Error('Dead secondary entered native controller or lost expected dead state');
  if(frames%30===0)event('C_dead_secondary_sample',{secondary,primary,primary_ticks:primaryTicks,dead_skips:deadSkips,
   secondary_puppet_ticks:puppetTicks,secondary_native_updates:secondaryUpdates,gi:giSnapshot()});
  if(primaryTicks>=90&&deadSkips>=90&&puppetTicks>=90){deathPassed=true;event('C_secondary_death_isolation_passed',{
   primary_ticks:primaryTicks,dead_skips:deadSkips,secondary_puppet_ticks:puppetTicks,secondary_native_updates:secondaryUpdates,
   death_timer:secondary.death_timer,primary,secondary,gi:giSnapshot(),full_m0:false});return true;}
 }
 if(frames>=360)throw new Error('Bounded dead-secondary observation did not collect 90 independent ticks');
 return false;
}
function projectileStatus(cleanup='pending'){const q=projectileProbe;return {weapon:q.weapon,
 pickup:q.pickup_observed?'passed':'pending',ammo:q.ammo_observed?'passed':'pending',real_weapon_hit:q.hit_observed?'passed':'pending',
 p2_projectile:q.weapon.kind==='hitscan'?'not_exercised_hitscan':q.p2_projectile_observed?'passed':'pending',
 ai_projectile:q.ai_projectile_observed?'passed':'pending',source_owner_natural_release:sourceOwnerReleases.length?'observed':'pending',
 cleanup,primary_inventory_unchanged:q.primary_inventory_unchanged===true,primary_inventory:q.last_p1_inventory,secondary_inventory:q.last_p2_inventory,
 shots_requested:q.shots,stop_reason:q.stop_reason,ownership_witnesses:q.ownership_witnesses,source_owner_releases:sourceOwnerReleases,
 full_m0:false,player_storage_released:false};}
function projectileStop(reason){projectileProbe.stop_reason=reason;event('C_projectile_mode_observation',projectileStatus());
 if(manualFlow&&!manualFlow.active()&&['native_release_pending','native_release_observed'].includes(reason)){manualFlow.start();return manualFlow.step();}
 return {buttons:0,turn:0,retire:true};}
function projectileStep(){if(inflightFlow?.active())return inflightFlow.step();if(manualFlow?.active())return manualFlow.step();
 const q=projectileProbe,s=graph(p2),p=graph(local),inv=inventory(p2),host=inventory(local),slot=q.weapon.slot;
 q.last_p1_inventory=host;q.last_p2_inventory=inv;q.primary_inventory_unchanged=sameInventory(q.initial_p1_inventory,host);
 if(!p.live_identity||p.actor!==q.initial_primary.actor||p.node!==q.initial_primary.node||p.owner!==q.initial_primary.owner)throw new Error('Primary graph changed in projectile probe');
 if(!q.primary_inventory_unchanged)return projectileStop('primary_inventory_changed');
 if(controller.counts().p2_identity_skips)throw new Error('Projectile controller identity guard rejected secondary');
 if(s.health<=(cfg.manual_respawn_test?0:100)||p.health<=100||(s.puppet_flags&0x200))return projectileStop('bounded_low_health');
 if(frames>=1200)return projectileStop('bounded_total_timeout');
 if(q.fire_inventory&&!q.ammo_observed&&inv.current===slot&&inv.counters[slot]<q.fire_inventory.counters[slot]){
  q.ammo_observed=true;event('C_secondary_ammo_consumed',{weapon:q.weapon,before:q.fire_inventory,after:inv,
   amount:q.fire_inventory.counters[slot]-inv.counters[slot],primary_before:q.initial_p1_inventory,primary_after:host,native_commands_only:true});
 }
 for(const rec of referenceCandidates){if(live(rec)&&(nativeClass(rec.actor)==='projectile'||rec.seen_impact))observeProjectileReference(rec,referenceView(rec));}
 const cmd={buttons:0,turn:0,retire:false,buttons1:cfg.inflight_respawn_test&&Math.hypot(...p.origin.map((v,i)=>v-q.initial_primary.origin[i]))<1100?4:0};
 if(q.phase==='clear'){
  const separation=Math.hypot(s.origin[0]-p.origin[0],s.origin[1]-p.origin[1]);
  if(separation<200){cmd.buttons=8;if(frames>=300)return projectileStop('insufficient_observed_pickup_separation');}
  if(frames>=60&&separation>=200){
   const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(s.origin[i]));
   const actor=callOnce('weapon_fixture_spawn',()=>spawn(factory,q.weapon.pickup_type,v,s.yaw,s.sector));
   if(actor.isNull())throw new Error('Native weapon fixture factory returned null');pickupNode=actor.add(0x10).readPointer();
   pickupIdentity=identity(actor,pickupNode,'weapon_fixture');if(!live(pickupIdentity))throw new Error('Weapon fixture identity mismatch');
   const definition=actor.add(0x170).readPointer();
   if(definition.isNull()||definition.readS32()!==q.weapon.pickup_type||definition.add(0x128).readS32()!==slot)throw new Error('Weapon fixture type/slot differs from shipped definitions');
   q.phase='pickup';q.phase_frame=frames;event('C_native_weapon_fixture_spawn',{weapon:q.weapon,actor:actor.toString(),node:pickupNode.toString(),
    vtable:actor.readPointer().toString(),definition:definition.toString(),origin:s.origin,sector:s.sector,separation,primary_inventory:host,secondary_inventory:inv});
  }return cmd;
 }
 if(q.phase==='pickup'){
  // C14 placed the item at a stationary Puppet; no touch callback occurred. Move
  // through it using native commands rather than calling Touch/GiveWeapon directly.
  const age=frames-q.phase_frame;if(age<=12)cmd.buttons=4;else if(age<=24)cmd.buttons=8;
  if([1,30].includes(age)&&live(pickupIdentity))event('C_weapon_fixture_passive_sample',{
   actor:pickupIdentity.actor.toString(),node:nodeView(pickupIdentity.node),vtable:pickupIdentity.actor.readPointer().toString(),
   origin:[0x18,0x1c,0x20].map(o=>pickupIdentity.actor.add(o).readFloat()),sector:pickupIdentity.actor.add(0x3c).readS32(),
   removed:pickupIdentity.actor.add(0x40).readU8(),secondary:s,inventory:inv,native_touch_calls_by_adapter:0});
  if(q.pickup_observed){q.phase='weapon';q.phase_frame=frames;}
  else if(frames-q.phase_frame>=180)return projectileStop('native_pickup_grant_not_observed');
  return cmd;
 }
 if(q.phase==='weapon'){
  if(inv.owned[slot]!==1)return projectileStop('selected_weapon_not_owned_after_pickup');
  if(inv.current===slot&&inv.pending===slot){
   if(frames-q.phase_frame<30)return cmd;
   if(cfg.inflight_respawn_test){
    if(Math.hypot(...p.origin.map((v,i)=>v-q.initial_primary.origin[i]))<1100&&frames-q.phase_frame<300)return cmd;
    if(Math.hypot(...p.origin.map((v,i)=>v-s.origin[i]))<900)return projectileStop('insufficient_safe_primary_blast_separation');
    inflightFlow.start();return inflightFlow.step();}
   if(cfg.projectile_target==='wall'){q.phase='wall_aim';q.phase_frame=frames;event('C_wall_projectile_observation_begin',{
    secondary:s,primary:p,weapon:q.weapon,inventory:inv,policy:'Two native attack pulses aimed across the corridor, away from P1. No AI fixture or reference-holder Remove/setter calls.'});return cmd;}
   const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(s.origin[i]));
   enemy=callOnce('enemy_spawn',()=>spawn(factory,1,v,s.yaw,s.sector));
   if(enemy.isNull())throw new Error('Native projectile-test enemy factory returned null');enemyNode=enemy.add(0x10).readPointer();
   enemyIdentity=identity(enemy,enemyNode,'fixture_enemy');if(!live(enemyIdentity)||enemy.add(0x170).readPointer().readS32()!==1)throw new Error('Native projectile-test enemy identity/type mismatch');
   q.phase='separate';q.phase_frame=frames;event('C_enemy_spawn',{enemy:enemyGraph(),secondary:s,weapon:q.weapon,inventory:inv,fixture:'Native AT_ANUBIS1, unmodified AI/health. Stop after first attributed hit.'});
  }else if(inv.pending!==slot&&(q.last_switch===undefined||frames-q.last_switch>=30)){
   if(q.switch_pulses>=8)return projectileStop('native_weapon_switch_not_observed');
   q.last_switch=frames;q.switch_pulses++;cmd.buttons=0x100;event('C_secondary_weaponright_pulse',{weapon:q.weapon,inventory:inv,pulse:q.switch_pulses});
  }
  if(frames-q.phase_frame>=240)return projectileStop('bounded_weapon_ready_timeout');return cmd;
 }
 if(q.phase==='wall_aim'){
  const desired=Math.PI/2,aim=Math.atan2(Math.sin(desired-s.yaw),Math.cos(desired-s.yaw));cmd.turn=Math.max(-.08,Math.min(.08,aim));
  if(inv.current!==slot||inv.pending!==slot||inv.counters[slot]===0)return projectileStop('selected_weapon_unready_or_empty');
  if(q.shots<q.weapon.max_shots&&Math.abs(aim)<.03&&frames-q.last_shot>=120){
   if(!q.fire_inventory)q.fire_inventory=inv;q.shots++;q.last_shot=frames;cmd.buttons=1;
   event('C_secondary_weapon_attack_pulse',{weapon:q.weapon,shot:q.shots,aim_error:aim,inventory:inv,target:'native_world_geometry'});
  }
  if(q.shots===q.weapon.max_shots&&frames-q.last_shot>=180)return projectileStop(sourceOwnerReleases.length?'native_release_observed':'native_release_pending');
  if(frames-q.phase_frame>=600)return projectileStop('bounded_wall_projectile_timeout');return cmd;
 }
 const e=enemyGraph();if(!e||e.health<=0)return projectileStop('enemy_not_alive_before_bounded_retirement');
 const dx=e.origin[0]-s.origin[0],dy=e.origin[1]-s.origin[1],distance=Math.hypot(dx,dy),desired=Math.atan2(dx,dy);
 const aim=Math.atan2(Math.sin(desired-s.yaw),Math.cos(desired-s.yaw));cmd.turn=Math.max(-.08,Math.min(.08,aim));
 lastAim={distance,desired_yaw:desired,error:aim,phase:q.phase};
 if(q.phase==='separate'){
  if(frames-q.phase_frame<30)cmd.buttons=8;else{q.phase='aim';q.phase_frame=frames;}return cmd;
 }
 if(q.phase==='aim'){
  if(q.hit_observed&&q.ammo_observed){q.phase='ranged';q.phase_frame=frames;event('C_ai_ranged_observation_begin',{enemy:e,secondary:s,inventory:inv,weapon:q.weapon});return cmd;}
  if(inv.current!==slot||inv.pending!==slot||inv.counters[slot]===0)return projectileStop('selected_weapon_unready_or_empty');
  if(q.shots>=q.weapon.max_shots&&frames-q.last_shot>=180)return projectileStop('bounded_hit_not_observed');
  if(distance<120)cmd.buttons=8;else if(distance>2000)cmd.buttons=4;
  else if(q.shots<q.weapon.max_shots&&Math.abs(aim)<.06&&Math.abs(e.origin[2]-s.origin[2])<96&&frames-q.last_shot>=45){
   if(!q.fire_inventory)q.fire_inventory=inv;q.shots++;q.last_shot=frames;cmd.buttons=1;
   event('C_secondary_weapon_attack_pulse',{weapon:q.weapon,shot:q.shots,distance,aim_error:aim,inventory:inv,enemy:e});
  }
  if(frames-q.phase_frame>=360)return projectileStop('bounded_aim_timeout');return cmd;
 }
 if(q.phase==='ranged'){
  if(frames-q.phase_frame<240&&distance<700)cmd.buttons=8;
  const ownership=q.weapon.kind==='hitscan'||q.p2_projectile_observed;
  if(q.pickup_observed&&q.ammo_observed&&q.hit_observed&&ownership&&q.ai_projectile_observed&&sourceOwnerReleases.length&&frames-q.last_witness_frame>=60)
   return projectileStop('requested_observations_recorded');
  if(frames-q.phase_frame>=480)return projectileStop('bounded_ai_projectile_or_release_pending');return cmd;
 }
 return projectileStop('unknown_projectile_probe_phase');
}
const manualFlow=cfg.manual_respawn_test?createM0RespawnFlow():null;
const contextWorldExit=cfg.context_world_exit_test?prepareM0ContextWorldExit():null;
const physicsFlow=cfg.physics_test?createDirectedPhysicsFlow():null;
const inflightFlow=cfg.inflight_respawn_test?createM0ProjectileRespawnFlow():null;
const directQuit=cfg.direct_quit_test?createM0DirectQuitObserver():null;
event('C_armed',{pid:Process.id,simulation_tid:cfg.simulation_tid,secondary_player:p2.toString(),
 constructor_adapter:'zero408bytes; +80=-1; +e0=self; +160/+178=interpolant vtable; ready +168=self+13c/+180=self+134; native Reset after defs loaded',
 native_initializer_not_recalled:true,alignment:16,alignment_passed:p2.and(15).isNull(),storage_policy:'pinned through disposable process exit; deallocation not yet proven'});
Interceptor.attach(base.add(0x1b6ec0),{onEnter(args){this.self=args[0];},onLeave(){
 if(Process.getCurrentThreadId()!==cfg.simulation_tid||!this.self.equals(gi))return;
 if(!gameplayState()){
  if(hostGate)try{hostGate.stateChange(gi.add(0x24).readS32());}catch(e){event('C_host_gate_cleanup_error',{error:String(e)});}
  if(manualFlow)manualFlow.stateChange(gi.add(0x24).readS32(),gi.add(0x28).readS32());
  if(['constructing','alive','retiring'].includes(stage)){
   const previous=stage;stage='teardown_unknown';try{stopHooks();}catch(e){event('C_hook_cleanup_error',{error:String(e)});}
   event('C_map_teardown_unknown',{previous,state:gi.add(0x24).readS32(),pending:gi.add(0x28).readS32(),
    note:'Secondary scheduling stopped. No unverified object calls or actor reads after state change; keep storage/code pinned.'});
  }return;
 }
 if(stage==='armed'){
  const n=local.add(0xd8).readPointer();if(n.isNull()||++frames<60)return;
  originalNode=n;original=n.add(0x28).readPointer();p1Identity=identity(original,n,'primary');stage='constructing';
  try{
   if(!live(p1Identity))throw new Error('Primary graph is not live at construction gate');
   if(cfg.death_test&&cycle===2&&!samePrimary(completedCycles[0].primary,graph(local)))throw new Error('Primary changed before same-context reconstruction');
   event('C_before',{local:graph(local),secondary:graph(p2),guards:guards()});
   installResolver();
   resolverEnter();let selected;try{selected=getPlayer(gi);}finally{resolverLeave();}
   event('C_resolver_preflight',{selected:selected.toString(),expected:p2.toString(),passed:selected.equals(p2)});
   if(!selected.equals(p2))throw new Error('Scoped resolver did not intercept native call; refusing Spawn');
   callOnce('secondary_initialize_reset',()=>reset(p2));event('C_reset_complete',{secondary:graph(p2),self:p2.add(0xe0).readPointer().toString(),guards:guards()});
   const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(cfg.combat_test?cfg.combat_origin[i]:original.add(0x18+i*4).readFloat()));
   const yaw=cfg.combat_test?Math.atan2(original.add(0x18).readFloat()-cfg.combat_origin[0],original.add(0x1c).readFloat()-cfg.combat_origin[1]):original.add(0x38).readFloat();
   const sector=cfg.combat_test?cfg.combat_sector:original.add(0x3c).readS32();
   event('C_factory_arguments',{origin:[0,4,8].map(o=>v.add(o).readFloat()),yaw,sector,placement:cfg.combat_test?'C3 observed sector258 position; floor Z from current native Player; aim toward P1':'same as native first Player'});
   resolverEnter();try{created=callOnce('secondary_spawn',()=>spawn(factory,0,v,yaw,sector));}finally{resolverLeave();}
   node2=p2.add(0xd8).readPointer();
   p2Identity=identity(created,node2,'secondary');p2Identity.generation=1;
   const binding=live(p2Identity)&&created.add(0x1b8).readPointer().equals(p2)&&live(p1Identity)&&local.add(0xd8).readPointer().equals(originalNode);
   event('C_spawn_result',{returned:created.toString(),local:graph(local),secondary:graph(p2),independent_binding:binding,guards:guards(),resolver_hits:resolverState.add(16).readU32()});
   if(!binding)throw new Error('Independent binding validation failed');
   if(cfg.weapon_ready){
    callOnce('secondary_weapon_ready',()=>native(0x20f9e0,'void',['pointer','int'])(p2.add(0xe0),0));
    event('C_weapon_ready',{secondary:graph(p2),helper_rva:'0x20f9e0',mode:0});
    if(p2.add(0xe8).readPointer().isNull())throw new Error('Weapon ready helper did not initialize secondary animation');
   }
   if(controller){controller.bindIdentity(created,node2);controller.install();controller.set(0,0,0);controller.enable();}stage='alive';frames=0;
   if(cfg.death_test){cycleInitial={secondary:graph(p2),primary:graph(local),gi:giSnapshot()};
    if(cycleInitial.secondary.health<=0||(cycleInitial.secondary.puppet_flags&0x200)||cycleInitial.secondary.death_timer!==0)throw new Error('Native spawn did not produce a ready alive secondary');
    event('C_death_cycle_alive',{same_player_context:p2.toString(),cycle_initial:cycleInitial,controller:controller.counts(),prior_retirements:completedCycles.length});}
   if(projectileProbe){projectileProbe.initial_primary=graph(local);projectileProbe.initial_secondary=graph(p2);projectileProbe.initial_p1_inventory=inventory(local);
    event('C_projectile_mode_armed',{weapon:projectileWeapon,primary:projectileProbe.initial_primary,secondary:projectileProbe.initial_secondary,
     primary_inventory:projectileProbe.initial_p1_inventory,secondary_inventory:inventory(p2),policy:'Native pickup touch and native command stream only; no GiveWeapon/ammo writes. Pistol is hitscan.'});}
   if(cfg.host_death_test){hostInitial={primary:graph(local),secondary:graph(p2),gi:giSnapshot()};event('C_host_death_mode_armed',{initial:hostInitial,camera:cameraFields(local)});}
  }catch(e){event('C_error',{stage,error:String(e),stack:e.stack,native_calls:nativeCalls,guards:guards()});beginRetirement('construction_error');}
  finally{scope=0;resolverState.writeU32(0);try{restoreResolver();}catch(e){
   event('C_hook_cleanup_error',{error:String(e)});if(controller)controller.stop();stage='cleanup_unknown';}}
 } else if(stage==='alive'){
  try{
  if(nativeOutcomeUnknown)throw new Error('Native reference-release outcome is unknown');
  if(!live(p1Identity)||!live(p2Identity)||(enemyIdentity&&!live(enemyIdentity)&&!enemyIdentity.pool_return_seen&&!enemyIdentity.native_destroyed))throw new Error('Live actor identity lost during experiment');
  ++frames;
  let projectileCommand=null,physicsCommand=null;
  if(controller){
   let b1=0,b2=0,y1=0,y2=0;
   if(frames>=60&&frames<120){b1=4;b2=8;}else if(frames>=120&&frames<180){b1=8;b2=4;}
   else if(frames>=180&&frames<240){y1=0.01;y2=-0.01;}
   else if(frames>=240&&frames<252)b2=1;else if(frames>=300&&frames<312)b1=1;
   if(cfg.combat_test){b1=frames>=180&&frames<192?1:0;b2=frames>=60&&frames<72?1:0;y1=0;y2=0;}
   if(cfg.death_test){b1=0;b2=frames<=30?8:0;y1=0;y2=0;}
   if(cfg.host_death_test){const age=hostProbe?frames-hostProbe.frame:0;b1=0;b2=hostProbe?(age<30?8:age<60?4:8):(frames<=60?8:0);y1=0;y2=0;}
   if(cfg.projectile_test){projectileCommand=projectileStep();b1=projectileCommand.buttons1||0;b2=projectileCommand.buttons;y1=0;y2=projectileCommand.turn;}
   if(cfg.direct_quit_test){b1=0;b2=frames<=45?8:0;y1=0;y2=0;directQuit.tick();}
   if(cfg.physics_test){physicsCommand=physicsFlow.step();b1=physicsCommand.b1;b2=physicsCommand.b2;y1=physicsCommand.y1;y2=physicsCommand.y2;}
   if(cfg.enemy_test){
    b1=0;b2=0;y1=0;y2=0;
    if(!enemy){
     if(frames<30)b2=8;
     if(frames===150){
      const ep=Memory.alloc(12);[0,4,8].forEach((o,i)=>ep.add(o).writeFloat(created.add(0x18+i*4).readFloat()));
      enemy=callOnce('enemy_spawn',()=>spawn(factory,1,ep,created.add(0x38).readFloat(),created.add(0x3c).readS32()));
      if(enemy.isNull())throw new Error('Native AT_ANUBIS spawn returned null');enemyNode=enemy.add(0x10).readPointer();
      enemyIdentity=identity(enemy,enemyNode,'fixture_enemy');if(!live(enemyIdentity))throw new Error('Native enemy node identity mismatch');
      event('C_enemy_spawn',{enemy:enemyGraph(),secondary:graph(p2),fixture:'Native factory type1 AT_ANUBIS, at observed P2 position/sector. No enemy health/AI patches.'});
      if(enemy.add(0x170).readPointer().readS32()!==1)throw new Error('Unexpected native enemy type');
     }
    }else{
     enemyFrames++;const e=enemyGraph(),s=graph(p2),dx=e.origin[0]-s.origin[0],dy=e.origin[1]-s.origin[1],distance=Math.hypot(dx,dy);
     const desired=Math.atan2(dx,dy),difference=Math.atan2(Math.sin(desired-s.yaw),Math.cos(desired-s.yaw));y2=Math.max(-.08,Math.min(.08,difference));
     if(enemyFrames<=24)b2=8;else if(!enemyAttackedAt&&!enemyHit){
      if(distance>110)b2=4;
      else if(Math.abs(difference)<.1&&Math.abs(e.origin[2]-s.origin[2])<64){enemyAttackedAt=frames;event('C_enemy_attack_command',{frame:frames,distance,aim_error:difference,enemy:e,secondary:s});}
     }
     if(enemyAttackedAt&&frames-enemyAttackedAt<12)b2=1;
     lastAim={distance,desired_yaw:desired,error:difference,enemy_frames:enemyFrames};
    }
   }
   controller.set(frames,b1,b2,y1,y2);
   if(frames===240&&!cfg.combat_test&&!cfg.enemy_test&&!cfg.death_test&&!cfg.projectile_test&&!cfg.host_death_test&&!cfg.physics_test&&!cfg.direct_quit_test){
    const damage=native(0x1a1370,'void',['pointer','pointer','int','int']);
    event('C_damage_before',{local:graph(local),secondary:graph(p2),type:0});damage(created,original,0,13);
    event('C_damage_p2',{local:graph(local),secondary:graph(p2),amount:13});damage(original,created,0,7);
    event('C_damage_p1',{local:graph(local),secondary:graph(p2),amount:7});
   }
  }
  if(frames%60===0)event('C_alive_sample',{frame:frames,local:graph(local),secondary:graph(p2),secondary_puppet_updates:updates,controller:controller?.counts(),weaponEvents,meleeEvents,collisionEvents,guards:guards(),enemy:enemyGraph(),aim:lastAim,enemy_hit:enemyHit});
  const retireNow=cfg.direct_quit_test?false:cfg.physics_test?physicsCommand?.retire:cfg.host_death_test?hostDeathTick():cfg.projectile_test?projectileCommand?.retire:cfg.death_test?deathTick():cfg.enemy_test?(!!enemyHit&&frames>=enemyHit.frame+120)||frames>=720:frames>=(controller?420:300);
  if(retireNow){
   beginRetirement(cfg.host_death_test?'bounded_host_death_observation_complete':cfg.projectile_test?projectileProbe.stop_reason:cfg.death_test?'death_recreate_cycle_complete':'bounded_experiment_complete');
  }
  }catch(e){
   event('C_enemy_experiment_error',{stage,frame:frames,error:String(e),stack:e.stack});
   beginRetirement('experiment_error');
  }
 } else if(stage==='retiring'){
  if(nativeOutcomeUnknown){stage='cleanup_unknown';event('C_cleanup_error',{native_calls:nativeCalls,note:'Native release outcome unknown; code/storage remain pinned.'});return;}
  ++retireFrames;if(retireFrames%60!==0)return;
  const secondary=nodeView(node2),fixture=nodeView(enemyNode),pickup=nodeView(pickupNode);
  const released=n=>n.refs===0&&n.payload==='0x0';
  const pickupRetired=!pickupIdentity||released(pickup)||(pickupIdentity.pool_return_seen&&pickupIdentity.pool_return_node?.refs===0);
  const oldRetired=retiredPuppets.every(r=>r.native_destroyed||r.pool_return_seen&&r.pool_return_node?.refs===0);
  const retired=!!node2&&released(secondary)&&(!enemyNode||released(fixture))&&pickupRetired&&oldRetired;
  const primary=graph(local),primaryIntact=primary.live_identity&&primary.node===originalNode.toString()&&primary.owner===local.toString();
  const passed=retired&&primaryIntact&&guards()&&!nativeOutcomeUnknown;
  event('C_retire_observation',{frame:retireFrames,local:primary,secondary:graph(p2),node_payload:secondary.payload,node_refs:secondary.refs,
   fixture_node:fixture,pickup_node:pickup,pickup_retired:pickupRetired,fixture_holders:fixtureHolders(),tracking_overflow:referenceOverflow,guards:guards(),primary_intact:primaryIntact,
   bounded_puppet_retirement:passed,native_calls:nativeCalls,player_storage_released:false,full_m0:false,
   note:'Storage/code remain pinned. Nonzero references are pending, never success by timer.'});
  if(passed){stage='retired';event('C_bounded_retirement_passed',{full_m0:false,player_storage_released:false});
   if(projectileProbe)event('C_projectile_mode_final',projectileStatus('passed'));
   if(cfg.death_test){completedCycles.push({cycle,secondary_node:secondary,primary,guards:guards(),death_passed:deathPassed,recreate_passed:recreatePassed});
    if(cycle===1&&deathPassed){
     cycle=2;frames=0;retireFrames=0;cleanupStarted=false;created=null;node2=null;p2Identity=null;cycleInitial=null;deathProbe=null;
     try{controller=prepareController();stage='armed';event('C_same_context_recreate_armed',{same_player_context:p2.toString(),prior_retirement:completedCycles[0],old_controller_pinned:true});}
     catch(e){stage='cleanup_unknown';event('C_cleanup_error',{error:String(e),note:'First cycle retired; second adapter preparation failed. Context/code remain pinned.'});}
    }else if(cycle===2&&deathPassed&&recreatePassed){event('C_death_recreate_bounded_passed',{same_player_context:p2.toString(),completed_cycles:completedCycles,
      player_storage_released:false,full_m0:false,note:'Two bounded Puppet lifecycles on one Player context. General respawn/map teardown/storage destruction remain unaccepted.'});}
   }
  }
  else if(retireFrames>=600){stage='retirement_pending';event('C_retirement_pending',{secondary,fixture,primary_intact:primaryIntact,
   note:'Bounded observation ended without complete native retirement. Keep storage/code pinned; no forced frees or refcount repair.'});
   if(projectileProbe)event('C_projectile_mode_final',projectileStatus('pending'));}
 }
}});
