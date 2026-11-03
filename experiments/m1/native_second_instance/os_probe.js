// Passive Windows API observations. No KEX hooks/reads/calls, no packet mutation.
let order=0;
function emit(kind,data) { send(Object.assign({kind,order:++order,pid:Process.id,tid:Process.getCurrentThreadId(),utc:new Date().toISOString()},data)); }
function inputPackets(buffers) {
  const total=buffers.reduce((n,b)=>n+b.length,0);
  if(total>65536)return [];
  const bytes=new Uint8Array(total);let offset=0;
  for(const b of buffers){bytes.set(b,offset);offset+=b.length;}
  const v=new DataView(bytes.buffer),out=[];
  // Record only recognized v1.10 INPUT fields. Never dump HELLO/WELCOME secrets.
  for(let i=0;i+45<=total;++i) {
    if(v.getUint32(i,true)!==0x50534558 || v.getUint16(i+4,true)!==0x0110 || bytes[i+6]!==0x10)continue;
    out.push({slot:bytes[i+11],generation:v.getUint32(i+12,true),input_seq:v.getUint32(i+16,true),client_tick:v.getUint32(i+20,true),forward:v.getFloat32(i+32,true)});
    i+=44;
  }
  return out;
}
for(const name of ['WSASendTo','sendto']) {
  Interceptor.attach(Module.getGlobalExportByName(name),{
    onEnter(a) {
      this.socket=a[0].toString();this.inputs=[];
      try {
        if(name==='WSASendTo') {
          const count=a[2].toUInt32(),buffers=[];
          if(count>64)return;
          for(let i=0;i<count;++i){const b=a[1].add(i*16),length=b.readU32();if(length>65536)return;buffers.push(new Uint8Array(b.add(8).readPointer().readByteArray(length)));}
          this.inputs=inputPackets(buffers);this.sentPtr=a[3];this.overlapped=!a[7].isNull();
        } else this.inputs=inputPackets([new Uint8Array(a[1].readByteArray(a[2].toUInt32()))]);
      } catch(e){emit('n51_udp_probe_error',{api:name,error:String(e)});}
    },
    onLeave(ret) {
      for(const identity of this.inputs)emit('n51_udp_input_send_return',Object.assign({api:name,socket:this.socket,result:ret.toInt32(),overlapped:this.overlapped||false,bytes_sent:this.sentPtr&&!this.sentPtr.isNull()?this.sentPtr.readU32():null},identity));
    }
  });
}
Interceptor.attach(Module.getGlobalExportByName('bind'),{
  onEnter(a){this.socket=a[0].toString();this.port=(a[1].add(2).readU8()<<8)|a[1].add(3).readU8();},
  onLeave(ret){emit('n51_socket_bind',{socket:this.socket,port:this.port,result:ret.toInt32()});}
});
Interceptor.attach(Module.getGlobalExportByName('closesocket'),{
  onEnter(a){this.socket=a[0].toString();emit('n51_socket_close_entry',{socket:this.socket});},
  onLeave(ret){emit('n51_socket_close_return',{socket:this.socket,result:ret.toInt32()});}
});
const getTid=new NativeFunction(Module.getGlobalExportByName('GetThreadId'),'uint32',['pointer'],'win64');
const getExit=new NativeFunction(Module.getGlobalExportByName('GetExitCodeThread'),'int',['pointer','pointer'],'win64');
for(const api of ['WaitForSingleObject','WaitForSingleObjectEx'])Interceptor.attach(Module.getGlobalExportByName(api),{
  onEnter(a){this.handle=a[0];this.target=getTid(a[0]);if(this.target)emit('n51_thread_wait_entry',{api,target_tid:this.target,handle:this.handle.toString(),timeout:a[1].toUInt32()});},
  onLeave(ret){if(!this.target)return;const code=Memory.alloc(4),ok=getExit(this.handle,code);emit('n51_thread_wait_return',{api,target_tid:this.target,handle:this.handle.toString(),result:ret.toUInt32(),exit_code:ok?code.readU32():null});}
});
emit('n51_os_probe_ready',{target:Process.mainModule.path,kex_hooks:0,kex_reads:0});
