"""External process-object observer. All waits use one retained kernel handle.

PSS/WCT/StackWalk helper is bounded as a separate process; no automatic game kill.
"""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import datetime, json, os, struct, subprocess, sys, time

LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2]
OUT=ROOT/'docs/evidence/m1/HOST_TERMINAL_52'
k=C.WinDLL('kernel32',use_last_error=True)
def api(n,rest,args):
    f=getattr(k,n);f.restype=rest;f.argtypes=args;return f
OpenProcess=api('OpenProcess',W.HANDLE,[W.DWORD,W.BOOL,W.DWORD])
CloseHandle=api('CloseHandle',W.BOOL,[W.HANDLE])
GetProcessId=api('GetProcessId',W.DWORD,[W.HANDLE])
QueryPath=api('QueryFullProcessImageNameW',W.BOOL,[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)])
Times=api('GetProcessTimes',W.BOOL,[W.HANDLE]+[C.POINTER(W.FILETIME)]*4)
Exit=api('GetExitCodeProcess',W.BOOL,[W.HANDLE,C.POINTER(W.DWORD)])
Wait=api('WaitForSingleObject',W.DWORD,[W.HANDLE,W.DWORD])
HandleInfo=api('GetHandleInformation',W.BOOL,[W.HANDLE,C.POINTER(W.DWORD)])
Duplicate=api('DuplicateHandle',W.BOOL,[W.HANDLE,W.HANDLE,W.HANDLE,C.POINTER(W.HANDLE),W.DWORD,W.BOOL,W.DWORD])
Current=api('GetCurrentProcess',W.HANDLE,[])
Compare=C.WinDLL('kernelbase',use_last_error=True).CompareObjectHandles
Compare.restype=W.BOOL;Compare.argtypes=[W.HANDLE,W.HANDLE]
Read=api('ReadProcessMemory',W.BOOL,[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)])
ACCESS=0x100000|0x400|0x10
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def ft(t):return t.dwLowDateTime|(t.dwHighDateTime<<32)
def identity(h):
    b=C.create_unicode_buffer(32768);n=W.DWORD(len(b));t=[W.FILETIME() for _ in range(4)];flags=W.DWORD()
    assert QueryPath(h,0,b,C.byref(n)),C.WinError(C.get_last_error())
    assert Times(h,*[C.byref(x) for x in t]),C.WinError(C.get_last_error())
    assert HandleInfo(h,C.byref(flags)),C.WinError(C.get_last_error())
    return dict(pid=GetProcessId(h),path=b.value,creation_filetime=ft(t[0]),exit_filetime=ft(t[1]),handle=hex(h),handle_flags=flags.value)
def state(h):
    code=W.DWORD(0xdeadbeef);C.set_last_error(0);ok=Exit(h,C.byref(code));err=0 if ok else C.get_last_error()
    C.set_last_error(0);wait=Wait(h,0);we=C.get_last_error() if wait==0xffffffff else 0
    return dict(utc=utc(),handle=hex(h),pid=GetProcessId(h),exit_query_ok=bool(ok),exit_code=code.value if ok else None,exit_error=err,wait=wait,wait_error=we)
def write(p,o):
    with p.open('x',encoding='utf8') as f:json.dump(o,f,indent=2)
def open_checked(pid,path,creation=None):
    h=OpenProcess(ACCESS,False,pid)
    if not h:raise C.WinError(C.get_last_error())
    try:
        i=identity(h)
        assert i['pid']==pid and Path(i['path']).resolve()==Path(path).resolve(),'PID/path mismatch'
        if creation is not None:assert i['creation_filetime']==creation,'creation time mismatch'
        return h,i
    except BaseException:CloseHandle(h);raise
def snapshot(h,i,dest,dump=False):
    sym=OUT/'symbols';sym.mkdir(exist_ok=True)
    argv=[str(LAB/'build/Release/terminal_snapshot.exe'),str(i['pid']),i['path'],str(i['creation_filetime']),str(sym)]
    if dump:argv.append(str(dest.with_suffix('.dmp')))
    with dest.with_suffix('.log').open('xb') as f:
        try:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=25);result=dict(argv=argv,exit_code=r.returncode)
        except subprocess.TimeoutExpired:result=dict(argv=argv,timeout=25,helper_only_terminated=True)
    write(dest.with_suffix('.json'),dict(**result,process_before_after=state(h)))
