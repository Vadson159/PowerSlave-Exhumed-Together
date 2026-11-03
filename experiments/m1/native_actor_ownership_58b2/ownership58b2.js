// DOC58B.2 passive Actor1007/ObjectNode ownership observer (client process). Prepended: const cfg={...}.
// Read-only: no NativeFunction, no writes to game memory, no KEX calls. Hooks only sites that neither the
// DOC58B client DLL (Tick/Update/StateExit/DirectQuit) nor adapter58b (render/tick/OnRemove/PoolReturn/
// Sweep/Drain, live-prefix-checked factory/Remove/LinkSector) own: Level::Unload 1c90e0, tagged-heap purge
// c58b0, per-block destructor dispatch c5310, kexSlaveActor destructor 19f210 and the CRT free IAT target.
// Lifetime rule: the Actor is read only while its tagged-heap block is still allocated (before the observed
// free of that block); the ObjectNode only before the af4600 purge frees its node block. Afterwards the
// recorded numbers are diagnostic identifiers only, never dereferenced.
'use strict';
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('ownership observer target');
const base=Process.mainModule.base,gi=base.add(0xaf33a0),level=base.add(0xa45940);
const ACTOR_HEAP=base.add(0xaf4490),NODE_HEAP=base.add(0xaf4600),DTOR_THUNK=base.add(0xd9980),SLAVE_ACTOR_VT=base.add(0x528608);
const REMOVAL_QUEUE_HEAD=base.add(0xaf3388),HDR=0x60,MAGIC=0x3151983,NODE_BLOCK=0x1210,ACTOR_SIZE=0x1b8;
const actor=ptr(cfg.actor),node=ptr(cfg.node),actorBlock=actor.sub(HDR);
const hex=b=>Array.from(new Uint8Array(b)).map(x=>x.toString(16).padStart(2,'0')).join('');
for(const [n,s] of Object.entries(cfg.sites)){const live=hex(base.add(s.rva).readByteArray(16));if(live!==s.prefix)throw Error('live prefix mismatch (foreign hook?) '+n+' '+live);}
const freeFn=base.add(cfg.free_iat_rva).readPointer();
const ucrtFree=Process.getModuleByName('ucrtbase.dll').getExportByName('free');
if(!freeFn.equals(ucrtFree))throw Error('free IAT target is not ucrtbase!free: '+freeFn);
const freePrefix=hex(freeFn.readByteArray(16));
const out=new File(cfg.log,'w');let seq=0;
function st(){return {state:gi.add(0x24).readS32(),next:gi.add(0x20).readS32(),pending:gi.add(0x28).readS32()};}
function w(event,d={},important=false){const r=Object.assign({event,seq:++seq,utc:new Date().toISOString(),tid:Process.getCurrentThreadId(),on_simulation_thread:Process.getCurrentThreadId()===cfg.simulation_tid},st(),d);
 out.write(JSON.stringify(r)+'\n');out.flush();if(important)send(Object.assign({kind:'n58b2_ownership'},r));}
// --- identity (reads only while both allocations are live) ---
let actorAlive=true,nodeAlive=true,loadSeq=null;
function header(p){return {magic:p.readU32()===MAGIC,tag:p.add(4).readU32(),alloc_seq:p.add(8).readU64().toString(),size:p.add(0x10).readU64().toString(),
 heap:p.add(0x18).readPointer().toString(),dtor:p.add(0x40).readPointer().toString(),ctor_count:p.add(0x48).readU64().toString(),ctor_size:p.add(0x50).readU64().toString(),locked:p.add(0x38).readU8()};}
function identity(){
 if(!actorAlive||!nodeAlive)throw Error('identity read after observed free');
 const h=header(actorBlock),vt=actor.readPointer(),own=actor.add(0x10).readPointer();
 const f={actor:actor.toString(),node:node.toString(),vtable:vt.toString(),vtable_is_kexSlaveActor:vt.equals(SLAVE_ACTOR_VT),own_node:own.toString(),own_node_matches:own.equals(node),
  payload:node.add(0x28).readPointer().toString(),reciprocal:node.add(0x28).readPointer().equals(actor),in_use:node.add(0x24).readU8(),refs:node.add(0x40).readS32(),
  sector:actor.add(0x3c).readS32(),removed:actor.add(0x40).readU8(),flags:actor.add(0x16c).readU32(),definition:actor.add(0x170).readPointer().toString(),block:h,
  block_ok:h.magic&&h.heap===ACTOR_HEAP.toString()&&h.dtor===DTOR_THUNK.toString()&&h.ctor_count==='1'&&h.ctor_size===String(ACTOR_SIZE)&&h.size===String(ACTOR_SIZE)};
 f.identity_ok=f.vtable_is_kexSlaveActor&&f.own_node_matches&&f.reciprocal&&f.in_use===1&&f.block_ok&&(loadSeq===null||h.alloc_seq===loadSeq);return f;}
