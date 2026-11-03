/* Offline policy regression: no Frida, process attachment or file operations. */
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'isolation_guard.js'),'utf8');
const policy=source.slice(0,source.indexOf('function installM0WriteGuard'));
const context={};vm.createContext(context);vm.runInContext(policy,context);
const workspace='S:\\lab\\mod';
const cases=[
 ['S:\\lab\\mod\\profile\\x',true],['\\\\?\\S:\\lab\\mod\\profile\\x',true],
 ['\\??\\S:\\lab\\mod\\profile\\x',true],['S:/LAB/mod/profile/x',true],
 ['S:\\lab\\mod_evil\\x',false],['S:\\lab\\mod\\..\\outside\\x',false],
 ['S:\\lab\\mod\\profile\\..\\x',true],['S:\\lab\\mod.\\x',false],
 ['C:\\Program Files (x86)\\Steam\\logs\\gameoverlay_renderer.txt',false],
 ['\\??\\UNC\\server\\share\\x',false],['\\Device\\HarddiskVolume3\\x',false],
 ['\\Device\\Mup\\server\\share\\x',false],['\\Device\\Volume{fake}\\x',false],
 ['\\Device\\NamedPipe\\frida-smoke',true],['\\Device\\000000ab',true],
 ['\\??\\NUL',true],['\\??\\NULfile',false],['relative\\x',false],
 ['\\??\\HID#VID_125F&PID_9418#test',true],['\\??\\HID#VID_125F&PID_9418#test\\kbd',true],
 ['\\Device\\MMCSS\\MmThread',true],['\\Device\\MMCSS\\diskfile',false],
];
const rows=cases.map(([value,expected])=>{const actual=context.m0DiskWritePolicy(value,workspace);return {value,expected,...actual,passed:expected===actual.allow};});
console.log(JSON.stringify({status:rows.every(r=>r.passed)?'PASS':'FAIL',tests:rows},null,2));
process.exitCode=rows.some(r=>!r.passed)?1:0;
