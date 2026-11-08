// P1 ordinary-input navigation witness. Reads local P1 only on its native scene thread.
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('host location target');
const b=Process.mainModule.base,s=cfg.site;
const hex=Array.from(new Uint8Array(b.add(s.rva).readByteArray(16))).map(v=>v.toString(16).padStart(2,'0')).join('');
if(hex!==s.prefix)throw Error('host scene prefix');
const f=new File(cfg.log,'w');let frame=0;
Interceptor.attach(b.add(s.rva),{onEnter(){
 if(Process.getCurrentThreadId()!==cfg.tid||b.add(0xaf33c4).readS32()!==3||b.add(0xaf33c0).readS32()!==3)return;
 if(frame++%60!==0)return;
 const p=b.add(0xaf33e0),n=p.add(0xd8).readPointer();if(n.isNull())return;
 const a=n.add(0x28).readPointer();if(a.isNull()||!a.add(0x10).readPointer().equals(n)||!a.add(0x1b8).readPointer().equals(p))return;
 const r={event:'host_p1_location',utc:new Date().toISOString(),tid:Process.getCurrentThreadId(),frame,
  pos:[a.add(0x18).readFloat(),a.add(0x1c).readFloat(),a.add(0x20).readFloat()],sector:a.add(0x3c).readS32(),
  yaw:a.add(0x38).readFloat(),health:a.add(0x17a).readS16(),flags:a.add(0x16c).readU32(),
  forward:p.add(0xc).readU32(),back:p.add(0x10).readU32(),game_writes:0};
 f.write(JSON.stringify(r)+'\n');f.flush();send(r);
}});
