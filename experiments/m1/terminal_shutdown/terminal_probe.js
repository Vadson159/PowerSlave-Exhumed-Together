// cfg binds fresh PID/path/primary TID. Native fixed-slot ledger, drained externally.
// No JS terminal callbacks, KEX calls, replacement, or game-state writes.
if(Process.id!==cfg.pid || Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('terminal probe identity');
const ledger=Memory.alloc(4096);ledger.writeByteArray(new Uint8Array(4096));
const cm=new CModule(`
#include <gum/guminterceptor.h>
typedef struct { volatile unsigned int entered; unsigned int tid; unsigned long long filetime; void *arg0; void *arg1; void *caller; } Rec;
extern Rec records[32];
extern unsigned int GetCurrentThreadId(void);
extern void GetSystemTimeAsFileTime(void *);
static void mark(GumInvocationContext *ic, unsigned int slot) {
  unsigned int tid=GetCurrentThreadId();
  if(tid!=${cfg.primary_tid})return;
  volatile Rec *r=&records[slot];r->tid=tid;GetSystemTimeAsFileTime((void*)&r->filetime);
  r->arg0=gum_invocation_context_get_nth_argument(ic,0);r->arg1=gum_invocation_context_get_nth_argument(ic,1);
  r->caller=gum_invocation_context_get_return_address(ic);r->entered=1;
}
void crt(GumInvocationContext *ic){mark(ic,0);}
void exit_process(GumInvocationContext *ic){mark(ic,1);}
void rtl(GumInvocationContext *ic){mark(ic,2);}
void ldr_enter(GumInvocationContext *ic){mark(ic,3);}
void ldr_leave(GumInvocationContext *ic){mark(ic,4);}
void nt_term(GumInvocationContext *ic){mark(ic,5);}
`,{records:ledger,GetCurrentThreadId:Module.getGlobalExportByName('GetCurrentThreadId'),GetSystemTimeAsFileTime:Module.getGlobalExportByName('GetSystemTimeAsFileTime')});
const iat=Process.mainModule.base.add(0x482c40).readPointer();
const hooks=[['ucrt__exit',iat,cm.crt],['ExitProcess',Module.getGlobalExportByName('ExitProcess'),cm.exit_process],['RtlExitUserProcess',Module.getGlobalExportByName('RtlExitUserProcess'),cm.rtl],['LdrShutdownProcess',Module.getGlobalExportByName('LdrShutdownProcess'),cm.ldr_enter,cm.ldr_leave],['NtTerminateProcess',Module.getGlobalExportByName('NtTerminateProcess'),cm.nt_term]];
for(const [name,p,enter,leave] of hooks)Interceptor.attach(p,leave?{onEnter:enter,onLeave:leave}:{onEnter:enter});
globalThis.terminal52={cm,ledger,hooks};
send({kind:'terminal52_ledger_ready',pid:Process.id,primary_tid:cfg.primary_tid,address:ledger.toString(),bytes:4096,record_size:40,slots:['ucrt__exit','ExitProcess','RtlExitUserProcess','LdrShutdownProcess_entry','LdrShutdownProcess_return','NtTerminateProcess'],hooks:hooks.map(([name,p])=>({name,address:p.toString(),module:Process.findModuleByAddress(p)?.path})),js_terminal_callbacks:0});
