/* M1.1b: bounded passive OS-boundary observer. No KEX calls, object writes,
   symbol/PDB lookup, destructor calls, DLL unload, or KEX/DLL code hooks.
   The existing native DLL owns all simulation and lifecycle operations. */
'use strict';
if (Process.id !== cfg.pid || Process.arch !== 'x64' ||
    Process.mainModule.path.toLowerCase() !== cfg.target.toLowerCase()) throw Error('M11b process/build binding mismatch');
const main = Process.mainModule, dll = Process.getModuleByName('pse_m11_bootstrap.dll');
if (dll.path.toLowerCase() !== cfg.dll.toLowerCase()) throw Error('M11b DLL path mismatch');
const p2 = ptr(cfg.player), keep = [], pools = [], detours = [];
if (p2.compare(dll.base) < 0 || p2.add(0x198 + 16).compare(dll.base.add(dll.size)) > 0) throw Error('Player storage is not inside pinned DLL');
const query = new NativeFunction(Module.getGlobalExportByName('VirtualQuery'), 'uint64', ['pointer','pointer','uint64'], 'win64');
const mbi = Memory.alloc(48), counters = {target_free_library:0,target_ldr_unload:0,target_virtual_free:0,os_exits:0};
function hex(address, size) { return Array.from(new Uint8Array(address.readByteArray(size)), b=>b.toString(16).padStart(2,'0')).join(''); }
function region(address) {
 if (Number(query(address, mbi, 48).toString()) !== 48) return {address:address.toString(),query_ok:false};
 return {address:address.toString(),query_ok:true,base:mbi.readPointer().toString(),allocation_base:mbi.add(8).readPointer().toString(),
  region_size:Number(mbi.add(24).readU64().toString()),state:mbi.add(32).readU32(),protect:mbi.add(36).readU32(),type:mbi.add(40).readU32()};
}
function readable(r) { return r.query_ok && r.state===0x1000 && !(r.protect&0x101) && !!(r.protect&0xee); }
function executable(r) { return readable(r) && !!(r.protect&0xf0); }
function emit(kind, data) { send(Object.assign({kind,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()}, data)); }

