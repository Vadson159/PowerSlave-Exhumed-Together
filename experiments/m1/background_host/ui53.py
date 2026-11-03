"""Owned-window real foreground changes and posted keys/mouse only. No focus messages."""
from pathlib import Path
import ctypes as C, datetime, json, sys, time, traceback
from ctypes import wintypes as W
from audit import ROOT, OUT, dump, stamp
u=C.WinDLL('user32',use_last_error=True);k=C.WinDLL('kernel32',use_last_error=True)
u.GetForegroundWindow.restype=W.HWND;u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.SetForegroundWindow.argtypes=[W.HWND];u.ShowWindow.argtypes=[W.HWND,C.c_int]
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.MapVirtualKeyW.argtypes=[W.UINT,W.UINT]
def foreground():
    h=u.GetForegroundWindow();p=W.DWORD();u.GetWindowThreadProcessId(h,C.byref(p));return dict(hwnd=hex(h or 0),pid=p.value)
def window(role):
    d=json.loads((ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')/'launch.json').read_text(encoding='utf8'))
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
    h=k.OpenProcess(0x1000,False,d['pid']);b=C.create_unicode_buffer(32768);n=W.DWORD(len(b))
    k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
    assert h and k.QueryFullProcessImageNameW(h,0,b,C.byref(n));k.CloseHandle(h)
    assert Path(b.value).resolve()==Path(d['target']).resolve() and Path(b.value).resolve().is_relative_to(ROOT)
    CB=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM);ws=[]
    u.IsWindowVisible.argtypes=[W.HWND];u.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
    @CB
    def enum(hwnd,unused):
        p=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(p))
        if p.value==d['pid'] and u.IsWindowVisible(hwnd):
            r=W.RECT();u.GetClientRect(hwnd,C.byref(r));ws.append((r.right*r.bottom,hwnd))
        return True
    u.EnumWindows.argtypes=[CB,W.LPARAM];u.EnumWindows(enum,0);assert ws
    return d,max(ws)[1]
def main():
    action=sys.argv[1];tag=sys.argv[2];assert tag.replace('_','').isalnum()
    rows=[]
    def record(e,**kw):rows.append(dict(event=e,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),foreground=foreground(),**kw))
    def key(hwnd,key,down,repeat=False):
        vk=ord(key);scan=u.MapVirtualKeyW(vk,0);lp=1|(scan<<16)|((1<<30) if repeat or not down else 0)|((1<<31) if not down else 0)
        assert u.PostMessageW(hwnd,0x100 if down else 0x101,vk,lp)
        record('posted_key',hwnd=hex(hwnd),key=key,down=down,repeat=repeat)
    def focus(role):
        d,h=window(role);u.ShowWindow(h,9);ok=u.SetForegroundWindow(h);time.sleep(.25)
        record('real_foreground_request',role=role,target_pid=d['pid'],return_value=bool(ok));assert foreground()['pid']==d['pid'],'Actual foreground required'
        return d,h
    try:
        if action=='focus':focus(sys.argv[3])
        elif action=='client_inputs':
            d,h=window('client');assert foreground()['pid']==d['pid']
            for ch in ['W','S']:
                key(h,ch,True)
                try:
                    time.sleep(.25)
                    for n in range(7):
                        assert foreground()['pid']==d['pid'];key(h,ch,True,True);time.sleep(.15)
                finally:key(h,ch,False)
                record('release_begin',key=ch);time.sleep(2.2)
            record('neutral_tail');time.sleep(2)
        elif action=='host_loss_inputs':
            d,h=window('host');assert foreground()['pid']==d['pid']
            ch=sys.argv[3];assert ch in ['W','S']
            key(h,ch,True);assert u.PostMessageW(h,0x201,1,0);record('posted_attack_down',hwnd=hex(h))
            try:
                time.sleep(.25);focus('client')
                for n in range(12):record('background_hold_observation',host_pid=d['pid']);time.sleep(.25)
            finally:
                key(h,ch,False);assert u.PostMessageW(h,0x202,0,0);record('posted_attack_up',hwnd=hex(h))
        elif action=='host_normal_input':
            d,h=window('host');assert foreground()['pid']==d['pid'];ch=sys.argv[3];assert ch in ['W','S']
            key(h,ch,True)
            try:
                for n in range(12):assert foreground()['pid']==d['pid'];key(h,ch,True,True);time.sleep(.15)
            finally:key(h,ch,False)
            time.sleep(1)
        else:raise ValueError(action)
        result=dict(status='COMPLETED',records=rows,synthetic_focus_messages=0,kex_calls=0,source='posted Windows test input, not physical keyboard')
    except BaseException as e:
        traceback.print_exc();result=dict(status='FAILED',error=str(e),records=rows,synthetic_focus_messages=0)
    dump(OUT/('UI_'+tag+'_'+stamp()+'.json'),result);assert result['status']=='COMPLETED'
if __name__=='__main__':main()
