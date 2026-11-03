// DOC58B native adapter (Frida side of the client DLL seam). Bound after DOC58A common/inventory/camera/
// cross_sector/one_actor/controller/movement_native (unchanged) and net_mapping58b. KEX mutations happen
// only inside DLL callbacks, i.e. on the attested Simulation thread inside the DLL's StateTick detour.
// The DLL owns StateTick/Update/StateExit hooks; this script never attaches to them.
const dll=Process.getModuleByName(cfg.dll_name);
if(dll.path.toLowerCase()!==cfg.dll_path.toLowerCase())throw Error('wrong client DLL');
for(const [name,s] of Object.entries(cfg.dll_owned))if(bytes(base.add(s.rva),1)!=='e9')throw Error('DLL-owned site not MinHook-patched: '+name);
const control=Memory.alloc(16);control.writeByteArray(new Uint8Array(16));
const positionBuffer=Memory.alloc(12);
const spawnNative=new NativeFunction(base.add(cfg.sites.factory.rva),'pointer',['pointer','int','pointer','float','int'],{abi:'win64',exceptions:'propagate'});
const removeNative=new NativeFunction(base.add(cfg.sites.remove.rva),'void',['pointer'],{abi:'win64',exceptions:'propagate'});
let inTick=false,worldStopping=false,inSweep=false,stateCalls=0,p1TickThisCall=0,p1NativeTicks=0,p1NativeSteps=0,lastP1Stamp=null,viewCache=null,viewFrames=0;
let initialPrimary=null,baselineInventory=null,stateBefore=null,handleSeq=0,liveHandle=0,spawnTarget=null,expectedSprite=null,currentCommand=null;
let spriteSubmissions=0,spriteVertices=0,inOwnSprite=false,postRemoveSubmissions=0,ambiguousSprites=0,intermediateSprites=0,oursSprites=0,foreignCalls=0,damageCallbacks=0,lerpSamples=0,lastLerp=null;
let removeStateCall=null,removeStep=null,afterRemove=null,lastRenderRevision=-1,area=null,allSectors=null,adapterRemoveRequest=false,fatal=false,deferred=0,unchanged=0,updatesApplied=0;
let snapAfterSpawn=false,snapAfterRemove=false,snapPost=false,nextLiveSnapshot=1800,postRetireSteps=0,lastPostRetireStamp=null,lastLocal=null;
const refusals={},revisionCommand={},QUIET=new Set(['controller_status','movement_applied','proxy_update','native_interpolation_sample','sprite_submission','sprite_intermediate_submission','update_refused']);
function safeEmit(event,data={}){const d=Object.assign({event,seq:++serial,utc:new Date().toISOString(),tid:Process.getCurrentThreadId(),state_calls:stateCalls,engine_stamp:base.add(0xaec390).readU32()},data);journal.write(JSON.stringify(d)+'\n');journal.flush();if(!QUIET.has(event))send(d);}
function affinity(){if(!thread()||!inTick||worldStopping)throw Error('native operation outside attested simulation scope');}
function sameGraph(p){return validPrimary(p)&&initialPrimary&&equal(graph(p),graph(initialPrimary));}
function stateSnapshot(label,p){
 const s=snapshotP1State57(label,p);safeEmit('p1_state_snapshot',{snapshot:s});
 if(s.status!=='OK')throw Error('mandatory P1 snapshot UNKNOWN: '+label);
 if(stateBefore){const c=compareP1State57(stateBefore,s);safeEmit('p1_state_comparison',{label,comparison:c});if(c.status!=='EQUAL')throw Error('P1 state comparison '+c.status+': '+label);}
 return s;
}
const faultSeen={};
function fault(reason,unsafe=false){
 if(unsafe){fatal=true;if(life.phase!=='retired')life.phase='blocked_unsafe';}else adapterRemoveRequest=true;
 if(faultSeen[reason]){faultSeen[reason]++;return;}faultSeen[reason]=1;life.faults.push(reason);
 safeEmit('experiment_fault',Object.assign(life.snapshot(),{reason,unsafe}));
}
// Attested area: native planes of sector 259 (+ AABB), native PointInSector, uniqueness against every native sector,
// flat native floor/ceiling. All read-only; computed once on a stable world before any Spawn.
function attestArea(){
 const n=sectorCount58(),skipped=[];allSectors=[];for(let i=0;i<n;i++){try{allSectors.push(sectorData58(i));}catch(e){skipped.push(i);}}
 const s=sectorPointer(cfg.area_sector),g=geometry58(cfg.area_sector),samples=[],[x0,x1,y0,y1]=cfg.area_box;
 for(let ix=0;ix<=8;ix++)for(let iy=0;iy<=8;iy++){const p=[x0+(x1-x0)*ix/8,y0+(y1-y0)*iy/8,cfg.area_z];if(!fits58(g,p))continue;
  const hits=allSectors.filter(d=>CrossSector58.inside(d,p)).map(d=>d.index);samples.push({p,hits,native_point_in_sector:nativePointInSector58(cfg.area_sector,p),floor:floorNative(query58(p),s,1),ceiling:ceilingNative(query58(p),s,1)});}
 const floors=[...new Set(samples.map(x=>x.floor))],ceilings=[...new Set(samples.map(x=>x.ceiling))];
 const ok=samples.length>=16&&samples.every(x=>x.hits.length===1&&x.hits[0]===cfg.area_sector&&x.native_point_in_sector)&&floors.length===1&&ceilings.length===1&&skipped.length===0;
 area={status:ok?'ATTESTED':'REFUSED',sector:cfg.area_sector,sector_count:n,skipped,planes:g.planes.length,samples:samples.length,floor:floors[0],ceiling:ceilings[0],floors,ceilings,flags14:sectorData58(cfg.area_sector).flags14,z_offset:cfg.z_offset,area_box:cfg.area_box,area_z:cfg.area_z};
 safeEmit('attested_area',Object.assign({sample_points:samples},area));
}
function validateTarget58B(t,withActor){
 if(!t.every(Number.isFinite))return 'NONFINITE_TARGET';
 if(!area||area.status!=='ATTESTED')return 'AREA_NOT_ATTESTED';
 if(!fits58(geometry58(cfg.area_sector),t))return 'OUT_OF_ATTESTED_AREA';
 if(!nativePointInSector58(cfg.area_sector,t))return 'NATIVE_POINT_IN_SECTOR_FALSE';
 const others=allSectors.filter(d=>d.index!==cfg.area_sector&&CrossSector58.inside(d,t)).map(d=>d.index);if(others.length)return 'TARGET_ALSO_IN_'+others.join('_');
 const s=sectorPointer(cfg.area_sector),f=floorNative(query58(t),s,1),c=ceilingNative(query58(t),s,1);
 if(withActor){const a=life.actor;if(!Object.is(f,a.add(0x11c).readFloat())||!Object.is(c,a.add(0x120).readFloat()))return 'FLOOR_CEILING_CACHE_MISMATCH';if(a.add(0x16c).readU32()&0x80)return 'PHYSICS_BIT7_SET';}
 else if(!Object.is(f,area.floor)||!Object.is(c,area.ceiling))return 'FLOOR_CEILING_CACHE_MISMATCH';
 return null;
}
function validateActor58B(a,strict=true){
 affinity();if(!stableWorld(world()))throw Error('native world not stable');
 const p=primary();if(!sameGraph(p))throw Error('P1 ownership changed');
 const id=actorFacts(a),f=id.facts,m=list(id.node,expectedSector),current=list(null,p.sector);
 if(current.entries.filter(e=>e.node===p.node&&e.payload===p.actor).length!==1)throw Error('P1 current sector membership invalid');
 if(a.toString()===p.actor||id.node.toString()===p.node||f.vtable!==base.add(0x528608).toString()||f.payload!==a.toString()||f.type!==1007||f.sector!==expectedSector||!equal(f.position,expectedPosition)||f.yaw!==expectedYaw||f.removed!==0||f.in_use!==1||m.found!==1)throw Error('returned Actor/ObjectNode/sector identity invalid');
 if(strict&&(f.flags!==0x40||f.refs!==0||f.scale!==1.5||f.target!=='0x0'||f.source_owner!=='0x0'||!equal(f.bounds_min,[-12,-12,-24])||!equal(f.bounds_max,[12,12,24])))throw Error('Actor passive properties changed');
 return {node:id.node,facts:{actor:f,sector_list:m,p1:p,p1_inventory_unchanged:equal(inventory(p),baselineInventory)}};
}
const life=new OneActor57C({
 emit:safeEmit,
 spawn:()=>{affinity();const before=primary();safeEmit('native_factory_call',{actor_type:1007,factory:base.add(0x5a10a0).toString(),position:spawnTarget,sector:cfg.area_sector,yaw:0,primary_before:before,command:currentCommand});return spawnNative(base.add(0x5a10a0),1007,positionBuffer,0,cfg.area_sector);},
 validate:a=>{const id=validateActor58B(a);expectedSprite={frame:id.facts.actor.frame};return id;},
 diagnostic:()=>{if(!equal(inventory(primary()),baselineInventory))throw Error('P1 inventory changed by Spawn');return {nested_factory_listener_required:false,return_pointer_is_primary_authority:true,network_command:currentCommand};},
 validateRemoval:(a,n)=>{const id=validateActor58B(a,false);if(!id.node.equals(n)||id.facts.actor.flags!==0x40)throw Error('cleanup identity/flags changed');if(bytes(base.add(cfg.sites.remove.rva),16)!==cfg.sites.remove.prefix)throw Error('Remove prefix changed');},
 remove:a=>{affinity();const before=primary();removeStateCall=stateCalls;removeStep=p1NativeSteps;removeNative(a);const p=primary();afterRemove={flags:a.add(0x16c).readU32(),removed:a.add(0x40).readU8(),refs:life.node.add(0x40).readS32(),sector_list:list(life.node,expectedSector),p1_exact_unchanged:equal(before,p),primary:p};safeEmit('native_remove_postcondition',afterRemove);if(!afterRemove.p1_exact_unchanged||afterRemove.removed!==1||(afterRemove.flags&0x2000)===0)throw Error('Remove native postcondition');}
});
const mover=new NativeVisualProxyMovementController({
 onSimulationThread:()=>thread()&&inTick,stateExitPending:()=>worldStopping||!stableWorld(world()),
 spawnOnce:()=>life.create()?life.actorId+'/'+life.nodeId:null,
 identityMatches:id=>life.phase==='live'&&life.actor&&life.node&&id===life.actorId+'/'+life.nodeId&&life.actor.add(0x10).readPointer().equals(life.node)&&life.node.add(0x28).readPointer().equals(life.actor)&&life.node.add(0x24).readU8()===1,
 interpolationValid:()=>interpolationValid58(life.actor),
 validate:(op,v)=>{validateActor58B(life.actor);
  if(op==='position'){const r=validateTarget58B(v,true);return r?{ok:false,reason:r}:{ok:true,value:v};}
  if(op==='yaw')return {ok:true,value:v};
  return {ok:false,reason:'CROSS_SECTOR_DEFERRED'};},
 apply:(op,v,mode)=>{
  affinity();const a=life.actor,p=primary(),before={position:vec(a.add(0x18)),yaw:a.add(0x38).readFloat(),interpolants:interpolants58(a)};
  if(op==='position'){v.forEach((x,i)=>a.add(0x18+i*4).writeFloat(x));expectedPosition=v.slice();if(mode==='teleport')resetInterpolantNative(a.add(0x50));}
  if(op==='yaw'){const yaw=NetMapping58B.normalizeYaw(v);a.add(0x38).writeFloat(yaw);expectedYaw=yaw;resetInterpolantNative(a.add(0x78));}
  movementRevision++;revisionCommand[movementRevision]=currentCommand?{sequence:currentCommand.sequence,server_tick:currentCommand.server_tick,op}:null;
  const after={actor:validateActor58B(a).facts.actor,interpolants:interpolants58(a),p1_exact_unchanged:equal(p,primary())};
  safeEmit('movement_applied',{operation:op,value:v,mode,revision:movementRevision,command:revisionCommand[movementRevision],before,after});if(!after.p1_exact_unchanged)throw Error('P1 changed during movement');
 },
 checkInvariants:()=>{validateActor58B(life.actor);return interpolationValid58(life.actor);},
 removeOnce:()=>{life.requestCleanup('network_proxy_remove');if(!life.cleanup())throw Error('native Remove refused');}
});
// ---------- DLL seam callbacks (all on the DLL's StateTick detour, Simulation thread) ----------
function onTickEnter(){if(!thread()){foreignCalls++;return;}inTick=true;stateCalls++;p1TickThisCall=0;}
function onWorldReady(){
 try{
  if(!thread()||!inTick){foreignCalls++;return 0;}
  let flags=0;const w=world();
  if(stableWorld(w)&&!worldStopping){list(null,cfg.area_sector);flags|=1;}
  const p=primary();
  if((flags&1)&&p1TickThisCall>0&&validPrimary(p)){
   if(!initialPrimary){initialPrimary=p;baselineInventory=inventory(p);lastLocal=p.pos;safeEmit('p1_baseline',{primary:p,inventory:baselineInventory});}
   if(sameGraph(p))flags|=2;else fault('P1 graph changed',true);
  }
  if(!area&&(flags&3)===3)attestArea();
  if(!fatal&&life.phase!=='blocked_unsafe'&&area&&area.status==='ATTESTED')flags|=4;
  if(adapterRemoveRequest&&life.phase==='live')flags|=8;
  return flags;
 }catch(e){try{fault('world readiness: '+String(e),true);}catch(x){}return 0;}
}
function onSpawn(cmdPtr,outPtr){
 let attempted=false;
 try{
  const c=NetMapping58B.readCommand(cmdPtr);currentCommand=c;if(c.abi!==NetMapping58B.abi)return 3;
  if(control.readU32()<1){if(deferred++%120===0)safeEmit('spawn_deferred',{reason:'NO_HARNESS_SPAWN_PERMIT',command:c});return 2;}
  affinity();if(life.spawnAttempted)return 3;
  const p=primary(),t=NetMapping58B.target(c,cfg.z_offset);
  const pre=[['stable world',stableWorld(world())],['P1 ticked this StateTick',p1TickThisCall>0],['P1 graph',sameGraph(p)],
   ['P1 own sector membership',list(null,p.sector).entries.filter(e=>e.node===p.node&&e.payload===p.actor).length===1],
   ['factory prefix',bytes(base.add(cfg.sites.factory.rva),16)===cfg.sites.factory.prefix],
   ['no prior adapter fault',!adapterRemoveRequest&&!fatal],
   ['factory vtable slot',base.add(0x5a10a0).readPointer().add(0x18).readPointer().equals(base.add(cfg.sites.factory.rva))]];
  const failed=pre.filter(x=>!x[1]).map(x=>x[0]),reason=failed.length?'PRECONDITION:'+failed.join(','):validateTarget58B(t,false);
  if(reason){refusals[reason]=(refusals[reason]||0)+1;if(refusals[reason]%60===1)safeEmit('spawn_refused',{reason,count:refusals[reason],command:c,target:t});return 3;}
  stateBefore=stateSnapshot('BEFORE',p);
  spawnTarget=t;expectedPosition=t.slice();expectedSector=cfg.area_sector;expectedYaw=0;t.forEach((v,i)=>positionBuffer.add(i*4).writeFloat(v));
  safeEmit('spawn_preflight',{command:c,target:t,area,primary:p,list:list(null,cfg.area_sector),camera:viewCache?Math57C.box(viewCache,t):null,local_slot_excluded:true});
  const st=mover.start();if(!st.ok)return 3;
  attempted=true;const r=mover.spawn();safeEmit('controller_spawn_status',Object.assign({command:c},r));
  if(!r.ok)return 0;
  const cache=validateTarget58B(t,true);if(cache)fault('spawn cache witness: '+cache);
  liveHandle=++handleSeq;outPtr.writeU64(liveHandle);
  safeEmit('proxy_spawned',{handle:liveHandle,actor:life.actorId,node:life.nodeId,command:c,target:t,caches:{floor:life.actor.add(0x11c).readFloat(),ceiling:life.actor.add(0x120).readFloat()},sector_list:list(life.node,expectedSector),primary:primary()});
  return 1;
 }catch(e){try{fault('spawn callback: '+String(e),attempted);}catch(x){}return attempted||life.spawnAttempted?0:3;}
}
function onUpdate(h,cmdPtr){
 try{
  if(!liveHandle||h.toNumber()!==liveHandle){fault('update with unknown handle '+h);return 0;}
  const c=NetMapping58B.readCommand(cmdPtr);if(c.abi!==NetMapping58B.abi)return 0;currentCommand=c;
  const t=NetMapping58B.target(c,cfg.z_offset),yaw=NetMapping58B.normalizeYaw(c.yaw),posChanged=!equal(t,expectedPosition),yawChanged=!Object.is(yaw,expectedYaw);
  if(!posChanged&&!yawChanged){unchanged++;return 2;}
  const steps=[],mode=posChanged?NetMapping58B.mode(expectedPosition,t,cfg.step_limit):null;
  if(posChanged){const r=mover.setPosition(t,mode);steps.push(r);safeEmit('controller_status',Object.assign({command:c.sequence},r));if(!r.ok){const code=NetMapping58B.updateCode(r);refusals[r.reason]=(refusals[r.reason]||0)+1;if(refusals[r.reason]%60===1)safeEmit('update_refused_sample',{reason:r.reason,count:refusals[r.reason],command:c,target:t,code});else safeEmit('update_refused',{reason:r.reason,command:c.sequence,target:t,code});return code;}}
  if(yawChanged){const r=mover.setYaw(c.yaw);steps.push(r);safeEmit('controller_status',Object.assign({command:c.sequence},r));if(!r.ok)return NetMapping58B.updateCode(r);}
  updatesApplied++;
  safeEmit('proxy_update',{handle:liveHandle,command:c,target:t,yaw,mode,revision:movementRevision,interpolants:interpolants58(life.actor),actor_origin:vec(life.actor.add(0x18)),actor_yaw:life.actor.add(0x38).readFloat(),updates_applied:updatesApplied});
  return 1;
 }catch(e){try{fault('update callback: '+String(e));}catch(x){}return 0;}
}
function onRemove(h){
 try{
  if(!liveHandle||h.toNumber()!==liveHandle){safeEmit('remove_unknown_handle',{handle:h.toString()});return 2;}
  const r=mover.remove();safeEmit('controller_remove_status',r);return NetMapping58B.removeCode(r,life.removeAttempted);
 }catch(e){try{fault('remove callback: '+String(e),true);}catch(x){}return life.removeAttempted?0:2;}
}
function onRetirement(h){try{if(!liveHandle||h.toNumber()!==liveHandle)return 0;return NetMapping58B.retirementCode(life.phase);}catch(e){return 0;}}
function onTickLeave(){
 try{
  if(!thread()||!inTick)return;
  if(worldStopping||!stableWorld(world()))return;
  const p=primary();if(!validPrimary(p)||p1TickThisCall===0||!initialPrimary)return;
  if(!sameGraph(p)){fault('P1 ownership changed',true);return;}
  if(!equal(inventory(p),baselineInventory))fault('P1 health/weapon inventory changed');
  const stamp=base.add(0xaec390).readU32();
  if(life.phase==='live'){
   const id=validateActor58B(life.actor);
   if(life.simulationStep(stamp)&&(life.simulationTicks<=3||life.simulationTicks%60===0))safeEmit('stable',Object.assign(life.snapshot(),id.facts,{p1_native_steps:p1NativeSteps,sprite_submissions:spriteSubmissions,ours_sprites:oursSprites,intermediate_sprites:intermediateSprites,lerp_samples:lerpSamples,updates_applied:updatesApplied,unchanged,refusals,damage_callbacks:damageCallbacks,local_p1_position:p.pos,local_p1_moved_since_baseline:!equal(p.pos,lastLocal)}));
   if(!snapAfterSpawn&&life.simulationTicks>=120){stateSnapshot('AFTER_SPAWN',p);snapAfterSpawn=true;}
   if(life.simulationTicks>=nextLiveSnapshot){stateSnapshot('LIVE_'+nextLiveSnapshot,p);nextLiveSnapshot+=1800;}
   if(life.simulationTicks>=300&&spriteSubmissions===0&&!adapterRemoveRequest)fault('no genuine sprite submission');
   if(life.simulationTicks>=cfg.maximum_live_steps&&!adapterRemoveRequest)fault('bounded lifetime limit');
  }else if(life.phase==='pending'&&p1NativeSteps-removeStep>120)fault('deferred retirement deadline (120 simulation steps)',true);
  else if(life.phase==='retired'){
   if(!snapAfterRemove){stateSnapshot('AFTER_REMOVE',p);snapAfterRemove=true;}
   if(stamp!==lastPostRetireStamp){lastPostRetireStamp=stamp;postRetireSteps++;if(postRetireSteps<=3||postRetireSteps%60===0)safeEmit('post_retire',Object.assign(life.snapshot(),{primary:p,p1_inventory_unchanged:equal(inventory(p),baselineInventory),sector_list:list(null,cfg.area_sector),all_weak_pointers_null:life.actor===null&&life.node===null&&life.retireNode===null,post_retire_steps:postRetireSteps,sprite_submissions_after_remove:postRemoveSubmissions,actor_reads:false}));}
   if(!snapPost&&postRetireSteps>=60){stateSnapshot('POST_RETIREMENT',p);snapPost=true;}
  }
 }catch(e){try{fault('tick leave observation: '+String(e));}catch(x){}}
 finally{inTick=false;}
}
function onWorldExit(reason){
 try{worldStopping=true;const phase=life.phase,present=life.actor!==null;life.actor=null;life.node=null;life.retireNode=null;life.identitySafe=false;expectedSprite=null;
  if(phase!=='retired'&&phase!=='waiting')fault('teardown with live/unknown Actor (G1 OPEN: no native call)',true);
  safeEmit('world_exit_observed',{reason,phase,weak_actor_present:present,all_weak_pointers_invalidated:true});}catch(e){}
}
// ---------- read-only observers (identical intent to DOC58A probe.js) ----------
function recordTiles(frame){
 const f=ptr(frame),p=f.add(0x50).readPointer(),count=f.add(0x5c).readU32();if(p.isNull()||count===0||count>16)throw Error('sprite frame tile bounds');
 const tiles=[];for(let i=0;i<count;i++){const row=p.add(i*16),sprite=row.readPointer(),id=row.add(8).readU16();if(sprite.isNull()||id>64)throw Error('sprite tile identity');const rect=sprite.readPointer().add(id*0x28);tiles.push({sprite:sprite.toString(),id,width:rect.add(0xc).readS32(),height:rect.add(0x10).readS32(),texture:sprite.add(0x18).readPointer().toString()});}
 return {frame,frame_flags:f.add(4).readU32(),tiles};
}
Interceptor.attach(base.add(cfg.sites.scene.rva),{onEnter(args){
 if(worldStopping)return;try{if(!stableWorld(world()))return;const p=primary();if(!validPrimary(p))return;
  const v=preparedView(args[0],args[1]);viewFrames++;v.frame=viewFrames;v.engine_stamp=base.add(0xaec390).readU32();v.tid=Process.getCurrentThreadId();viewCache=v;
 }catch(e){if(viewFrames<3)safeEmit('camera_observation_error',{error:String(e)});}
}});
Interceptor.attach(base.add(cfg.sites.sprite.rva),{onEnter(args){
 this.ours=false;if(!expectedSprite||worldStopping)return;
 try{const e=args[2],frame=e.readPointer().toString();if(frame!==expectedSprite.frame)return;
 const entry={frame,origin:vec(e.add(0x10)),angles:vec(e.add(0x28)),sector:e.add(0x50).readS32(),flags:e.add(0x4c).readU32(),scale:e.add(8).readFloat()};
 let match;
 if(life.phase==='live'&&life.actor&&thread()){const r=renderFacts58(life.actor);match=CrossSector58.spriteIdentity(entry,r);if(match.status==='MISMATCH')return;match=CrossSector58.spriteIdentity(entry,r,sameSectorRenderables58(entry.sector,life.node));}
 else{if(!entry.origin.every((v,i)=>Math.abs(v-expectedPosition[i])<.01)||entry.sector!==expectedSector||entry.flags!==0x40||entry.scale!==1.5)return;match={status:'SETTLED_FILTER_NO_ACTOR_READ'};}
 this.ours=true;this.vertices=spriteVertices;inOwnSprite=true;spriteSubmissions++;if(match.status==='OURS')oursSprites++;
 if(life.removeAttempted){postRemoveSubmissions++;safeEmit('sprite_after_remove',{phase:life.phase,origin:entry.origin,frame});}
 if(match.status==='AMBIGUOUS'){ambiguousSprites++;if(ambiguousSprites<=8)safeEmit('sprite_identity_ambiguous',{revision:movementRevision,entry,match});}
 if(match.intermediate&&intermediateSprites<4000){intermediateSprites++;safeEmit('sprite_intermediate_submission',{revision:movementRevision,command:revisionCommand[movementRevision],render_frame:viewFrames,entry,match:match.status,last_lerp:lastLerp,correlated_with_last_lerp:!!lastLerp&&equal(lastLerp.output,entry.origin)});}
 if(lastRenderRevision!==movementRevision){lastRenderRevision=movementRevision;safeEmit(spriteSubmissions<=3?'sprite_submission_first':'sprite_submission',{revision:movementRevision,command:revisionCommand[movementRevision],render_frame:viewFrames,identity:match.status,intermediate:!!match.intermediate,entry,view_eye:vec(args[0].add(0xc0)),tiles:spriteSubmissions<=3?recordTiles(frame):undefined,interpolants:life.phase==='live'&&life.actor?interpolants58(life.actor):null});}
 }catch(e){fault('sprite observation: '+String(e));}
 },onLeave(){if(this.ours)inOwnSprite=false;}});
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
 if(life.retireNode){const n=life.retireNode,m=list(n,expectedSector),facts={payload_null:n.add(0x28).readPointer().isNull(),refs:n.add(0x40).readS32(),in_use:n.add(0x24).readU8(),sector_occurrences:m.found,sector_list:m};life.finishRetirement(facts);}
 }catch(e){life.retireNode=null;fault('retirement witness: '+String(e),true);}inSweep=false;}});
