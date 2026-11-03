// Instrumentation-only fixture in a disposable Python process, never KEX.
if(Process.mainModule.name.toLowerCase()!=='python.exe')throw Error('fixture target must be Python');
const cm=new CModule('int doc57_echo(int a) { volatile int v=a; v+=7; return v; }');
let observed=0;
Interceptor.attach(cm.doc57_echo,{onEnter(){observed++;}});
Interceptor.flush();
const ordinary=new NativeFunction(cm.doc57_echo,'int',['int'],{abi:'win64',exceptions:'propagate'});
const all=new NativeFunction(cm.doc57_echo,'int',['int'],{abi:'win64',exceptions:'propagate',traps:'all'});
const before=observed,a=ordinary(35),afterDefault=observed,b=all(35),afterAll=observed;
send({kind:'vp57_frida_call_fixture',pid:Process.id,kex_process:false,kex_calls:0,game_writes:0,default_return:a,default_hook_calls:afterDefault-before,all_return:b,all_hook_calls:afterAll-afterDefault,scope:'NativeFunction/Interceptor observability only; not a KEX Actor lifecycle test'});
