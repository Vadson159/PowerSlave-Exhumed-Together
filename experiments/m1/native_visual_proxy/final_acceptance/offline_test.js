const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const lab=__dirname,rows=[];
function check(name,fn){try{fn();rows.push({name,status:'PASS'});}catch(e){rows.push({name,status:'FAIL',error:e.stack});}}
const membership=require('./sector_membership.js');
const p={node:'pnode',actor:'pactor',sector:259},a={node:'anode',payload:'aactor'},pe={node:p.node,payload:p.actor};
const m={valid:true,found:1,count:2,entries:[a,pe]};
check('P1+Actor259',()=>assert(membership(m,a.node,p)));
check('P1 leaves123 Actor stays259',()=>assert(membership({...m,count:1,entries:[a]},a.node,{...p,sector:123})));
check('P1 returns259',()=>assert(membership(m,a.node,p)));
for(const [name,mm,pp] of [['duplicate node',{...m,entries:[a,a]},p],['stale payload',{...m,entries:[a,{...pe,payload:'stale'}]},p],['missing actor',{...m,found:0},p],['invalid reciprocal list',{...m,valid:false},p],['unknown current sector',m,{...p,sector:undefined}],['wrong current sector',m,{...p,sector:123}]])check(name,()=>assert(!membership(mm,a.node,pp)));
const ctx={};vm.createContext(ctx);vm.runInContext(fs.readFileSync(lab+'/one_actor.js','utf8')+'\nthis.State=OneActor57C;',ctx);
check('diagnostic error safe cleanup exactly once',()=>{let spawns=0,removes=0;const ptr={toString:()=> 'actor',isNull:()=>false};const s=new ctx.State({spawn:()=>{spawns++;return ptr;},validate:()=>({node:{toString:()=> 'node'},facts:{}}),emit:()=>{},diagnostic:()=>{throw Error('injected unreadable inventory');},validateRemoval:()=>{},remove:()=>removes++});s.create();s.create();assert(s.cleanup());s.cleanup();assert.equal(spawns,1);assert.equal(removes,1);});
const inv=require('./inventory_snapshot.js');inv.compare=inv.compareP1State57;
function snap(){return {status:'OK',binary_sha256:'exact',pid:123,runtime_base:'base',identity:{player:'p'},fields:Object.fromEntries(inv.P1Fields57.map(([name,off,size])=>[name,{status:'OK',raw:'00'.repeat(size),decoded:0,offset:off}]))};}
check('complete contract 12 mandatory fields',()=>assert.equal(inv.P1Fields57.length,12));
check('per-field equality',()=>assert.equal(inv.compare(snap(),snap()).status,'EQUAL'));
check('exact difference names and bytes',()=>{const s=snap();s.fields.artifacts.raw='01000000';const c=inv.compare(snap(),s);assert.equal(c.status,'DIFFERENT');assert.equal(c.differences[0].field,'artifacts');assert.equal(c.differences[0].after_raw,'01000000');});
check('mandatory unreadable -> UNKNOWN',()=>{const s=snap();s.status='UNKNOWN';s.fields.keys.status='UNKNOWN';assert.equal(inv.compare(snap(),s).status,'UNKNOWN');});
check('missing field -> UNKNOWN',()=>{const s=snap();delete s.fields.ammo;assert.equal(inv.compare(snap(),s).status,'UNKNOWN');});
const report={status:rows.every(r=>r.status==='PASS')?'PASS':'FAIL',scope:'offline fixtures, no KEX',checks:rows};console.log(JSON.stringify(report,null,2));process.exitCode=report.status==='PASS'?0:1;