Interceptor.attach(base.add(cfg.sites.drain.rva),{onEnter(){this.ours=thread()&&inTick&&life.phase==='pending';if(this.ours&&life.node)safeEmit('deferred_drain_entry',{refs:life.node.add(0x40).readS32(),sector_list:list(life.node,expectedSector)});},onLeave(){if(this.ours&&life.node)safeEmit('deferred_drain_return',{refs:life.node.add(0x40).readS32(),payload:life.node.add(0x28).readPointer().toString(),in_use:life.node.add(0x24).readU8(),sector_list:list(life.node,expectedSector)});}});
// Read-only natural renderer interpolation observation. No calls or writes here.
Interceptor.attach(base.add(cfg.sites.interpolant_lerp.rva),{onEnter(args){this.ours=life.phase==='live'&&life.actor&&args[0].equals(life.actor.add(0x50));if(this.ours){this.a=life.actor;this.prev=vec(this.a.add(0x60));this.current=vec(this.a.add(0x18));}},onLeave(){if(!this.ours||!life.actor||!this.a.equals(life.actor))return;const output=vec(this.a.add(0x6c)),delta=this.current.map((x,i)=>x-this.prev[i]),axis=delta.findIndex(x=>Math.abs(x)>.001);if(axis>=0){const t=(output[axis]-this.prev[axis])/delta[axis];lerpSamples++;lastLerp={sample:lerpSamples,output,render_frame:viewFrames,revision:movementRevision};if(lerpSamples<=6000)safeEmit('native_interpolation_sample',{sample:lerpSamples,render_frame:viewFrames,revision:movementRevision,command:revisionCommand[movementRevision],previous:this.prev,current:this.current,output,t,formula_error:Math.max(...output.map((x,i)=>Math.abs(x-(this.prev[i]+delta[i]*t))))});}}});
// ---------- registration with the DLL (before Permit) ----------
const callbacks58b={
 tickEnter:new NativeCallback(onTickEnter,'void',['uint64'],'win64'),
 worldReady:new NativeCallback(onWorldReady,'int32',['uint64'],'win64'),
 spawn:new NativeCallback(onSpawn,'int32',['pointer','pointer'],'win64'),
 update:new NativeCallback(onUpdate,'int32',['uint64','pointer'],'win64'),
 remove:new NativeCallback(onRemove,'int32',['uint64'],'win64'),
 retirement:new NativeCallback(onRetirement,'int32',['uint64'],'win64'),
 tickLeave:new NativeCallback(onTickLeave,'void',['uint64','uint32'],'win64'),
 worldExit:new NativeCallback(onWorldExit,'void',['uint32'],'win64')};
