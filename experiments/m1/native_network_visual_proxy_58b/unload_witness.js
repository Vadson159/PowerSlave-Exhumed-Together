// DOC58B G1 read-only witness: does the level vtable slot-2 Unload (RVA 1c90e0, DOC56 witness) run, when, on which
// thread? No reads beyond GameInstance state ints, no calls, no writes. Stays loaded through process exit.
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('unload witness target');
const base=Process.mainModule.base,gi=base.add(0xaf33a0),site=base.add(0x1c90e0),levelVtable=base.add(0xa45940).readPointer();
const prefix=Array.from(new Uint8Array(site.readByteArray(16))).map(x=>x.toString(16).padStart(2,'0')).join('');
if(prefix.startsWith('e9'))throw Error('1c90e0 already patched by another hook');
const out=new File(cfg.log,'w');let n=0;
function w(event,d={}){const r=Object.assign({event,seq:++n,utc:new Date().toISOString(),tid:Process.getCurrentThreadId(),simulation_tid:cfg.simulation_tid,on_simulation_thread:Process.getCurrentThreadId()===cfg.simulation_tid,state:gi.add(0x24).readS32(),next:gi.add(0x20).readS32(),pending:gi.add(0x28).readS32()},d);out.write(JSON.stringify(r)+'\n');out.flush();send(Object.assign({kind:'n58b_unload_witness'},r));}
Interceptor.attach(site,{onEnter(args){this.self=args[0].toString();w('level_unload_entry',{this:this.self,is_level_global:args[0].equals(base.add(0xa45940)),return_address:this.returnAddress.sub(base).toString()});},onLeave(){w('level_unload_return',{this:this.self});}});
w('unload_witness_loaded',{site_rva:'0x1c90e0',prefix16:prefix,level_vtable:levelVtable.toString(),slot2_matches:levelVtable.isNull()?null:levelVtable.add(0x10).readPointer().equals(site),writes:0,calls:0});
