// Runs the SAME one-shot state class as the game, inside nested onLeave calls.
if(Process.mainModule.name.toLowerCase()!=='python.exe')throw Error('fixture refuses KEX');
const fixtureStorage=Memory.alloc(64);fixtureStorage.writeByteArray(new Uint8Array(64));
const cm=new CModule('extern int values[12]; extern int calls; extern int removals; int* make(int mode){calls++; if(mode==1)return 0; values[0]=1234;values[1]=0;return values;} void drop(int* p){if(p==values){removals++;values[1]=1;}} int outer(int x){volatile int v=x;return v+1;} int count(){return calls;} int removed(){return removals;} void reset(){calls=0;removals=0;}',{values:fixtureStorage,calls:fixtureStorage.add(48),removals:fixtureStorage.add(52)});
const make=new NativeFunction(cm.make,'pointer',['int'],{abi:'win64',exceptions:'propagate'});
const drop=new NativeFunction(cm.drop,'void',['pointer'],{abi:'win64',exceptions:'propagate'});
const nativeCount=new NativeFunction(cm.count,'int',[],'win64'),removedCount=new NativeFunction(cm.removed,'int',[],'win64'),reset=new NativeFunction(cm.reset,'void',[],'win64');
let listener=0,mode=0,current=null;
Interceptor.attach(cm.make,{onEnter(){listener++;}});
function check(condition,message){if(!condition)throw Error('FIXTURE: '+message);}
function api(options={}){return {spawn:()=>make(options.nullReturn?1:0),remove:p=>drop(p),validate:p=>{if(options.badIdentity)throw Error('untrusted graph');check(p.readS32()===1234,'pointer payload');return {node:p.add(4),facts:{graph_verified:true}};},validateRemoval:(p,n)=>{check(p.readS32()===1234,'cleanup identity');check(n.equals(p.add(4)),'node identity');},emit:()=>{},diagnostic:()=>{if(options.diagnosticError)throw Error('telemetry failed');return {listener_observed:listener>0,listener_required:false};}};}
const results=[];
Interceptor.attach(cm.outer,{onLeave(){
 reset();listener=0;const m=new OneActor57C(api());current=m;
 check(m.create(),'nested create');check(m.actor&&!m.actor.isNull()&&m.identitySafe,'returned pointer retained immediately');check(listener===0,'nested listener is absent, reproducing historical defect');
 m.create();check(nativeCount()===1,'duplicate spawn refused');
 for(let i=1;i<=125;i++){check(m.simulationStep(i),'new simulation stamp counted');check(!m.simulationStep(i),'duplicate stamp excluded');}
 check(m.simulationTicks===125&&m.actorTicks===0,'simulation lifetime independent from Actor callbacks');
 m.requestCleanup('explicit');check(m.cleanup(),'cleanup succeeds');m.cleanup();check(removedCount()===1,'single native remove');
 const identity=m.actor;m.poolReturn(identity);check(m.actor===null&&m.node===null,'Actor invalidated at pool return');
 check(m.finishRetirement({payload_null:true,refs:0,in_use:0,sector_occurrences:0}),'retire');m.create();check(nativeCount()===1,'no respawn after retirement');
 results.push({case:'missing_listener',status:'PASS',native_factory_calls:nativeCount(),native_remove_calls:removedCount(),simulation_steps:125,actor_callbacks:0,listener_calls:listener});
 reset();listener=0;const telem=new OneActor57C(api({diagnosticError:true}));check(telem.create()&&telem.identitySafe,'telemetry failure does not lose pointer');check(telem.cleanupReason==='diagnostic_failure','fail-safe cleanup requested');check(telem.cleanup()&&removedCount()===1,'telemetry failure cleanup');results.push({case:'telemetry_failure_cleanup',status:'PASS'});
 reset();const bad=new OneActor57C(api({badIdentity:true}));check(!bad.create()&&bad.actor!==null&&!bad.identitySafe,'untrusted pointer retained as evidence only');bad.requestCleanup('unsafe');check(!bad.cleanup()&&removedCount()===0,'unsafe cleanup refused');bad.create();check(nativeCount()===1,'unsafe telemetry never retries spawn');results.push({case:'untrusted_identity_no_cleanup',status:'PASS'});
 reset();const nil=new OneActor57C(api({nullReturn:true}));check(!nil.create()&&nil.actor===null,'null distinguished from telemetry');nil.create();check(nativeCount()===1&&removedCount()===0,'null one shot');results.push({case:'null_return',status:'PASS'});
}});
Interceptor.flush();const outer=new NativeFunction(cm.outer,'int',['int'],{abi:'win64',exceptions:'propagate'});outer(1);
send({kind:'vp57c_harness_fixture',kex:false,kex_calls:0,game_writes:0,passed:results.length===4,results,source_scope:'Shared one_actor.js + benign CModule; no Actor lifecycle/native game PASS implied'});
