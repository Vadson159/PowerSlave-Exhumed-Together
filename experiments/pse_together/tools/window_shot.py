import ctypes,sys
from ctypes import wintypes as W
from PIL import Image
u=ctypes.WinDLL('user32',use_last_error=True);g=ctypes.WinDLL('gdi32')
pid=int(sys.argv[1]);out=sys.argv[2] if len(sys.argv)>2 else None
wins=[]
@ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
def cb(h,l):
    p=W.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(p))
    if p.value==pid:
        buf=ctypes.create_unicode_buffer(256);u.GetWindowTextW(h,buf,256);r=W.RECT();u.GetWindowRect(h,ctypes.byref(r))
        wins.append((h,buf.value,u.IsWindowVisible(h),(r.left,r.top,r.right,r.bottom)))
    return True
u.EnumWindows(cb,0)
for w in wins:print(hex(w[0]),repr(w[1]),'visible' if w[2] else 'hidden',w[3])
if out and wins:
    best=max(wins,key=lambda w:(w[2],(w[3][2]-w[3][0])*(w[3][3]-w[3][1])))
    h=best[0];r=best[3];w_,h_=r[2]-r[0],r[3]-r[1]
    if w_>0 and h_>0:
        dc=u.GetWindowDC(h);mdc=g.CreateCompatibleDC(dc);bm=g.CreateCompatibleBitmap(dc,w_,h_);g.SelectObject(mdc,bm)
        u.PrintWindow(h,mdc,2)
        class BI(ctypes.Structure):_fields_=[('s',W.DWORD),('w',W.LONG),('h',W.LONG),('p',W.WORD),('b',W.WORD),('c',W.DWORD),('si',W.DWORD),('x',W.LONG),('y',W.LONG),('cu',W.DWORD),('ci',W.DWORD)]
        bi=BI(40,w_,-h_,1,32,0,0,0,0,0,0);data=ctypes.create_string_buffer(w_*h_*4)
        g.GetDIBits(mdc,bm,0,h_,data,ctypes.byref(bi),0)
        Image.frombuffer('RGBA',(w_,h_),data,'raw','BGRA',0,1).convert('RGB').save(out);print('saved',out,w_,h_)
