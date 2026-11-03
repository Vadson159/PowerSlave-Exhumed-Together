"""Resume the primary thread of a workspace-owned suspended run, or stop that run only."""
from pathlib import Path
import argparse,ctypes,json
from ctypes import wintypes as W
from prepare import ROOT,OUT,COPY

def main():
    p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('--stop',action='store_true');a=p.parse_args()
    r=OUT/a.run/'launch.json';d=json.loads(r.read_text());pid=d['pid']
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
    k.CloseHandle.argtypes=[W.HANDLE]
    h=k.OpenProcess(0x1000|0x0001,False,pid)
    if not h:raise ctypes.WinError(ctypes.get_last_error())
    try:
        path=ctypes.create_unicode_buffer(32768);n=W.DWORD(len(path))
        k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,ctypes.POINTER(W.DWORD)]
        if not k.QueryFullProcessImageNameW(h,0,path,ctypes.byref(n)):raise ctypes.WinError(ctypes.get_last_error())
        if Path(path.value).resolve()!=Path(d['target']).resolve() or not Path(path.value).resolve().is_relative_to(ROOT):raise RuntimeError('Process identity does not match owned test copy')
        if a.stop:
            k.TerminateProcess.argtypes=[W.HANDLE,W.UINT]
            if not k.TerminateProcess(h,0):raise ctypes.WinError(ctypes.get_last_error())
            print(json.dumps({'pid':pid,'terminated':True}));return
        class THREADENTRY32(ctypes.Structure):
            _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('th32ThreadID',W.DWORD),('th32OwnerProcessID',W.DWORD),('tpBasePri',W.LONG),('tpDeltaPri',W.LONG),('dwFlags',W.DWORD)]
        k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];k.CreateToolhelp32Snapshot.restype=W.HANDLE
        k.Thread32First.argtypes=[W.HANDLE,ctypes.POINTER(THREADENTRY32)];k.Thread32Next.argtypes=k.Thread32First.argtypes
        snap=k.CreateToolhelp32Snapshot(4,0);entry=THREADENTRY32();entry.dwSize=ctypes.sizeof(entry);ids=[]
        if k.Thread32First(snap,ctypes.byref(entry)):
            while True:
                if entry.th32OwnerProcessID==pid:ids.append(entry.th32ThreadID)
                if not k.Thread32Next(snap,ctypes.byref(entry)):break
        k.CloseHandle(snap)
        # Only resume the launch-created primary thread; instrumentation can add other threads.
        tid=min(ids) if len(ids)==1 else d.get('primary_thread_id')
        if tid is None:raise RuntimeError('Primary thread ID not recorded; use launch-stage NtResumeProcess or capture primary thread explicitly')
        k.OpenThread.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenThread.restype=W.HANDLE
        th=k.OpenThread(2,False,tid)
        if not th:raise ctypes.WinError(ctypes.get_last_error())
        k.ResumeThread.argtypes=[W.HANDLE];k.ResumeThread.restype=W.DWORD
        result=k.ResumeThread(th);k.CloseHandle(th)
        if result==0xffffffff:raise ctypes.WinError(ctypes.get_last_error())
        print(json.dumps({'pid':pid,'thread':tid,'previous_suspend_count':result}))
    finally:k.CloseHandle(h)

if __name__=='__main__':main()
