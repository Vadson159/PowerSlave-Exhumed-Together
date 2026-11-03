// One read-only failure snapshot. No native game calls or game writes.
if(Process.id!==32064||!Process.mainModule.path.toLowerCase().includes('powerslave_exhumed_together_mod\\experiments\\m0\\game\\'))throw Error('failure witness target');
const b=Process.mainModule.base,g=b.add(0xaf33a0),p=g.add(0x40),n=p.add(0xd8).readPointer(),a=n.isNull()?ptr(0):n.add(0x28).readPointer();
const result={kind:'vp57_failure_readonly_witness',pid:Process.id,tid:Process.getCurrentThreadId(),utc:new Date().toISOString(),image_base:b.toString(),state:g.add(0x24).readS32(),next:g.add(0x20).readS32(),pending:g.add(0x28).readS32(),native_calls:0,game_writes:0,observation_context:'Frida diagnostic thread; not simulation attestation',primary:{player:p.toString(),node:n.toString(),actor:a.toString()},actors:[]};
if(!a.isNull())Object.assign(result.primary,{owner:a.add(0x1b8).readPointer().toString(),own_node:a.add(0x10).readPointer().toString(),vtable:a.readPointer().toString(),health:a.add(0x17a).readS16(),sector:a.add(0x3c).readS32(),pos:[a.add(0x18).readFloat(),a.add(0x1c).readFloat(),a.add(0x20).readFloat()]});
if(result.state===3&&result.next===3&&result.pending===0){
 const s=b.add(0xa45968).readPointer().add(259*0x88),h=s.add(0x78).readPointer();let node=h.add(8).readPointer(),count=0;
 while(!node.isNull()&&!node.add(8).readPointer().isNull()){
  if(++count>64)throw Error('bounded read-only list');
  const actor=node.add(0x28).readPointer();
  if(!actor.isNull())result.actors.push({node:node.toString(),actor:actor.toString(),vtable:actor.readPointer().toString(),pos:[actor.add(0x18).readFloat(),actor.add(0x1c).readFloat(),actor.add(0x20).readFloat()],sector:actor.add(0x3c).readS32(),flags:actor.add(0x16c).readU32(),refs:node.add(0x40).readS32(),definition:actor.add(0x170).readPointer().toString(),animation:actor.add(0x108).readPointer().toString()});
  node=node.add(8).readPointer();
 }
 result.sector259_count=count;
}
send(result);
