// Only adapter may write the three proven origin floats and normalized yaw.
// It never writes links, sectorIndex, registry entries, flags, bounds, floor/ceiling caches or render output.
// Sector changes go through native LinkSector1d3d70 only, in the factory1d4780 order: origin -> LinkSector -> interpolant init/reset.
let expectedPosition=cfg.position.slice(),expectedSector=259,expectedYaw=0,movementPlan=null,movementRevision=0,crossState=null;
const resetInterpolantNative=new NativeFunction(base.add(cfg.sites.interpolant_reset.rva),'void',['pointer'],{abi:'win64',exceptions:'propagate'});
const linkSectorNative=new NativeFunction(base.add(cfg.sites.link_sector.rva),'void',['pointer','int'],{abi:'win64',exceptions:'propagate'});
// Read-only native queries (assembly: no stores beyond home space): bool(sector*,float*,eps) and float(float*,sector*,char).
const pointInSectorNative=new NativeFunction(base.add(cfg.sites.point_in_sector.rva),'uint8',['pointer','pointer','float'],{abi:'win64',exceptions:'propagate'});
const floorNative=new NativeFunction(base.add(cfg.sites.floor_height.rva),'float',['pointer','pointer','uint8'],{abi:'win64',exceptions:'propagate'});
const ceilingNative=new NativeFunction(base.add(cfg.sites.ceiling_height.rva),'float',['pointer','pointer','uint8'],{abi:'win64',exceptions:'propagate'});
const queryPoint=Memory.alloc(12);
function interpolants58(a){return {position_ref:a.add(0x58).readPointer().toString(),angles_ref:a.add(0x80).readPointer().toString(),previous:vec(a.add(0x60)),output:vec(a.add(0x6c)),angles_previous:vec(a.add(0x88)),angles_output:vec(a.add(0x94))};}
function interpolationValid58(a){return a.add(0x58).readPointer().equals(a.add(0x18))&&a.add(0x80).readPointer().equals(a.add(0x30))&&a.add(0x50).readPointer().equals(base.add(0x52bdf0))&&a.add(0x78).readPointer().equals(base.add(0x52be68))&&[...vec(a.add(0x60)),...vec(a.add(0x6c)),...vec(a.add(0x88)),...vec(a.add(0x94))].every(Number.isFinite);}
function geometry58(index){
 const s=sectorPointer(index),count=base.add(0xa45988).readU64().toNumber(),faces=base.add(0xa45980).readPointer(),first=s.readU16(),last=s.add(2).readU16();
 if(first>last||last+2>=count||last-first>128)throw Error('unverified face range');
 const planes=[];for(let i=first;i<=last+2;i++){const f=faces.add(i*0x108);planes.push({face:i,n:vec(f.add(0xc)),d:f.add(0x18).readFloat(),flags:f.add(0x1c).readU32(),owner:f.add(0x44).readS32(),raw:bytes(f,0x70)});}
 return {sector:index,first,last,planes,raw_sector:bytes(s,0x88)};
}
function sectorCount58(){return base.add(0xa45970).readU64().toNumber();}
// Numeric copy for CrossSector58: faces first..last+2 with portal flag0x2000 and adjoin s16 face+6; sector flags u16+14, own index+80.
function sectorData58(index){
 const s=sectorPointer(index),count=base.add(0xa45988).readU64().toNumber(),faces=base.add(0xa45980).readPointer(),first=s.readU16(),last=s.add(2).readU16();
 if(first>last||last+2>=count||last-first>512)throw Error('unverified face range');
 const out=[];for(let i=first;i<=last+2;i++){const f=faces.add(i*0x108);out.push({face:i,n:vec(f.add(0xc)),d:f.add(0x18).readFloat(),flags:f.add(0x1c).readU32(),adjoin:f.add(6).readS16()});}
 if(s.add(0x80).readS32()!==index)throw Error('sector self index mismatch');
 return {index,first,last,flags14:s.add(0x14).readU16(),faces:out};
}
function containing58(p){const n=sectorCount58(),hits=[],skipped=[];for(let i=0;i<n;i++){let s;try{s=sectorData58(i);}catch(e){skipped.push(i);continue;}if(CrossSector58.inside(s,p))hits.push(i);}return {count:n,hits,skipped};}
function query58(p){p.forEach((v,k)=>queryPoint.add(k*4).writeFloat(v));return queryPoint;}
function nativePointInSector58(index,p){return pointInSectorNative(sectorPointer(index),query58(p),0)===1;}
function fits58(g,p){return Math57C.planes(g.planes.map(f=>[...f.n,f.d]),Math57C.corners(p)).full_inside;}
function crossAttest58(dest,withCache){
 const a=life.actor,t=cfg.cross_target,scan=containing58(t),input={source:sectorData58(expectedSector),destination:sectorData58(dest),sector_count:sectorCount58(),position:expectedPosition.slice(),target:t,bounds:a?{min:vec(a.add(0xec)),max:vec(a.add(0xf8))}:{min:[-12,-12,-24],max:[12,12,24]},containing:scan.hits,containing_skipped:scan.skipped,find_sector:CrossSector58.findSector(sectorData58,expectedSector,t),native_point_in_sector:{destination:nativePointInSector58(dest,t),source:nativePointInSector58(expectedSector,t),epsilon:0}};
 if(withCache){const d=sectorPointer(dest);input.cache={actor_floor:a.add(0x11c).readFloat(),actor_ceiling:a.add(0x120).readFloat(),destination_floor:floorNative(query58(t),d,1),destination_ceiling:ceilingNative(query58(t),d,1),source_flags14:input.source.flags14,destination_flags14:input.destination.flags14,actor_flags:a.add(0x16c).readU32(),physics_gate_bit7:(a.add(0x16c).readU32()&0x80)!==0};}
 const verdict=CrossSector58.evaluate(input);
 return Object.assign({},verdict,{input:{source:input.source.index,destination:dest,position:input.position,target:t,bounds:input.bounds,scan,find_sector:input.find_sector,native_point_in_sector:input.native_point_in_sector,cache:input.cache||null},target_view:viewCache?Math57C.box(viewCache,t):null});
}
function plan58(){
 const g=geometry58(259),g123=geometry58(123),candidates=[];
 for(const dx of [-64,64,-32,32])for(const dy of [0,32,-32]){const p=[cfg.position[0]+dx,cfg.position[1]+dy,cfg.position[2]];
 if(fits58(g,p)&&viewCache&&Math57C.box(viewCache,p).frustum.full_inside&&Math57C.box(viewCache,p).clip_full_inside)candidates.push(p);}
 if(candidates.length<4)throw Error('insufficient native-plane/frustum approved points');
 movementPlan={geometry:g,adjacent_candidate_geometry:g123,positions:candidates.slice(0,4),home:fits58(g,cfg.position)?cfg.position.slice():null};safeEmit('movement_plan',movementPlan);
 // Geometry part only (Actor cache measured again immediately before the native call).
 try{safeEmit('cross_sector_plan',crossAttest58(cfg.cross_destination,false));}catch(e){safeEmit('cross_sector_plan_error',{error:String(e)});}
 return movementPlan;
}
function makeMovementController58(){return new NativeVisualProxyMovementController({
 onSimulationThread:()=>thread()&&inTick,stateExitPending:()=>worldStopping||!stableWorld(world()),
 spawnOnce:()=>life.create()?life.actorId+'/'+life.nodeId:null,
 identityMatches:id=>life.phase==='live'&&life.actor&&life.node&&id===life.actorId+'/'+life.nodeId&&life.actor.add(0x10).readPointer().equals(life.node)&&life.node.add(0x28).readPointer().equals(life.actor)&&life.node.add(0x24).readU8()===1,
 interpolationValid:()=>interpolationValid58(life.actor),
 validate:(op,v)=>{
  validateActor(life.actor);
  if(op==='sector'){
   if(v<0||v>=sectorCount58())return {ok:false,reason:'INVALID_SECTOR_INDEX'};
   if(v===expectedSector)return {ok:true,value:{sector:v,position:expectedPosition.slice(),cross:false}};
   if(v!==cfg.cross_destination||expectedSector!==259||crossState||!equal(expectedPosition,cfg.position))return {ok:false,reason:'DESTINATION_NOT_IN_APPROVED_PLAN'};
   const c=crossAttest58(v,true);safeEmit('cross_sector_attestation',c);
   return c.status==='ATTESTED'?{ok:true,value:{sector:v,position:cfg.cross_target.slice(),cross:true,from:expectedSector}}:{ok:false,reason:c.reason};
  }
  if(op==='position'&&crossState)return {ok:false,reason:'POSITION_PLAN_NOT_ATTESTED_IN_DESTINATION'};
  if(op==='position'&&(!movementPlan||!(movementPlan.positions.some(p=>equal(p,v))||(movementPlan.home&&equal(movementPlan.home,v)))||!fits58(geometry58(expectedSector),v)))return {ok:false,reason:'OUT_OF_BOUNDS_OR_UNATTESTED_TARGET'};
  return {ok:true,value:v};
 },
 apply:(op,v,mode)=>{
  affinity();const a=life.actor,p=primary(),before={actor:actorFacts(a).facts,interpolants:interpolants58(a),sector_list:list(life.node,expectedSector)};
  if(op==='position'){v.forEach((x,i)=>a.add(0x18+i*4).writeFloat(x));expectedPosition=v.slice();if(mode==='teleport')resetInterpolantNative(a.add(0x50));}
  if(op==='yaw'){const yaw=Math.fround(((v+Math.PI)%(2*Math.PI)+2*Math.PI)%(2*Math.PI)-Math.PI);a.add(0x38).writeFloat(yaw);expectedYaw=yaw;resetInterpolantNative(a.add(0x78));}
  if(op==='sector'&&!v.cross)linkSectorNative(a,v.sector);
  if(op==='sector'&&v.cross){
   const lists={source:list(life.node,v.from),destination:list(life.node,v.sector)};
   if(lists.source.found!==1||lists.destination.found!==0||a.add(0x3c).readS32()!==v.from)throw Error('cross-sector precondition');
   const caches={floor:a.add(0x11c).readFloat(),ceiling:a.add(0x120).readFloat()};
   v.position.forEach((x,i)=>a.add(0x18+i*4).writeFloat(x));
   linkSectorNative(a,v.sector);
   resetInterpolantNative(a.add(0x50));
   expectedSector=v.sector;expectedPosition=v.position.slice();
   const after={source:list(life.node,v.from),destination:list(life.node,v.sector)},m=CrossSector58.membership(lists,after,life.node.toString(),a.toString());
   crossState={from:v.from,to:v.sector,target:v.position,revision:movementRevision+1,state_calls:stateCalls,simulation_step:life.simulationTicks,engine_stamp:base.add(0xaec390).readU32()};
   safeEmit('cross_sector_link',{sequence:['origin_store_xyz','LinkSector1d3d70(actor,'+v.sector+')','interpolant_reset1d3e60(actor+50)'],before_lists:lists,after_lists:after,membership:m,sector_index:a.add(0x3c).readS32(),own_node:a.add(0x10).readPointer().toString(),caches_before:caches,caches_after:{floor:a.add(0x11c).readFloat(),ceiling:a.add(0x120).readFloat()},interpolants:interpolants58(a),teleport_reset:CrossSector58.teleportReset(interpolants58(a),v.position),cross:crossState});
   if(!m.ok||a.add(0x3c).readS32()!==v.sector||!CrossSector58.teleportReset(interpolants58(a),v.position))throw Error('cross-sector postcondition '+m.reason);
  }
  movementRevision++;const after={actor:validateActor(a).facts,interpolants:interpolants58(a),p1_exact_unchanged:equal(p,primary())};
  safeEmit('movement_applied',{operation:op,value:v,mode,revision:movementRevision,before,after});if(!after.p1_exact_unchanged)throw Error('P1 changed during movement');
 },
 checkInvariants:()=>{validateActor(life.actor);return interpolationValid58(life.actor);},
 removeOnce:()=>{life.requestCleanup('movement_controller_explicit_remove');if(!life.cleanup())throw Error('native Remove refused');}
 });}
