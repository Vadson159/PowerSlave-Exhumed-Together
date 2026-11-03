// One read-only numeric copy from the established prepared-view callback.
if(Process.id!==33460)throw Error('owned final run only');
const b=Process.mainModule.base;let done=false;
Interceptor.attach(b.add(0x1eff40),{onEnter(args){
 if(done||Process.getCurrentThreadId()!==32052)return;
 const p=b.add(0xaf33e0),n=p.add(0xd8).readPointer(),a=n.add(0x28).readPointer(),gi=b.add(0xaf33a0);
 const pos=[0,4,8].map(o=>a.add(0x6c+o).readFloat()),eye=[0,4,8].map(o=>args[0].add(0xc0+o).readFloat());
 const zterms=[gi.add(0x1cc).readFloat(),gi.add(0x1b4).readFloat()];
 done=true;send({kind:'post_retirement_native_camera',pid:Process.id,tid:Process.getCurrentThreadId(),native_tick:b.add(0xaec390).readU32(),player:p.toString(),node:n.toString(),puppet:a.toString(),owner:a.add(0x1b8).readPointer().toString(),interpolated_position:pos,eye,zterms,camera_matches:Math.abs(eye[0]-pos[0])<0.01&&Math.abs(eye[1]-pos[1])<0.01&&Math.abs(eye[2]-pos[2]-zterms[0]-zterms[1])<0.01,game_writes:0,retired_actor_reads:0});
}});
