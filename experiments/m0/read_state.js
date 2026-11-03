'use strict';
const m=Process.mainModule;if(Process.id!==cfg.pid||m.path.toLowerCase()!==cfg.target.toLowerCase())throw new Error('state target mismatch');
const gi=m.base.add(0xaf33a0),p=gi.add(0x40),th=cfg.observe_thread_stacks?Process.enumerateThreads().find(t=>t.id===cfg.simulation_tid):null;
send({kind:'M0_state_read',utc:new Date().toISOString(),state:gi.add(0x24).readS32(),pending:gi.add(0x28).readS32(),player_node:p.add(0xd8).readPointer().toString(),
 getter_prefix:Array.from(new Uint8Array(m.base.add(0x1b5520).readByteArray(5)),x=>x.toString(16).padStart(2,'0')).join(''),
 thread:th?{id:th.id,state:th.state,rip:th.context.rip.toString(),symbol:DebugSymbol.fromAddress(th.context.rip).toString(),
  trace:Thread.backtrace(th.context,Backtracer.ACCURATE).slice(0,12).map(x=>({address:x.toString(),symbol:DebugSymbol.fromAddress(x).toString()}))}:null});
