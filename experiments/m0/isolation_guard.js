/* Diagnostic write-open guard for the disposable process, NOT an OS sandbox.
   Documented NtCreateFile/NtOpenFile ABI; no engine/Steamworks functions changed.
   Known path redirection remains in observer.js. This layer rejects other disk writes.
   Does not cover direct syscalls, inherited writable handles, rename APIs or other processes. */
function m0CanonicalPath(value) {
  let p=value.replace(/\//g,'\\');
  if(p.startsWith('\\\\?\\'))p=p.slice(4);
  else if(p.startsWith('\\??\\'))p=p.slice(4);
  else if(/^\\DosDevices\\/i.test(p))p=p.slice(12);
  if(!/^[a-z]:\\/i.test(p))return null;
  const pieces=[];
  for(const part of p.slice(3).split('\\')) {
    if(!part||part==='.')continue;
    if(part==='..'){if(!pieces.length)return null;pieces.pop();}
    else {if(/[ .]$/.test(part))return null;pieces.push(part);}
  }
  return (p.slice(0,3)+pieces.join('\\')).toLowerCase();
}
function m0DiskWritePolicy(path,workspace) {
  const p=m0CanonicalPath(path),root=m0CanonicalPath(workspace);
  if(!root)throw new Error('Guard requires absolute DOS workspace');
  if(p)return {allow:p===root||p.startsWith(root+'\\'),path:p,reason:'disk'};
  // Explicit non-filesystem handles needed by SDL/audio/graphics/Frida. Disk devices and
  // redirectors stay denied. These exemptions are reported and are NOT filesystem evidence.
  if(/^\\Device\\(?:Null$|NamedPipe\\|ConDrv\\|Afd\\|Nsi$|KsecDD$|MMCSS\\MmThread$|000000[0-9a-f]+$)/i.test(path) ||
     /^\\(?:\?\?|DosDevices)\\(?:(?:NUL|CONIN\$|CONOUT\$)$|PIPE\\|HID#[^\\]+(?:\\kbd)?$)/i.test(path))
    return {allow:true,path,reason:'device_exemption'};
  return {allow:false,path,reason:'unresolved_or_external_namespace'};
}
function installM0WriteGuard(cfg,emit) {
  if(Process.arch!=='x64'||!cfg.workspace)throw new Error('Write guard target/config mismatch');
  const workspace=m0CanonicalPath(cfg.workspace);
  if(!workspace||workspace.length>=4096)throw new Error('Write guard workspace path invalid');
  const originals=Memory.alloc(16);originals.writePointer(ptr(0));originals.add(8).writePointer(ptr(0));
  const workspaceBuffer=Memory.allocUtf16String(workspace);
  // CModule data is read-only. All mutable native storage is separately owned, explicitly
  // zeroed and supplied as extern symbols; no writable static definitions in compiled code.
  function zeroBuffer(size){const value=Memory.alloc(size);value.writeByteArray(new Uint8Array(size));return value;}
  const lockBuffer=zeroBuffer(8),scratchBuffer=zeroBuffer(16388*16),ledgerBuffer=zeroBuffer(2088*256),nativeCountBuffer=zeroBuffer(56);
  // Both Nt* replacements execute exclusively native code. In particular, loader/file APIs
  // never enter the Frida JS lock. The JS timer only drains adapter-owned native records.
  const nativeSource=String.raw`
    typedef unsigned int U32;
    typedef int S32;
    typedef unsigned long long U64;
    typedef unsigned short W;
    typedef struct {unsigned short length, maximum; U32 padding; W *buffer;} Name;
    typedef struct {U32 length, padding; void *root; Name *name; U32 attributes, pad2; void *security, *qos;} Attributes;
    typedef struct {void *base, *allocation; U32 allocation_protect; unsigned short partition, pad; U64 size; U32 state, protect, type, pad2;} MemoryInfo;
    typedef char check_name_size[(sizeof(Name)==16)?1:-1];
    typedef char check_attributes_size[(sizeof(Attributes)==48)?1:-1];
    typedef char check_memory_info_size[(sizeof(MemoryInfo)==48)?1:-1];
    extern void *guard_originals[];
    extern W guard_workspace[];
    extern void lock_exclusive(void *lock);
    extern void unlock_exclusive(void *lock);
    extern U32 full_path(const W *path,U32 size,W *out,void *part);
    extern U32 final_path(void *file,W *out,U32 size,U32 flags);
    extern U32 file_attributes(const W *path);
    extern U64 virtual_query(const void *address,MemoryInfo *info,U64 size);
    extern U32 current_tid(void);
    extern U64 tick_count(void);
    extern U32 get_last_error(void);
    extern void set_last_error(U32 error);
    #define PATH_CAP 4096
    #define LOG_PATH_CAP 1024
    #define LOG_CAP 256
    #define SCRATCH_CAP 16
    typedef struct {U32 used; W path[PATH_CAP], canonical[PATH_CAP];} Scratch;
    typedef struct {U32 kind, api, access, disposition, options, reason, tid, reserved; U64 tick; W path[LOG_PATH_CAP];} Record;
    typedef char check_scratch_size[(sizeof(Scratch)==16388)?1:-1];
    typedef char check_record_size[(sizeof(Record)==2088)?1:-1];
    extern void *guard_lock;
    extern Scratch scratch[SCRATCH_CAP];
    extern Record records[LOG_CAP];
    // write_opens, allowed_disk, denied, device_exemptions, errors, unique_overflow, unique_records
    extern U64 counts[7];
    static U32 lower(U32 c){return c>='A'&&c<='Z'?c+32:c;}
    static int prefix(const W *value,const char *text){U32 i=0;while(text[i]){if(lower(value[i])!=lower((unsigned char)text[i]))return 0;i++;}return 1;}
    static int equal_ascii(const W *value,const char *text){U32 i=0;while(text[i]){if(lower(value[i])!=lower((unsigned char)text[i]))return 0;i++;}return value[i]==0;}
    static int equal_wide(const W *a,const W *b){U32 i=0;while(a[i]&&b[i]){if(lower(a[i])!=lower(b[i]))return 0;i++;}return a[i]==b[i];}
    static U32 length(const W *value,U32 cap){U32 i=0;while(i<cap&&value[i])i++;return i;}
    static void ascii_path(W *out,const char *value){U32 i=0;while(value[i]&&i<PATH_CAP-1){out[i]=(unsigned char)value[i];i++;}out[i]=0;}
    static int readable(const void *value,U64 size,int writing){
      U64 at=(U64)value,end=at+size;
      if(!at||end<at)return 0;
      while(at<end){MemoryInfo info;U64 next;
        if(virtual_query((const void *)at,&info,sizeof(info))!=sizeof(info)||info.state!=0x1000||(info.protect&0x101))return 0;
        if(!(info.protect&(writing?0xcc:0xee)))return 0;
        next=(U64)info.base+info.size;if(next<=at)return 0;at=next;
      }
      return 1;
    }
    static Scratch *take_scratch(void){U32 i;Scratch *result=0;
      lock_exclusive(&guard_lock);
      for(i=0;i<SCRATCH_CAP;i++)if(!scratch[i].used){scratch[i].used=1;result=&scratch[i];break;}
      unlock_exclusive(&guard_lock);return result;
    }
    static void release_scratch(Scratch *item){lock_exclusive(&guard_lock);item->used=0;unlock_exclusive(&guard_lock);}
    static void lost_record(void){lock_exclusive(&guard_lock);counts[0]++;counts[2]++;counts[5]++;unlock_exclusive(&guard_lock);}
    static void record(U32 kind,U32 api,U32 access,U32 disposition,U32 options,U32 reason,const W *path,int error,int overflow){
      U32 i,n=length(path,PATH_CAP),tid=current_tid();U64 tick=tick_count();Record *item;
      lock_exclusive(&guard_lock);counts[0]++;counts[kind==1?1:kind==2?3:2]++;
      if(error)counts[4]++;if(overflow||n>=LOG_PATH_CAP)counts[5]++;
      for(i=0;i<(U32)counts[6];i++)if(records[i].kind==kind&&equal_wide(records[i].path,path)){unlock_exclusive(&guard_lock);return;}
      if(counts[6]>=LOG_CAP){counts[5]++;unlock_exclusive(&guard_lock);return;}
      item=&records[(U32)counts[6]++];item->kind=kind;item->api=api;item->access=access;item->disposition=disposition;
      item->options=options;item->reason=reason;item->tid=tid;item->tick=tick;
      for(i=0;i<n&&i<LOG_PATH_CAP-1;i++)item->path[i]=path[i];item->path[i]=0;
      unlock_exclusive(&guard_lock);
    }
    static int device(const W *path){const W *tail;U32 i;
      if(prefix(path,"\\Device\\")){
        tail=path+8;
        if(equal_ascii(tail,"Null")||prefix(tail,"NamedPipe\\")||prefix(tail,"ConDrv\\")||prefix(tail,"Afd\\")||
           equal_ascii(tail,"Nsi")||equal_ascii(tail,"KsecDD")||equal_ascii(tail,"MMCSS\\MmThread"))return 1;
        if(prefix(tail,"000000")){i=6;while((tail[i]>='0'&&tail[i]<='9')||(lower(tail[i])>='a'&&lower(tail[i])<='f'))i++;if(i>6&&!tail[i])return 1;}
      }
      if(prefix(path,"\\??\\"))tail=path+4;
      else if(prefix(path,"\\DosDevices\\"))tail=path+12;else return 0;
      if(equal_ascii(tail,"NUL")||equal_ascii(tail,"CONIN$")||equal_ascii(tail,"CONOUT$")||prefix(tail,"PIPE\\"))return 1;
      if(prefix(tail,"HID#")){i=4;while(tail[i]&&tail[i]!='\\')i++;if(i>4&&(!tail[i]||equal_ascii(tail+i,"\\kbd")))return 1;}
      return 0;
    }
    static int canonical(const W *value,W *out){U32 i=0,n=3,start,size,j;const W *path=value;
      if(prefix(path,"\\\\?\\")||prefix(path,"\\??\\"))path+=4;
      else if(prefix(path,"\\DosDevices\\"))path+=12;
      if(lower(path[0])<'a'||lower(path[0])>'z'||path[1]!=':'||(path[2]!='\\'&&path[2]!='/'))return 0;
      out[0]=(W)lower(path[0]);out[1]=':';out[2]='\\';out[3]=0;i=3;
      while(path[i]){
        while(path[i]=='\\'||path[i]=='/')i++;
        if(!path[i])break;start=i;while(path[i]&&path[i]!='\\'&&path[i]!='/')i++;size=i-start;
        if(size==1&&path[start]=='.')continue;
        if(size==2&&path[start]=='.'&&path[start+1]=='.'){
          if(n<=3)return 0;while(n>3&&out[n-1]!='\\')n--;if(n>3)n--;out[n]=0;continue;
        }
        if(path[i-1]=='.'||path[i-1]==' ')return 0;
        if(n>3){if(n+1>=PATH_CAP)return -1;out[n++]='\\';}
        if(n+size>=PATH_CAP)return -1;for(j=0;j<size;j++)out[n++]=(W)lower(path[start+j]);out[n]=0;
      }
      return 1;
    }
    static int within(const W *path){U32 i=0;while(guard_workspace[i]){if(lower(path[i])!=lower(guard_workspace[i]))return 0;i++;}return !path[i]||path[i]=='\\';}
    // Result: 1 resolved, 0 deliberately unresolved/denied, -1 malformed input, -2 bounded overflow.
    static int object_path(Attributes *oa,Scratch *item){U32 i,n,rootlen;Name *name;W *path=item->path,*buffer;
      if(!readable(oa,sizeof(Attributes),0)||oa->length!=48){ascii_path(path,"<invalid OBJECT_ATTRIBUTES>");return -1;}
      name=oa->name;
      if(!name){ascii_path(path,"<unnamed write-open>");return 0;}
      if(!readable(name,sizeof(Name),0)||(name->length&1)){ascii_path(path,"<invalid UNICODE_STRING>");return -1;}
      n=name->length/2;
      if(n>=PATH_CAP){ascii_path(path,"<native path overflow>");return -2;}
      buffer=name->buffer;
      if(n&&!readable(buffer,(U64)n*2,0)){ascii_path(path,"<unreadable UNICODE_STRING>");return -1;}
      for(i=0;i<n;i++){if(!buffer[i]){ascii_path(path,"<embedded native path NUL>");return -1;}path[i]=buffer[i];}path[n]=0;
      if(n&&path[0]=='\\')return 1;
      if(!oa->root){if(!n)ascii_path(path,"<empty native path>");return 0;}
      rootlen=final_path(oa->root,item->canonical,PATH_CAP,0);
      if(!rootlen){ascii_path(path,"<unresolved RootDirectory>");return 0;}
      if(rootlen>=PATH_CAP||rootlen+(n?1+n:0)>=PATH_CAP){ascii_path(path,"<RootDirectory path overflow>");return -2;}
      if(n){item->canonical[rootlen++]='\\';for(i=0;i<n;i++)item->canonical[rootlen+i]=path[i];rootlen+=n;item->canonical[rootlen]=0;}
      for(i=0;i<=rootlen;i++)path[i]=item->canonical[i];return 1;
    }
    static int no_reparse(Scratch *item){U32 n,attrs;
      n=full_path(item->canonical,PATH_CAP,item->path,0);if(!n)return -1;if(n>=PATH_CAP)return -2;
      while(n>3){attrs=file_attributes(item->path);if(attrs!=0xffffffffu&&(attrs&0x400))return 0;
        while(n>3&&item->path[n-1]!='\\')n--;if(n>3)n--;item->path[n]=0;
      }
      return 1;
    }
    static int decide(U32 api,U32 access,Attributes *oa,U32 disposition,U32 options){
      Scratch *item;U32 kind=0,reason=2;int result,error=0,overflow=0;const W *logpath;
      if(!(access&0x520d0156u)&&disposition==1&&!(options&0x1000))return 1;
      item=take_scratch();if(!item){lost_record();return 0;}
      result=object_path(oa,item);logpath=item->path;
      if(result<0){reason=result==-2?6:5;overflow=result==-2;error=result==-1;}
      else if(result==1){
        result=canonical(item->path,item->canonical);
        if(result<0){reason=6;overflow=1;}
        else if(result==1){
          reason=0;logpath=item->canonical;
          if(within(item->canonical)){
            result=no_reparse(item);if(result==1)kind=1;else if(result==0)reason=3;else if(result==-2){reason=6;overflow=1;}else reason=2;
          }
        }else if(device(item->path)){kind=2;reason=1;}
      }
      if(options&0x2000){kind=0;reason=4;}
      if(length(logpath,PATH_CAP)>=LOG_PATH_CAP){kind=0;reason=6;overflow=1;}
      record(kind,api,access,disposition,options,reason,logpath,error,overflow);release_scratch(item);return kind!=0;
    }
    static S32 deny(void **out,void *iosb){
      if(readable(out,8,1))*out=0;
      if(readable(iosb,16,1)){((U64 *)iosb)[0]=0xc0000022u;((U64 *)iosb)[1]=0;}
      return (S32)0xc0000022u;
    }
    typedef S32 (*Create)(void **,U32,Attributes *,void *,void *,U32,U32,U32,U32,void *,U32);
    typedef S32 (*Open)(void **,U32,Attributes *,void *,U32,U32);
    S32 guard_create(void **out,U32 access,Attributes *oa,void *iosb,void *allocation,U32 attributes,U32 share,U32 disposition,U32 options,void *ea,U32 ea_size){
      U32 error=get_last_error();int allow=decide(0,access,oa,disposition,options);set_last_error(error);
      if(!allow||!guard_originals[0]){S32 status=deny(out,iosb);set_last_error(error);return status;}
      return ((Create)guard_originals[0])(out,access,oa,iosb,allocation,attributes,share,disposition,options,ea,ea_size);
    }
    S32 guard_open(void **out,U32 access,Attributes *oa,void *iosb,U32 share,U32 options){
      U32 error=get_last_error();int allow=decide(1,access,oa,1,options);set_last_error(error);
      if(!allow||!guard_originals[1]){S32 status=deny(out,iosb);set_last_error(error);return status;}
      return ((Open)guard_originals[1])(out,access,oa,iosb,share,options);
    }
    U32 guard_snapshot(U64 *out,U32 first,Record *destination,U32 capacity){U32 i,j,count;
      lock_exclusive(&guard_lock);for(i=0;i<7;i++)out[i]=counts[i];
      count=first<(U32)counts[6]?(U32)counts[6]-first:0;if(count>capacity)count=capacity;
      for(i=0;i<count;i++){
        const unsigned char *src=(const unsigned char *)&records[first+i];unsigned char *dst=(unsigned char *)&destination[i];
        for(j=0;j<sizeof(Record);j++)dst[j]=src[j];
      }
      unlock_exclusive(&guard_lock);return count;
    }
    // Called BEFORE installing any hook. Validate writable protection first, then actually
    // write/read/reset every allocation through volatile accesses. A read-only CModule data
    // regression returns a failure bitmask without attempting those writes.
    U32 guard_preflight(void){U32 result=0,i;Scratch *item;volatile U64 *numbers;
      if(!readable(&guard_lock,8,1))result|=1;
      if(!readable(scratch,sizeof(Scratch)*SCRATCH_CAP,1))result|=2;
      if(!readable(records,sizeof(Record)*LOG_CAP,1))result|=4;
      if(!readable(counts,56,1))result|=8;
      if(!readable(guard_originals,16,1))result|=16;
      if(result)return result;
      *(void * volatile *)&guard_lock=0;
      numbers=(volatile U64 *)counts;
      for(i=0;i<7;i++){numbers[i]=0x4d300000u+i;if(numbers[i]!=0x4d300000u+i)result|=32;numbers[i]=0;}
      for(i=0;i<SCRATCH_CAP;i++){
        *(volatile U32 *)&scratch[i].used=0x4d30;
        *(volatile W *)&scratch[i].path[0]=0x4d30;
        *(volatile W *)&scratch[i].path[PATH_CAP-1]=0x4d31;
        *(volatile W *)&scratch[i].canonical[PATH_CAP-1]=0x4d32;
        if(*(volatile U32 *)&scratch[i].used!=0x4d30||*(volatile W *)&scratch[i].path[0]!=0x4d30||
           *(volatile W *)&scratch[i].path[PATH_CAP-1]!=0x4d31||*(volatile W *)&scratch[i].canonical[PATH_CAP-1]!=0x4d32)result|=64;
        scratch[i].used=0;scratch[i].path[0]=0;scratch[i].path[PATH_CAP-1]=0;scratch[i].canonical[PATH_CAP-1]=0;
      }
      for(i=0;i<LOG_CAP;i++){
        *(volatile U32 *)&records[i].kind=0x4d30;*(volatile W *)&records[i].path[LOG_PATH_CAP-1]=0x4d31;
        if(*(volatile U32 *)&records[i].kind!=0x4d30||*(volatile W *)&records[i].path[LOG_PATH_CAP-1]!=0x4d31)result|=128;
        records[i].kind=0;records[i].path[LOG_PATH_CAP-1]=0;
      }
      *(void * volatile *)&guard_originals[0]=(void *)0x4d30;
      if(*(void * volatile *)&guard_originals[0]!=(void *)0x4d30)result|=256;
      guard_originals[0]=0;guard_originals[1]=0;
      item=take_scratch();if(item!=&scratch[0])result|=512;if(item)release_scratch(item);
      if(scratch[0].used||guard_lock)result|=1024;
      return result;
    }
  `;
  const cm=new CModule(nativeSource,{
    guard_originals:originals,guard_workspace:workspaceBuffer,
    guard_lock:lockBuffer,scratch:scratchBuffer,records:ledgerBuffer,counts:nativeCountBuffer,
    lock_exclusive:Module.getGlobalExportByName('AcquireSRWLockExclusive'),unlock_exclusive:Module.getGlobalExportByName('ReleaseSRWLockExclusive'),
    full_path:Module.getGlobalExportByName('GetFullPathNameW'),final_path:Module.getGlobalExportByName('GetFinalPathNameByHandleW'),
    file_attributes:Module.getGlobalExportByName('GetFileAttributesW'),virtual_query:Module.getGlobalExportByName('VirtualQuery'),
    current_tid:Module.getGlobalExportByName('GetCurrentThreadId'),tick_count:Module.getGlobalExportByName('GetTickCount64'),
    get_last_error:Module.getGlobalExportByName('GetLastError'),set_last_error:Module.getGlobalExportByName('SetLastError')
  });
  const preflight=new NativeFunction(cm.guard_preflight,'uint32',[],{abi:'win64',exceptions:'steal'});
  const preflightResult=preflight();
  if(preflightResult!==0)throw new Error('Native write-guard mutable storage preflight failed: 0x'+preflightResult.toString(16));
  emit('isolation_write_guard_preflight',{passed:true,mutable_storage:'Explicit zeroed extern Memory.alloc buffers; native protection/write/read/reset and SRW checks before hooks'});
  const ntdll=Process.getModuleByName('ntdll.dll'),entries=[ntdll.getExportByName('NtCreateFile'),ntdll.getExportByName('NtOpenFile')];
  const installed=[];
  try {
    originals.writePointer(Interceptor.replaceFast(entries[0],cm.guard_create));installed.push(entries[0]);
    originals.add(8).writePointer(Interceptor.replaceFast(entries[1],cm.guard_open));installed.push(entries[1]);
    Interceptor.flush();
  }catch(e){installed.reverse().forEach(at=>Interceptor.revert(at));Interceptor.flush();throw e;}
  const snapshot=new NativeFunction(cm.guard_snapshot,'uint32',['pointer','uint32','pointer','uint32'],{abi:'win64'});
  const countBuffer=Memory.alloc(56),recordsBuffer=Memory.alloc(2088*32),stats={};let cursor=0,samples=0;
  const fields=['write_opens','allowed_disk','denied','device_exemptions','errors','unique_overflow','unique_records'];
  const kinds=['isolation_write_denied','isolation_disk_write_allowed','isolation_device_exemption'];
  const reasons=['disk','device_exemption','unresolved_or_external_namespace','reparse_ancestor','FILE_OPEN_BY_FILE_ID','invalid_native_attributes','native_buffer_overflow'];
  function drain(){
    let count;
    do {
      count=snapshot(countBuffer,cursor,recordsBuffer,32);
      fields.forEach((name,i)=>{stats[name]=Number(countBuffer.add(i*8).readU64().toString());});
      for(let i=0;i<count;i++){
        const at=recordsBuffer.add(i*2088),kind=at.readU32();
        emit(kinds[kind],{api:at.add(4).readU32()?'NtOpenFile':'NtCreateFile',path:at.add(40).readUtf16String(),
          access:'0x'+at.add(8).readU32().toString(16),disposition:at.add(12).readU32(),options:'0x'+at.add(16).readU32().toString(16),
          reason:reasons[at.add(20).readU32()],tid:at.add(24).readU32(),native_tick_ms:at.add(32).readU64().toString(),
          observation:'Native bounded ledger drained by JS timer; utc is drain time'});
      }
      cursor+=count;
    }while(count===32);
  }
  emit('isolation_write_guard_loaded',{workspace:cfg.workspace,apis:['NtCreateFile','NtOpenFile'],implementation:'native CModule replacements; no JS callbacks from Nt*',
    policy:'deny external disk write-opens, reject reparse ancestors; explicit device exemptions',
    native_bounds:{scratch_slots:16,path_chars:4096,unique_records:256,record_path_chars:1024},limitations:'Diagnostic guard, not complete OS sandbox; direct syscalls, inherited writable handles, rename APIs and other processes not covered; no engine object writes'});
  const timer=setInterval(()=>{drain();if(++samples<=10||samples%10===0)emit('isolation_write_guard_summary',{...stats});},1000);
  return {kept:[cm,originals,workspaceBuffer,lockBuffer,scratchBuffer,ledgerBuffer,nativeCountBuffer,preflight,snapshot,countBuffer,recordsBuffer],stats,timer,drain};
}
