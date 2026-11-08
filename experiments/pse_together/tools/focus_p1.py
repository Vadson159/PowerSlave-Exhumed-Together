"""Actual foreground fallback for P1: one verified owned-window title-bar click.

No synthetic activation messages, game-memory writes or calls into the game.
Only used after the accepted foreground request fails. Never click through another window.
"""
import ctypes as C
from ctypes import wintypes as W
import time

def titlebar_focus(document, hwnd, records):
    u=C.WinDLL('user32',use_last_error=True)
    u.GetWindowLongPtrW.argtypes=[W.HWND,C.c_int];u.GetWindowLongPtrW.restype=C.c_ssize_t
    u.GetWindowRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
    u.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
    u.WindowFromPoint.argtypes=[W.POINT];u.WindowFromPoint.restype=W.HWND
    u.GetAncestor.argtypes=[W.HWND,W.UINT];u.GetAncestor.restype=W.HWND
    u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    u.GetForegroundWindow.restype=W.HWND
    u.GetCursorPos.argtypes=[C.POINTER(W.POINT)]
    u.mouse_event.argtypes=[W.DWORD,W.DWORD,W.DWORD,W.DWORD,C.c_size_t]
    u.SendMessageTimeoutW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
    u.SendMessageTimeoutW.restype=C.c_size_t
    pid=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(pid))
    if pid.value!=document['pid']: raise RuntimeError('Owned titlebar PID changed')
    if u.GetWindowLongPtrW(hwnd,-16)&0x00c00000!=0x00c00000: raise RuntimeError('No verified caption; refuse click')
    rect=W.RECT();assert u.GetWindowRect(hwnd,C.byref(rect))
    assert u.SetWindowPos(hwnd,0,0,0,0,0,0x13) # TOP, no size/move/activation
    time.sleep(.1)
    point=W.POINT((rect.left+rect.right)//2,rect.top+14)
    actual=u.WindowFromPoint(point);root=u.GetAncestor(actual,2)
    check=W.DWORD();u.GetWindowThreadProcessId(root,C.byref(check))
    if root!=hwnd or check.value!=document['pid']: raise RuntimeError('Caption point covered; refuse click')
    old=W.POINT();assert u.GetCursorPos(C.byref(old))
    assert u.SetCursorPos(point.x,point.y)
    try:
        # Re-check after moving the cursor and before either physical mouse event.
        current=u.GetAncestor(u.WindowFromPoint(point),2)
        check=W.DWORD();u.GetWindowThreadProcessId(current,C.byref(check))
        if current!=hwnd or check.value!=document['pid']: raise RuntimeError('Caption no longer owned')
        hit=C.c_size_t();position=(point.x&0xffff)|((point.y&0xffff)<<16)
        if not u.SendMessageTimeoutW(hwnd,0x84,0,position,2,1000,C.byref(hit)) or hit.value!=2:
            raise RuntimeError('Point is not native HTCAPTION; refuse click')
        u.mouse_event(2,0,0,0,0)
        try: time.sleep(.03)
        finally: u.mouse_event(4,0,0,0,0)
        time.sleep(.1)
    finally: u.SetCursorPos(old.x,old.y)
    foreground=u.GetForegroundWindow();observed=W.DWORD();u.GetWindowThreadProcessId(foreground,C.byref(observed))
    records.append(dict(event='owned_caption_click_focus',pid=document['pid'],hwnd=hex(hwnd),
        point=[point.x,point.y],foreground_pid=observed.value,caption_verified=True,synthetic_focus_messages=0,game_writes=0))
    return observed.value==document['pid']
