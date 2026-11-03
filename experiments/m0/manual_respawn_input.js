/* Read-only Win32 message observation. MSG layout checked against installed
   Windows SDK10.0.26100.0 um/WinUser.h and Win64 alignment, not a game structure. */
function createM0RespawnInput(cfg,event) {
 let armed=false,pending=null,serial=0;
 const peek=Process.getModuleByName('user32.dll').getExportByName('PeekMessageW');
 Interceptor.attach(peek,{onEnter(args){
  if(Process.getCurrentThreadId()!==cfg.simulation_tid||!(args[4].toUInt32()&1))return;
  this.message=args[0];
 },onLeave(result){
  if(!this.message||result.toInt32()===0)return;
  try{const msg=this.message;if(msg.add(8).readU32()!==0x100||msg.add(16).readU64().toString()!=='69'||(msg.add(24).readU32()&0x40000000))return;
   const key={serial:++serial,hwnd:msg.readPointer().toString(),message:'WM_KEYDOWN',vk:69,utc:new Date().toISOString(),armed};
   if(armed&&!pending){pending=key;event('C_manual_respawn_E_received',key);}
  }catch(e){event('C_manual_respawn_input_error',{error:String(e)});}
 }});
 return {arm(){if(Process.getCurrentThreadId()!==cfg.simulation_tid)throw Error('Input arming outside simulation thread');armed=true;pending=null;},
  take(){if(Process.getCurrentThreadId()!==cfg.simulation_tid)throw Error('Input read outside simulation thread');const q=pending;pending=null;if(q)armed=false;return q;}};
}
