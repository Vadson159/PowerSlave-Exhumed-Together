// Exact-build read-only helpers. Heap offsets here are object-layout offsets.
const base=Process.mainModule.base, gi=base.add(0xaf33a0), local=gi.add(0x40);
if(Process.id!==cfg.pid || Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('target mismatch');
function bytes(p,n){return Array.from(new Uint8Array(p.readByteArray(n))).map(x=>x.toString(16).padStart(2,'0')).join('');}
for(const [name,s] of Object.entries(cfg.sites))if(bytes(base.add(s.rva),16)!==s.prefix)throw Error('prefix mismatch '+name);
const journal=new File(cfg.log,'w');let serial=0;
function emit(event,fields={}){const d=Object.assign({event,seq:++serial,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},fields);journal.write(JSON.stringify(d)+'\n');journal.flush();send(d);}
function thread(){return Process.getCurrentThreadId()===cfg.simulation_tid;}
function vec(p){return [p.readFloat(),p.add(4).readFloat(),p.add(8).readFloat()];}
function world(){
 const name=gi.add(0x1e0).readPointer(),len=gi.add(0x1e8).readU64().toNumber(),cap=gi.add(0x1f0).readU64().toNumber(),info=gi.add(0x1f8).readPointer();
 const map=len>0&&len<=32&&cap>len&&!name.isNull()?name.readUtf8String(len):'';
 const key=!info.isNull()?info.add(0x50).readPointer().readUtf8String():'';
 return {state:gi.add(0x24).readS32(),next:gi.add(0x20).readS32(),pending:gi.add(0x28).readS32(),map,key,len,cap,nul:len<=32&&!name.isNull()?name.add(len).readU8():-1,
  level_vtable:base.add(0xa45940).readPointer().equals(base.add(0x52b478)),version:base.add(0xa45940+0x208).readU8(),count_a:base.add(0xa45940+0xd8).readU64().toString(),count_b:base.add(0xa45940+0x18).readU64().toString()};
}
function stableWorld(w){return w.state===3&&w.next===3&&w.pending===0&&w.map==='maps/TOMB.MAP'&&w.key===w.map&&w.nul===0&&w.level_vtable&&w.version===1&&w.count_a==='71'&&w.count_b==='28608';}
function primary(){
 const node=local.add(0xd8).readPointer();if(node.isNull())return null;
 const a=node.add(0x28).readPointer();if(a.isNull())return null;
 return {player:local.toString(),node:node.toString(),actor:a.toString(),own_node:a.add(0x10).readPointer().toString(),owner:a.add(0x1b8).readPointer().toString(),vtable:a.readPointer().toString(),pos:vec(a.add(0x18)),yaw:a.add(0x38).readFloat(),pitch:a.add(0x30).readFloat(),sector:a.add(0x3c).readS32(),health:a.add(0x17a).readS16(),weapons:bytes(local.add(0xcc),8),ammo:bytes(local.add(0xbc),16),weapon:local.add(0x88).readS32(),pending_weapon:local.add(0x8c).readS32(),input:bytes(local,0x80)};
}
function validPrimary(p){return p&&p.node===p.own_node&&p.owner===p.player&&p.vtable===base.add(0x52d198).toString();}
function protectedPrimary(p){return {player:p.player,node:p.node,actor:p.actor,own_node:p.own_node,owner:p.owner,vtable:p.vtable,health:p.health,weapons:p.weapons,ammo:p.ammo,weapon:p.weapon,pending_weapon:p.pending_weapon};}
function geometry(point){
 const count=base.add(0xa45970).readU64().toNumber(),sectors=base.add(0xa45968).readPointer(),fc=base.add(0xa45988).readU64().toNumber(),faces=base.add(0xa45980).readPointer();
 if(count<=259||count>100000||fc>1000000)throw Error('geometry bounds');
 const s=sectors.add(259*0x88),first=s.readU16(),end=s.add(2).readU16()+3;
 if(end<=first||end>fc||end-first>512)throw Error('face range');
 const planes=[];
 for(let i=first;i<end;i++){const f=faces.add(i*0x108),n=vec(f.add(0xc)),d=f.add(0x18).readFloat();const distance=n[0]*point[0]+n[1]*point[1]+n[2]*point[2]+d;if(!Number.isFinite(distance))throw Error('nonfinite plane');planes.push({face:i,n,d,distance});}
 return {sector:259,count,face_count:fc,first,end,point,planes,inside:planes.every(x=>x.distance>=-0.05)};
}
function list(identity){
 const count=base.add(0xa45970).readU64().toNumber();if(count<=259)throw Error('sector absent');
 const sector=base.add(0xa45968).readPointer().add(259*0x88),head=sector.add(0x78).readPointer();
 if(head.isNull()||!head.readPointer().isNull())throw Error('bad sentinel');
 let prev=head,node=head.add(8).readPointer(),found=0,total=0;
 // Engine list has separate leading/trailing sentinels, not a circular sentinel.
 while(!node.isNull()&&!node.add(8).readPointer().isNull()){
  if(++total>4096||!node.readPointer().equals(prev)||!prev.add(8).readPointer().equals(node))throw Error('sector list corruption');
  if(identity&&!identity.isNull()&&node.equals(identity))found++;
  prev=node;node=node.add(8).readPointer();
 }
 if(node.isNull()||!node.readPointer().equals(prev))throw Error('bad tail sentinel');
 return {count:total,found,head:head.toString(),tail:node.toString(),valid:true};
}
