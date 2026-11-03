/* One native Amun throw -> controlled native death -> queued E -> native respawn.
   Never mutate projectile/Actor/node/refs/targets/SourceOwner. All engine calls
   use the established end-of-simulation-tick seam; passive callbacks only read. */
function createM0ProjectileRespawnFlow() {
 prefix('give_weapon',0x1e06d0);
 const giveWeapon=native(0x1e06d0,'uint8',['pointer','int','uint8']);
 const input=createM0RespawnInput(cfg,event),user=Process.getModuleByName('user32.dll');
 const post=new NativeFunction(user.getExportByName('PostMessageW'),'int',['pointer','uint','uint64','int64'],{abi:'win64'});
 const windowOwner=new NativeFunction(user.getExportByName('GetWindowThreadProcessId'),'uint',['pointer','pointer'],{abi:'win64'});
 const mapKey=new NativeFunction(user.getExportByName('MapVirtualKeyW'),'uint',['uint','uint'],{abi:'win64'});
 let phase='dormant',phaseFrame=0,start=null,primary=null,old=null,shot=null,press=null,afterFrame=null,shotFrame=null;
 let damage=null,flightTicks=0,afterRespawnMoves=0,collisions=0,clipped=0,postMovement=0,firstAfter=null;
 let dtorEntries=0,dtorReturns=0;
 const cmd=(buttons=0,turn=0)=>({buttons,turn,retire:false});
 function sampleProjectile(){
  if(!shot||!live(shot)||nativeClass(shot.actor)!=='projectile')return null;
  return {actor:shot.actor.toString(),node:nodeView(shot.node),generation:shot.generation,
   source_owner:shot.actor.add(0x148).readPointer().toString(),target:shot.actor.add(0x28).readPointer().toString(),
   origin:[0x18,0x1c,0x20].map(o=>shot.actor.add(o).readFloat()),velocity:[0xe0,0xe4,0xe8].map(o=>shot.actor.add(o).readFloat()),
   sector:shot.actor.add(0x3c).readS32(),removed:shot.actor.add(0x40).readU8(),projectile_flags:shot.actor.add(0x1bc).readU32()};
 }
 function oldView(){if(!old)return null;return {actor:old.actor.toString(),node:nodeView(old.node),generation:old.generation,
  live:live(old),native_actor_destructor_returned:!!old.native_actor_destructor_returned,pool_return_seen:old.pool_return_seen};}
 function fail(reason){phase='incomplete';event('C_inflight_respawn_incomplete',{reason,frame:frames,projectile:sampleProjectile(),old:oldView(),full_m0:false});return cmd();}
 function unchangedPrimary(){return samePrimary(primary.graph,graph(local))&&sameInventory(primary.inventory,inventory(local));}
 // Entries are prechecked by full_player_probe before installing any observers.
 Interceptor.attach(base.add(0x1e4b10),{onEnter(args){
  if(!simulationThread()||!gameplayState()||stage!=='alive'||phase==='dormant'||phase==='incomplete')return;
  if(nativeClass(args[0])!=='projectile')return;
  const definition=args[0].add(0x170).readPointer();if(definition.isNull()||definition.readS32()!==27)return;
  if(!shot&&phase==='await_flight'&&args[0].add(0x148).readPointer().equals(p2Identity.node)){
   shot=candidate(args[0],'inflight_amun');shotFrame=frames;event('C_inflight_projectile_selected',{frame:frames,projectile:sampleProjectile(),secondary:graph(p2)});
  }
  if(!shot||!args[0].equals(shot.actor)||!live(shot))return;
  this.before=sampleProjectile();flightTicks++;
 },onLeave(){if(!this.before)return;const now=sampleProjectile();if(!now)return;
  if(this.before.source_owner!==now.source_owner)throw Error('Native flight changed the projectile owner generation');
  const moved=Math.hypot(...now.origin.map((v,i)=>v-this.before.origin[i]));
  if(afterFrame!==null){if(moved>.001)afterRespawnMoves++;if(!firstAfter)firstAfter=now.origin.slice();postMovement=Math.max(postMovement,Math.hypot(...now.origin.map((v,i)=>v-firstAfter[i])));}
  if(flightTicks<=8||flightTicks%10===0)event('C_inflight_projectile_tick',{phase,frame:frames,tick:flightTicks,before:this.before,after:now,moved,
   after_respawn:afterFrame!==null,old:oldView(),native_calls_by_observer:0});
 }});
 Interceptor.attach(base.add(0x1a4990),{onEnter(args){
  if(!simulationThread()||!gameplayState()||!shot||!live(shot)||!args[1].equals(shot.actor))return;
  this.saved={scratch:args[0],wanted:[0,4,8].map(o=>args[2].add(o).readFloat()),before:sampleProjectile()};
 },onLeave(ret){if(!this.saved||!live(shot))return;const q=this.saved,hit=(ret.toUInt32()&255)!==0;
  const end=[0xc,0x10,0x14].map(o=>q.scratch.add(o).readFloat());collisions++;
  if(hit){clipped++;if(clipped<=8)event('C_inflight_projectile_collision',{phase,frame:frames,hit,wanted:q.wanted,end,before:q.before,
   normal:[0x38,0x3c,0x40].map(o=>q.scratch.add(o).readFloat()),sector:q.scratch.add(0x24).readS32(),
   opaque_reference:q.scratch.add(0x30).readPointer().toString(),after_respawn:afterFrame!==null,native_calls_by_observer:0});}
 }});
 Interceptor.attach(base.add(0x19f210),{onEnter(args){
  if(!simulationThread()||!shot||!old||!args[0].equals(shot.actor)||!args[0].readPointer().equals(base.add(0x52dad8)))return;
  const source=args[0].add(0x148).readPointer();if(!source.equals(old.node))return;
  this.saved={actor:args[0],source,before:nodeView(source)};dtorEntries++;
  event('C_inflight_projectile_destructor_enter',{frame:frames,actor:args[0].toString(),source:source.toString(),refs_before:this.saved.before,entries:dtorEntries});
 },onLeave(){if(!this.saved)return;const q=this.saved;dtorReturns++;shot.native_actor_destructor_returned=true;shot.invalid=true;
  event('C_inflight_projectile_destructor_return',{actor:q.actor.toString(),source_before:q.source.toString(),source_after:q.actor.add(0x148).readPointer().toString(),
   refs_before:q.before,refs_after:old.native_actor_destructor_returned?{not_read:'Old Puppet lifetime already ended'}:nodeView(q.source),
   entries:dtorEntries,returns:dtorReturns,native_calls_by_observer:0,note:'Actor destructor does not free its own allocation; outer arena callback/free has not returned yet.'});
 }});
 return {active(){return phase!=='dormant';},start(){
  if(phase!=='dormant')throw Error('In-flight fixture may start only once');
  start={origin:projectileProbe.initial_primary.origin.slice(),yaw:projectileProbe.initial_primary.yaw,sector:projectileProbe.initial_primary.sector};
  primary={graph:graph(local),inventory:inventory(local)};
  if(Math.hypot(...primary.graph.origin.map((v,i)=>v-graph(p2).origin[i]))<900)throw Error('P1 was not parked outside the native blast fixture');
  phase='aim';phaseFrame=frames;event('C_inflight_respawn_begin',{start,primary,secondary:graph(p2),inventory:inventory(p2),
   policy:'Actual native Amun weapon action, one controlled native Damage death, one synthetic queued E. No reference repairs.'});
 },step(){
  if(phase==='incomplete'||phase==='await_exit')return cmd();
  if(!unchangedPrimary()||!guards())return fail('primary_or_guard_invariant');
  const s=graph(p2),inv=inventory(p2);
  if(!s.live_identity)return fail('secondary_identity_lost');
  if(phase==='aim'){
   const error=Math.atan2(Math.sin(start.yaw-s.yaw),Math.cos(start.yaw-s.yaw));
   if(Math.abs(error)>.02)return cmd(0,Math.max(-.08,Math.min(.08,error)));
   if(inv.current!==3||inv.pending!==3||!inv.counters[3])return fail('native_bomb_not_ready');
   projectileProbe.fire_inventory=inv;projectileProbe.shots++;phase='await_flight';phaseFrame=frames;
   event('C_inflight_throw_requested',{frame:frames,secondary:s,inventory:inv});return cmd(1);
  }
  if(phase==='await_flight'){
   if(frames-phaseFrame>180)return fail('actual_amun_flight_not_observed');
   const view=sampleProjectile();if(!view||flightTicks<2)return cmd();
   if(view.removed||(view.projectile_flags&4)||Math.hypot(...view.velocity)<.01)return fail('projectile_not_flying_at_death');
   old=p2Identity;old.generation=1;
   callOnce('inflight_controlled_death',()=>native(0x1a1370,'void',['pointer','pointer','int','int'])(old.actor,p1Identity.actor,0,200));
   const dead=graph(p2);if(dead.health>0||!(dead.puppet_flags&0x200))throw Error('Native controlled death failed');
   input.arm();phase='await_E';phaseFrame=frames;
   event('C_inflight_death_observed',{frame:frames,secondary:dead,projectile:sampleProjectile(),old:oldView(),primary:graph(local),controller:controller.counts()});
   const hwnd=ptr(cfg.respawn_fixture_hwnd),pid=Memory.alloc(4),tid=windowOwner(hwnd,pid);
   if(pid.readU32()!==cfg.pid||tid!==cfg.simulation_tid)throw Error('Queued E HWND ownership mismatch');
   const scan=mapKey(69,0),down=post(hwnd,0x100,69,1|(scan<<16)),up=post(hwnd,0x101,69,(1|(scan<<16)|0xc0000000)>>>0);
   event('C_inflight_E_posted',{hwnd:hwnd.toString(),pid:pid.readU32(),tid,down,up,after_dead:true,synthetic:true});
   if(!down||!up)throw Error('Bounded queued E failed');return cmd();
  }
  if(phase==='await_E'){
   if(frames-phaseFrame>60)return fail('queued_E_not_observed');
   press=input.take();if(!press)return cmd();
   const before=sampleProjectile();if(!before||before.removed||(before.projectile_flags&4)||!live(old))return fail('active_old_projectile_missing_at_E');
   retiredPuppets.push(old);controller.stop();removeOwned(old,'inflight_old_puppet_remove');
   callOnce('inflight_player_reset',()=>reset(p2));const pending=oldView();
   if(!pending.live||pending.node.refs<1||before.source_owner!==old.node.toString())throw Error('Old generation no longer valid with its native holder');
   callOnce('inflight_starter_pistol',()=>giveWeapon(p2,1,0));
   const v=Memory.alloc(12);[0,4,8].forEach((o,i)=>v.add(o).writeFloat(start.origin[i]));let fresh=null;
   installResolver();try{resolverEnter();try{fresh=callOnce('inflight_new_puppet_spawn',()=>spawn(factory,0,v,start.yaw,start.sector));}finally{resolverLeave();}}
   finally{scope=0;resolverState.writeU32(0);restoreResolver();}
   if(fresh===null||fresh.isNull())throw Error('In-flight respawn Spawn null');
   created=fresh;node2=p2.add(0xd8).readPointer();p2Identity=identity(fresh,node2,'secondary');p2Identity.generation=2;
   if(!live(p2Identity)||node2.equals(old.node)||fresh.equals(old.actor)||!live(old)||!fresh.add(0x1b8).readPointer().equals(p2))throw Error('Native fresh generation binding failed');
   if(!shot.actor.add(0x148).readPointer().equals(old.node))throw Error('Respawn changed the old projectile SourceOwner');
   callOnce('inflight_new_weapon_ready',()=>native(0x20f9e0,'void',['pointer','int'])(p2.add(0xe0),0));
   const after=graph(p2),kit=inventory(p2),camera=cameraFields(p2);
   if(after.health!==200||after.death_timer!==0||!after.origin.every((v,i)=>v===start.origin[i])||after.sector!==start.sector||
     kit.owned[0]!==1||kit.owned[1]!==1||!kit.owned.slice(2).every(v=>v===0)||kit.counters[1]!==60||camera.at_130!==104||camera.at_13c!==104||
     !Object.values(camera).every(Number.isFinite)||!unchangedPrimary()||!guards())throw Error('Fresh kit/camera/start/primary contract failed');
   controller=prepareController();controller.bindIdentity(fresh,node2);controller.install();controller.set(frames,0,0);controller.enable();
   afterFrame=frames;phase='after_respawn';
   event('C_inflight_E_respawn_created',{frame:frames,press,old:oldView(),secondary:after,inventory:kit,camera,projectile_before:before,projectile_after:sampleProjectile(),primary:graph(local),world:giSnapshot()});return cmd();
  }
  if(phase==='after_respawn'){
   const hit=lastNativeDamage[s.actor];
   if(hit&&hit.after<hit.before&&hit.source_owner===p2.toString()&&(hit.source_attribution?.generation??hit.source_attribution?.player_generation)===1){
    if(!damage){damage=hit;event('C_inflight_original_shooter_damage',{frame:frames,damage,old:oldView(),secondary:s,projectile:sampleProjectile()});}
   }
   if(s.health<=0)return fail('fresh_P2_died_before_bounded_observation');
   if(damage&&frames-afterFrame>=120&&afterRespawnMoves>=2&&clipped>0){
    phase='await_exit';event('C_inflight_respawn_bounded_observed',{frame:frames,shot_frame:shotFrame,after_frame:afterFrame,flight_ticks:flightTicks,
     after_respawn_moves:afterRespawnMoves,post_movement:postMovement,collisions,clipped,damage,old:oldView(),secondary:s,primary:graph(local),
     projectile:sampleProjectile(),controller:controller.counts(),guards:guards(),next:'Normal menu exit for native retained-owner destructor release; do not force refs0',full_m0:false});return cmd();
   }
   if(frames-afterFrame>480)return fail('bounded_motion_collision_or_original_shooter_damage_not_observed');
   const view=sampleProjectile();if(view&&!view.removed&&!(view.projectile_flags&4)&&!damage){
    const dx=view.origin[0]-s.origin[0],dy=view.origin[1]-s.origin[1],distance=Math.hypot(dx,dy),yaw=Math.atan2(dx,dy);
    const error=Math.atan2(Math.sin(yaw-s.yaw),Math.cos(yaw-s.yaw));return cmd(distance>210&&Math.abs(error)<.15?4:0,Math.max(-.08,Math.min(.08,error)));
   }
   return cmd();
  }
  return fail('unknown_inflight_phase');
 }};
}
