// Passive OS thread-handle evidence for the network join. No KEX hooks/reads.
const threadId = new NativeFunction(Module.getGlobalExportByName('GetThreadId'),'uint32',['pointer'],'win64');
const exitCode = new NativeFunction(Module.getGlobalExportByName('GetExitCodeThread'),'int',['pointer','pointer'],'win64');
for (const api of ['WaitForSingleObject','WaitForSingleObjectEx']) {
  Interceptor.attach(Module.getGlobalExportByName(api),{
    onEnter(args) {
      this.handle=args[0]; this.targetTid=threadId(args[0]);
      if(this.targetTid) send({kind:'m12a_thread_wait_entry',api,tid:Process.getCurrentThreadId(),target_tid:this.targetTid,handle:this.handle.toString(),timeout:args[1].toUInt32()});
    },
    onLeave(result) {
      if(!this.targetTid) return;
      const code=Memory.alloc(4); const ok=exitCode(this.handle,code);
      send({kind:'m12a_thread_wait_return',api,tid:Process.getCurrentThreadId(),target_tid:this.targetTid,handle:this.handle.toString(),result:result.toUInt32(),exit_code:ok ? code.readU32() : null});
    }
  });
}
send({kind:'m12a_join_probe_ready',pid:Process.id,kex_hooks:0});
