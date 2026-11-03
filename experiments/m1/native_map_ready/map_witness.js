// DOC56 independent read-only map identity witness (one shot). No writes, no calls, no hooks.
// Prefixed at bind time with: const cfg = {pid, target};
if(Process.id!==cfg.pid || Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('map witness target mismatch');
(function(){
  const base=Process.mainModule.base,gi=base.add(0xaf33a0),level=base.add(0xa45940);
  const out={kind:'n56_map_witness',label:cfg.label,pid:Process.id,tid:Process.getCurrentThreadId(),utc:new Date().toISOString(),
    image_base:base.toString(),game_instance:gi.toString(),writes:0,kex_calls:0,
    note:'Read from a Frida thread, not the KEX simulation thread: a racing snapshot, never used as gate input'};
  try{
    out.state=gi.add(0x24).readS32();out.next=gi.add(0x20).readS32();out.pending=gi.add(0x28).readS32();
    const data=gi.add(0x1e0).readPointer(),len=gi.add(0x1e8).readU64().toNumber(),cap=gi.add(0x1f0).readU64().toNumber();
    out.kexstr={data:data.toString(),length:len,capacity:cap};
    if(!data.isNull() && len<=63){
      const bytes=new Uint8Array(data.readByteArray(len+1));out.kexstr.bytes_hex=Array.from(bytes).map(b=>b.toString(16).padStart(2,'0')).join('');
      out.kexstr.terminator_at_length=bytes[len]===0;out.kexstr.text=data.readUtf8String(len);
      out.kexstr.strlen=data.readCString(64).length;
    }
    const info=gi.add(0x1f8).readPointer();out.mapinfo={value:info.toString()};
    if(!info.isNull()){const key=info.add(0x50).readPointer();out.mapinfo.key_ptr=key.toString();if(!key.isNull())out.mapinfo.key=key.readCString(64);}
    const vt=level.readPointer();out.level={object:level.toString(),vtable:vt.toString(),vtable_rva:'0x'+vt.sub(base).toString(16),
      vtable_expected:vt.equals(base.add(0x52b478)),psm_version:level.add(0x208).readU8(),
      count_a:level.add(0xd8).readU64().toNumber(),count_b:level.add(0x18).readU64().toNumber()};
    const slot1=vt.add(8).readPointer(),slot2=vt.add(16).readPointer();
    out.level.slot1_rva='0x'+slot1.sub(base).toString(16);out.level.slot2_rva='0x'+slot2.sub(base).toString(16);
  }catch(e){out.error=String(e);}
  send(out);
})();