// MinHook v1.3.4's shipped source defines entry E9 -> FF25 absolute relay.
// Only read actual installed relays, check their targets against OUR DLL range,
// and retain their complete bounded allocation pages (including original trampolines).
for (const s of cfg.sites) {
 const entry=main.base.add(s.rva);
 if (s.name==='Getter') { if(hex(entry,16)!==s.prefix)throw Error('Getter scope has not been restored'); continue; }
 if (entry.readU8()!==0xe9)throw Error('Expected existing DLL MinHook entry: '+s.name);
 const relay=entry.add(5).add(entry.add(1).readS32());
 if(hex(relay,6)!=='ff2500000000')throw Error('Unexpected MinHook relay: '+s.name);
 const target=relay.add(6).readPointer(), r=region(target), pr=region(relay);
 if(target.compare(dll.base)<0||target.compare(dll.base.add(dll.size))>=0||!executable(r)||r.allocation_base!==dll.base.toString())throw Error('Unmapped/foreign DLL callback: '+s.name);
 if(!executable(pr)||pr.type!==0x20000||pr.region_size>0x10000)throw Error('Unexpected trampoline allocation: '+s.name);
 detours.push({name:s.name,address:target.toString(),bytes:hex(target,16),memory:r,relay:relay.toString()});
 if(!pools.some(p=>p.base===pr.base)) {
  if(pools.length>=8)throw Error('Trampoline pool diagnostic bound');
  pools.push({base:pr.base,size:pr.region_size,allocation_base:pr.allocation_base,bytes:hex(ptr(pr.base),pr.region_size),memory:pr});
 }
}
if(detours.length!==10)throw Error('Expected ten active native DLL callbacks');
function snapshot() {
 const data=region(p2), validData=readable(data)&&data.allocation_base===dll.base.toString()&&data.type===0x1000000;
 const callbacks=detours.map(d=>{const r=region(ptr(d.address));return {name:d.name,address:d.address,memory:r,
  mapped_as_same_image:executable(r)&&r.allocation_base===dll.base.toString()&&r.type===0x1000000,
  code_unchanged:executable(r)&&hex(ptr(d.address),16)===d.bytes};});
 const buffers=pools.map(p=>{const r=region(ptr(p.base));return {base:p.base,size:p.size,memory:r,
  mapped_as_same_allocation:executable(r)&&r.allocation_base===p.allocation_base&&r.region_size>=p.size,
  bytes_unchanged:executable(r)&&r.region_size>=p.size&&hex(ptr(p.base),p.size)===p.bytes};});
 const controls=cfg.controls.map(s=>({name:s.name,rva:'0x'+s.rva.toString(16),actual:hex(main.base.add(s.rva),16),expected:s.prefix}));
 return {player:p2.toString(),player_memory:data,player_storage_readable:validData,
  external_handle:validData?p2.add(0xd8).readPointer().toString():null,
  guards:validData&&hex(p2.sub(16),16)==='a5'.repeat(16)&&hex(p2.add(0x198),16)==='a5'.repeat(16),
  callbacks,callback_code_mapped:callbacks.every(x=>x.mapped_as_same_image&&x.code_unchanged),
  trampoline_buffers:buffers,trampoline_code_mapped:buffers.every(x=>x.mapped_as_same_allocation&&x.bytes_unchanged),
  controls,controls_restored:controls.every(s=>s.actual===s.expected),counters:{...counters},
  actor_or_node_reads:false,game_calls:0,dll_unload_or_free_calls:0,observer_retained_until_process_exit:true};
}
const ready=snapshot();
if(!ready.player_storage_readable||ready.external_handle==='0x0'||!ready.guards||!ready.callback_code_mapped||!ready.trampoline_code_mapped)throw Error('Pre-Quit pinned context gate failed');
const apiSites=[];
function watch(moduleName, name, cb) {
 const address=Process.getModuleByName(moduleName).getExportByName(name);
 keep.push(Interceptor.attach(address, cb));apiSites.push({api:moduleName+'!'+name,address:address.toString()});
}
watch('kernel32.dll','FreeLibrary',{onEnter(args){if(args[0].equals(dll.base)){counters.target_free_library++;emit('m11b_target_free_library',{module:args[0].toString()});}}});
watch('ntdll.dll','LdrUnloadDll',{onEnter(args){if(args[0].equals(dll.base)){counters.target_ldr_unload++;emit('m11b_target_ldr_unload',{module:args[0].toString()});}}});
watch('kernel32.dll','VirtualFree',{onEnter(args){
 const address=Number(args[0].toString()),size=Number(args[1].toString()),end=address+Math.max(size,1);
 if(pools.some(p=>args[0].equals(ptr(p.allocation_base))||(address<Number(ptr(p.base).add(p.size).toString())&&end>Number(p.base)))) {
  counters.target_virtual_free++;emit('m11b_target_virtual_free',{address:args[0].toString(),size:args[1].toString(),free_type:args[2].toUInt32()});
 }
}});
for(const [moduleName,name] of [['kernel32.dll','ExitProcess'],['ntdll.dll','RtlExitUserProcess']]) {
 watch(moduleName,name,{onEnter(args){
  const entered=new Date().toISOString();counters.os_exits++;
  emit('m11b_os_exit',{api:moduleName+'!'+name,entered_utc:entered,exit_code:args[0].toUInt32(),snapshot:snapshot()});
 }});
}
emit('m11b_exit_observer_ready',{pid:Process.id,dll:dll.path,dll_base:dll.base.toString(),dll_sha256:cfg.dll_sha256,
 expected_simulation_tid:cfg.simulation_tid,snapshot:ready,api_sites:apiSites,
 trampoline_pool_before:pools,code_hooks:'OS APIs only',symbol_lookup:false,diagnostic_bounds:{native_callbacks:10,pool_pages:8,max_pool_region_bytes:65536},
 no_kex_function_calls:true,no_kex_or_dll_code_hooks:true});
