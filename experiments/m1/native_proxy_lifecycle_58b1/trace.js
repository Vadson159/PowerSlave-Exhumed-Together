// No NativeFunction, no Actor creation/removal, no pointer writes. Frida observers only.
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('target mismatch');
const base=Process.mainModule.base,gi=base.add(0xaf33a0),level=base.add(0xa45940),f=new File(cfg.log,'w');
const listeners=[];let serial=0,ticks=0,localTicks=0,unloads=0,depth=0,last='',inTick=false;
function bytes(p,n){return Array.from(new Uint8Array(p.readByteArray(n))).map(v=>v.toString(16).padStart(2,'0')).join('');}
for(const [name,s] of Object.entries(cfg.sites))if(bytes(base.add(s.rva),16)!==s.prefix)throw Error('site prefix '+name);
function world(){const n=gi.add(0x1e8).readU64().toNumber(),p=gi.add(0x1e0).readPointer();return {state:gi.add(0x24).readS32(),next:gi.add(0x20).readS32(),pending:gi.add(0x28).readS32(),map:n>0&&n<=32&&!p.isNull()?p.readUtf8String(n):'',sector_array:level.add(0x28).readPointer().toString(),sector_count:level.add(0x30).readU64().toString(),canonical_140_bytes:bytes(level.add(0x140),16),level_vtable:level.readPointer().sub(base).toString()};}
function emit(event,data={}){const r=Object.assign({seq:++serial,event,utc:new Date().toISOString(),tid:Process.getCurrentThreadId(),expected_simulation_tid:cfg.simulation_tid,ticks,localTicks,depth},world(),data);f.write(JSON.stringify(r)+'\n');f.flush();if(!event.startsWith('tick_'))send(r);}
function hook(name,callbacks){listeners.push(Interceptor.attach(base.add(cfg.sites[name].rva),callbacks));}
hook('state_tick',{onEnter(){inTick=true;ticks++;const w=JSON.stringify(world());this.log=ticks<=3||ticks%600===0||w!==last;if(this.log){emit('tick_entry');last=w;}},onLeave(){if(this.log)emit('tick_return');inTick=false;}});
hook('player_update',{onEnter(args){if(args[0].equals(gi.add(0x40))&&inTick){localTicks++;if(localTicks<=3)emit('local_simulation_tick');}}});
hook('state_exit',{onEnter(args){depth++;emit('state_exit_entry',{self:args[0].toString()});},onLeave(){emit('state_exit_return');depth--;}});
let unloadingSectors=null;
hook('unload',{onEnter(args){unloads++;depth++;unloadingSectors=level.add(0x28).readPointer();emit('level_unload_entry',{self:args[0].toString(),is_level:args[0].equals(level),return_rva:this.returnAddress.sub(base).toString()});},onLeave(){emit('level_unload_return');unloadingSectors=null;depth--;}});
// Saved assembly: 1c92dc CALL [482ae8], RCX=[level+28]; import is free.
listeners.push(Interceptor.attach(base.add(0x482ae8).readPointer(),{onEnter(args){this.ours=unloadingSectors&&!unloadingSectors.isNull()&&args[0].equals(unloadingSectors);if(this.ours)emit('sector_free_entry',{free_argument:args[0].toString(),return_rva:this.returnAddress.sub(base).toString()});},onLeave(){if(this.ours)emit('sector_free_return',{sector_memory_dereferenced:false});}}));
hook('shutdown',{onEnter(){emit('normal_shutdown_entry');},onLeave(){emit('normal_shutdown_return');}});
hook('arena_cleanup',{onEnter(args){if(depth>0)emit('arena_cleanup_entry',{arena:args[0].toString()});},onLeave(){if(depth>0)emit('arena_cleanup_return');}});
hook('factory',{onEnter(){if(depth>0)emit('engine_factory_during_teardown');}});
hook('remove',{onEnter(){if(depth>0)emit('engine_remove_during_teardown');}});
hook('drain',{onEnter(){if(depth>0)emit('engine_drain_during_teardown');}});
rpc.exports={status(){emit('trace_status',{unloads,writes:0,kex_calls:0,mapready:'not instantiated: no networking DLL'});},detach(){for(const l of listeners)l.detach();Interceptor.flush();const check=Object.fromEntries(Object.entries(cfg.sites).map(([n,s])=>[n,bytes(base.add(s.rva),16)===s.prefix]));emit('hooks_restored',{check});return check;}};
emit('trace_loaded',{base:base.toString(),sites:cfg.sites,writes:0,kex_calls:0,no_proxy:true});
