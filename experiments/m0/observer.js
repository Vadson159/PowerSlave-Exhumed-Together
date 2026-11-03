/* M0-A: read-only engine observation. Interceptor instrumentation, no native game calls or data writes.
   cfg is generated from verified copy bytes, not guessed signatures. */
'use strict';
const main = Process.mainModule;
let sequence = 0, tickCount = 0, lastSample = 0, factoryCount = 0;
function emit(kind, data) { send(Object.assign({kind, sequence:++sequence, utc:new Date().toISOString(),tid:Process.getCurrentThreadId()},data)); }
function attempt(fn) { try { return fn(); } catch (e) { return {read_error:String(e)}; } }
function nonnull(p) { return p && !p.isNull(); }
function player(p) {
  if (!nonnull(p)) return null;
  return attempt(()=>{ const node=p.add(0xd8).readPointer();return {address:p.toString(),buttons:p.readU32(),node:node.toString(),
    actor:nonnull(node)?node.add(0x28).readPointer().toString():null}; });
}
function puppet(p) {
  if (!nonnull(p)) return null;
  return attempt(()=>{const node=p.add(0x10).readPointer();return {address:p.toString(),vtable:p.readPointer().toString(),node:node.toString(),
    node_payload:nonnull(node)?node.add(0x28).readPointer().toString():null,node_refs:nonnull(node)?node.add(0x40).readS32():null,
    owner:p.add(0x1b8).readPointer().toString(),origin:[0x18,0x1c,0x20].map(o=>p.add(o).readFloat()),
    rotation:[0x30,0x34,0x38].map(o=>p.add(o).readFloat()),velocity:[0xe0,0xe4,0xe8].map(o=>p.add(o).readFloat()),
    sector:p.add(0x3c).readS32(),health:p.add(0x17a).readS16()};});
}
function singleton() {return main.base.add(0xaf33a0).add(0x40);}
if (Process.id!==cfg.pid || main.path.toLowerCase()!==cfg.target.toLowerCase() || Process.arch!=='x64')
  throw new Error('Target identity mismatch; refusing instrumentation');
const verified={};
for (const [name,s] of Object.entries(cfg.sites)) {
  const at=main.base.add(s.rva);const bytes=new Uint8Array(at.readByteArray(16));
  const hex=Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
  if(hex!==s.prefix)throw new Error('Original prefix mismatch '+name+': '+hex);
  verified[name]={address:at.toString(),rva:'0x'+s.rva.toString(16),prefix:hex};
}
emit('observer_identity',{pid:Process.id,arch:Process.arch,path:main.path,base:main.base.toString(),size:main.size,
  binary_sha256_host_verified:cfg.binary_sha256,frida:Frida.version,sites:verified,mode:'READ_ONLY_ENGINE_OBSERVATION'});
// Scope isolation, not an engine-object patch: honor the documented SHGetKnownFolderPath
// output allocation/free contract, redirecting FOLDERID_SavedGames only in this owned test process.
// The engine opens stdout/stderr BEFORE parsing -userpath; env USERPROFILE is insufficient.
const known=Module.findGlobalExportByName('SHGetKnownFolderPath');
const allocAddress=Module.findGlobalExportByName('CoTaskMemAlloc');
const freeAddress=Module.findGlobalExportByName('CoTaskMemFree');
if(!known || !allocAddress || !freeAddress)throw new Error('Early profile isolation APIs unavailable');
const taskAlloc=new NativeFunction(allocAddress,'pointer',['uint64'],'win64');
// Windows SDK KnownFolders.h: {4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4}.
const savedGuid='ff325c4c9dbbb043b5b42d72e54eaaa4';
const knownOriginal=new NativeFunction(known,'int',['pointer','uint32','pointer','pointer'],'win64');
const knownReplacement=new NativeCallback((guid,flags,token,output)=>{
  const b=new Uint8Array(guid.readByteArray(16));
  if(Array.from(b,x=>x.toString(16).padStart(2,'0')).join('')!==savedGuid)return knownOriginal(guid,flags,token,output);
  // Return the already-created test folder directly. Calling the original first can attempt
  // KF_FLAG_CREATE outside workspace before an onLeave-only redirect gets control (C06).
  const path=cfg.profile+'\\early_saved_games',replacement=taskAlloc((path.length+1)*2);
  if(replacement.isNull())return -2147024882; // E_OUTOFMEMORY
  replacement.writeUtf16String(path);output.writePointer(replacement);
  emit('known_folder_isolation',{folder:'FOLDERID_SavedGames',redirected_path:path,allocation:'CoTaskMemAlloc; caller owns CoTaskMemFree',original_not_called:true});
  return 0;
},'int',['pointer','uint32','pointer','pointer'],'win64');
Interceptor.replace(known,knownReplacement);
function hook(name,callbacks){Interceptor.attach(main.base.add(cfg.sites[name].rva),callbacks);}
hook('userpath',{onEnter(args){this.self=args[0];},onLeave(){emit('userpath_result',{object:this.self.toString(),
  path:attempt(()=>this.self.add(8).readPointer().readUtf8String())});}});
