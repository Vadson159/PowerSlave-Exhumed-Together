/* Exact-build diagnostic restart gate, based on saved native PlayerUpdate assembly.
   No object/refcount writes, no spectator, no skipped Player update. */
function createHostDeathGate(base,cfg,event) {
 const site=base.add(0x1dffd6),original=site.readByteArray(6),expected='0f82ae040000',patched='e9af04000090';
 const hex=a=>Array.from(new Uint8Array(a),b=>b.toString(16).padStart(2,'0')).join('');
 if(hex(original)!==expected)throw new Error('Host dead branch differs from exact-build saved assembly');
 const branch=Instruction.parse(site);
 if(branch.size!==6||!['jb','jc','jnae'].includes(branch.mnemonic)||!ptr(branch.operands[0].value).equals(base.add(0x1e048a)))
  throw new Error('Host restart gate does not target the verified camera continuation');
 let installed=false;const requests=[];
 function safeThread(){if(Process.getCurrentThreadId()!==cfg.simulation_tid)throw new Error('Host gate mutation outside simulation thread');}
 function restore(reason){safeThread();if(!installed)return;
  if(hex(site.readByteArray(6))!==patched)throw new Error('Host gate changed; refusing to overwrite another hook');
  Memory.patchCode(site,6,code=>code.writeByteArray(original));installed=false;
  event('C_host_restart_gate_restored',{reason,rva:'0x1dffd6',actual:hex(site.readByteArray(6)),passed:hex(site.readByteArray(6))===expected});
 }
 // Once this native process-exit boundary is entered, no game loop may run again.
 // Keep code/storage pinned until then; never detach an installed gate in gameplay.
 const exit=Process.getModuleByName('ntdll.dll').getExportByName('RtlExitUserProcess');
 const shutdown=cfg.native_prefixes?.native_shutdown;
 if(!shutdown||shutdown.rva!==0xfcc20||hex(base.add(shutdown.rva).readByteArray(16))!==shutdown.prefix)throw new Error('Missing exact native SDL shutdown prefix');
 Interceptor.attach(base.add(shutdown.rva),{onEnter(){if(Process.getCurrentThreadId()!==cfg.simulation_tid)return;
  try{restore('native_SDL_shutdown_entry');event('C_host_native_shutdown_boundary',{rva:'0xfcc20',gate_installed:installed});}
  catch(e){event('C_host_gate_cleanup_error',{error:String(e),reason:'native_shutdown'});}
 }});
 Interceptor.attach(exit,{onEnter(args){if(Process.getCurrentThreadId()!==cfg.simulation_tid)return;
  try{restore('native_RtlExitUserProcess_entry');event('C_host_process_exit_boundary',{status:args[0].toUInt32(),gate_installed:installed});}
  catch(e){event('C_host_gate_cleanup_error',{error:String(e),reason:'process_exit'});}
 }});
 for(const rva of [0x1b1960,0x1b5390])Interceptor.attach(base.add(rva),{onEnter(args){
  if(Process.getCurrentThreadId()!==cfg.simulation_tid||!installed)return;
  const caller=this.returnAddress.sub(base).toUInt32();
  if(caller>=0x1dfe30&&caller<=0x1e04c0){const q={rva:'0x'+rva.toString(16),caller:'0x'+caller.toString(16),this_ptr:args[0].toString()};requests.push(q);event('C_host_unexpected_global_request',q);}
 }});
 return {
  install(){safeThread();if(installed)throw new Error('Host gate already installed');
   if(hex(site.readByteArray(6))!==expected)throw new Error('Host branch changed before install');
   Memory.patchCode(site,6,code=>code.writeByteArray([0xe9,0xaf,0x04,0x00,0x00,0x90]));installed=true;
   event('C_host_restart_gate_installed',{rva:'0x1dffd6',original:expected,patched,continuation_rva:'0x1e048a',
    preserved:'Native input, commands, dead weapon update, death timer and final camera/viewZ calculation; only two global requests bypassed'});
  },
  stateChange(state){if(state!==3)restore('native_gameplay_state_left');},
  snapshot(){return {installed,original:expected,actual:hex(site.readByteArray(6)),unexpected_global_requests:requests.length};}
 };
}
