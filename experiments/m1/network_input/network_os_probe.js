// Passive OS I/O observation only. No KEX/game-memory access or native calls.
const seen = new Set();
for (const name of ['recvfrom','sendto','WSARecvFrom','WSASendTo']) {
  Interceptor.attach(Module.getGlobalExportByName(name), {
    onEnter(args) {
      const id = name+':'+Process.getCurrentThreadId()+':'+args[0];
      if (!seen.has(id)) {
        seen.add(id);
        send({kind:'m12a_socket_io',api:name,tid:Process.getCurrentThreadId(),socket:args[0].toString()});
      }
    }
  });
}
Interceptor.attach(Module.getGlobalExportByName('bind'), {
  onEnter(args) {
    this.socket=args[0].toString();
    this.port=(args[1].add(2).readU8()<<8)|args[1].add(3).readU8();
  },
  onLeave(ret) { send({kind:'m12a_socket_bind',tid:Process.getCurrentThreadId(),socket:this.socket,port:this.port,result:ret.toInt32()}); }
});
Interceptor.attach(Module.getGlobalExportByName('closesocket'), {
  onEnter(args) { this.socket=args[0].toString(); send({kind:'m12a_socket_close_entry',tid:Process.getCurrentThreadId(),socket:this.socket}); },
  onLeave(ret) { send({kind:'m12a_socket_close_return',tid:Process.getCurrentThreadId(),socket:this.socket,result:ret.toInt32()}); }
});
send({kind:'m12a_network_os_probe_ready',pid:Process.id,kex_hooks:0});
