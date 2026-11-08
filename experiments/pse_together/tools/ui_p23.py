"""Ordinary owned-window input/capture for P23; run via accepted REA Python lane.
No Frida, injection, game-memory access, or synthetic focus/activation messages.
"""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import datetime
import json
import sys
import time
import types
import runtime_p23 as wrapper
from focus_p1 import titlebar_focus

r=wrapper.r

def main():
    role,action=sys.argv[1:3]
    if role not in r.RUNS:raise ValueError(role)
    r.verify_artifacts()
    name=r.RUNS[role]
    # Reuse the accepted PID/path/HWND owner validator and largest owned window.
    source=(r.ROOT/'experiments/m1/background_host/ui53.py').read_text('utf8')
    source=source.replace('from audit import ROOT, OUT, dump, stamp','from runtime_p4 import ROOT, OUT, dump, stamp')
    source=source.replace("ROOT/'docs/evidence/m1'/('N53_'+role.upper()+'_MAIN_01')","OUT/"+repr(r.RUNS)+"[role]")
    ui=types.ModuleType('p23_accepted_window');exec(compile(source,'accepted_ui53_p23','exec'),ui.__dict__)
    d,h=ui.window(role);records=[]
    if action=='capture':
        m=r.accepted_runner(role);m.action(name,'ui',['capture']);return
    ui.u.ShowWindow(h,9);ui.u.SetForegroundWindow(h);time.sleep(.1)
    if ui.foreground()['pid']!=d['pid']:titlebar_focus(d,h,records)
    if ui.foreground()['pid']!=d['pid']:raise RuntimeError('Actual owned foreground required')
    def key(vk,down):
        scan=ui.u.MapVirtualKeyW(vk,0);lp=1|(scan<<16)|((1<<30)|(1<<31) if not down else 0)
        if not ui.u.PostMessageW(h,0x100 if down else 0x101,vk,lp):raise C.WinError()
        records.append(dict(event='key_down' if down else 'key_up',vk=vk))
    if action=='key':
        keys={'F8':119,'T':84,'TAB':9,'RETURN':13,'ESC':27,'BACKSLASH':220,'MAP':9,'W':87,'A':65,'D':68,'S':83,'UP':38,'DOWN':40,'LEFT':37,'RIGHT':39}
        vk=keys[sys.argv[3]];key(vk,True);time.sleep(.06);key(vk,False)
    elif action=='text':
        text=sys.argv[3]
        if len(text)>100 or any(ord(c)<32 for c in text):raise ValueError('Bounded plain text only')
        for ch in text:
            if ui.foreground()['pid']!=d['pid']:raise RuntimeError('Focus changed during text')
            # WM_CHAR is ordinary Unicode window input, consumed by SDL_TEXTINPUT.
            for i in range(0,len(ch.encode('utf-16-le')),2):
                unit=int.from_bytes(ch.encode('utf-16-le')[i:i+2],'little')
                if not ui.u.PostMessageW(h,0x102,unit,1):raise C.WinError()
            time.sleep(.015)
        records.append(dict(event='unicode_window_text',text=text))
    elif action in ['hold','mouse']:
        duration=float(sys.argv[3]);assert 0<duration<=.4
        if action=='hold':
            vk=ord(sys.argv[4]);assert chr(vk) in 'WASD';key(vk,True)
            try:time.sleep(duration)
            finally:key(vk,False)
        else:
            ui.u.PostMessageW(h,0x201,1,0);time.sleep(duration);ui.u.PostMessageW(h,0x202,0,0)
            records.append(dict(event='owned_mouse_down_up',duration=duration))
    else:raise ValueError(action)
    time.sleep(.12)
    result=dict(role=role,action=action,pid=d['pid'],hwnd=hex(h),records=records,foreground=ui.foreground(),game_memory_writes=0,kex_calls=0)
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    (r.OUT/name/('p23_input_'+stamp+'.json')).write_text(json.dumps(result,indent=2,ensure_ascii=False),'utf8')
    print(json.dumps(result,ensure_ascii=False))

if __name__=='__main__':main()
