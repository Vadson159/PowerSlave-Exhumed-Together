// Diagnostic preview of the ONE existing proxy. The only control call publishes
// the P8 atomic mailbox; C++ still attests thread/world/identity before any bind.
'use strict';
if(Process.id!==cfg.pid || Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('P8 target');
const d=Process.getModuleByName(cfg.dll_name);
if(d.path.toLowerCase()!==cfg.dll.toLowerCase())throw Error('P8 DLL');
const preview=new NativeFunction(d.getExportByName('PSE_P8_Preview'),'uint32',['uint32','uint32'],'win64');
const log=new File(cfg.log,'w');
const steps=[];
for(let slot=0;slot<2;++slot) for(let pose=0;pose<4;++pose) steps.push({slot,pose});
let index=0;
function next() {
    const s=index<steps.length?steps[index++]:{slot:8,pose:0};
    const r=preview(s.slot,s.pose);
    const row={event:'p8_preview_mailbox',utc:new Date().toISOString(),...s,result:r,kex_calls_from_js:0,diagnostic_not_gameplay:true};
    log.write(JSON.stringify(row)+'\n');log.flush();send(row);
    if(r!==0)throw Error('P8 mailbox refused');
    if(s.slot!==8) setTimeout(next,s.pose===3?3500:2200);
}
next();
