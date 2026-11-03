// Pure DOC-58A completion checks over numeric COPIES of exact-build sector/face/list reads.
// Sector copy: {index,first,last,flags14,faces:[{face,n,d,flags,adjoin}]} covering first..last+2.
// Native point-in-sector1cf540 tests faces first..last; FindSector1c98f0 and this module test first..last+2.
const CrossSector58={
 margin(f,p){return f.n[0]*p[0]+f.n[1]*p[1]+f.n[2]*p[2]+f.d;},
 corners(o,lo,hi){const out=[];for(let m=0;m<8;m++)out.push(o.map((v,i)=>v+((m>>i)&1?hi[i]:lo[i])));return out;},
 inside(s,p,skip=[]){return s.faces.every(f=>skip.includes(f.face)||this.margin(f,p)>=0);},
 boxInside(s,o,lo,hi,skip=[]){const c=this.corners(o,lo,hi);return {full_inside:c.every(p=>this.inside(s,p,skip)),minimum:Math.min(...s.faces.filter(f=>!skip.includes(f.face)).map(f=>Math.min(...c.map(p=>this.margin(f,p)))))};},
 // Read-only mirror of native FindSector1c98f0: BFS from hint through portal faces behind which the point lies.
 findSector(get,hint,p,limit=4096){
  const seen=new Set([hint]),queue=[hint];
  while(queue.length){if(seen.size>limit)return {sector:-1,reason:'SEARCH_LIMIT'};const s=get(queue.shift());if(this.inside(s,p))return {sector:s.index,visited:seen.size};
   for(const f of s.faces)if((f.flags&0x2000)&&f.adjoin>=0&&!seen.has(f.adjoin)&&this.margin(f,p)<0){seen.add(f.adjoin);queue.push(f.adjoin);}}
  return {sector:-1,visited:seen.size};
 },
 portal(a,b){
  const ab=a.faces.filter(f=>(f.flags&0x2000)&&f.adjoin===b.index),ba=b.faces.filter(f=>(f.flags&0x2000)&&f.adjoin===a.index);
  if(ab.length!==1||ba.length!==1)return {ok:false,reason:'PORTAL_NOT_RECIPROCAL',ab:ab.map(f=>f.face),ba:ba.map(f=>f.face)};
  const x=ab[0],y=ba[0],coplanar=x.n.every((v,i)=>Math.abs(v+y.n[i])<1e-4)&&Math.abs(x.d+y.d)<1e-2;
  return coplanar?{ok:true,source_face:x.face,destination_face:y.face,plane:{n:x.n,d:x.d}}:{ok:false,reason:'PORTAL_NOT_COPLANAR'};
 },
 evaluate(i){
  const checks=[],fail=r=>({status:'REJECTED',reason:r,checks});
  const ok=(name,value,detail={})=>{checks.push(Object.assign({name,passed:!!value},detail));return !!value;};
  const {source:s,destination:d,sector_count:count,position:p,target:t,bounds:{min:lo,max:hi}}=i;
  for(const k of [s&&s.index,d&&d.index])if(!ok('sector index in native array',Number.isInteger(k)&&k>=0&&k<count,{index:k,count}))return fail('INVALID_SECTOR_INDEX');
  if(!ok('distinct sectors',s.index!==d.index))return fail('SAME_SECTOR_NOT_CROSS');
  if(!ok('finite source/target',[...p,...t].every(Number.isFinite)))return fail('INVALID_POSITION');
  const src=this.boxInside(s,p,lo,hi);if(!ok('source AABB fully inside source sector',src.full_inside,src))return fail('SOURCE_GEOMETRY_INVALID');
  const dst=this.boxInside(d,t,lo,hi);if(!ok('target AABB fully inside destination sector',dst.full_inside,dst))return fail('TARGET_AABB_OUTSIDE_DESTINATION');
  const leak=this.corners(t,lo,hi).filter(c=>this.inside(s,c));if(!ok('no target corner inside source sector',leak.length===0,{leak}))return fail('TARGET_OVERLAPS_SOURCE');
  const portal=this.portal(s,d);if(!ok('reciprocal coplanar portal',portal.ok,portal))return fail('INVALID_PORTAL');
  const ms=this.margin(portal.plane,p),mt=this.margin(portal.plane,t);
  if(!ok('source and target on opposite portal sides',ms>0&&mt<0,{source_margin:ms,target_margin:mt}))return fail('INVALID_PORTAL');
  const u=ms/(ms-mt),cross=p.map((v,k)=>v+(t[k]-v)*u);
  const ap1=this.boxInside(s,cross,lo,hi,[portal.source_face]),ap2=this.boxInside(d,cross,lo,hi,[portal.destination_face]);
  if(!ok('AABB clears portal aperture at crossing point',ap1.full_inside&&ap2.full_inside,{crossing:cross,source_side:ap1,destination_side:ap2}))return fail('PORTAL_APERTURE_TOO_SMALL');
  if(!ok('all sectors readable in uniqueness scan',Array.isArray(i.containing_skipped)&&i.containing_skipped.length===0,{skipped:i.containing_skipped}))return fail('SECTOR_SCAN_INCOMPLETE');
  const ex=i.containing||[];if(!ok('target center in exactly one sector (3D scan)',ex.length===1&&ex[0]===d.index,{containing:ex}))return fail('DESTINATION_NOT_UNIQUE');
  const fs=i.find_sector||{};if(!ok('FindSector mirror from source hint',fs.sector===d.index,fs))return fail('DESTINATION_NOT_REACHABLE');
  const n=i.native_point_in_sector||{};if(!ok('native point-in-sector witness',n.destination===true&&n.source===false,n))return fail('NATIVE_POINT_IN_SECTOR_MISMATCH');
  const c=i.cache;
  if(!ok('floor/ceiling cache contract measured',c&&[c.actor_floor,c.actor_ceiling,c.destination_floor,c.destination_ceiling].every(Number.isFinite)&&Number.isInteger(c.source_flags14)&&Number.isInteger(c.destination_flags14),c||{}))return fail('FLOOR_CEILING_CONTRACT_UNKNOWN');
  if(!ok('native destination floor/ceiling equal actor cache',c.actor_floor===c.destination_floor&&c.actor_ceiling===c.destination_ceiling,c))return fail('FLOOR_CEILING_CACHE_WOULD_BE_STALE');
  if(!ok('sector water flag unchanged',(c.source_flags14&0x100)===(c.destination_flags14&0x100),c))return fail('WATER_FLAG_CONTRACT_UNKNOWN');
  if(!ok('target Z between native floor and ceiling',t[2]+lo[2]>=c.destination_floor&&t[2]+hi[2]<=c.destination_ceiling,c))return fail('TARGET_OUTSIDE_FLOOR_CEILING');
  return {status:'ATTESTED',reason:'ATTESTED',checks,crossing:cross,portal};
 },
 // Walker results from common.list(); exactly one node/payload across source+destination.
 membership(before,after,node,payload){
  const lists=[before.source,before.destination,after.source,after.destination];
  if(!lists.every(l=>l&&l.valid===true))return {ok:false,reason:'RECIPROCAL_LIST_CHECK_FAILED'};
  if(before.source.found!==1||before.destination.found!==0)return {ok:false,reason:'PRECONDITION_MEMBERSHIP'};
  const hits=l=>l.entries.filter(e=>e.node===node);
  if(hits(after.source).length!==0)return {ok:false,reason:'STILL_IN_SOURCE'};
  if(hits(after.destination).length!==1||after.destination.found!==1)return {ok:false,reason:'DUPLICATE_OR_MISSING_MEMBERSHIP'};
  if(hits(after.destination)[0].payload!==payload)return {ok:false,reason:'PAYLOAD_MISMATCH'};
  if(after.source.count!==before.source.count-1||after.destination.count!==before.destination.count+1)return {ok:false,reason:'UNEXPECTED_LIST_COUNT'};
  return {ok:true,reason:'ONE_MEMBERSHIP'};
 },
 // Sprite entry vs the live Actor render outputs read at submission; others: same-sector renderables.
 spriteIdentity(entry,actor,others=[]){
  const same=(a,b)=>a.length===b.length&&a.every((v,k)=>Object.is(v,b[k]));
  if(entry.frame!==actor.frame||entry.sector!==actor.sector||entry.flags!==actor.flags||entry.scale!==actor.scale)return {status:'MISMATCH',reason:'IDENTITY_FIELDS'};
  if(actor.z_offset!==0||!same(entry.origin,actor.render_output))return {status:'MISMATCH',reason:'ORIGIN_NOT_ACTOR_RENDER_OUTPUT'};
  if(!same(entry.angles,actor.angles_output))return {status:'MISMATCH',reason:'ANGLES_NOT_ACTOR_RENDER_OUTPUT'};
  const twins=others.filter(o=>o.frame===entry.frame&&same(o.render_output,entry.origin));
  if(twins.length)return {status:'AMBIGUOUS',reason:'OTHER_SPRITE_SAME_TRANSFORM',twins};
  const settled=same(actor.render_output,actor.current);
  return {status:'OURS',intermediate:!settled,settled};
 },
 teleportReset(interp,target){return ['previous','output'].every(k=>interp[k].length===3&&interp[k].every((v,i)=>v===target[i]));}
};
if(typeof module!=='undefined')module.exports={CrossSector58};
