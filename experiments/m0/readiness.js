/* Passive C4 scheduling evidence. No gameplay calls, no engine writes, no constructor probes. */
'use strict';
const mod=Process.mainModule,gi=mod.base.add(0xaf33a0),p1=gi.add(0x40);
if(Process.id!==cfg.pid||mod.path.toLowerCase()!==cfg.target.toLowerCase())throw new Error('Readiness target mismatch');
function sendEvent(kind,data){send(Object.assign({kind,utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},data));}
function location(p){const m=Process.findModuleByAddress(p);return {address:p.toString(),module:m?.name??null,rva:m?p.sub(m.base).toString():null};}
function safe(f){try{return f();}catch(e){return {error:String(e)};}}
const sites={};for(const name of ['state_tick','player_update','get_player','factory','puppet_spawn','puppet_tick']){
 const s=cfg.sites[name],at=mod.base.add(s.rva),hex=Array.from(new Uint8Array(at.readByteArray(16)),v=>v.toString(16).padStart(2,'0')).join('');
 if(hex!==s.prefix)throw new Error('Readiness prefix mismatch '+name);sites[name]={rva:'0x'+s.rva.toString(16),prefix:hex};
}
sendEvent('observer_identity',{pid:Process.id,path:mod.path,base:mod.base.toString(),frida:Frida.version,binary_sha256_host_verified:cfg.binary_sha256,sites,constructor_analysis_repeated:false});
let ticks=0,leaves=0,sleeps=0,sleepMs=0,lastSleep=null,lastThis=null,lastTid=null,lastTickWall=null,previousTicks=0,samples=0,stalled=0;
Interceptor.attach(mod.base.add(0x1b6ec0),{onEnter(a){ticks++;lastThis=a[0].toString();lastTid=Process.getCurrentThreadId();lastTickWall=Date.now();},onLeave(){leaves++;}});
const sleep=Module.findGlobalExportByName('SleepEx');if(sleep&&cfg.observe_sleep)Interceptor.attach(sleep,{onEnter(a){if(Process.getCurrentThreadId()!==cfg.simulation_tid)return;
 sleeps++;const ms=a[0].toUInt32();if(ms<0xffffffff)sleepMs+=ms;lastSleep={milliseconds:ms,alertable:a[1].toUInt32(),caller:location(this.returnAddress)};
}});
const fgAt=Module.findGlobalExportByName('GetForegroundWindow'),guiAt=Module.findGlobalExportByName('GetGUIThreadInfo');
const foreground=fgAt?new NativeFunction(fgAt,'pointer',[],'win64'):null;
const gui=guiAt?new NativeFunction(guiAt,'bool',['uint32','pointer'],'win64'):null;
const guiBuffer=Memory.alloc(72);
function focusState(){if(!gui)return null;guiBuffer.writeByteArray(new Uint8Array(72));guiBuffer.writeU32(72);const ok=gui(cfg.simulation_tid,guiBuffer);
 return {query_ok:ok,foreground:foreground?.().toString()??null,flags:ok?guiBuffer.add(4).readU32():null,
 active:ok?guiBuffer.add(8).readPointer().toString():null,focus:ok?guiBuffer.add(16).readPointer().toString():null,
 capture:ok?guiBuffer.add(24).readPointer().toString():null,menu_owner:ok?guiBuffer.add(32).readPointer().toString():null};}
function graph(){const n=p1.add(0xd8).readPointer(),a=n.isNull()?ptr(0):n.add(0x28).readPointer();return {player:p1.toString(),node:n.toString(),actor:a.toString(),
 owner:a.isNull()?null:a.add(0x1b8).readPointer().toString(),health:a.isNull()?null:a.add(0x17a).readS16(),
 origin:a.isNull()?null:[0x18,0x1c,0x20].map(o=>a.add(o).readFloat()),sector:a.isNull()?null:a.add(0x3c).readS32()};}
function mainThread(){const t=Process.enumerateThreads().find(t=>t.id===cfg.simulation_tid);return t?{id:t.id,state:t.state,pc:location(t.context.rip),
 trace:Thread.backtrace(t.context,Backtracer.ACCURATE).slice(0,10).map(location)}:null;}
setInterval(()=>{
 const delta=ticks-previousTicks;previousTicks=ticks;samples++;stalled=delta?0:stalled+1;
 if(samples>12&&samples%10!==0&&stalled!==4)return;
 const state=gi.add(0x24).readS32();sendEvent('tick_sample',{sample:samples,tick_count:ticks,tick_delta:delta,leave_count:leaves,this_ptr:lastThis,
  simulation_tid:lastTid,tid:cfg.simulation_tid,state,pending:gi.add(0x28).readS32(),ms_since_tick:lastTickWall===null?null:Date.now()-lastTickWall,
  player:safe(graph),focus:safe(focusState),sleep_hook_enabled:!!cfg.observe_sleep,sleep_calls:sleeps,requested_sleep_ms:sleepMs,last_sleep:lastSleep,
  // Thread enumeration/unwind can need loader locks while the primary is in early file APIs.
  // Never do it from an automatic startup timer; actual tick TID is measured by the hook.
  stalled_samples:stalled,main_thread:cfg.observe_thread_stacks&&ticks>0&&state===3&&stalled>=4?safe(mainThread):null,
  note:'Independent Frida timer; tick_delta covers previous500ms sample, including unprinted samples. No gameplay functions called.'});
},500);
