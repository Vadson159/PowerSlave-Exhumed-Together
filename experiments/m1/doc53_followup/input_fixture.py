"""Bounded SendInput fixture on validated owned foreground windows; no fake focus."""
import ctypes as C,datetime,json,sys,time,traceback
from ctypes import wintypes as W
from workflow import ROOT,OUT,dump,stamp
u=C.WinDLL('user32',use_last_error=True);k=C.WinDLL('kernel32',use_last_error=True)
u.GetForegroundWindow.restype=W.HWND
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.SetForegroundWindow.argtypes=[W.HWND];u.SetForegroundWindow.restype=W.BOOL
u.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL];u.AttachThreadInput.restype=W.BOOL
u.ShowWindow.argtypes=[W.HWND,C.c_int];u.IsWindowVisible.argtypes=[W.HWND]
k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
k.CloseHandle.argtypes=[W.HANDLE];k.GetCurrentThreadId.restype=W.DWORD
class MI(C.Structure):_fields_=[('dx',W.LONG),('dy',W.LONG),('mouseData',W.DWORD),('flags',W.DWORD),('time',W.DWORD),('extra',C.c_size_t)]
class KI(C.Structure):_fields_=[('vk',W.WORD),('scan',W.WORD),('flags',W.DWORD),('time',W.DWORD),('extra',C.c_size_t)]
class HI(C.Structure):_fields_=[('msg',W.DWORD),('low',W.WORD),('high',W.WORD)]
class IU(C.Union):_fields_=[('mouse',MI),('key',KI),('hardware',HI)]
class INPUT(C.Structure):_fields_=[('type',W.DWORD),('data',IU)]
assert C.sizeof(INPUT)==40
u.SendInput.argtypes=[W.UINT,C.POINTER(INPUT),C.c_int];u.SendInput.restype=W.UINT
u.MapVirtualKeyW.argtypes=[W.UINT,W.UINT]
def foreground():
    hwnd=u.GetForegroundWindow();pid=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(pid));return dict(hwnd=hex(hwnd or 0),pid=pid.value)
def window(name):
    d=json.loads((ROOT/'docs/evidence/m1'/name/'launch.json').read_text(encoding='utf8'))
    h=k.OpenProcess(0x1000,False,d['pid']);buf=C.create_unicode_buffer(32768);n=W.DWORD(len(buf))
    try:assert h and k.QueryFullProcessImageNameW(h,0,buf,C.byref(n));assert Path(buf.value).resolve()==Path(d['target']).resolve() and Path(buf.value).resolve().is_relative_to(ROOT)
    finally:
        if h:k.CloseHandle(h)
    CB=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM);windows=[]
    @CB
    def enum(hwnd,unused):
        pid=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(pid))
        if pid.value==d['pid'] and u.IsWindowVisible(hwnd):
            rect=W.RECT();u.GetClientRect(hwnd,C.byref(rect));windows.append((rect.right*rect.bottom,hwnd))
        return True
    u.EnumWindows.argtypes=[CB,W.LPARAM];u.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)];u.EnumWindows(enum,0)
    assert windows;return d,max(windows)[1]