// Live render facts for sprite identity (Sim/render thread only; never after PoolReturn).
function renderFacts58(a){const f=actorFacts(a).facts;return {frame:f.frame,sector:f.sector,flags:f.flags,scale:f.scale,z_offset:a.add(0x128).readFloat(),render_output:vec(a.add(0x6c)),angles_output:vec(a.add(0x94)),current:vec(a.add(0x18))};}
function sameSectorRenderables58(sector,own){
 return list(null,sector).entries.filter(e=>e.node!==own.toString()&&e.payload!=='0x0').map(e=>{const a=ptr(e.payload);try{const anim=a.add(0x108).readPointer(),id=a.add(0x178).readS16();const frame=!anim.isNull()&&id>=0&&id<anim.add(0x24).readU32()?anim.add(0x18).readPointer().add(id*0x110).toString():null;return {payload:e.payload,frame,render_output:vec(a.add(0x6c))};}catch(x){return {payload:e.payload,frame:'unreadable',render_output:[]};}});
}
function exceptionSmoke58(){
 // Second controller instance with the real identity but a throwing diagnostic; no KEX call/write is reachable.
 const revision=movementRevision,before=actorFacts(life.actor).facts;let writes=0,removes=0;
 const c=new NativeVisualProxyMovementController({onSimulationThread:()=>thread()&&inTick,stateExitPending:()=>worldStopping||!stableWorld(world()),spawnOnce:()=>life.actorId+'/'+life.nodeId,identityMatches:id=>id===life.actorId+'/'+life.nodeId&&life.phase==='live',interpolationValid:()=>{throw Error('diagnostic exception-to-status smoke');},validate:()=>({ok:false,reason:'UNREACHABLE'}),apply:()=>{writes++;},checkInvariants:()=>true,removeOnce:()=>{removes++;}});
 const start=c.start(),bind=c.spawn(),first=c.setYaw(1),second=c.setYaw(1),after=actorFacts(life.actor).facts;
 return {start,identity_bind_no_native_spawn:bind,first,second,adapter_writes:writes,removes,revision_unchanged:revision===movementRevision,actor_unchanged:equal(before,after),passed:first.reason==='VALIDATION_UNKNOWN'&&second.reason==='STOPPED'&&writes===0&&removes===0&&revision===movementRevision&&equal(before,after)};
}
