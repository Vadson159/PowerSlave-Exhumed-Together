// External read diagnostics. No injection, remote writes, explicit suspend, or termination.
#include <windows.h>
#include <processsnapshot.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <wct.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <vector>

struct Module { DWORD64 base; DWORD size; std::wstring path; };
std::vector<Module> modules;
void address(DWORD64 pc) {
    for (const auto& m : modules) if (pc>=m.base && pc-m.base<m.size) {
        std::wprintf(L"0x%llx %ls+0x%llx", pc, m.path.c_str(), pc-m.base); return;
    }
    std::wprintf(L"0x%llx <not-in-Toolhelp-module-list>", pc);
}
unsigned long long ft(FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
void state(HANDLE p) {
    DWORD code=0xdeadbeef; SetLastError(0); const BOOL ok=GetExitCodeProcess(p,&code); const DWORD err=ok?0:GetLastError();
    SetLastError(0); const DWORD wait=WaitForSingleObject(p,0); const DWORD we=wait==WAIT_FAILED?GetLastError():0;
    FILETIME c{},e{},k{},u{}; const BOOL times=GetProcessTimes(p,&c,&e,&k,&u);
    std::wprintf(L"STATE pid=%lu handle=%p exit_ok=%d code=%lu error=%lu wait=%lu wait_error=%lu times_ok=%d create=%llu exit_time=%llu kernel=%llu user=%llu\n",GetProcessId(p),p,ok,code,err,wait,we,times,ft(c),ft(e),ft(k),ft(u));
    std::fflush(stdout);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5 && argc!=6) { std::fwprintf(stderr,L"snapshot PID EXPECTED_EXE CREATION_FILETIME SYMBOL_DIR [DUMP_FILE]\n");return 2; }
    const DWORD pid=std::wcstoul(argv[1],nullptr,10);
    const DWORD access=PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE;
    HANDLE p=OpenProcess(access,FALSE,pid);
    if(!p) { std::wprintf(L"OpenProcess error=%lu\n",GetLastError());return 3; }
    wchar_t path[32768]{};DWORD size=32768;FILETIME c{},e{},k{},u{};
    if(!QueryFullProcessImageNameW(p,0,path,&size)||!GetProcessTimes(p,&c,&e,&k,&u)||GetProcessId(p)!=pid||_wcsicmp(path,argv[2])||ft(c)!=_wcstoui64(argv[3],nullptr,10)) {
        std::wprintf(L"IDENTITY_REJECT error=%lu path=%ls create=%llu\n",GetLastError(),path,ft(c));CloseHandle(p);return 4;
    }
    std::wprintf(L"IDENTITY_PASS pid=%lu path=%ls creation=%llu access=0x%lx\n",pid,path,ft(c),access);state(p);
    HANDLE ms=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(ms==INVALID_HANDLE_VALUE) std::wprintf(L"MODULE_SNAPSHOT error=%lu\n",GetLastError());
    else {
        MODULEENTRY32W m{};m.dwSize=sizeof(m);BOOL more=Module32FirstW(ms,&m);
        while(more) { modules.push_back({reinterpret_cast<DWORD64>(m.modBaseAddr),m.modBaseSize,m.szExePath});std::wprintf(L"MODULE base=%p size=0x%lx path=%ls\n",m.modBaseAddr,m.modBaseSize,m.szExePath);more=Module32NextW(ms,&m); }
        std::wprintf(L"MODULE_END error=%lu\n",GetLastError());CloseHandle(ms);
    }
    // Empty caller-owned symbol directory: no symbol server or writes outside workspace.
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    const BOOL symbols=SymInitializeW(p,argv[4],TRUE);
    std::wprintf(L"SymInitialize ok=%d error=%lu\n",symbols,symbols?0:GetLastError());
    const HWCT wct=OpenThreadWaitChainSession(0,nullptr);
    std::wprintf(L"WCT_OPEN ok=%d error=%lu\n",wct!=nullptr,wct?0:GetLastError());
    HPSS snapshot{};
    const auto flags=static_cast<PSS_CAPTURE_FLAGS>(PSS_CAPTURE_THREADS|PSS_CAPTURE_THREAD_CONTEXT);
    const DWORD capture=PssCaptureSnapshot(p,flags,CONTEXT_FULL,&snapshot);
    std::wprintf(L"PSS_CAPTURE result=%lu context_flags=0x%lx\n",capture,static_cast<DWORD>(CONTEXT_FULL));std::fflush(stdout);
    if(capture==ERROR_SUCCESS) {
        HPSSWALK marker{};DWORD rc=PssWalkMarkerCreate(nullptr,&marker);
        std::wprintf(L"PSS_MARKER result=%lu\n",rc);
        if(rc==ERROR_SUCCESS) {
            PSS_THREAD_ENTRY t{};
            while((rc=PssWalkSnapshot(snapshot,PSS_WALK_THREADS,marker,&t,sizeof(t)))==ERROR_SUCCESS) {
                std::wprintf(L"THREAD tid=%lu pid=%lu status=%lu suspend=%u context_bytes=%u start=",t.ThreadId,t.ProcessId,t.ExitStatus,t.SuspendCount,t.SizeOfContextRecord);address(reinterpret_cast<DWORD64>(t.Win32StartAddress));std::wprintf(L"\n");std::fflush(stdout);
                if(wct) {
                    WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT]{};DWORD n=WCT_MAX_NODE_COUNT;BOOL cycle{};
                    const BOOL ok=GetThreadWaitChain(wct,0,WCT_OUT_OF_PROC_FLAG|WCT_OUT_OF_PROC_CS_FLAG,t.ThreadId,&n,nodes,&cycle);
                    std::wprintf(L"WCT tid=%lu ok=%d error=%lu count=%lu cycle=%d\n",t.ThreadId,ok,ok?0:GetLastError(),n,cycle);
                    if(ok) for(DWORD i=0;i<n && i<WCT_MAX_NODE_COUNT;++i) {
                        std::wprintf(L"WCT_NODE index=%lu type=%d status=%d",i,nodes[i].ObjectType,nodes[i].ObjectStatus);
                        if(nodes[i].ObjectType==WctThreadType) std::wprintf(L" pid=%lu tid=%lu wait_ms=%lu switches=%lu",nodes[i].ThreadObject.ProcessId,nodes[i].ThreadObject.ThreadId,nodes[i].ThreadObject.WaitTime,nodes[i].ThreadObject.ContextSwitches);
                        else std::wprintf(L" name=%.*ls",WCT_OBJNAME_LENGTH,nodes[i].LockObject.ObjectName);
                        std::wprintf(L"\n");
                    }
                }
                if(t.ContextRecord && t.SizeOfContextRecord>=sizeof(CONTEXT)) {
                    CONTEXT ctx=*t.ContextRecord;STACKFRAME64 f{};
                    f.AddrPC.Offset=ctx.Rip;f.AddrStack.Offset=ctx.Rsp;f.AddrFrame.Offset=ctx.Rbp;
                    f.AddrPC.Mode=f.AddrStack.Mode=f.AddrFrame.Mode=AddrModeFlat;
                    std::wprintf(L"CONTEXT rip=0x%llx rsp=0x%llx rbp=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx\n",ctx.Rip,ctx.Rsp,ctx.Rbp,ctx.Rcx,ctx.Rdx,ctx.R8,ctx.R9);
                    for(unsigned i=0;i<64;++i) {
                        std::wprintf(L"FRAME tid=%lu index=%u sp=0x%llx pc=",t.ThreadId,i,f.AddrStack.Offset);address(f.AddrPC.Offset);
                        alignas(SYMBOL_INFO) char data[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto* si=reinterpret_cast<SYMBOL_INFO*>(data);si->SizeOfStruct=sizeof(SYMBOL_INFO);si->MaxNameLen=MAX_SYM_NAME;DWORD64 displacement{};
                        if(symbols && SymFromAddr(p,f.AddrPC.Offset,&displacement,si))std::wprintf(L" symbol=%hs+0x%llx",si->Name,displacement);
                        std::wprintf(L"\n");
                        if(!symbols || !StackWalk64(IMAGE_FILE_MACHINE_AMD64,p,nullptr,&f,&ctx,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!f.AddrPC.Offset)break;
                    }
                    // Raw bounded stack preserves candidate addresses; these are NOT verified frames.
                    DWORD64 words[128]{};SIZE_T bytes{};
                    const BOOL read=ReadProcessMemory(p,reinterpret_cast<void*>(t.ContextRecord->Rsp),words,sizeof(words),&bytes);
                    std::wprintf(L"STACK_READ ok=%d error=%lu bytes=%zu\n",read,read?0:GetLastError(),bytes);
                    for(SIZE_T i=0;i<bytes/8;++i)std::wprintf(L"STACK_WORD offset=0x%zx value=0x%llx\n",i*8,words[i]);
                }
                std::fflush(stdout);
            }
            std::wprintf(L"PSS_WALK_END result=%lu\n",rc);std::wprintf(L"PSS_MARKER_FREE result=%lu\n",PssWalkMarkerFree(marker));
        }
        std::wprintf(L"PSS_FREE result=%lu\n",PssFreeSnapshot(GetCurrentProcess(),snapshot));
    }
    if(argc==6) {
        HANDLE f=CreateFileW(argv[5],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f!=INVALID_HANDLE_VALUE) {
            const BOOL ok=MiniDumpWriteDump(p,pid,f,static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules),nullptr,nullptr,nullptr);
            std::wprintf(L"MINIDUMP ok=%d error=%lu\n",ok,ok?0:GetLastError());CloseHandle(f);
        } else std::wprintf(L"MINIDUMP_CREATE error=%lu\n",GetLastError());
    }
    if(wct)CloseThreadWaitChainSession(wct);
    if(symbols)std::wprintf(L"SymCleanup ok=%d\n",SymCleanup(p));
    state(p);std::wprintf(L"CloseProcessHandle ok=%d\n",CloseHandle(p));return capture==ERROR_SUCCESS?0:5;
}
