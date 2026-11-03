"""Capture/input only a verified workspace test process window, never the full desktop."""
import argparse,ctypes,datetime,json,time
from ctypes import wintypes as W
from pathlib import Path
from PIL import Image
from prepare import ROOT,OUT

def main():
    p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('action',choices=['capture','key','focus','activate','close']);p.add_argument('--key',default='RETURN');a=p.parse_args()
    run=OUT/a.run;d=json.loads((run/'launch.json').read_text());pid=d['pid']
    k=ctypes.WinDLL('kernel32',use_last_error=True);u=ctypes.WinDLL('user32',use_last_error=True);g=ctypes.WinDLL('gdi32',use_last_error=True)
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    h=k.OpenProcess(0x1000,False,pid);buf=ctypes.create_unicode_buffer(32768);n=W.DWORD(len(buf))
    k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,ctypes.POINTER(W.DWORD)]
    if not h or not k.QueryFullProcessImageNameW(h,0,buf,ctypes.byref(n)):raise ctypes.WinError(ctypes.get_last_error())
    k.CloseHandle(h)
    if Path(buf.value).resolve()!=Path(d['target']).resolve() or not Path(buf.value).resolve().is_relative_to(ROOT):raise RuntimeError('Window owner target mismatch')
    u.GetWindowThreadProcessId.argtypes=[W.HWND,ctypes.POINTER(W.DWORD)];u.GetClientRect.argtypes=[W.HWND,ctypes.POINTER(W.RECT)]
    u.IsWindowVisible.argtypes=[W.HWND];u.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,ctypes.c_int]
    candidates=[];CB=ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    @CB
    def enum(hwnd,lp):
        wp=W.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(wp))
        if wp.value==pid and u.IsWindowVisible(hwnd):
            rc=W.RECT();u.GetClientRect(hwnd,ctypes.byref(rc));title=ctypes.create_unicode_buffer(512);u.GetWindowTextW(hwnd,title,512)
            candidates.append((hwnd,rc.right,rc.bottom,title.value))
        return True
    u.EnumWindows.argtypes=[CB,W.LPARAM];u.EnumWindows(enum,0)
    if not candidates:raise RuntimeError('No visible window owned by test process')
    hwnd,width,height,title=max(candidates,key=lambda x:x[1]*x[2]);stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    record={'pid':pid,'hwnd':hex(hwnd),'width':width,'height':height,'title':title,'action':a.action,'utc':stamp}
    if a.action=='focus':
        u.ShowWindow.argtypes=[W.HWND,ctypes.c_int];u.SetForegroundWindow.argtypes=[W.HWND];u.GetForegroundWindow.restype=W.HWND
        u.ShowWindow(hwnd,9);ok=u.SetForegroundWindow(hwnd)
        fg=u.GetForegroundWindow();record.update(set_foreground_result=bool(ok),foreground=hex(fg) if fg else None)
    elif a.action=='activate':
        u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
        record.update(method='Synthetic WM_ACTIVATEAPP/WM_ACTIVATE/WM_SETFOCUS to validated owned HWND; does not prove actual foreground',
          results=[bool(u.PostMessageW(hwnd,m,wp,0)) for m,wp in [(0x1c,1),(0x6,1),(0x7,0)]])
    elif a.action=='close':
        u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
        record.update(post_close_result=bool(u.PostMessageW(hwnd,0x10,0,0)),method='WM_CLOSE to selected game HWND; native shutdown requested')
    elif a.action=='key':
        keys={'RETURN':13,'DOWN':40,'UP':38,'LEFT':37,'RIGHT':39,'ESC':27,'SPACE':32,'GRAVE':192,'W':87,'S':83,'A':65,'D':68,'E':69,'F1':112}
        vk=keys[a.key];u.MapVirtualKeyW.argtypes=[W.UINT,W.UINT];scan=u.MapVirtualKeyW(vk,0)
        u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
        down=u.PostMessageW(hwnd,0x100,vk,1|(scan<<16));time.sleep(.08)
        up=u.PostMessageW(hwnd,0x101,vk,1|(scan<<16)|(1<<30)|(1<<31));record.update(key=a.key,key_down_result=bool(down),key_up_result=bool(up),method='PostMessageW to selected game HWND')
    else:
        u.GetDC.argtypes=[W.HWND];u.GetDC.restype=W.HDC;u.ReleaseDC.argtypes=[W.HWND,W.HDC]
        g.CreateCompatibleDC.argtypes=[W.HDC];g.CreateCompatibleDC.restype=W.HDC
        g.CreateCompatibleBitmap.argtypes=[W.HDC,ctypes.c_int,ctypes.c_int];g.CreateCompatibleBitmap.restype=W.HBITMAP
        g.SelectObject.argtypes=[W.HDC,W.HGDIOBJ];g.SelectObject.restype=W.HGDIOBJ
        g.DeleteObject.argtypes=[W.HGDIOBJ];g.DeleteDC.argtypes=[W.HDC]
        dc=u.GetDC(hwnd);mem=g.CreateCompatibleDC(dc);bitmap=g.CreateCompatibleBitmap(dc,width,height);old=g.SelectObject(mem,bitmap)
        try:
            u.PrintWindow.argtypes=[W.HWND,W.HDC,W.UINT]
            ok=u.PrintWindow(hwnd,mem,3)
            class BIH(ctypes.Structure):
                _fields_=[('size',W.DWORD),('width',W.LONG),('height',W.LONG),('planes',W.WORD),('bits',W.WORD),('compression',W.DWORD),('image',W.DWORD),('xppm',W.LONG),('yppm',W.LONG),('used',W.DWORD),('important',W.DWORD)]
            info=BIH();info.size=ctypes.sizeof(info);info.width=width;info.height=-height;info.planes=1;info.bits=32
            pixels=ctypes.create_string_buffer(width*height*4);g.GetDIBits.argtypes=[W.HDC,W.HBITMAP,W.UINT,W.UINT,ctypes.c_void_p,ctypes.c_void_p,W.UINT]
            rows=g.GetDIBits(mem,bitmap,0,height,pixels,ctypes.byref(info),0)
            if rows!=height:raise ctypes.WinError(ctypes.get_last_error())
            image=Image.frombytes('RGB',(width,height),pixels.raw,'raw','BGRX');dest=run/f'window_{stamp}.png';image.save(dest)
            record.update(path=str(dest),printwindow_result=bool(ok),extrema=image.getextrema())
        finally:g.SelectObject(mem,old);g.DeleteObject(bitmap);g.DeleteDC(mem);u.ReleaseDC(hwnd,dc)
    (run/f'ui_{stamp}.json').write_text(json.dumps(record,indent=2),encoding='utf8');print(json.dumps(record))

if __name__=='__main__':main()
