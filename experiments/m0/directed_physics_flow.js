/* Directed native movement observations. No direct collision invocation, position,
   velocity, flags, node or refcount writes. The command seam drives both fixtures. */
function createDirectedPhysicsFlow() {
 const phases=['p2_wall_portal','p2_into_p1','p2_clear','p1_into_p2','p1_clear','p2_jump_floor','p1_jump_floor'];
 let ticks=0,lastUpdates=0,current=null,initial=null,finished=false;
 const cases=[];
 // In the observed Frida QJS session global Infinity was NaN. Never seed
 // aggregates with it, and stop rather than silently serializing NaN as null.
 function stats(){return {collisions:0,horizontal_clips:0,vertical_clips:0,sector_changes:0,pair_touches:0,min_floor_gap:null,max_floor_gap:null,min_z:null,max_z:null};}
 function blank(name,p,s){return {name,ticks:0,initial:{p1:p,p2:s},final:null,min_distance:null,p1:stats(),p2:stats()};}
 function minimum(old,value){if(!Number.isFinite(value))throw Error('Nonfinite directed physics sample');return old===null?value:Math.min(old,value);}
 function maximum(old,value){if(!Number.isFinite(value))throw Error('Nonfinite directed physics sample');return old===null?value:Math.max(old,value);}
 // The full probe prechecks this entry before installing its generic observer.
 Interceptor.attach(base.add(0x1a4990),{onEnter(args){
  if(!current||!simulationThread()||!gameplayState()||stage!=='alive')return;
  const found=playerActor(args[1]);if(!found)return;
  this.saved={rec:found.rec,which:found.which,scratch:args[0],wanted:[0,4,8].map(o=>args[2].add(o).readFloat()),
   origin:[0x18,0x1c,0x20].map(o=>args[1].add(o).readFloat()),sector:args[1].add(0x3c).readS32(),caller:this.returnAddress.toString()};
 },onLeave(ret){if(!this.saved||!current||!live(this.saved.rec))return;
  const q=this.saved,hit=(ret.toUInt32()&255)!==0,end=[0xc,0x10,0x14].map(o=>q.scratch.add(o).readFloat()),
   normal=[0x38,0x3c,0x40].map(o=>q.scratch.add(o).readFloat()),sector=q.scratch.add(0x24).readS32();
  const s=current[q.which],horizontal=Math.hypot(end[0]-q.wanted[0],end[1]-q.wanted[1]);s.collisions++;
  if(sector!==q.sector)s.sector_changes++;
  const clipped=hit&&horizontal>.05&&Math.hypot(q.wanted[0]-q.origin[0],q.wanted[1]-q.origin[1])>.05;
  if(clipped)s.horizontal_clips++;
  if(hit&&Math.abs(end[2]-q.wanted[2])>.05)s.vertical_clips++;
  if(clipped&&s.horizontal_clips<=3)event('C_directed_collision_sample',{phase:current.name,which:q.which,hit,origin:q.origin,wanted:q.wanted,end,normal,
   sector_before:q.sector,sector_after:sector,opaque_hit_index:q.scratch.add(0x28).readS32(),opaque_reference:q.scratch.add(0x30).readPointer().toString(),native_caller:q.caller,engine_calls_by_observer:0});
 }});
 prefix('puppet_touch',0x1dbfc0);Interceptor.attach(base.add(0x1dbfc0),{onEnter(args){
  if(!current||!simulationThread()||!gameplayState()||stage!=='alive')return;
  const a=playerActor(args[0]),b=playerActor(args[1]);if(!a||!b||a.which===b.which)return;
  const count=++current[a.which].pair_touches;if(count<=3)event('C_directed_puppet_pair_touch',{phase:current.name,which:a.which,other:b.which,actor:args[0].toString(),other_actor:args[1].toString(),native_rva:'0x1dbfc0'});
 }});
 function finishCase(p,s){if(!current)return;current.final={p1:p,p2:s};cases.push(current);event('C_directed_physics_case',current);}
 return {step(){
  const result={b1:0,b2:0,y1:0,y2:0,retire:false};if(finished)return result;
  const p=graph(local),s=graph(p2),count=controller.counts().p2_controller;
  if(!p.live_identity||!s.live_identity||p.health!==200||s.health!==200||!guards())throw Error('Directed physics identity/health/guard invariant');
  if(count===lastUpdates)return result;lastUpdates=count;ticks++;
  if(!initial)initial={p1:p,p2:s};
  const phase=phases[Math.floor((ticks-1)/120)];
  if(!phase){finishCase(p,s);finished=true;event('C_directed_physics_observed',{cases,controller:controller.counts(),primary:p,secondary:s,
   interpretation:'Native outcomes recorded per direction; opaque trace references are not assumed to be Actors. Mutual blocking acceptance requires reviewing pair contacts and distances.',next:'Leave both players alive for a normal direct-Quit cleanup observation.',full_m0:false});return result;}
  if(current?.name!==phase){finishCase(p,s);current=blank(phase,p,s);}
  current.ticks++;current.min_distance=minimum(current.min_distance,Math.hypot(...p.origin.map((x,i)=>x-s.origin[i])));
  for(const [which,rec,g] of [['p1',p1Identity,p],['p2',p2Identity,s]]){
   const gap=g.origin[2]-rec.actor.add(0x11c).readFloat(),st=current[which];
   st.min_floor_gap=minimum(st.min_floor_gap,gap);st.max_floor_gap=maximum(st.max_floor_gap,gap);
   st.min_z=minimum(st.min_z,g.origin[2]);st.max_z=maximum(st.max_z,g.origin[2]);
  }
  if(phase==='p2_wall_portal'||phase==='p2_clear')result.b2=8;
  if(phase==='p2_into_p1'&&s.origin[1]>p.origin[1]-32)result.b2=4;
  if(phase==='p1_into_p2'&&p.origin[1]<s.origin[1]+32)result.b1=8;
  if(phase==='p1_clear'&&p.origin[1]>initial.p1.origin[1])result.b1=4;
  if(phase==='p2_jump_floor'&&current.ticks<=4)result.b2=2;
  if(phase==='p1_jump_floor'&&current.ticks<=4)result.b1=2;
  if(phase.includes('jump')&&current.ticks<=8)event('C_directed_jump_input_sample',{phase,tick:current.ticks,p1_jump_counter:local.add(8).readU32(),p2_jump_counter:p2.add(8).readU32(),primary:p,secondary:s});
  return result;
 }};
}