def ledger(h,run):
    p=run/'LEDGER_CONFIG.json'
    if not p.exists():return None
    cfg=json.loads(p.read_text());buf=C.create_string_buffer(cfg['bytes']);n=C.c_size_t()
    ok=Read(h,int(cfg['address'],16),buf,len(buf),C.byref(n))
    if not ok:return dict(read_ok=False,error=C.get_last_error(),bytes=n.value)
    rows=[]
    for slot,name in enumerate(cfg['slots']):
        ready,tid,stamp,a0,a1,caller=struct.unpack_from('<IIQQQQ',buf,slot*40)
        rows.append(dict(slot=slot,name=name,entered=ready,tid=tid,filetime=stamp,arg0=hex(a0),arg1=hex(a1),caller=hex(caller)))
    return dict(read_ok=True,rows=rows)
def watch(name):
    run=ROOT/'docs/evidence/m1'/name;launch=json.loads((run/'launch.json').read_text())
    assert run.resolve().is_relative_to(ROOT/'docs/evidence/m1') and name in ['N52_HOST_DIAG_01','N52_CLIENT_DIAG_01','N52_HOST_VERIFY_01','N52_CLIENT_VERIFY_01']
    h,i=open_checked(launch['pid'],launch['target']);dup=W.HANDLE()
    try:
        assert Duplicate(Current(),h,Current(),C.byref(dup),0,False,2)
        same=bool(Compare(h,dup));assert same
        write(run/'OS_HANDLE_IDENTITY.json',dict(utc=utc(),observer_pid=os.getpid(),access_requested=ACCESS,primary=i,duplicate=identity(dup.value),compare_same_kernel_object=same,initial=state(h)))
        started=time.monotonic();final_seen=None;captured=False;last=None;last_ledger=None
        with (run/'PROCESS_WAIT.jsonl').open('x',encoding='utf8',buffering=1) as f:
            while time.monotonic()-started<1200:
                s=state(h);l=ledger(h,run)
                key=(s['exit_query_ok'],s['exit_code'],s['wait'])
                if key!=last or l!=last_ledger or s['wait']==0:
                    f.write(json.dumps(dict(**s,ledger=l,duplicate_wait=Wait(dup,0)))+'\n');last=key;last_ledger=l
                if s['wait']==0:
                    write(run/'OS_SIGNALED.json',dict(**s,identity=i,duplicate_wait=Wait(dup,0),normal_exit_requires_no_cleanup_marker=not (run/'EMERGENCY_CLEANUP.json').exists()));break
                assert s['wait']==258 and s['exit_query_ok']
                journal=run/'native.jsonl'
                if journal.exists():
                    with journal.open('rb') as jf:
                        jf.seek(max(0,journal.stat().st_size-16384));tail=jf.read()
                    if b'"native_final_exit"' in tail or b'"client_native_final_exit"' in tail:
                        if final_seen is None:final_seen=time.monotonic()
                if final_seen and not captured and time.monotonic()-final_seen>2:
                    write(run/'OS_HANG_STATE.json',dict(**s,identity_now=identity(h),identity_start=i,ledger=l,duplicate_wait=Wait(dup,0)))
                    snapshot(h,i,run/'TERMINAL_SNAPSHOT',True);captured=True
                if (run/'REQUEST_LIVE_SNAPSHOT').exists() and not (run/'LIVE_SNAPSHOT.json').exists():snapshot(h,i,run/'LIVE_SNAPSHOT')
                time.sleep(.25)
            else:write(run/'OBSERVER_BUDGET_EXHAUSTED.json',state(h))
    finally:
        results=dict(utc=utc(),close_duplicate=bool(CloseHandle(dup)) if dup else None,close_primary=bool(CloseHandle(h)))
        write(run/'OBSERVER_CLOSED.json',results)
def test():
    child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(3);raise SystemExit(37)'])
    h,i=open_checked(child.pid,sys.executable)
    try:
        active=state(h);assert active['wait']==258 and active['exit_code']==259
        denied=[]
        for path,creation in [(str(ROOT/'wrong.exe'),None),(sys.executable,i['creation_filetime']+1)]:
            try:other,_=open_checked(child.pid,path,creation);CloseHandle(other)
            except AssertionError:denied.append(True)
        assert len(denied)==2
        snapshot(h,i,OUT/'FIXTURE_SNAPSHOT')
        assert Wait(h,5000)==0
        exited=state(h);assert exited['exit_query_ok'] and exited['exit_code']==37 and exited['wait']==0
        write(OUT/'OBSERVER_TEST.json',dict(active=active,exited=exited,identity=i,rejected_wrong_path_and_creation=True,child_return=child.wait(),game_runs=0))
        print('observer fixture PASS: active259/timeout258 -> signaled0/exit37, identity rejects, external PSS')
    finally:CloseHandle(h)
if __name__=='__main__': test() if sys.argv[1]=='test' else watch(sys.argv[1])
