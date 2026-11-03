/* Real observer with mock callbacks: transient arenas must not grow the trace. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const callbacks=new Map(),events=[],memory=new Map([[0x700000,0x5189d0],[0x518a50,0xfc580],[0xaec370,0x600000],[0x600010,0xa4c90]]);
const prefixes=new Map([[0x1b5520,'488d4140c3']]);
class P{constructor(n){this.n=n;}add(n){return new P(this.n+n);}sub(p){return new P(this.n-p.n);}toString(){return '0x'+this.n.toString(16);}equals(p){return this.n===p.n;}readPointer(){return new P(memory.get(this.n)||0);}readByteArray(n){return Uint8Array.from(Buffer.from(prefixes.get(this.n)||'00'.repeat(n),'hex')).buffer;}}
const cfg={native_prefixes:{}};for(const [i,name]of ['commands','turning','controller_update'].entries()){cfg.native_prefixes[name]={rva:100+i,prefix:'11'.repeat(16)};prefixes.set(100+i,'11'.repeat(16));}
const ctx={base:new P(0),p2:new P(0x900000),local:new P(0x800000),cfg,frames:120,
 prefix:()=>{},Process:{getModuleByName:()=>({findExportByName:()=>null})},
 Interceptor:{attach:(p,cb)=>callbacks.set(p.n,cb)},simulationThread:()=>true,guards:()=>true,
 p1Identity:null,p2Identity:null,retiredPuppets:[],referenceCandidates:[],controller:{counts:()=>({})},
 graph:p=>({live_identity:true,health:200,actor:p.toString(),node:p.toString()}),
 event:(kind,data)=>events.push({kind,...JSON.parse(JSON.stringify(data))})};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/direct_quit_observer.js','utf8')+'\nglobalThis.observer=createM0DirectQuitObserver();',ctx);ctx.observer.tick();
function arena(n){const frame={};callbacks.get(0xc58b0).onEnter.call(frame,[new P(n)]);callbacks.get(0xc58b0).onLeave.call(frame);}
for(let i=0;i<10000;i++)arena(0xaecc60);
callbacks.get(0xfcc20).onEnter.call({},[new P(0x700000)]);
for(let i=0;i<10000;i++)arena(0xaecc60);
arena(0xaecc20);arena(0xaecca0);
callbacks.get(0xfc580).onEnter.call({});
let last=events.at(-1);assert.equal(last.kind,'C_direct_quit_final_exit');assert.equal(last.snapshot.trace.arenas.length,2);
assert.equal(last.snapshot.trace.other_arenas_after_quiesce,10000);assert.equal(last.snapshot.trace.diagnostic_overflow,false);
assert.equal(last.snapshot.trace.hooks.passed,true);assert.ok(events.some(e=>e.kind==='C_direct_quit_arena_entry'&&e.arena_kind==='known_engine_arena'));
assert.ok(JSON.stringify(last).length<6000);
for(let i=0;i<20;i++)arena(0xaf4490);callbacks.get(0xfc580).onEnter.call({});
last=events.at(-1);assert.equal(last.snapshot.trace.arenas.length,16);assert.equal(last.snapshot.trace.diagnostic_overflow,true);
console.log(JSON.stringify({status:'PASS',transient_callbacks:20000,retained_arenas_limit:16,event_kind_preserved:true,native_hooks:0,engine_calls:0,scope:'Actual observer with own mock callbacks; not game acceptance'}));
