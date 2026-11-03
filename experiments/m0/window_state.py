"""Read window/desktop/session state for a verified owned test PID. Does not activate a window."""
import ctypes,datetime,json,sys
from ctypes import wintypes as W
from pathlib import Path
from prepare import ROOT,OUT

def main():
    run=(OUT/sys.argv[1]).resolve()
    if not run.is_relative_to(OUT.resolve()):raise ValueError('Outside evidence root')
    cfg=json.loads((run/'launch.json').read_text());pid=cfg['pid'];tid=cfg['primary_thread_id']
    k=ctypes.WinDLL('kernel32',use_last_error=True);u=ctypes.WinDLL('user32',use_last_error=True)
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    h=k.OpenProcess(0x1000,False,pid);path=ctypes.create_unicode_buffer(32768);size=W.DWORD(len(path))
    k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,ctypes.POINTER(W.DWORD)]
    if not h or not k.QueryFullProcessImageNameW(h,0,path,ctypes.byref(size)):raise ctypes.WinError(ctypes.get_last_error())
    k.CloseHandle(h)
    if Path(path.value).resolve()!=Path(cfg['target']).resolve() or not Path(path.value).resolve().is_relative_to(ROOT):raise ValueError('Process path mismatch')
    k.GetCurrentThreadId.restype=W.DWORD;k.GetCurrentProcessId.restype=W.DWORD
    k.ProcessIdToSessionId.argtypes=[W.DWORD,ctypes.POINTER(W.DWORD)];k.WTSGetActiveConsoleSessionId.restype=W.DWORD
    def session(p):
        v=W.DWORD();ok=k.ProcessIdToSessionId(p,ctypes.byref(v));return {'ok':bool(ok),'session':v.value if ok else None,'error':ctypes.get_last_error() if not ok else 0}
    u.GetThreadDesktop.argtypes=[W.DWORD];u.GetThreadDesktop.restype=W.HANDLE;u.GetProcessWindowStation.restype=W.HANDLE
    u.OpenInputDesktop.argtypes=[W.DWORD,W.BOOL,W.DWORD];u.OpenInputDesktop.restype=W.HANDLE;u.CloseDesktop.argtypes=[W.HANDLE]
    u.GetUserObjectInformationW.argtypes=[W.HANDLE,ctypes.c_int,ctypes.c_void_p,W.DWORD,ctypes.POINTER(W.DWORD)]
    def object_name(h):
        if not h:return {'name':None,'error':ctypes.get_last_error()}
        b=ctypes.create_unicode_buffer(512);n=W.DWORD();ok=u.GetUserObjectInformationW(h,2,b,ctypes.sizeof(b),ctypes.byref(n))
        return {'name':b.value if ok else None,'error':ctypes.get_last_error() if not ok else 0}
    target_desktop=object_name(u.GetThreadDesktop(tid));helper_desktop=object_name(u.GetThreadDesktop(k.GetCurrentThreadId()));station=object_name(u.GetProcessWindowStation())
    ih=u.OpenInputDesktop(0,False,1);input_desktop=object_name(ih)
    if ih:u.CloseDesktop(ih)
    u.GetForegroundWindow.restype=W.HWND;foreground=u.GetForegroundWindow()
    u.GetWindowThreadProcessId.argtypes=[W.HWND,ctypes.POINTER(W.DWORD)];fp=W.DWORD()
    if foreground:u.GetWindowThreadProcessId(foreground,ctypes.byref(fp))
    u.IsWindowVisible.argtypes=[W.HWND];u.IsWindowEnabled.argtypes=[W.HWND];u.IsIconic.argtypes=[W.HWND];u.IsHungAppWindow.argtypes=[W.HWND]
    u.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,ctypes.c_int];u.GetClientRect.argtypes=[W.HWND,ctypes.POINTER(W.RECT)]
    windows=[];CB=ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    @CB
    def enum(hwnd,param):
        wp=W.DWORD();wt=u.GetWindowThreadProcessId(hwnd,ctypes.byref(wp))
        if wp.value==pid:
            title=ctypes.create_unicode_buffer(512);u.GetWindowTextW(hwnd,title,512);rc=W.RECT();u.GetClientRect(hwnd,ctypes.byref(rc))
            windows.append({'hwnd':hex(hwnd),'tid':wt,'title':title.value,'visible':bool(u.IsWindowVisible(hwnd)),'enabled':bool(u.IsWindowEnabled(hwnd)),
                'minimized':bool(u.IsIconic(hwnd)),'hung':bool(u.IsHungAppWindow(hwnd)),'width':rc.right,'height':rc.bottom})
        return True
    u.EnumWindows.argtypes=[CB,W.LPARAM];u.EnumWindows(enum,0)
    report={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pid':pid,'target_session':session(pid),'helper_session':session(k.GetCurrentProcessId()),
      'active_console_session':k.WTSGetActiveConsoleSessionId(),'helper_window_station':station,'helper_desktop':helper_desktop,'target_thread_desktop':target_desktop,
      'input_desktop':input_desktop,'foreground':hex(foreground) if foreground else None,'foreground_pid':fp.value if foreground else None,'owned_windows':windows,
      'scope':'Read-only Win32 desktop/window/session facts, no focus or gameplay changes'}
    dest=run/('window_state_'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')+'.json');dest.write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report))

if __name__=='__main__':main()
