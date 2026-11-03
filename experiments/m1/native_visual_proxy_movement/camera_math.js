// Pure column-major math, used with numeric COPIES of the prepared native view.
const Math57C={
 corners(origin,lo=[-12,-12,-24],hi=[12,12,24]){const out=[];for(let mask=0;mask<8;mask++)out.push(origin.map((v,i)=>v+((mask>>i)&1?hi[i]:lo[i])));return out;},
 project(m,p){const v=Array.from({length:4},(_,r)=>m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r]);return {clip:v,ndc:v.slice(0,3).map(x=>x/v[3])};},
 planes(planes,corners){const margins=planes.map(p=>corners.map(c=>p[0]*c[0]+p[1]*c[1]+p[2]*c[2]+p[3]));return {minimum:margins.map(v=>Math.min(...v)),maximum:margins.map(v=>Math.max(...v)),full_inside:margins.every(v=>Math.min(...v)>=0),native_overlap:margins.every(v=>Math.max(...v)>=0)};},
 box(view,origin){const corners=this.corners(origin),clip=corners.map(c=>this.project(view.clip,c));return {origin,corners,projected:clip,frustum:this.planes(view.planes,corners),clip_full_inside:clip.every(p=>p.clip[3]>0&&p.ndc.every(v=>Number.isFinite(v)&&v>=-1&&v<=1))};}
};
function preparedView(v,sector){
 const clip=floats(v.add(0x1a0),16),model=floats(v.add(0x120),16),depth=Math.hypot(model[2],model[6],model[10]);
 const d={eye:vec(v.add(0xc0)),tan_horizontal_half:v.add(0xcc).readFloat(),tan_vertical_half:v.add(0xd0).readFloat(),projection:floats(v.add(0xe0),16),modelview:model,clip,planes:Array.from({length:6},(_,i)=>floats(v.add(i*16),4)),quaternion:floats(v.add(0x3e0),4),fov:v.add(0x3f0).readFloat(),near:v.add(0x3f8).readFloat(),far:v.add(0x3fc).readFloat(),width:v.add(0x400).readFloat(),height:v.add(0x404).readFloat(),aspect:v.add(0x408).readFloat(),horizontal_mode:v.add(0x45c).readU8(),forward:[-model[2]/depth,-model[6]/depth,-model[10]/depth],depth_scale:depth,sector_pointer:sector.toString(),sector_matches:sector.equals(sectorPointer()),source_rva:'1eff40 RCX prepared view / RDX sector',stack_view_retained:false};
 if(![...d.eye,...clip,...d.planes.flat(),d.fov,d.near,d.far,d.aspect].every(Number.isFinite)||d.fov<=1||d.fov>=179||d.near<=0||d.far<=d.near)throw Error('invalid prepared native view');
 return d;
}
