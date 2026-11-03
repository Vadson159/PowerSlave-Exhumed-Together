/* Bounded M0 flow, concatenated with full_player_probe.js. Engine mutations use
   reviewed native calls on the simulation thread; old generations stay pinned. */
function createM0RespawnFlow() {
 prefix('give_weapon',0x1e06d0);
 const giveWeapon=native(0x1e06d0,'uint8',['pointer','int','uint8']);
 const input=createM0RespawnInput(cfg,event);
 let phase='dormant',start=null,phaseFrame=0,lastSwitch=-1000,attackFrame=null,deadFrame=null,press=null,afterFrame=null,newStart=null,primaryAtE=null;
 let old=null,oldHolder=null,maxMovement=0;
 const command=(buttons=0,turn=0,retire=false)=>({buttons,turn,retire});
 function pendingSnapshot(r){return {generation:r.generation,actor:r.actor.toString(),node:nodeView(r.node),pool_return_seen:r.pool_return_seen,native_destroyed:!!r.native_destroyed,
  owner:live(r)?r.actor.add(0x1b8).readPointer().toString():null};}
 function stop(reason){phase='stopped';event('C_manual_respawn_incomplete',{reason,frame:frames,full_m0:false});return command(0,0,true);}
 function tracked(rva,name,requiredVtable){
  prefix(name,rva);Interceptor.attach(base.add(rva),{onEnter(args){
   if(!simulationThread())return;
   const rec=[p2Identity,...retiredPuppets,...referenceCandidates].find(r=>r&&!r.invalid&&r.actor.equals(args[0]));
   if(!rec||!live(rec)||!args[0].readPointer().equals(base.add(requiredVtable)))return;
   this.record=rec;this.before={generation:rec.generation,actor:rec.actor.toString(),node:nodeView(rec.node),phase:name};
   event('C_manual_native_destructor_enter',this.before);
  },onLeave(){if(!this.record)return;
   // The deleting destructor may have freed the actor; touch only adapter-owned metadata.
   this.record.native_destroyed=true;this.record.invalid=true;event('C_manual_native_destructor_return',{...this.before,actor_read_after_return:false});
  }});
 }
 tracked(0x1d9370,'puppet_deleting_destructor',0x52d198);
 tracked(0x1e45e0,'projectile_deleting_destructor',0x52dad8);
 // full_player_probe's holder observer has already checked and attached this
 // native entry. Rechecking its live prologue would compare our trampoline.
 Interceptor.attach(base.add(0x19f210),{onEnter(args){
  if(!simulationThread())return;
  if(!args[0].readPointer().equals(base.add(0x52dad8)))return;
  const source=args[0].add(0x148).readPointer(),owner=[p2Identity,...retiredPuppets].find(r=>r&&r.node.equals(source));
  if(!owner)return;this.saved={actor:args[0],source,owner,before:nodeView(source)};
 },onLeave(){if(!this.saved)return;const q=this.saved;
  event('C_manual_native_source_owner_release',{actor:q.actor.toString(),source_node:q.source.toString(),before:q.before,
   source_owner_after:q.actor.add(0x148).readPointer().toString(),after:q.owner.native_destroyed?{not_read:'Puppet deleting destructor already returned'}:nodeView(q.source),
   native_rva:'0x19f210',native_calls_by_observer:0,world:giSnapshot()});
 }});
 return {
  active(){return phase!=='dormant'&&phase!=='stopped';},
  start(){
   if(phase!=='dormant')throw Error('Manual respawn flow may start only once');
   start={origin:projectileProbe.initial_primary.origin.slice(),yaw:projectileProbe.initial_primary.yaw,sector:projectileProbe.initial_primary.sector};
   oldHolder=referenceCandidates.find(r=>live(r)&&nativeClass(r.actor)==='projectile'&&r.actor.add(0x148).readPointer().equals(p2Identity.node));
   if(!oldHolder)throw Error('No actual native projectile holder for the old Puppet');
   hostGate.install();phase='machete';phaseFrame=frames;
   event('C_manual_respawn_flow_begin',{start,secondary:graph(p2),primary:graph(local),holder:referenceView(oldHolder),policy:'Native melee provokes AI, actual AI death, E event required; no forced source/target release'});
  },
  stateChange(state,pending){if(phase==='dormant'||phase==='stopped'||phase==='map_left')return;
   if(state!==3){phase='map_left';event('C_manual_map_left',{state,pending,
    identities:[p2Identity,...retiredPuppets].filter(Boolean).map(r=>({generation:r.generation,actor:r.actor.toString(),node:r.node.toString(),pool_return_seen:r.pool_return_seen,native_destroyed:!!r.native_destroyed})),
    player_handle:p2.add(0xd8).readPointer().toString(),engine_actor_reads:false,full_m0:false});}
  },
  step(){
   const s=graph(p2),p=graph(local),inv=inventory(p2);
   if(!p.live_identity||p.health<=0)return stop('host_did_not_remain_alive');
   if(!s.live_identity)return stop('secondary_generation_lost');
   if(phase==='machete'){
    if(s.health<=0)return stop('secondary_died_before_native_AI_fixture');
    if(frames-phaseFrame>300)return stop('machete_selection_timeout');
    if(inv.current!==0||inv.pending!==0){if(inv.pending!==0&&frames-lastSwitch>=30){lastSwitch=frames;return command(0x200);}return command();}
    const error=Math.atan2(Math.sin(start.yaw-s.yaw),Math.cos(start.yaw-s.yaw));
    if(Math.abs(error)>.03)return command(0,Math.max(-.08,Math.min(.08,error)));
    phase='approach_setup';phaseFrame=frames;return command();
   }
   if(phase==='approach_setup'){
    if(frames-phaseFrame<24)return command(4);
    const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(s.origin[i]));
    enemy=callOnce('manual_ai_spawn',()=>spawn(factory,1,v,s.yaw,s.sector));
    if(enemy.isNull())throw Error('Manual AI fixture Spawn null');enemyNode=enemy.add(0x10).readPointer();enemyIdentity=identity(enemy,enemyNode,'fixture_enemy');
    if(!live(enemyIdentity)||enemy.add(0x170).readPointer().readS32()!==1)throw Error('Manual AI fixture type mismatch');
    phase='provoke';phaseFrame=frames;event('C_manual_ai_spawn',{enemy:enemyGraph(),secondary:s});return command();
   }
   if(phase==='provoke'){
    const e=enemyGraph();if(!e||e.health<=0)return stop('AI_fixture_lost_before_death');
    if(enemyHit){
     if(e.target_node!==p2Identity.node.toString())return stop('native_AI_did_not_target_melee_attacker');
     phase='AI_death';phaseFrame=frames;event('C_manual_AI_provoked',{enemy:e,hit:enemyHit,secondary:s});return command();
    }
    if(frames-phaseFrame>360)return stop('native_melee_provocation_timeout');
    const dx=e.origin[0]-s.origin[0],dy=e.origin[1]-s.origin[1],distance=Math.hypot(dx,dy),desired=Math.atan2(dx,dy);
    const error=Math.atan2(Math.sin(desired-s.yaw),Math.cos(desired-s.yaw)),turn=Math.max(-.08,Math.min(.08,error));
    if(frames-phaseFrame<=24)return command(8,turn);
    if(!attackFrame&&distance<=110&&Math.abs(error)<.1){attackFrame=frames;event('C_manual_melee_attack_requested',{distance,error,enemy:e});}
    if(attackFrame&&frames-attackFrame<12)return command(1,turn);
    if(!attackFrame&&distance>110)return command(4,turn);return command(0,turn);
   }
   if(phase==='AI_death'){
    if(frames-phaseFrame>600)return stop('native_AI_death_timeout');
    if(s.health>0)return command();
    const damage=lastNativeDamage[s.actor];
    if(!(s.puppet_flags&0x200)||!damage||damage.source!==enemyIdentity.actor.toString()||damage.after>0)return stop('death_not_attributed_to_native_AI_fixture');
    old=p2Identity;old.generation=1;
    event('C_manual_respawn_AI_death',{secondary:s,primary:p,damage,old:pendingSnapshot(old),holder:live(oldHolder)?referenceView(oldHolder):null,controller:controller.counts()});
    removeOwned(enemyIdentity,'manual_ai_retire_after_death');
    input.arm();deadFrame=frames;phase='await_E';
    event('C_manual_respawn_await_E',{old:pendingSnapshot(old),secondary:s,primary:p,world:giSnapshot(),automatic_respawn:false});return command();
   }
   if(phase==='await_E'){
    if(s.health>0||!(s.puppet_flags&0x200)||s.death_timer!==0)return stop('dead_secondary_isolation_failed');
    press=input.take();if(!press){if(frames-deadFrame>600)return stop('bounded_E_input_deadline');return command();}
    if(!live(old)||old.node.equals(ptr(0)))throw Error('Old Puppet identity lost before E');
    primaryAtE={graph:p,inventory:inventory(local)};retiredPuppets.push(old);
    controller.stop();removeOwned(old,'manual_old_puppet_remove');
    callOnce('manual_player_reset',()=>reset(p2));
    const pending=pendingSnapshot(old);if(pending.node.refs<1||pending.node.payload!==old.actor.toString())throw Error('Old pending generation was not retained at E');
    callOnce('manual_starter_pistol',()=>giveWeapon(p2,1,0));
    const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(start.origin[i]));
    let fresh=null;installResolver();
    try{resolverEnter();try{fresh=callOnce('manual_new_puppet_spawn',()=>spawn(factory,0,v,start.yaw,start.sector));}finally{resolverLeave();}}
    finally{scope=0;resolverState.writeU32(0);restoreResolver();}
    if(fresh===null||fresh.isNull())throw Error('Respawn Spawn returned null');
    created=fresh;node2=p2.add(0xd8).readPointer();p2Identity=identity(fresh,node2,'secondary');p2Identity.generation=2;
    if(!live(p2Identity)||node2.equals(old.node)||fresh.equals(old.actor)||!live(old)||!fresh.add(0x1b8).readPointer().equals(p2))throw Error('Fresh generation binding differs from native contract');
    if(live(oldHolder)&&!oldHolder.actor.add(0x148).readPointer().equals(old.node))throw Error('Live old projectile lost its previous source generation');
    callOnce('manual_new_weapon_ready',()=>native(0x20f9e0,'void',['pointer','int'])(p2.add(0xe0),0));
    const after=graph(p2),kit=inventory(p2),cam=cameraFields(p2),primaryAfter=graph(local);
    if(after.health!==200||(after.puppet_flags&0x200)||after.death_timer!==0||kit.owned[0]!==1||kit.owned[1]!==1||!kit.owned.slice(2).every(x=>x===0)||kit.counters[1]!==60||!Object.values(cam).every(Number.isFinite)||cam.at_130!==104||cam.at_13c!==104||!guards())throw Error('Native respawn health/kit/camera contract not observed');
    if(!after.origin.every((x,i)=>x===start.origin[i])||after.sector!==start.sector||Math.abs(after.yaw-start.yaw)>.00001)throw Error('Native respawn did not preserve the saved level-start arguments');
    if(!samePrimary(primaryAtE.graph,primaryAfter)||!sameInventory(primaryAtE.inventory,inventory(local)))throw Error('Respawn changed primary graph/health/inventory');
    controller=prepareController();controller.bindIdentity(fresh,node2);controller.install();controller.set(frames,0,0);controller.enable();
    afterFrame=frames;newStart=after.origin.slice();phase='new_alive';
    event('C_manual_E_respawn_created',{press,old:pendingSnapshot(old),secondary:after,inventory:kit,camera:cam,primary_before:primaryAtE,primary_after:primaryAfter,
     holder:live(oldHolder)?referenceView(oldHolder):null,start,exact_start:after.origin.every((x,i)=>x===start.origin[i]),world:giSnapshot(),full_m0:false});return command();
   }
   if(phase==='new_alive'){
    if(s.health<=0||(s.puppet_flags&0x200))return stop('fresh_secondary_did_not_remain_alive');
    if(!samePrimary(primaryAtE.graph,p)||!sameInventory(primaryAtE.inventory,inventory(local)))return stop('primary_changed_during_respawn_observation');
    maxMovement=Math.max(maxMovement,Math.hypot(...s.origin.map((x,i)=>x-newStart[i])));
    if(frames-afterFrame<30)return command(8);
    if(frames-afterFrame>=120){
     if(maxMovement<=1||controller.counts().p2_controller<120||!guards())throw Error('Fresh secondary movement/updates/guards not observed');
     phase='await_unload';event('C_manual_respawn_bounded_observed',{secondary:s,inventory:inv,camera:cameraFields(p2),old:pendingSnapshot(old),
      holder:live(oldHolder)?referenceView(oldHolder):null,controller:controller.counts(),max_movement:maxMovement,primary:p,world:giSnapshot(),
      next:'Await normal menu/map exit while fresh P2 is alive; context/code remain pinned',full_m0:false});
    }return command();
   }
   if(phase==='await_unload')return command();
   return stop('unknown_manual_respawn_phase');
  }
 };
}
