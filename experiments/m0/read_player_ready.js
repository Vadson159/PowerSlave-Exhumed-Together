'use strict';
const m=Process.mainModule,p=m.base.add(0xaf33e0);
if(Process.id!==cfg.pid||m.path.toLowerCase()!==cfg.target.toLowerCase())throw new Error('ready read target mismatch');
send({kind:'C_original_ready_interpolants',pid:Process.id,utc:new Date().toISOString(),player:p.toString(),
 interpolants:[0x160,0x178].map(o=>{const q=p.add(o),v=q.readPointer(),ref=q.add(8).readPointer();return {
  member_offset:o,vtable:v.toString(),reference:ref.toString(),reference_relative_to_player:ref.sub(p).toString(),
  values:[q.add(0x10).readFloat(),q.add(0x14).readFloat()],referenced_value:ref.isNull()?null:ref.readFloat()};})});
