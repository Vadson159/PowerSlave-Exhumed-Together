// Reproduce NativeFunction calls inside an Interceptor.onLeave; never KEX.
if(Process.mainModule.name.toLowerCase()!=='python.exe')throw Error('fixture target must be Python');
const cm=new CModule('int doc57_inner(int a) { volatile int v=a; v+=7; return v; } int doc57_outer(int a) { volatile int v=a; v+=1; return v; }');
let seen=0,nested=null;
Interceptor.attach(cm.doc57_inner,{onEnter(){seen++;}});
const normal=new NativeFunction(cm.doc57_inner,'int',['int'],{abi:'win64',exceptions:'propagate'});
const trapped=new NativeFunction(cm.doc57_inner,'int',['int'],{abi:'win64',exceptions:'propagate',traps:'all'});
Interceptor.attach(cm.doc57_outer,{onLeave(){const before=seen,a=normal(35),middle=seen,b=trapped(35);nested={default_return:a,default_hook_calls:middle-before,all_return:b,all_hook_calls:seen-middle};}});
Interceptor.flush();
const outer=new NativeFunction(cm.doc57_outer,'int',['int'],{abi:'win64',exceptions:'propagate'});
const value=outer(3);
send({kind:'vp57_frida_nested_call_fixture',pid:Process.id,kex_process:false,kex_calls:0,game_writes:0,outer_return:value,nested,scope:'Interceptor.onLeave calling NativeFunction; no KEX ABI/lifecycle acceptance'});
