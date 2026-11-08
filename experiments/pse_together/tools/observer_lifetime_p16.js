// Bind cfg only after actual native P2 creation. Read-only native lifetime
// observer; no KEX calls, pointer/ref/target/SourceOwner/flag writes.
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('target');
const base=Process.mainModule.base;
const hex=a=>Array.from(new Uint8Array(a),b=>b.toString(16).padStart(2,'0')).join('');
const primary=base.add(0xaf33e0),secondary=ptr(cfg.secondary);
const sites=[{name:'block_dispatch',rva:0xc5310,prefix:'48895c2410574883ec20488379400048'}];
sites.push({name:'scene',rva:cfg.scene.rva,prefix:cfg.scene.prefix});
if(cfg.role==='host')sites.push({name:'level_unload',rva:0x1c90e0,prefix:'48895c241048896c2418488974242057'});
const hooks=[];let calls=0,unloads=0,stopped=false;
let lastSample=0,samples=0,errors=0;
function state(p,slot){
 const node=p.add(0xd8).readPointer();if(node.isNull())return null;
 const actor=node.add(0x28).readPointer();if(actor.isNull()||!actor.readPointer().equals(base.add(0x52d198))||
  !actor.add(0x10).readPointer().equals(node)||!actor.add(0x1b8).readPointer().equals(p))throw Error('current Puppet graph');
 const header=actor.sub(0x60);if(!header.add(0x18).readPointer().equals(base.add(0xaf4490)))throw Error('Puppet heap');
 let owned=0;const ammo=[];for(let i=0;i<8;++i){if(p.add(0xcc+i).readU8())owned|=1<<i;ammo.push(p.add(0xbc+i*2).readU16());}
 return {slot,player:p.toString(),actor:actor.toString(),node:node.toString(),allocation:header.add(8).readU64().toString(),
  health:actor.add(0x17a).readS16(),dead:(actor.add(0x1c0).readU32()&0x200)!==0,owned,ammo,
  weapon:p.add(0x88).readS32(),pending:p.add(0x8c).readS32(),keys:p.add(0x98).readU16(),
  position:[0x18,0x1c,0x20].map(o=>actor.add(o).readFloat()),position_bits:[0x18,0x1c,0x20].map(o=>actor.add(o).readU32()),sector:actor.add(0x3c).readS32()};
}
for(const spec of sites){
 const site=base.add(spec.rva);if(hex(site.readByteArray(16))!==spec.prefix)throw Error('Observer site must be original: '+spec.name);
 hooks.push(Interceptor.attach(site,{
 onEnter(args){
  this.owned=null;
  if(Process.getCurrentThreadId()!==cfg.tid)return;
  if(spec.name==='scene'){
   if(base.add(0xaf33c4).readS32()!==3||base.add(0xaf33c0).readS32()!==3||base.add(0xaf33c8).readS32()!==0||Date.now()-lastSample<200||samples>=9000)return;
   lastSample=Date.now();
   try{const players=[state(primary,cfg.role==='host'?0:1)];if(!secondary.isNull())players.push(state(secondary,1));
    send({kind:'p16_native_state',ordinal:++samples,players,tid:cfg.tid,utc:new Date().toISOString(),native_calls:0});}
   catch(e){++errors;send({kind:'p16_native_state_error',error:String(e),tid:cfg.tid});}return;
  }
  if(spec.name==='level_unload'){++unloads;send({kind:'p16_level_unload',tid:cfg.tid,utc:new Date().toISOString(),native_calls:0});return;}
  const b=args[0],a=args[1];if(b.isNull()||a.isNull()||!a.equals(b.add(0x60)))return;
  // Accepted DOC-58B.2/P3 dispatch contract: header remains allocated through
  // this call. Exact Puppet class header from accepted P7; no guessed ABI.
  if(b.readU32()!==0x3151983||!b.add(0x18).readPointer().equals(base.add(0xaf4490))||
     !b.add(0x40).readPointer().equals(base.add(0x1e1000))||b.add(0x50).readU64().toString()!=='480')return;
  const owner=a.add(0x1b8).readPointer();if(!owner.equals(primary)&&!owner.equals(secondary))return;
  const node=a.add(0x10).readPointer();
  this.owned={kind:'p16_arena_dispatch',slot:owner.equals(primary)?0:1,actor:a.toString(),node:node.toString(),allocation:b.add(8).readU64().toString(),tid:Process.getCurrentThreadId(),utc:new Date().toISOString(),native_calls:0};
  send(this.owned);++calls;
 },
 onLeave(){if(this.owned)send({...this.owned,kind:'p16_arena_dispatch_return',utc:new Date().toISOString(),actor_reads_after_return:false});}
 }));
}
function stop(reason){if(stopped)return;stopped=true;for(const h of hooks)h.detach();Interceptor.flush();
 for(const spec of sites){const actual=hex(base.add(spec.rva).readByteArray(16));send({kind:'p16_lifetime_observer_restored',site:spec.name,reason,calls,unloads,samples,errors,actual,expected:spec.prefix,exact16:actual===spec.prefix,native_calls:0});}}
const begin=Date.now(),timer=setInterval(()=>{if(base.add(0xaf33c4).readS32()!==3||Date.now()-begin>1800000){stop('native gameplay state left or deadline');clearInterval(timer);}},100);
send({kind:'p16_lifetime_observer_started',pid:Process.id,tid:cfg.tid,native_calls:0});
