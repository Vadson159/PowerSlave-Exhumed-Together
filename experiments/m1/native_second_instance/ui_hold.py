"""Bounded synthetic Windows input to the verified CLIENT window, no KEX access."""
from pathlib import Path
import ctypes,datetime,json,sys,time
from ctypes import wintypes as W
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1'
def main():
    name=sys.argv[1];assert name=='N51_CLIENT_MAIN_01'
    tag=sys.argv[2] if len(sys.argv)>2 else ''
    assert tag in ['', 'ACTIVE_HOST']
    run=OUT/name;d=json.loads((run/'launch.json').read_text(encoding='utf8'))
    k=ctypes.WinDLL('kernel32',use_last_error=True);u=ctypes.WinDLL('user32',use_last_error=True)
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,ctypes.POINTER(W.DWORD)]
    h=k.OpenProcess(0x1000,False,d['pid']);buf=ctypes.create_unicode_buffer(32768);n=W.DWORD(len(buf))
    assert h and k.QueryFullProcessImageNameW(h,0,buf,ctypes.byref(n));k.CloseHandle(h)
    assert Path(buf.value).resolve()==Path(d['target']).resolve() and Path(buf.value).resolve().is_relative_to(LAB/'game')
    u.GetWindowThreadProcessId.argtypes=[W.HWND,ctypes.POINTER(W.DWORD)];u.IsWindowVisible.argtypes=[W.HWND];u.GetClientRect.argtypes=[W.HWND,ctypes.POINTER(W.RECT)]
    CB=ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM);windows=[]
    @CB
    def enum(hwnd,lp):
        owner=W.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==d['pid'] and u.IsWindowVisible(hwnd):
            rc=W.RECT();u.GetClientRect(hwnd,ctypes.byref(rc));windows.append((rc.right*rc.bottom,hwnd))
        return True
    u.EnumWindows.argtypes=[CB,W.LPARAM];u.EnumWindows(enum,0);assert windows
    hwnd=max(windows)[1];u.ShowWindow.argtypes=[W.HWND,ctypes.c_int];u.SetForegroundWindow.argtypes=[W.HWND];u.GetForegroundWindow.restype=W.HWND
    if u.GetForegroundWindow()!=hwnd:
        u.ShowWindow(hwnd,9);u.SetForegroundWindow(hwnd);time.sleep(.2)
    assert u.GetForegroundWindow()==hwnd,'Actual CLIENT foreground required for this test'
    u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.MapVirtualKeyW.argtypes=[W.UINT,W.UINT]
    records=[]
    def record(event,**data):records.append(dict(event=event,pid=d['pid'],hwnd=hex(hwnd),utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source='automated PostMessageW Windows window-input test; not physical keyboard',**data))
    record('window_input_begin',actual_foreground=True)
    # A short repeated keydown models Windows held-key repeat. Native held
    # counters, rather than the posted events alone, are the acceptance source.
    for key in ['W','S']:
        vk=ord(key);scan=u.MapVirtualKeyW(vk,0);record('keydown_begin',key=key)
        try:
            assert u.PostMessageW(hwnd,0x100,vk,1|(scan<<16));time.sleep(.25)
            for i in range(7):
                assert u.GetForegroundWindow()==hwnd
                assert u.PostMessageW(hwnd,0x100,vk,1|(scan<<16)|(1<<30));time.sleep(.15)
        finally:
            ok=u.PostMessageW(hwnd,0x101,vk,1|(scan<<16)|(1<<30)|(1<<31));record('keyup',key=key,post_success=bool(ok))
        time.sleep(1.8)
    record('window_input_end')
    path=run/('WINDOW_INPUT'+('_'+tag if tag else '')+'.json');assert not path.exists();path.write_text(json.dumps(records,indent=2),encoding='utf8')
    print(json.dumps(dict(pid=d['pid'],events=len(records),path=str(path))))
if __name__=='__main__':main()
