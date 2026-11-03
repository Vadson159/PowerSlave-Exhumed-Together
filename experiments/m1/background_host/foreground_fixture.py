"""Real owned-window foreground fixture. No WM_ACTIVATE*/WM_SETFOCUS injection.

If Windows rejects the ordinary request, temporarily join the helper's input
queue to the current foreground and owned target queues, request the real OS
foreground, then detach both in finally. Never part of the mod DLL.
"""
import ctypes as C,json,sys
from ctypes import wintypes as W
import ui53
from audit import OUT,dump,stamp
u=ui53.u;k=ui53.k
original=u.SetForegroundWindow
u.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL];u.AttachThreadInput.restype=W.BOOL
k.GetCurrentThreadId.restype=W.DWORD
attempts=[]
def actual(hwnd):
    before=ui53.foreground();ok=bool(original(hwnd))
    if not ok:
        p=W.DWORD();fgTid=u.GetWindowThreadProcessId(u.GetForegroundWindow(),C.byref(p));targetTid=u.GetWindowThreadProcessId(hwnd,C.byref(p));ownTid=k.GetCurrentThreadId();attached=[]
        try:
            for tid in dict.fromkeys([fgTid,targetTid]):
                if tid and tid!=ownTid:
                    joined=bool(u.AttachThreadInput(ownTid,tid,True));attempts.append(dict(operation='AttachThreadInput',helper_tid=ownTid,target_tid=tid,result=joined))
                    if joined:attached.append(tid)
            ok=bool(original(hwnd))
        finally:
            for tid in reversed(attached):attempts.append(dict(operation='DetachThreadInput',result=bool(u.AttachThreadInput(ownTid,tid,False))))
    attempts.append(dict(operation='SetForegroundWindow',target_hwnd=hex(hwnd),before=before,after=ui53.foreground(),result=ok,synthetic_focus_messages=0))
    return ok
u.SetForegroundWindow=actual
tag=sys.argv[2];p=OUT/('FOREGROUND_FIXTURE_'+tag+'_'+stamp()+'.log')
try:
    with p.open('x',encoding='utf8') as f:
        sys.stdout=f;sys.stderr=f;ui53.main()
finally:dump(OUT/('FOREGROUND_OS_'+tag+'_'+stamp()+'.json'),attempts)
