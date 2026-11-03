// One stock Actor1007. No camera/input replacement, flag edit, network, or pointer search.
const control=Memory.alloc(16);control.writeByteArray(new Uint8Array(16));
const positionBuffer=Memory.alloc(12);cfg.position.forEach((v,i)=>positionBuffer.add(i*4).writeFloat(v));
const spawnNative=new NativeFunction(base.add(cfg.sites.factory.rva),'pointer',['pointer','int','pointer','float','int'],{abi:'win64',exceptions:'propagate'});
const removeNative=new NativeFunction(base.add(cfg.sites.remove.rva),'void',['pointer'],{abi:'win64',exceptions:'propagate'});
let inTick=false,worldStopping=false,inSweep=false,stateCalls=0,p1TickThisCall=0,p1NativeTicks=0,p1NativeSteps=0,lastP1Stamp=null;
let viewCache=null,viewFrames=0,preflightSent=false,initialPrimary=null,baselineInventory=null,baselineList=null,spawnStamp=null;
let expectedSprite=null,spriteSubmissions=0,spriteVertices=0,inOwnSprite=false,postRemoveSubmissions=0,postRetireSteps=0,foreignCalls=0,damageCallbacks=0;
let readyFrames=0,lastEntry=null,afterRemove=null,removeStateCall=null,lastCommand=-1,lastPostRetireStamp=null;
function safeEmit(event,data={}){emit(event,Object.assign({state_calls:stateCalls,engine_stamp:base.add(0xaec390).readU32()},data));}
function affinity(){if(!thread()||!inTick||worldStopping)throw Error('native operation outside attested simulation scope');}
function sameGraph(p){return validPrimary(p)&&initialPrimary&&equal(graph(p),graph(initialPrimary));}
function validateActor(a,strict=true){
 affinity();if(!stableWorld(world()))throw Error('native world not stable');
 const p=primary();if(!sameGraph(p))throw Error('P1 ownership changed');
 const id=actorFacts(a),f=id.facts,m=list(id.node);
 if(a.toString()===p.actor||id.node.toString()===p.node||f.vtable!==base.add(0x528608).toString()||f.payload!==a.toString()||f.type!==1007||f.sector!==259||!equal(f.position,cfg.position)||f.removed!==0||f.in_use!==1||!passiveSectorMembership57(m,id.node.toString(),p))throw Error('returned Actor/ObjectNode/sector identity invalid');
 if(strict&&(f.flags!==0x40||f.refs!==0||f.scale!==1.5||f.target!=='0x0'||f.source_owner!=='0x0'||!equal(f.bounds_min,[-12,-12,-24])||!equal(f.bounds_max,[12,12,24])))throw Error('Actor passive properties changed');
 return {node:id.node,facts:{actor:f,sector_list:m,p1:p,p1_inventory_unchanged:equal(inventory(p),baselineInventory)}};
}
const life=new OneActor57C({
 emit:safeEmit,
 spawn:()=>{affinity();spawnStamp=base.add(0xaec390).readU32();const before=primary();safeEmit('native_factory_call',{actor_type:1007,factory:base.add(0x5a10a0).toString(),position:cfg.position,sector:259,yaw:0,primary_before:before});const result=spawnNative(base.add(0x5a10a0),1007,positionBuffer,0,259);return result;},
 validate:a=>{const id=validateActor(a);expectedSprite={frame:id.facts.actor.frame,origin:cfg.position,scale:1.5,sector:259,flags:0x40};return id;},
 diagnostic:()=>{if(!equal(inventory(primary()),baselineInventory))throw Error('P1 inventory changed by Spawn');return {nested_factory_listener_required:false,return_pointer_is_primary_authority:true,abi_source:'exact SHA/static stack+0x28 verified; returned node/list validated directly'};},
 validateRemoval:(a,n)=>{const id=validateActor(a,false);if(!id.node.equals(n)||id.facts.actor.flags!==0x40)throw Error('cleanup identity/flags changed');if(bytes(base.add(cfg.sites.remove.rva),16)!==cfg.sites.remove.prefix)throw Error('Remove prefix changed');},
 remove:a=>{affinity();const before=primary();removeStateCall=stateCalls;removeNative(a);const p=primary();afterRemove={flags:a.add(0x16c).readU32(),removed:a.add(0x40).readU8(),refs:life.node.add(0x40).readS32(),sector_list:list(life.node),p1_exact_unchanged:equal(before,p),primary:p};safeEmit('native_remove_postcondition',afterRemove);if(!afterRemove.p1_exact_unchanged||afterRemove.removed!==1||(afterRemove.flags&0x2000)===0)throw Error('Remove native postcondition');}
});
function fault(reason,unsafe=false){
 life.faults.push(reason);
 if(unsafe)life.phase='blocked_unsafe';else life.requestCleanup(reason);
 life.log('experiment_fault',Object.assign(life.snapshot(),{reason,unsafe}));
}
function recordTiles(frame){
 const f=ptr(frame),p=f.add(0x50).readPointer(),count=f.add(0x5c).readU32();if(p.isNull()||count===0||count>16)throw Error('sprite frame tile bounds');
 const tiles=[];for(let i=0;i<count;i++){const row=p.add(i*16),sprite=row.readPointer(),id=row.add(8).readU16();if(sprite.isNull()||id>64)throw Error('sprite tile identity');const rect=sprite.readPointer().add(id*0x28);tiles.push({sprite:sprite.toString(),id,x_offset:row.add(0xa).readS16(),y_offset:row.add(0xc).readS16(),flip:row.add(0xe).readU8(),width:rect.add(0xc).readS32(),height:rect.add(0x10).readS32(),texture:sprite.add(0x18).readPointer().toString()});}
 return {frame,frame_flags:f.add(4).readU32(),tiles};
}
Interceptor.attach(base.add(cfg.sites.scene.rva),{onEnter(args){
 if(worldStopping)return;try{
  if(!stableWorld(world()))return;const p=primary();if(!validPrimary(p))return;
  const v=preparedView(args[0],args[1]);viewFrames++;v.frame=viewFrames;v.engine_stamp=base.add(0xaec390).readU32();v.tid=Process.getCurrentThreadId();v.primary=p;v.gi_z_terms=[gi.add(0x1cc).readFloat(),gi.add(0x1b4).readFloat()];
  v.old_box=Math57C.box(v,cfg.old_position);v.new_box=Math57C.box(v,cfg.position);viewCache=v;
  if(life.phase==='waiting'&&v.sector_matches&&v.new_box.frustum.full_inside&&v.new_box.clip_full_inside){readyFrames++;if(readyFrames===1)safeEmit('camera_preflight',v);}
 }catch(e){safeEmit('camera_observation_error',{error:String(e)});}
}});
Interceptor.attach(base.add(cfg.sites.sprite.rva),{onEnter(args){
 this.ours=false;if(!expectedSprite||worldStopping)return;
 try{const e=args[2],origin=vec(e.add(0x10)),frame=e.readPointer().toString();
 if(frame!==expectedSprite.frame||!origin.every((v,i)=>Math.abs(v-cfg.position[i])<.01)||e.add(0x50).readS32()!==259||e.add(0x4c).readU32()!==0x40||e.add(8).readFloat()!==1.5)return;
 this.ours=true;this.vertices=spriteVertices;inOwnSprite=true;spriteSubmissions++;
 if(life.removeAttempted){postRemoveSubmissions++;safeEmit('sprite_after_remove',{phase:life.phase,origin,frame});}
 if(spriteSubmissions<=2){lastEntry={frame,origin,scale:e.add(8).readFloat(),flags:e.add(0x4c).readU32(),sector:e.add(0x50).readS32(),normal:e.add(0x54).readU8(),transparency:e.add(0x55).readU8(),view_eye:vec(args[0].add(0xc0)),tiles:recordTiles(frame)};safeEmit('sprite_submission',lastEntry);}
 }catch(e){fault('sprite observation: '+String(e));}
 },onLeave(){if(this.ours){inOwnSprite=false;if(spriteSubmissions<=2)safeEmit('sprite_submission_return',{vertices_emitted:spriteVertices-this.vertices,sprite_submissions:spriteSubmissions});}}});
