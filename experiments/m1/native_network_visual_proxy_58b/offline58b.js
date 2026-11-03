// DOC58B offline checks of the JS adapter decisions with the UNCHANGED DOC58A controller. No Frida/KEX.
const path=require('path'),fs=require('fs');
const {NetMapping58B:M}=require('./net_mapping58b.js');
const {NativeVisualProxyMovementController:C}=require(path.join(__dirname,'../native_visual_proxy_movement/controller.js'));
const results=[];function check(id,name,pass){results.push({id,name,status:pass?'PASS':'FAIL'});console.log(id,pass?'PASS':'FAIL',name);}
const f=Math.fround;
check('J01','z presentation offset maps host floor 1152 to attested Actor z 1240; x/y unchanged',JSON.stringify(M.target({x:-7420,y:-6775,z:1152},88))===JSON.stringify([-7420,-6775,1240]));
const yaw58a=v=>Math.fround(((v+Math.PI)%(2*Math.PI)+2*Math.PI)%(2*Math.PI)-Math.PI);
check('J02','yaw normalization bit-identical to DOC58A apply()',[0,0.25,-3.5,7,1e-7,-Math.PI,Math.PI].every(v=>Object.is(M.normalizeYaw(v),yaw58a(v))));
check('J03','<=160 units -> native_step (KEX lerp), larger -> teleport+reset',M.mode([0,0,0],[132,0,0],160)==='native_step'&&M.mode([0,0,0],[161,0,0],160)==='teleport');
check('J04','validation refusals map to Refused(3) (no write, controller usable); faults map to Stopped(4)',
 ['OUT_OF_ATTESTED_AREA','TARGET_ALSO_IN_123','NATIVE_POINT_IN_SECTOR_FALSE','FLOOR_CEILING_CACHE_MISMATCH'].every(r=>M.updateCode({ok:false,reason:r})===3)&&
 ['MUTATION_FAILED_STOPPED','STOPPED','STALE_IDENTITY','VALIDATION_UNKNOWN','INVALID_INTERPOLATION','IDENTITY_UNKNOWN'].every(r=>M.updateCode({ok:false,reason:r})===4)&&M.updateCode({ok:true})===1);
check('J05','Remove: only a provably un-attempted call is DefiniteNoCall(2); after an attempt failure is UNKNOWN(0)',M.removeCode({ok:true},true)===1&&M.removeCode({ok:false},false)===2&&M.removeCode({ok:false},true)===0);
check('J06','retirement: only phase retired is Retired; pending phases Pending; blocked Failed; else UNKNOWN',M.retirementCode('retired')===1&&M.retirementCode('pending')===2&&M.retirementCode('retiring')===2&&M.retirementCode('blocked_unsafe')===3&&M.retirementCode('live')===0);
// Controller with an adapter-shaped fake: refusal keeps it usable, apply failure stops it, remove once.
function api(over={}){const s={writes:0,removes:0};const a=Object.assign({onSimulationThread:()=>true,stateExitPending:()=>false,spawnOnce:()=>'a/n',identityMatches:id=>id==='a/n',interpolationValid:()=>true,
 validate:(op,v)=>op==='position'&&v[0]<-8000?{ok:false,reason:'OUT_OF_ATTESTED_AREA'}:{ok:true,value:v},apply:()=>{s.writes++;},checkInvariants:()=>true,removeOnce:()=>{s.removes++;}},over);return {a,s};}
{const {a,s}=api();const c=new C(a);c.start();c.spawn();const r1=c.setPosition([-9000,0,0],'native_step');const r2=c.setPosition([-7400,-6775,1240],'native_step');
 check('J07','out-of-area target refused without write; next valid target applied (controller not stopped)',M.updateCode(r1)===3&&r2.ok&&s.writes===1);}
{const {a,s}=api({apply:()=>{throw Error('postcondition');}});const c=new C(a);c.start();c.spawn();const r1=c.setPosition([-7400,0,0],'native_step');const r2=c.setYaw(1);const rm=c.remove();const rm2=c.remove();
 check('J08','apply failure -> Stopped; later ops Stopped; exactly one Remove even if requested twice',M.updateCode(r1)===4&&M.updateCode(r2)===4&&rm.ok&&!rm2.ok&&rm2.reason==='DOUBLE_REMOVE'&&s.removes===1);}
{const {a}=api({onSimulationThread:()=>false});const c=new C(a);const r=c.start();check('J09','wrong thread refused by controller scope',!r.ok&&r.reason==='WRONG_THREAD');}
{const {a}=api();const c=new C(a);c.start();const s1=c.spawn(),s2=c.spawn();check('J10','controller allows one Spawn attempt only',s1.ok&&!s2.ok&&s2.reason==='SPAWN_ALREADY_ATTEMPTED');}
// Bound-source syntax: the exact concatenation used by runner58b.bind() must parse.
{const sources=JSON.parse(fs.readFileSync(path.join(__dirname,'bound_sources.json'),'utf8'));const body=sources.map(n=>fs.readFileSync(path.join(__dirname,n),'utf8')).join('\n');
 let ok=true;try{new Function('cfg',body);}catch(e){ok=false;console.log(String(e));}check('J11','bound adapter source (DOC58A helpers + adapter58b) parses as one script',ok);}
const failed=results.filter(r=>r.status!=='PASS').length;
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify({suite:'DOC58B offline58b.js',total:results.length,passed:results.length-failed,failed,cases:results},null,2),{flag:'wx'});
console.log('TOTAL',results.length,'FAILED',failed);process.exit(failed?1:0);
