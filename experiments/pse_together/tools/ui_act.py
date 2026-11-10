import ctypes,sys,time
from ctypes import wintypes as W
u=ctypes.WinDLL('user32',use_last_error=True);k=ctypes.WinDLL('kernel32')
VK={'F8':0x77,'RETURN':0x0D,'ESC':0x1B,'UP':0x26,'DOWN':0x28,'LEFT':0x25,'RIGHT':0x27,'TAB':0x09,'BACKSPACE':0x08,'SPACE':0x20,'BACKSLASH':0xDC,'T':0x54,'W':0x57,'A':0x41,'S':0x53,'D':0x44,'E':0x45,'Y':0x59,'N':0x4E,'CONTROL':0x11,'F':0x46}
for c in '0123456789':VK[c]=ord(c)
def hwnd_of(pid):
    r=[]
    @ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    def cb(h,l):
        p=W.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(p))
        if p.value==pid and u.IsWindowVisible(h):
            b=ctypes.create_unicode_buffer(256);u.GetWindowTextW(h,b,256)
            if 'Powerslave' in b.value:r.append(h)
        return True
    u.EnumWindows(cb,0);return r[0] if r else None
def focus(h):
    fg=u.GetForegroundWindow();t1=u.GetWindowThreadProcessId(fg,None);t2=u.GetWindowThreadProcessId(h,None);me=k.GetCurrentThreadId()
    u.AttachThreadInput(me,t1,True);u.AttachThreadInput(me,t2,True)
    u.ShowWindow(h,9);u.BringWindowToTop(h);u.SetForegroundWindow(h);time.sleep(0.15)
    u.AttachThreadInput(me,t1,False);u.AttachThreadInput(me,t2,False)
    return u.GetForegroundWindow()==h
class KI(ctypes.Structure):_fields_=[('vk',W.WORD),('scan',W.WORD),('flags',W.DWORD),('time',W.DWORD),('extra',ctypes.c_size_t)]
class MI(ctypes.Structure):_fields_=[('dx',W.LONG),('dy',W.LONG),('data',W.DWORD),('flags',W.DWORD),('time',W.DWORD),('extra',ctypes.c_size_t)]
class UN(ctypes.Union):_fields_=[('ki',KI),('mi',MI)]
class IN(ctypes.Structure):_fields_=[('type',W.DWORD),('u',UN)]
def send(vk,down,ext=False):
    sc=u.MapVirtualKeyW(vk,0);fl=0x8|(0 if down else 2)|(1 if ext else 0)
    i=IN(1,UN(ki=KI(0,sc,fl,0,0)));u.SendInput(1,ctypes.byref(i),ctypes.sizeof(IN))
HW=[None]
def post(vk,down):
    sc=u.MapVirtualKeyW(vk,0);lp=1|(sc<<16)|(0 if down else ((1<<30)|(1<<31)))
    return u.PostMessageW(HW[0],0x100 if down else 0x101,vk,lp)
def mouse_move(dx,dy=0,steps=20):
    # Relative mouse motion (SDL raw input) delivered in small steps; the window must have the foreground.
    for _ in range(steps):
        i=IN(0,UN(mi=MI(int(dx/steps),int(dy/steps),0,0x0001,0,0)));u.SendInput(1,ctypes.byref(i),ctypes.sizeof(IN));time.sleep(0.01)
def cursor_to(h):
    # SendInput mouse buttons go to the window under the OS cursor: park it in the middle of the game window first.
    r=W.RECT();u.GetWindowRect(h,ctypes.byref(r));u.SetCursorPos((r.left+r.right)//2,(r.top+r.bottom)//2)
def fire(seconds):
    # Hold the left mouse button (continuous attack).
    for fl,wait in ((0x0002,seconds),(0x0004,0)):
        i=IN(0,UN(mi=MI(0,0,0,fl,0,0)));u.SendInput(1,ctypes.byref(i),ctypes.sizeof(IN));time.sleep(wait)
def click(hold=0.15):
    # Left mouse button (attack) through SendInput; the window must have the foreground.
    for fl in (0x0002,0x0004):
        i=IN(0,UN(mi=MI(0,0,0,fl,0,0)));u.SendInput(1,ctypes.byref(i),ctypes.sizeof(IN));time.sleep(hold)
def tap(vk,hold=0.06):
    post(vk,True);time.sleep(hold);post(vk,False)
def text(s):
    for ch in s:
        for i in range(0,len(ch.encode('utf-16-le')),2):
            u.PostMessageW(HW[0],0x102,int.from_bytes(ch.encode('utf-16-le')[i:i+2],'little'),1)
        time.sleep(0.02)
if __name__=='__main__':
    pid=int(sys.argv[1]);h=hwnd_of(pid)
    if not h:print('no window');sys.exit(1)
    HW[0]=h;ok=focus(h);print('foreground',ok)
    for a in sys.argv[2:]:
        if a.startswith('wait='):time.sleep(float(a[5:]));continue
        if a.startswith('hold:'):
            name,sec=a[5:].split('=');focus(h);post(VK[name],True);time.sleep(float(sec));post(VK[name],False);continue
        if a.startswith('text:'):focus(h);text(a[5:]);continue
        if a.startswith('mouse:'):focus(h);mouse_move(int(a[6:]));continue
        if not focus(h):print('lost focus before',a)
        tap(VK[a]);time.sleep(0.25)