from pathlib import Path
def main():
    action,hostName,clientName=sys.argv[1:4];names={'host':hostName,'client':clientName};rows=[]
    def record(event,**kw):rows.append(dict(event=event,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),foreground=foreground(),**kw))
    def focus(role):
        d,h=window(names[role]);before=foreground();u.ShowWindow(h,9);ok=bool(u.SetForegroundWindow(h));joined=[]
        if not ok:
            helper=k.GetCurrentThreadId();unused=W.DWORD()
            try:
                for tid in dict.fromkeys([u.GetWindowThreadProcessId(u.GetForegroundWindow(),C.byref(unused)),u.GetWindowThreadProcessId(h,C.byref(unused))]):
                    if tid and tid!=helper and u.AttachThreadInput(helper,tid,True):joined.append(tid)
                ok=bool(u.SetForegroundWindow(h))
            finally:
                for tid in reversed(joined):assert u.AttachThreadInput(helper,tid,False)
        time.sleep(.25);record('real_foreground',role=role,before=before,result=ok,joined_then_detached=joined)
        assert foreground()['pid']==d['pid'];return d,h
    def key(ch,down):
        x=INPUT();x.type=1;x.data.key=KI(0,u.MapVirtualKeyW(ord(ch),0),8|(0 if down else 2),0,0)
        assert u.SendInput(1,C.byref(x),C.sizeof(x))==1,C.get_last_error();record('SendInput_key',key=ch,down=down)
    def move(dx,dy):
        x=INPUT();x.type=0;x.data.mouse=MI(dx,dy,0,1,0,0)
        assert u.SendInput(1,C.byref(x),C.sizeof(x))==1,C.get_last_error();record('SendInput_relative_mouse',dx=dx,dy=dy)
    def button(down):
        x=INPUT();x.type=0;x.data.mouse=MI(0,0,0,2 if down else 4,0,0)
        assert u.SendInput(1,C.byref(x),C.sizeof(x))==1,C.get_last_error();record('SendInput_left_button',down=down)
    def inputs():
        d,h=window(clientName);assert foreground()['pid']==d['pid']
        for ch in ['W','S']:
            key(ch,True)
            try:
                for _ in range(7):assert foreground()['pid']==d['pid'];time.sleep(.1)
            finally:key(ch,False)
            time.sleep(.7)
    def native_tail():
        p=ROOT/'docs/evidence/m1'/hostName/'native.jsonl'
        with p.open('rb') as f:f.seek(max(0,p.stat().st_size-250000));lines=f.read().splitlines()[1:]
        result=[]
        for line in lines:
            try:result.append(json.loads(line))
            except ValueError:pass
        return result
    try:
        if action=='focus':focus(sys.argv[4])
        elif action=='client_inputs':focus('client');inputs()
        elif action in ['attack','attack_mouse','mouse']:
            d,h=focus('host');start=datetime.datetime.now(datetime.timezone.utc).isoformat()
            if action.startswith('attack'):
                fire=button if action=='attack_mouse' else lambda down:key('K',down)
                fire(True)
                try:
                    time.sleep(.45)
                    positive=[r for r in native_tail() if r.get('event')=='native_primary_input' and r['utc']>=start and r.get('held_attack',0)>0 and r.get('buttons',0)&1 and not r.get('neutral')]
                    record('positive_native_precondition',kind=action,samples=positive);assert positive,'No positive native Attack'
                    focus('client');time.sleep(.35)
                finally:fire(False)
            else:
                for _ in range(20):assert foreground()['pid']==d['pid'];move(8,3);time.sleep(.02)
                positive=[r for r in native_tail() if r.get('event')=='native_primary_input' and r['utc']>=start and (r.get('mouse_x',0) or r.get('mouse_y',0)) and (r.get('yaw_delta',0) or r.get('pitch_delta',0)) and not r.get('neutral')]
                record('positive_native_precondition',kind=action,samples=positive);assert positive,'No positive native mouse sample'
                focus('client')
                for _ in range(8):move(5,-2);time.sleep(.025)
            inputs();time.sleep(.5);focus('host');record('gain_observation_begin');time.sleep(1)
            record('gain_native_tail',samples=[r for r in native_tail() if r.get('event')=='native_primary_input'][-70:])
        else:raise ValueError(action)
        result=dict(status='COMPLETED',action=action,records=rows,synthetic_activation_messages=0,game_memory_writes=0)
    except BaseException as e:
        traceback.print_exc();result=dict(status='FAILED',action=action,error=str(e),records=rows,synthetic_activation_messages=0)
    dump(OUT/('INPUT_'+action+'_'+stamp()+'.json'),result);assert result['status']=='COMPLETED'
if __name__=='__main__':main()
