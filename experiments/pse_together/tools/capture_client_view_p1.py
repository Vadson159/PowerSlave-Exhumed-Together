"""Ordinary bounded client mouse-look, then the accepted owned-window capture."""
import ctypes as C
from ctypes import wintypes as W
import json
import time
import runtime_p1 as p

dy=int(__import__('sys').argv[1])
dx=int(__import__('sys').argv[2]) if len(__import__('sys').argv)>2 else 0
assert abs(dy)<=400 and abs(dx)<=400
p.inputs(['focus','client'])
runner=p.accepted_runner('client');name=p.RUNS['client'];run,launch=runner.owned_run(name)
u=C.WinDLL('user32',use_last_error=True)
u.GetForegroundWindow.restype=W.HWND
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.mouse_event.argtypes=[W.DWORD,W.DWORD,W.DWORD,W.DWORD,C.c_size_t]
events=[]
for _ in range(4):
    pid=W.DWORD();u.GetWindowThreadProcessId(u.GetForegroundWindow(),C.byref(pid))
    assert pid.value==launch['pid'],'Owned client foreground required'
    u.mouse_event(1,(dx//4)&0xffffffff,(dy//4)&0xffffffff,0,0)
    events.append(dict(pid=pid.value,dx=dx//4,dy=dy//4,utc=p.stamp()))
    time.sleep(.04)
time.sleep(.2)
p.dump(run/('VIEW_ALIGNMENT_'+p.stamp()+'.json'),dict(events=events,method='ordinary OS mouse-look',game_memory_writes=0))
runner.action(name,'ui',['capture'])