let getterCount=0;
hook('get_player',{onEnter(args){this.self=args[0];},onLeave(ret){if(getterCount++<6)emit('get_player',{this_ptr:this.self.toString(),returned:ret.toString(),expected:this.self.add(0x40).toString()});}});
hook('puppet_spawn',{onEnter(args){this.self=args[0];emit('puppet_spawn_enter',{this_ptr:this.self.toString(),return_address:this.returnAddress.toString(),
  before_player:player(singleton()),before_puppet:puppet(this.self),rsp:this.context.rsp.toString()});},onLeave(ret){
  emit('puppet_spawn_leave',{returned:ret.toString(),after_player:player(singleton()),after_puppet:puppet(this.self)});}});
hook('factory',{onEnter(args){this.capture=args[1].toInt32()===0;this.ordinal=++factoryCount;if(this.capture)emit('player_factory_enter',{
  ordinal:this.ordinal,rcx:args[0].toString(),type:args[1].toInt32(),vec:args[2].toString(),
  input_origin:attempt(()=>[0,4,8].map(o=>args[2].add(o).readFloat())),stack_sector:attempt(()=>this.context.rsp.add(0x28).readS32()),
  return_address:this.returnAddress.toString(),note:'XMM3 not exposed here; yaw register ABI remains unmeasured'});},
  onLeave(ret){if(this.capture)emit('player_factory_leave',{ordinal:this.ordinal,actor:puppet(ret),player:player(singleton())});}});
hook('map_factory',{onEnter(args){this.capture=attempt(()=>args[1].readS16())===0;
  if(this.capture)emit('map_player_factory_enter',{factory:args[0].toString(),record:args[1].toString(),record_type:args[1].readS16(),
    record_sector:args[1].add(2).readS16(),record_origin:[4,6,8].map(o=>args[1].add(o).readS16()),record_angle:args[1].add(16).readFloat(),return_address:this.returnAddress.toString()});},
  onLeave(ret){if(this.capture)emit('map_player_factory_leave',{actor:puppet(ret),player:player(singleton())});}});
let playerUpdateCount=0, puppetTickCount=0;
hook('player_update',{onEnter(args){if(playerUpdateCount++<3)emit('player_update',{rcx:args[0].toString(),player:player(args[0]),return_address:this.returnAddress.toString()});}});
hook('puppet_tick',{onEnter(args){if(puppetTickCount++<3)emit('puppet_tick',{rcx:args[0].toString(),puppet:puppet(args[0]),return_address:this.returnAddress.toString()});}});
hook('state_tick',{onEnter(args){tickCount++;const now=Date.now();if(now-lastSample<1000)return;lastSample=now;
  const p=player(args[0].add(0x40));emit('tick_sample',{tick_count:tickCount,this_ptr:args[0].toString(),state:attempt(()=>args[0].add(0x24).readS32()),
    player:p,puppet:p&&p.actor?puppet(ptr(p.actor)):null});}});