Interceptor.attach(base.add(cfg.sites.vertex.rva),{onEnter(){if(inOwnSprite)spriteVertices++;}});
Interceptor.attach(base.add(cfg.sites.puppet_tick.rva),{onEnter(args){
 if(!thread()||!inTick)return;const n=local.add(0xd8).readPointer();if(n.isNull()||!args[0].equals(n.add(0x28).readPointer()))return;
 p1TickThisCall++;p1NativeTicks++;const stamp=base.add(0xaec390).readU32();if(stamp!==lastP1Stamp){lastP1Stamp=stamp;p1NativeSteps++;}
}});
Interceptor.attach(base.add(cfg.sites.actor_tick.rva),{onEnter(args){if(life.actor&&args[0].equals(life.actor)){if(!thread()||!inTick){foreignCalls++;fault('Actor Tick affinity',true);return;}life.actorTick();}}});
Interceptor.attach(base.add(cfg.sites.damage.rva),{onEnter(args){if(life.actor&&args[0].equals(life.actor)){damageCallbacks++;fault('unexpected Actor damage callback');}}});
Interceptor.attach(base.add(cfg.sites.on_remove.rva),{onEnter(args){this.ours=life.actor&&args[0].equals(life.actor);if(this.ours)safeEmit('native_on_remove_entry',life.snapshot());},onLeave(){if(this.ours&&life.actor)safeEmit('native_on_remove_return',{own_node_cleared:life.actor.add(0x10).readPointer().isNull()});}});
Interceptor.attach(base.add(cfg.sites.pool_return.rva),{onEnter(args){
 this.ours=life.actor&&args[1].equals(life.actor);if(!this.ours)return;
 const validScope=thread()&&inTick&&inSweep&&life.removeAttempted;
 const facts=validScope?{own_node_cleared:life.actor.add(0x10).readPointer().isNull(),node_refs:life.node.add(0x40).readS32(),factory:args[0].toString(),return_address:this.returnAddress.toString()}:{};
 life.poolReturn(args[1]);safeEmit('native_pool_return_entry',Object.assign(facts,{valid_scope:validScope,actor_reads_after_entry:false}));
 if(!validScope){life.retireNode=null;fault('retirement outside controlled sweep',true);}
 },onLeave(){if(this.ours)safeEmit('native_pool_return_complete',{actor_dereferenced:false});}});