const seamTable58b=Memory.alloc(72);seamTable58b.writeU32(NetMapping58B.abi);seamTable58b.add(4).writeU32(72);
['tickEnter','worldReady','spawn','update','remove','retirement','tickLeave','worldExit'].forEach((k,i)=>seamTable58b.add(8+i*8).writePointer(callbacks58b[k]));
const registerSeam58b=new NativeFunction(dll.getExportByName('PSE58B_RegisterSeam'),'uint32',['pointer'],'win64');
const registration58b=registerSeam58b(seamTable58b);
globalThis.keep58b={callbacks58b,seamTable58b,control};
emit('probe_loaded',{pid:Process.id,image_base:base.toString(),expected_tid:cfg.simulation_tid,control:control.toString(),source_sha256:cfg.source_sha256,binary_sha256_host_verified:cfg.binary_sha256,dll:dll.path,dll_base:dll.base.toString(),seam_registration:registration58b,
 frida_hooks:['scene','sprite','vertex','puppet_tick','actor_tick','damage','on_remove','pool_return','sweep','drain','interpolant_lerp'],dll_owned_hooks:Object.keys(cfg.dll_owned),actor_type:1007,z_offset:cfg.z_offset,step_limit:cfg.step_limit,area_sector:cfg.area_sector,camera_writes:0,player_writes:0});
if(registration58b!==0)throw Error('seam registration refused: '+registration58b);
