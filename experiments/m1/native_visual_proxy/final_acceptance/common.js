// Exact-build reads only. No Actor/Player/camera field is written here.
const base=Process.mainModule.base,gi=base.add(0xaf33a0),local=gi.add(0x40);
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('target mismatch');
function bytes(p,n){return Array.from(new Uint8Array(p.readByteArray(n))).map(x=>x.toString(16).padStart(2,'0')).join('');}
for(const [name,s] of Object.entries(cfg.sites))if(bytes(base.add(s.rva),16)!==s.prefix)throw Error('prefix mismatch '+name);
const journal=new File(cfg.log,'w');let serial=0;
function emit(event,fields={}){const d=Object.assign({event,seq:++serial,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},fields);journal.write(JSON.stringify(d)+'\n');journal.flush();send(d);}
function thread(){return Process.getCurrentThreadId()===cfg.simulation_tid;}
function floats(p,n){return Array.from({length:n},(_,i)=>p.add(i*4).readFloat());}
function vec(p){return floats(p,3);}
function equal(a,b){return JSON.stringify(a)===JSON.stringify(b);}
function world(){
 const name=gi.add(0x1e0).readPointer(),len=gi.add(0x1e8).readU64().toNumber(),cap=gi.add(0x1f0).readU64().toNumber(),info=gi.add(0x1f8).readPointer();
 return {state:gi.add(0x24).readS32(),next:gi.add(0x20).readS32(),pending:gi.add(0x28).readS32(),map:len>0&&len<=32&&cap>len&&!name.isNull()?name.readUtf8String(len):'',key:!info.isNull()?info.add(0x50).readPointer().readUtf8String():'',len,cap,nul:len<=32&&!name.isNull()?name.add(len).readU8():-1,level_vtable:base.add(0xa45940).readPointer().equals(base.add(0x52b478)),version:base.add(0xa45940+0x208).readU8(),count_a:base.add(0xa45940+0xd8).readU64().toString(),count_b:base.add(0xa45940+0x18).readU64().toString()};
}
function stableWorld(w){return w.state===3&&w.next===3&&w.pending===0&&w.map==='maps/TOMB.MAP'&&w.key===w.map&&w.nul===0&&w.level_vtable&&w.version===1&&w.count_a==='71'&&w.count_b==='28608';}
function primary(){
 const n=local.add(0xd8).readPointer();if(n.isNull())return null;const a=n.add(0x28).readPointer();if(a.isNull())return null;
 return {player:local.toString(),node:n.toString(),actor:a.toString(),own_node:a.add(0x10).readPointer().toString(),owner:a.add(0x1b8).readPointer().toString(),vtable:a.readPointer().toString(),pos:vec(a.add(0x18)),yaw:a.add(0x38).readFloat(),pitch:a.add(0x30).readFloat(),interpolated_pos:vec(a.add(0x6c)),interpolated_angles:vec(a.add(0x94)),sector:a.add(0x3c).readS32(),health:a.add(0x17a).readS16(),owned_weapons:bytes(local.add(0xcc),8),ammo:bytes(local.add(0xbc),16),weapon:local.add(0x88).readS32(),pending_weapon:local.add(0x8c).readS32(),input:bytes(local,0x80)};
}
function validPrimary(p){return p&&p.node===p.own_node&&p.owner===p.player&&p.vtable===base.add(0x52d198).toString();}
function graph(p){return {player:p.player,node:p.node,actor:p.actor,own_node:p.own_node,owner:p.owner,vtable:p.vtable};}
function inventory(p){return {health:p.health,owned_weapons:p.owned_weapons,ammo:p.ammo,weapon:p.weapon,pending_weapon:p.pending_weapon};}
function sectorPointer(index=259){const count=base.add(0xa45970).readU64().toNumber();if(count<=index||index<0||count>100000)throw Error('sector bounds');return base.add(0xa45968).readPointer().add(index*0x88);}
function list(identity=null,index=259){
 const head=sectorPointer(index).add(0x78).readPointer();if(head.isNull()||!head.readPointer().isNull())throw Error('bad leading sentinel');
 let prev=head,n=head.add(8).readPointer(),found=0;const entries=[];
 while(!n.isNull()&&!n.add(8).readPointer().isNull()){
  if(entries.length>=4096||!n.readPointer().equals(prev)||!prev.add(8).readPointer().equals(n))throw Error('sector list corruption');
  const a=n.add(0x28).readPointer();entries.push({node:n.toString(),payload:a.toString()});if(identity&&n.equals(identity))found++;
  prev=n;n=n.add(8).readPointer();
 }
 if(n.isNull()||!n.readPointer().equals(prev))throw Error('bad trailing sentinel');
 return {valid:true,count:entries.length,found,entries,head:head.toString(),tail:n.toString()};
}
function sectorGeometry(point){
 const s=sectorPointer(),count=base.add(0xa45988).readU64().toNumber(),faces=base.add(0xa45980).readPointer(),first=s.readU16(),end=s.add(2).readU16()+3;
 if(end<=first||end>count||end-first>512)throw Error('native face range');
 const planes=[];for(let i=first;i<end;i++){const f=faces.add(i*0x108),n=vec(f.add(0xc)),d=f.add(0x18).readFloat();planes.push({face:i,n,d,distance:n[0]*point[0]+n[1]*point[1]+n[2]*point[2]+d});}
 if(!planes.every(p=>Number.isFinite(p.distance)))throw Error('nonfinite sector plane');return {sector:259,point,planes};
}
function actorFacts(a){
 const n=a.add(0x10).readPointer();if(n.isNull())throw Error('Actor has no node');
 const anim=a.add(0x108).readPointer(),id=a.add(0x178).readS16();if(anim.isNull()||id<0||id>=anim.add(0x24).readU32())throw Error('animation/frame bounds');
 const frame=anim.add(0x18).readPointer().add(id*0x110);
 return {node:n,facts:{actor:a.toString(),node:n.toString(),payload:n.add(0x28).readPointer().toString(),vtable:a.readPointer().toString(),position:vec(a.add(0x18)),interpolated_position:vec(a.add(0x6c)),bounds_min:vec(a.add(0xec)),bounds_max:vec(a.add(0xf8)),yaw:a.add(0x38).readFloat(),sector:a.add(0x3c).readS32(),flags:a.add(0x16c).readU32(),removed:a.add(0x40).readU8(),refs:n.add(0x40).readS32(),in_use:n.add(0x24).readU8(),definition:a.add(0x170).readPointer().toString(),type:a.add(0x170).readPointer().readS32(),animation:anim.toString(),frame_id:id,frame:frame.toString(),scale:a.add(0x124).readFloat(),target:a.add(0x28).readPointer().toString(),source_owner:a.add(0x148).readPointer().toString()}};
}
