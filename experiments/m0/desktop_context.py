"""Read own Win32 desktop context, for comparing two authorized launch providers."""
import argparse,ctypes,datetime,json,os
from ctypes import wintypes as W
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];out=Path(a.output).resolve()
    if not out.is_relative_to(root):raise ValueError('Output outside workspace')
    u=ctypes.WinDLL('user32',use_last_error=True);k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.GetCurrentThreadId.restype=W.DWORD;k.ProcessIdToSessionId.argtypes=[W.DWORD,ctypes.POINTER(W.DWORD)]
    u.GetProcessWindowStation.restype=W.HANDLE;u.GetThreadDesktop.argtypes=[W.DWORD];u.GetThreadDesktop.restype=W.HANDLE
    u.GetUserObjectInformationW.argtypes=[W.HANDLE,ctypes.c_int,ctypes.c_void_p,W.DWORD,ctypes.POINTER(W.DWORD)]
    u.OpenInputDesktop.argtypes=[W.DWORD,W.BOOL,W.DWORD];u.OpenInputDesktop.restype=W.HANDLE;u.CloseDesktop.argtypes=[W.HANDLE]
    def name(h):
        b=ctypes.create_unicode_buffer(512);n=W.DWORD();ok=bool(h) and u.GetUserObjectInformationW(h,2,b,ctypes.sizeof(b),ctypes.byref(n))
        return {'name':b.value if ok else None,'error':0 if ok else ctypes.get_last_error()}
    station=name(u.GetProcessWindowStation());desktop=name(u.GetThreadDesktop(k.GetCurrentThreadId()))
    ih=u.OpenInputDesktop(0,False,1);active=name(ih)
    if ih:u.CloseDesktop(ih)
    sid=W.DWORD();ok=k.ProcessIdToSessionId(os.getpid(),ctypes.byref(sid))
    r={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pid':os.getpid(),'session':sid.value if ok else None,
       'station':station,'desktop':desktop,'input_desktop':active,'scope':'Read-only own-process environment; no desktop switch, no game/Steam operations'}
    out.write_text(json.dumps(r,indent=2),encoding='utf8')

if __name__=='__main__':main()