const atLoad=identity();loadSeq=atLoad.block.alloc_seq;
if(!atLoad.identity_ok)throw Error('Actor1007/ObjectNode identity not attested at load: '+JSON.stringify(atLoad));
// --- teardown phase tracking ---
let inUnload=false,sectorArray=null,sectorCount=null,purge=null,sectorFreed=false,actorFreed=false,nodeBlockFreed=false;
const stats={unload_entries:0,purges:[],dispatch_ours:0,dtor_ours_entries:0,dtor_ours_returns:0,actor_block_frees:0,node_block_frees:0,sector_frees:0,reads_after_free:0,dispatch_after_free:0,dtor_after_free:0};
let cur=null;
function hit(p,lo,len){return p.compare(lo)>=0&&p.compare(lo.add(len))<0;}
Interceptor.attach(base.add(cfg.sites.unload.rva),{onEnter(args){
 if(Process.getCurrentThreadId()!==cfg.simulation_tid)return;stats.unload_entries++;this.ours=true;inUnload=true;
 sectorArray=level.add(0x28).readPointer();sectorCount=level.add(0x30).readU64().toString();
 const q=REMOVAL_QUEUE_HEAD.readPointer(),pendingCell=q.isNull()?null:q.readPointer();
 let id=null;try{id=identity();}catch(e){id={error:String(e)};}
 w('level_unload_entry',{this:args[0].toString(),is_level_global:args[0].equals(level),return_rva:this.returnAddress.sub(base).toString(),sector_array:sectorArray.toString(),sector_count:sectorCount,
   removal_queue_head:q.toString(),removal_queue_next_cell:pendingCell===null?null:pendingCell.toString(),removal_queue_empty:pendingCell!==null&&pendingCell.isNull(),identity_pre_unload:id},true);
},onLeave(){if(!this.ours)return;inUnload=false;w('level_unload_return',{sector_array:level.add(0x28).readPointer().toString(),sector_count:level.add(0x30).readU64().toString(),sector_freed_observed:sectorFreed},true);}});
Interceptor.attach(base.add(cfg.sites.arena_purge.rva),{onEnter(args){
 if(Process.getCurrentThreadId()!==cfg.simulation_tid)return;const h=args[0];this.ours=true;
 const kind=h.equals(ACTOR_HEAP)?'af4490_actor_heap':h.equals(NODE_HEAP)?'af4600_node_heap':'other';
 cur={kind,heap:h.toString(),dispatches:0,frees:0,vtables:{},caller_rva:this.returnAddress.sub(base).toString()};purge=kind;
 w('arena_purge_entry',{heap:h.toString(),kind,caller_rva:cur.caller_rva,sector_array_now:level.add(0x28).readPointer().toString()},kind!=='other');
},onLeave(){if(!this.ours)return;const done=cur;stats.purges.push(done);purge=null;cur=null;w('arena_purge_return',done,done.kind!=='other');
 if(done.kind==='af4600_node_heap')w('ownership_summary',{stats,actor_freed:actorFreed,node_block_freed:nodeBlockFreed,sector_freed:sectorFreed},true);}});