Interceptor.attach(base.add(cfg.sites.sweep.rva),{onEnter(){this.ours=thread()&&inTick;if(this.ours)inSweep=true;},onLeave(){if(!this.ours)return;try{
 if(life.retireNode){const n=life.retireNode,m=list(n),facts={payload_null:n.add(0x28).readPointer().isNull(),refs:n.add(0x40).readS32(),in_use:n.add(0x24).readU8(),sector_occurrences:m.found,sector_list:m};life.finishRetirement(facts);}
 }catch(e){life.retireNode=null;fault('retirement witness: '+String(e),true);}inSweep=false;}});
Interceptor.attach(base.add(cfg.sites.drain.rva),{onEnter(){this.ours=thread()&&inTick&&life.phase==='pending';if(this.ours&&life.node)safeEmit('deferred_drain_entry',{refs:life.node.add(0x40).readS32(),sector_list:list(life.node)});},onLeave(){if(this.ours&&life.node)safeEmit('deferred_drain_return',{refs:life.node.add(0x40).readS32(),payload:life.node.add(0x28).readPointer().toString(),in_use:life.node.add(0x24).readU8(),sector_list:list(life.node)});}});
Interceptor.attach(base.add(cfg.sites.tick.rva),{onEnter(args){this.ours=thread()&&args[0].equals(gi);if(this.ours){inTick=true;stateCalls++;p1TickThisCall=0;}},onLeave(){
 if(!this.ours)return;try{
  if(worldStopping||life.phase==='blocked_unsafe')return;const w=world();if(!stableWorld(w)){if(life.spawnAttempted)fault('world no longer stable',true);return;}
  const p=primary();if(!validPrimary(p)||p1TickThisCall===0)return;
  const command=control.readU32();if(command!==lastCommand){lastCommand=command;safeEmit('mailbox_observed',{command,phase:life.phase});}
  if(life.phase==='waiting'){
   const m=list(),geometry=sectorGeometry(cfg.position),sectorBox=Math57C.planes(geometry.planes.map(x=>[...x.n,x.d]),Math57C.corners(cfg.position));
   if(!initialPrimary){initialPrimary=p;baselineInventory=inventory(p);baselineList=m;}
   const ready=p1NativeSteps>=30&&readyFrames>=3&&viewCache&&viewCache.sector_matches&&viewCache.new_box.frustum.full_inside&&viewCache.new_box.clip_full_inside&&sectorBox.full_inside&&p.sector===259&&equal(p.pos,cfg.initial_position)&&sameGraph(p)&&m.count===1&&m.entries[0].payload===p.actor&&bytes(base.add(cfg.sites.factory.rva),16)===cfg.sites.factory.prefix&&base.add(0x5a10a0).readPointer().add(0x18).readPointer().equals(base.add(cfg.sites.factory.rva));
   if(ready&&!preflightSent){preflightSent=true;safeEmit('native_preflight',{world:w,primary:p,sector_list:m,geometry,sector_box:sectorBox,camera:viewCache,p1_native_steps:p1NativeSteps,ready_frames:readyFrames,existing_experimental_actor:false});}
   if(command===1&&ready){life.create();}
  }else if(life.phase==='live'){
   const id=validateActor(life.actor);if(!equal(inventory(p),baselineInventory)){fault('P1 health/weapon inventory changed');}
   const stamp=base.add(0xaec390).readU32();if(life.simulationStep(stamp)&&(life.simulationTicks<=3||life.simulationTicks%60===0))safeEmit('stable',Object.assign(life.snapshot(),id.facts,{p1_native_steps:p1NativeSteps,sprite_submissions:spriteSubmissions,sprite_vertices:spriteVertices,damage_callbacks:damageCallbacks}));
   if(command===2)life.requestCleanup('explicit_remove');
   if(life.simulationTicks>=300&&spriteSubmissions===0)fault('no genuine sprite submission');
   if(life.simulationTicks>=cfg.maximum_simulation_ticks)life.requestCleanup('bounded_lifetime_limit');
   if(life.cleanupReason)life.cleanup();
  }else if(life.phase==='pending'&&stateCalls-removeStateCall>120)fault('deferred retirement deadline',true);
  else if(life.phase==='retired'){
   if(!sameGraph(p)||!equal(inventory(p),baselineInventory))fault('P1 changed after retirement',true);
   const stamp=base.add(0xaec390).readU32();if(stamp!==lastPostRetireStamp){lastPostRetireStamp=stamp;postRetireSteps++;if(postRetireSteps<=3||postRetireSteps%60===0)safeEmit('post_retire',Object.assign(life.snapshot(),{primary:p,p1_inventory_unchanged:equal(inventory(p),baselineInventory),sector_list:list(),all_weak_pointers_null:life.actor===null&&life.node===null&&life.retireNode===null,post_retire_steps:postRetireSteps,sprite_submissions_after_remove:postRemoveSubmissions,actor_reads:false}));}
  }
  // Telemetry failures after create still reach the planned safe cleanup path.
  if(life.cleanupReason&&life.phase==='live'&&!life.removeAttempted)life.cleanup();
 }catch(e){
  try{fault('simulation observation: '+String(e));if(life.identitySafe&&!life.removeAttempted)life.cleanup();}catch(cleanupError){fault('safe cleanup refused: '+String(cleanupError),true);}
 }finally{inTick=false;}
}});
Interceptor.attach(base.add(cfg.sites.state_exit.rva),{onEnter(){if(gi.add(0x24).readS32()===3&&gi.add(0x20).readS32()!==3){worldStopping=true;const phase=life.phase,present=life.actor!==null;life.actor=null;life.node=null;life.retireNode=null;life.identitySafe=false;expectedSprite=null;if(phase!=='retired')fault('unplanned teardown with live/unknown Actor',true);life.log('world_exit_observed',{phase,weak_actor_present:present,all_weak_pointers_invalidated:true});}}});
emit('probe_loaded',{pid:Process.id,image_base:base.toString(),expected_tid:cfg.simulation_tid,control:control.toString(),source_sha256:cfg.source_sha256,binary_sha256_host_verified:cfg.binary_sha256,actor_type:1007,position:cfg.position,old_position:cfg.old_position,sector:259,simulation_measure:'distinct engine stamps with original P1 Puppet Tick inside StateTick; Actor Tick separate',camera_writes:0,game_field_writes:0,nested_factory_listener_required:false});
