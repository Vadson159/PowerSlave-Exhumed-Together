// One stock passive actor, no Player construction/rebinding and no game-field writes.
const control=Memory.alloc(16);control.writeU32(0);
let phase='waiting',ticks=0,inTick=false,updates=0,spawnAttempted=false,removeAttempted=false,actor=null,node=null,retireNode=null;
let actorIdentity='',nodeIdentity='',lifeTicks=0,spawnTick=0,removeTick=0,spawnCount=0,removeCount=0,retireCount=0,baseline=null,firstSample=null,sectorCount=null;
let actualFactory=null,foreign=0,blocked=false,inSweep=false,scope=false,factoryWitness=false;
const nativeSpawn=new NativeFunction(base.add(cfg.sites.factory.rva),'pointer',['pointer','int','pointer','float','int'],{abi:'win64',exceptions:'propagate'});
const nativeRemove=new NativeFunction(base.add(cfg.sites.remove.rva),'void',['pointer'],{abi:'win64',exceptions:'propagate'});
const pos=Memory.alloc(12);cfg.position.forEach((v,i)=>pos.add(i*4).writeFloat(v));
function stop(reason){if(blocked)return;blocked=true;phase='blocked';emit('blocked',{reason,ticks,actor_identity:actorIdentity,node_identity:nodeIdentity,spawnCount,removeCount,foreign});}
function ownership(p){return validPrimary(p)&&JSON.stringify(protectedPrimary(p))===JSON.stringify(baseline);}
function status(){
 const p=primary(),membership=list(node);
 if(!ownership(p))throw Error('P1 protected state changed');
 const a={pointer:actor.toString(),node:actor.add(0x10).readPointer().toString(),payload:node.add(0x28).readPointer().toString(),vtable:actor.readPointer().toString(),position:vec(actor.add(0x18)),yaw:actor.add(0x38).readFloat(),sector:actor.add(0x3c).readS32(),flags:actor.add(0x16c).readU32(),removed:actor.add(0x40).readU8(),refs:node.add(0x40).readS32(),in_use:node.add(0x24).readU8(),definition:actor.add(0x170).readPointer().toString(),animation:actor.add(0x108).readPointer().toString(),scale:actor.add(0x124).readFloat(),target:actor.add(0x28).readPointer().toString(),source_owner:actor.add(0x148).readPointer().toString()};
 if(a.vtable!==base.add(0x528608).toString()||a.node!==nodeIdentity||a.payload!==actorIdentity||a.sector!==259||a.flags!==0x40||a.removed!==0||a.refs!==0||a.in_use!==1||a.animation==='0x0'||a.target!=='0x0'||a.source_owner!=='0x0'||membership.found!==1||membership.count!==sectorCount+1||JSON.stringify(a.position)!==JSON.stringify(cfg.position))throw Error('passive actor/list invariant');
 return {ticks,lifeTicks,phase,spawnCount,removeCount,actor:a,primary:p,sector_list:membership,p1_protected_unchanged:true};
}
Interceptor.attach(base.add(cfg.sites.factory.rva),{onEnter(args){this.ours=scope;if(!this.ours)return;if(!thread()||!inTick){foreign++;stop('factory affinity');return;}actualFactory={rcx:args[0].toString(),edx:args[1].toInt32(),r8:args[2].toString(),position:vec(args[2]),entry_rsp:this.context.rsp.toString(),sector_entry_rsp_28:this.context.rsp.add(0x28).readS32(),return_address:this.returnAddress.toString()};if(actualFactory.edx!==1007||actualFactory.sector_entry_rsp_28!==259||!args[0].equals(base.add(0x5a10a0)))stop('factory ABI witness');},onLeave(result){if(this.ours){factoryWitness=true;emit('factory_abi_witness',{arguments:actualFactory,returned:result.toString(),yaw_post_return:!result.isNull()?result.add(0x38).readFloat():null});}}});
Interceptor.attach(base.add(cfg.sites.update.rva),{onEnter(args){if(args[0].equals(local)&&thread()&&inTick)updates++;}});
Interceptor.attach(base.add(cfg.sites.actor_tick.rva),{onEnter(args){this.ours=actor&&args[0].equals(actor);if(!this.ours)return;if(!thread()||!inTick){foreign++;stop('actor tick affinity');return;}if(phase==='live')lifeTicks++;},onLeave(){if(this.ours&&phase==='live'&&!blocked){try{if(!ownership(primary()))throw Error('P1 changed around actor tick');}catch(e){stop(String(e));}}}});
Interceptor.attach(base.add(cfg.sites.pool_return.rva),{onEnter(args){this.ours=actor&&args[1].equals(actor);if(!this.ours)return;if(!thread()||!inTick||!inSweep||!removeAttempted){foreign++;stop('unexpected retirement');return;}emit('pool_return_entry',{ticks,actor:actorIdentity,node:nodeIdentity,factory:args[0].toString(),return_address:this.returnAddress.toString(),own_node_cleared:actor.add(0x10).readPointer().isNull(),onremove_callback:base.add(0x528608+0x28).readPointer().toString(),refs:node.add(0x40).readS32(),actor_reads_after_entry:false});retireNode=node;actor=null;node=null;phase='retiring';retireCount++;},onLeave(){if(this.ours)emit('pool_return_complete',{ticks,actor:actorIdentity,actor_dereferenced:false});}});
Interceptor.attach(base.add(cfg.sites.sweep.rva),{onEnter(){this.ours=thread();if(this.ours)inSweep=true;},onLeave(){if(!this.ours)return;try{if(retireNode){const n=retireNode;const proof={ticks,node:nodeIdentity,payload:n.add(0x28).readPointer().toString(),refs:n.add(0x40).readS32(),in_use:n.add(0x24).readU8(),sector_list:list(n),retireCount};retireNode=null;if(proof.payload!=='0x0'||proof.refs!==0||proof.in_use!==0||proof.sector_list.found!==0||proof.sector_list.count!==sectorCount)throw Error('retirement postcondition');phase='retired';emit('deferred_retired',proof);}}catch(e){stop(String(e));}inSweep=false;}});
Interceptor.attach(base.add(cfg.sites.tick.rva),{onEnter(args){this.ours=thread()&&args[0].equals(gi);if(this.ours){inTick=true;ticks++;this.before=updates;}},onLeave(){if(!this.ours)return;try{
 if(blocked||phase==='shutdown')return;
 const w=world(),p=primary(),command=control.readU32();
 if(spawnAttempted&&!stableWorld(w)){stop('world/transition invalidated');return;}
 if(!stableWorld(w)||!validPrimary(p)||updates===this.before)return;
 if(phase==='waiting'&&command===1){
  if(spawnAttempted)throw Error('duplicate spawn');
  if(p.sector!==259||JSON.stringify(p.pos)!==JSON.stringify(cfg.initial_position))throw Error('fixed initial P1 witness changed');
  const geom=geometry(cfg.position),dx=cfg.position[0]-p.pos[0],dy=cfg.position[1]-p.pos[1],forward=Math.sin(p.yaw)*dx+Math.cos(p.yaw)*dy;
  if(!geom.inside||forward<=0)throw Error('fixed point geometry/view preflight');
  if(!base.add(0x5a10a0).readPointer().add(0x18).readPointer().equals(base.add(cfg.sites.factory.rva)))throw Error('factory vtable');
  sectorCount=list(null).count;baseline=protectedPrimary(p);firstSample=p;
  emit('spawn_preflight',{ticks,world:w,primary:p,geometry:geom,forward,factory:base.add(0x5a10a0).toString(),sector_list:list(null)});
  spawnAttempted=true;spawnTick=ticks;phase='spawning';scope=true;
  const result=nativeSpawn(base.add(0x5a10a0),1007,pos,0,259);scope=false;
  if(blocked||result.isNull()||!factoryWitness)throw Error('native factory failure');
  actor=result;node=actor.add(0x10).readPointer();actorIdentity=actor.toString();nodeIdentity=node.toString();spawnCount++;phase='live';
  if(JSON.stringify(p)!==JSON.stringify(primary()))throw Error('P1 changed by spawn');
  emit('spawned',Object.assign(status(),{spawnTick,p1_exact_unchanged:true,game_field_writes:0,mod_node_refs:0,lifetime_generation:'one-shot experimental identity 1; no native generation established'}));
 }else if(phase==='live'){
  const s=status();if(lifeTicks<=3||lifeTicks%120===0)emit('stable',s);
  if(command===2){if(lifeTicks<120)throw Error('remove before minimum stability');if(removeAttempted)throw Error('duplicate Remove');
   removeAttempted=true;removeCount++;removeTick=ticks;phase='removing';emit('remove_request',{ticks,lifeTicks,actor:actorIdentity,node:nodeIdentity,primary:p});
   nativeRemove(actor);
   if(JSON.stringify(p)!==JSON.stringify(primary()))throw Error('P1 changed by Remove');
   emit('remove_return',{ticks,lifeTicks,flags:actor.add(0x16c).readU32(),removed:actor.add(0x40).readU8(),refs:node.add(0x40).readS32(),sector_list:list(node),p1_exact_unchanged:true,destructor_completion:false});phase='pending';
  }
 }else if(phase==='pending'&&ticks-removeTick>120)throw Error('deferred retirement timeout');
 else if(phase==='retired'&&ticks%120===0){if(!ownership(p))throw Error('P1 after retirement');emit('post_retire',{ticks,lifeTicks,spawnCount,removeCount,retireCount,primary:p,sector_list:list(null),weak_actor_null:actor===null,weak_node_null:node===null,retire_node_null:retireNode===null,actor_reads:false});}
 }catch(e){scope=false;stop(String(e));}finally{inTick=false;}}});
Interceptor.attach(base.add(cfg.sites.state_exit.rva),{onEnter(){if(gi.add(0x24).readS32()===3&&gi.add(0x20).readS32()!==3){stop('unexpected level exit');actor=null;node=null;retireNode=null;}}});
emit('probe_loaded',{pid:Process.id,image_base:base.toString(),expected_tid:cfg.simulation_tid,control:control.toString(),source_sha256:cfg.source_sha256,binary_sha256_host_verified:cfg.binary_sha256,actor_type:1007,position:cfg.position,sector:259,player_hooks:'read-only Update observer; original always executes',game_field_writes:0});