Interceptor.attach(base.add(cfg.sites.block_dispatch.rva),{onEnter(args){
 if(Process.getCurrentThreadId()!==cfg.simulation_tid||!cur)return;const block=args[0],user=args[1];cur.dispatches++;
 // Count destroyed object classes in this purge (vtable read while the block is still allocated).
 try{const vt=user.readPointer().sub(base).toString();cur.vtables[vt]=(cur.vtables[vt]||0)+1;}catch(e){cur.vtables.unreadable=(cur.vtables.unreadable||0)+1;}
 if(!user.equals(actor))return;
 if(actorFreed){stats.dispatch_after_free++;w('VIOLATION_dispatch_after_actor_free',{},true);return;}
 stats.dispatch_ours++;let id;try{id=identity();}catch(e){id={error:String(e)};}
 w('actor_dtor_dispatch_entry',{purge:cur.kind,block:block.toString(),block_is_actor_header:block.equals(actorBlock),dispatcher_rva:'0xc5310',caller_rva:this.returnAddress.sub(base).toString(),
   sector_freed_before:sectorFreed,identity_at_dispatch:id,node_still_live:nodeAlive},true);
}});
Interceptor.attach(base.add(cfg.sites.actor_dtor.rva),{onEnter(args){
 if(Process.getCurrentThreadId()!==cfg.simulation_tid||!args[0].equals(actor))return;this.ours=true;
 if(actorFreed){stats.dtor_after_free++;w('VIOLATION_dtor_after_actor_free',{},true);this.ours=false;return;}
 stats.dtor_ours_entries++;w('actor_destructor_entry',{rva:'0x19f210',return_rva:this.returnAddress.sub(base).toString(),vtable:actor.readPointer().toString(),own_node:actor.add(0x10).readPointer().toString(),
   node_refs:nodeAlive?node.add(0x40).readS32():null,in_purge:purge},true);
},onLeave(){if(!this.ours)return;stats.dtor_ours_returns++;
 // Block not yet freed (free follows in c58b0): vtable/own-node field are still allocated memory.
 w('actor_destructor_return',{vtable_after:actor.readPointer().toString(),own_node_field_after:actor.add(0x10).readPointer().toString(),node_payload_after:nodeAlive?node.add(0x28).readPointer().toString():null,
   node_refs_after:nodeAlive?node.add(0x40).readS32():null,node_in_use_after:nodeAlive?node.add(0x24).readU8():null},true);}});
Interceptor.attach(freeFn,{onEnter(args){
 if(!inUnload&&!cur)return;if(Process.getCurrentThreadId()!==cfg.simulation_tid)return;const p=args[0];if(p.isNull())return;
 if(cur)cur.frees++;
 if(inUnload&&sectorArray&&p.equals(sectorArray)){stats.sector_frees++;sectorFreed=true;w('sector_array_free_entry',{free_argument:p.toString(),return_rva:this.returnAddress.sub(base).toString(),sector_memory_dereferenced:false},true);return;}
 if(!cur)return;
 if(p.equals(actorBlock)){
  if(actorFreed){w('VIOLATION_double_free_actor_block',{},true);return;}
  const h=header(p);actorFreed=true;actorAlive=false;stats.actor_block_frees++;
  w('actor_block_free_entry',{purge:cur.kind,block:p.toString(),return_rva:this.returnAddress.sub(base).toString(),header_before_free:h,alloc_seq_matches_spawn:h.alloc_seq===loadSeq,sector_freed_before:sectorFreed,actor_reads_after:false},true);return;}
 if(cur.kind==='af4600_node_heap'&&nodeAlive&&hit(node,p.add(HDR),NODE_BLOCK)){
  const h=header(p);const ok=h.magic&&h.heap===NODE_HEAP.toString()&&h.size===String(NODE_BLOCK);
  const payload=node.add(0x28).readPointer(),inUse=node.add(0x24).readU8(),refs=node.add(0x40).readS32();
  nodeAlive=false;nodeBlockFreed=true;stats.node_block_frees++;
  w('node_block_free_entry',{block:p.toString(),node:node.toString(),node_index:node.sub(p.add(HDR+8)).toInt32()/0x48,header_before_free:h,header_ok:ok,
   node_payload_before_free:payload.toString(),node_payload_is_old_actor:payload.equals(actor),node_in_use_before_free:inUse,node_refs_before_free:refs,actor_block_freed_before:actorFreed,node_reads_after:false},true);}
}});
w('ownership_observer_loaded',{actor_handle:cfg.handle,identity_at_load:atLoad,free_target:freeFn.toString(),free_prefix16:freePrefix,
 sites:Object.fromEntries(Object.entries(cfg.sites).map(([k,v])=>[k,'0x'+v.rva.toString(16)])),writes:0,native_calls:0},true);