for(const n of ['player_reset','remove','pool_return']) {
  let count=0;
  hook(n,{onEnter(args){if(count++<8)emit('lifecycle_'+n,{rcx:args[0].toString(),rdx:args[1].toString(),return_address:this.returnAddress.toString()});}});
}
const writePaths=new Set(), remappedPaths=new Set();
function isolatedFilePath(path) {
  const p=path.startsWith('\\\\?\\')?path.slice(4):path;
  const relative=p.match(/^(?:\.\\)?(stdout\.txt|stderr\.txt|kexengine\.cfg|consoleHistory\.txt)$/i);
  if(relative)return cfg.profile+'\\early_native_logs\\'+relative[1];
  const steamLog=p.match(/^[a-z]:\\.*\\Steam\\logs\\(gameoverlay_renderer[^\\]*\.txt)$/i);
  if(steamLog)return cfg.profile+'\\steam_overlay\\'+steamLog[1];
  const steamCache=p.match(/^[a-z]:\\.*\\steamapps\\shadercache\\1678430\\(.+)$/i);
  if(steamCache && !steamCache[1].split('\\').some(seg=>seg==='..'||seg==='.'))return cfg.profile+'\\gpu_cache\\steam\\'+steamCache[1];
  const m=p.match(/^[a-z]:\\Users\\[^\\]+\\Saved Games\\Nightdive Studios\\Powerslave EX\\(.+)$/i);
  if(m)return cfg.profile+'\\early_native_logs\\'+m[1];
  const cache=p.match(/^[a-z]:\\\.cache\\AMD\\DxcCache\\([^\\]+)$/i);
  if(cache)return cfg.profile+'\\gpu_cache\\'+cache[1];
  return null;
}
const cf=Module.findGlobalExportByName('CreateFileW');
const fullPath=new NativeFunction(Module.getGlobalExportByName('GetFullPathNameW'),'uint32',['pointer','uint32','pointer','pointer'],'win64');
function resolvedFilePath(path){const out=Memory.alloc(65536),n=fullPath(Memory.allocUtf16String(path),32768,out,ptr(0));return n&&n<32768?out.readUtf16String():path;}
if(cf)Interceptor.attach(cf,{onEnter(args){
  let original=attempt(()=>args[0].readUtf16String());
  if(typeof original==='string'){
    const mapped=isolatedFilePath(original);
    if(mapped){this.pathBuffer=Memory.allocUtf16String(mapped);args[0]=this.pathBuffer;
      if(!remappedPaths.has(original)){remappedPaths.add(original);emit('filesystem_path_isolation',{original_path:original,redirected_path:mapped,api:'CreateFileW',note:'Documented API string argument, not an engine object pointer'});}}
  }
  const access=args[1].toUInt32(),create=args[4].toUInt32();
  if((access&0x50000000)===0 && ![1,2,4,5].includes(create))return;
  const rawPath=attempt(()=>args[0].readUtf16String());if(typeof rawPath!=='string'||rawPath.startsWith('\\\\?\\hid')||rawPath.startsWith('\\\\.\\pipe\\'))return;
  const path=resolvedFilePath(rawPath);if(writePaths.has(path)||writePaths.size>=40)return;
  writePaths.add(path);this.loggedPath=path;this.access=access;this.disposition=create;
},onLeave(ret){if(this.loggedPath)emit('file_write_open_observed',{path:this.loggedPath,access:'0x'+this.access.toString(16),creation_disposition:this.disposition,handle:ret.toString(),succeeded:!ret.equals(ptr(-1))});}});
emit('observer_loaded',{engine_data_writes:false,native_game_calls:false,profile:cfg.profile});
Process.setExceptionHandler(details=>{emit('native_exception',{type:details.type,address:details.address.toString(),
  memory:details.memory?{operation:details.memory.operation,address:details.memory.address.toString()}:null,
  rip:details.context.rip?.toString(),rsp:details.context.rsp?.toString()});return false;});
