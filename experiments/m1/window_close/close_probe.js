// Passive observation: no KEX calls, replacement, game writes or cleanup.
// cfg is fresh-PID / exact-build bound by bind_probe.py. Load AFTER native hooks.
if (Process.id !== cfg.pid || Process.mainModule.path.toLowerCase() !== cfg.target.toLowerCase()) throw Error('DOC48 target mismatch');
let serial=0, closing=false;
const base=Process.mainModule.base;
function event(kind, data={}) { send(Object.assign({kind,order:++serial,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},data)); }
function addr(p) { const m=Process.findModuleByAddress(p);return {address:p.toString(),module:m ? m.name : null,rva:m ? p.sub(m.base).toString() : null}; }
function trace(context) { return Thread.backtrace(context,Backtracer.ACCURATE).slice(0,14).map(addr); }
for (const s of cfg.sites) {
  const p=base.add(s.rva), bytes=Array.from(new Uint8Array(p.readByteArray(16))).map(x=>x.toString(16).padStart(2,'0')).join('');
  if (bytes!==s.prefix16) throw Error('Unhooked route prefix mismatch '+s.name);
  Interceptor.attach(p, {
    onEnter(args) {
      if (s.name==='dispatch') {
        const type=args[1].readU32();
        if (type!==0x100 && type!==0x101 && !(type===0x200 && args[1].add(12).readU8()===14)) return;
        closing=true;event('wc_native_dispatch',{type,site:s.name,at:addr(p),caller:addr(this.returnAddress),stack:trace(this.context)});
      } else if (s.name==='quit_request' || s.name==='engine_quit') {
        event('wc_native_route',{site:s.name,at:addr(p),caller:addr(this.returnAddress)});
      } else if (s.name==='game_pre_shutdown') {
        event('wc_game_pre_shutdown_entry',{at:addr(p)});
      }
    },
    onLeave() {
      if(s.name==='game_pre_shutdown')event('wc_game_pre_shutdown_return');
    }
  });
}
const sdl=Process.getModuleByName('SDL2.dll');
for (const api of ['SDL_PollEvent','SDL_PushEvent']) {
  Interceptor.attach(sdl.getExportByName(api),{
    onEnter(args) {
      this.ev=args[0];this.caller=addr(this.returnAddress);
      if(api==='SDL_PushEvent' && !this.ev.isNull()) {
        const type=this.ev.readU32();
        if(type===0x100 || type===0x101 || (type===0x200 && this.ev.add(12).readU8()===14)) event('wc_sdl_push',{type,window_event:type===0x200 ? this.ev.add(12).readU8() : null,caller:this.caller});
      }
    },
    onLeave(ret) {
      if(api!=='SDL_PollEvent'||ret.toInt32()!==1||this.ev.isNull())return;
      const type=this.ev.readU32();
      if(type===0x100 || type===0x101 || (type===0x200 && this.ev.add(12).readU8()===14)) event('wc_sdl_poll',{type,window_event:type===0x200 ? this.ev.add(12).readU8() : null,caller:this.caller});
    }
  });
}
for (const api of ['SDL_DestroyWindow','SDL_Quit']) Interceptor.attach(sdl.getExportByName(api),{onEnter(){event('wc_sdl_cleanup',{api});}});
const user=Process.getModuleByName('user32.dll');
Interceptor.attach(user.getExportByName('DispatchMessageW'),{
  onEnter(args) {
    if(args[0].add(8).readU32()!==0x10)return;
    closing=true;event('wc_dispatch_message',{message:0x10,hwnd:args[0].readPointer().toString(),stack:trace(this.context)});
  }
});
let hostSocket=null;
const ioCounts={}, workerTids=new Set();
const sock=Process.getModuleByName('ws2_32.dll');
Interceptor.attach(sock.getExportByName('bind'),{
  onEnter(args){this.socket=args[0].toString();this.port=args[1].add(2).readU16();this.port=((this.port&255)<<8)|(this.port>>>8);},
  onLeave(ret){if(this.port===28745){hostSocket=this.socket;event('wc_socket_bind',{socket:this.socket,port:this.port,result:ret.toInt32()});}}
});
for(const api of ['recvfrom','sendto','WSARecvFrom','WSASendTo']) Interceptor.attach(sock.getExportByName(api),{
  onEnter(args){
    if(args[0].toString()!==hostSocket)return;
    const tid=Process.getCurrentThreadId(),id=api+':'+tid;
    workerTids.add(tid);ioCounts[id]=(ioCounts[id]||0)+1;
    if(ioCounts[id]===1)event('wc_socket_io',{api,socket:hostSocket});
  }
});
Interceptor.attach(sock.getExportByName('closesocket'),{
  onEnter(args){this.selected=args[0].toString()===hostSocket;if(this.selected)event('wc_socket_close_entry',{socket:hostSocket,io_counts:ioCounts,closing});},
  onLeave(ret){if(this.selected)event('wc_socket_close_return',{socket:hostSocket,result:ret.toInt32()});}
});
const getTid=new NativeFunction(Module.getGlobalExportByName('GetThreadId'),'uint32',['pointer'],'win64');
const getCode=new NativeFunction(Module.getGlobalExportByName('GetExitCodeThread'),'int',['pointer','pointer'],'win64');
for(const api of ['WaitForSingleObject','WaitForSingleObjectEx']) Interceptor.attach(Module.getGlobalExportByName(api),{
  onEnter(args){
    this.handle=args[0];this.target=getTid(this.handle);
    this.selected=closing && workerTids.has(this.target);
    if(this.selected)event('wc_worker_wait_entry',{api,handle:this.handle.toString(),target_tid:this.target,timeout:args[1].toUInt32()});
  },
  onLeave(ret){
    if(!this.selected)return;
    const code=Memory.alloc(4),ok=getCode(this.handle,code);
    event('wc_worker_wait_return',{api,handle:this.handle.toString(),target_tid:this.target,result:ret.toUInt32(),get_exit_ok:!!ok,exit_code:ok ? code.readU32() : null});
  }
});
const tables=[];
for(const [role,rva,slots] of [['platform',0xa414b0,[0x88,0x90,0x80]],['engine',0xaec370,[0x20,0x10]],['game',0xaf33a0,[0x18]]]) {
  const v=base.add(rva).readPointer();
  tables.push({role,object:addr(base.add(rva)),vtable:addr(v),slots:slots.map(s=>({slot:s,target:addr(v.add(s).readPointer())}))});
}
event('wc_probe_ready',{pid:Process.id,simulation_tid:cfg.simulation_tid,kex_calls:0,replacements:0,game_writes:0,sdl:addr(sdl.base),sites:cfg.sites,tables});
