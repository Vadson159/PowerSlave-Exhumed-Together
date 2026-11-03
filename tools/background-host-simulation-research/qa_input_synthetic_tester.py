"""
Standalone QA Input Synthetic Tester: PostMessageW vs SendInput Analysis
Author: Senior C++ QA Automation Engineer / Windows Timing Analyst
Target: PowerSlave Exhumed Together (P2 Investigation)
"""

import ctypes
from ctypes import wintypes as W
import time

user32 = ctypes.WinDLL('user32', use_last_error=True)

# Define SendInput structures
class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", W.LONG),
        ("dy", W.LONG),
        ("mouseData", W.DWORD),
        ("dwFlags", W.DWORD),
        ("time", W.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    ]

class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", W.WORD),
        ("wScan", W.WORD),
        ("dwFlags", W.DWORD),
        ("time", W.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    ]

class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", W.DWORD),
        ("wParamL", W.WORD),
        ("wParamH", W.WORD),
    ]

class INPUT_UNION(ctypes.Union):
    _fields_ = [
        ("mi", MOUSEINPUT),
        ("ki", KEYBDINPUT),
        ("hi", HARDWAREINPUT),
    ]

class INPUT(ctypes.Structure):
    _fields_ = [
        ("type", W.DWORD),
        ("u", INPUT_UNION),
    ]

INPUT_MOUSE = 0
INPUT_KEYBOARD = 1

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010
MOUSEEVENTF_MIDDLEDOWN = 0x0020
MOUSEEVENTF_MIDDLEUP = 0x0040

user32.SendInput.argtypes = [W.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.SendInput.restype = W.UINT
user32.GetAsyncKeyState.argtypes = [ctypes.c_int]
user32.GetAsyncKeyState.restype = W.SHORT

def test_async_keystate_comparison():
    print("=" * 65)
    print("QA Input Tester: PostMessageW vs SendInput Hardware State")
    print("=" * 65)

    VK_LBUTTON = 0x01
    
    # Check baseline state
    baseline_state = user32.GetAsyncKeyState(VK_LBUTTON) & 0x8000
    print(f"[Initial] GetAsyncKeyState(VK_LBUTTON): {'DOWN' if baseline_state else 'UP'}")

    # 1. PostMessageW test
    print("\n[Phase 1] Simulating ui53.py approach: PostMessageW(hwnd, 0x201, 1, 0)...")
    desktop_hwnd = user32.GetDesktopWindow()
    user32.PostMessageW(desktop_hwnd, 0x201, 1, 0)
    time.sleep(0.05)
    post_state = user32.GetAsyncKeyState(VK_LBUTTON) & 0x8000
    print(f"  Result: GetAsyncKeyState(VK_LBUTTON) is {'DOWN' if post_state else 'UP'} (Expected: UP)")
    print("  Analysis: PostMessageW queues a message to the target window proc,")
    print("            but DOES NOT update OS hardware input table or Raw Input stream.")

    # 2. SendInput test
    print("\n[Phase 2] Simulating SendInput(MOUSEEVENTF_LEFTDOWN)...")
    inp = INPUT()
    inp.type = INPUT_MOUSE
    inp.u.mi.dwFlags = MOUSEEVENTF_LEFTDOWN
    sent = user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
    time.sleep(0.01)
    send_down_state = user32.GetAsyncKeyState(VK_LBUTTON) & 0x8000
    print(f"  SendInput count: {sent}")
    print(f"  Result: GetAsyncKeyState(VK_LBUTTON) is {'DOWN' if send_down_state else 'UP'} (Expected: DOWN)")

    # Release button
    inp_up = INPUT()
    inp_up.type = INPUT_MOUSE
    inp_up.u.mi.dwFlags = MOUSEEVENTF_LEFTUP
    user32.SendInput(1, ctypes.byref(inp_up), ctypes.sizeof(INPUT))
    time.sleep(0.01)
    send_up_state = user32.GetAsyncKeyState(VK_LBUTTON) & 0x8000
    print(f"  After MOUSEEVENTF_LEFTUP: GetAsyncKeyState(VK_LBUTTON) is {'DOWN' if send_up_state else 'UP'}")

    # 3. Mouse Delta Generation
    print("\n[Phase 3] Simulating Relative Mouse Delta via SendInput(MOUSEEVENTF_MOVE)...")
    move_inp = INPUT()
    move_inp.type = INPUT_MOUSE
    move_inp.u.mi.dx = 15  # +15 yaw delta
    move_inp.u.mi.dy = -10 # -10 pitch delta
    move_inp.u.mi.dwFlags = MOUSEEVENTF_MOVE
    sent_move = user32.SendInput(1, ctypes.byref(move_inp), ctypes.sizeof(INPUT))
    print(f"  SendInput MOUSEEVENTF_MOVE (dx=15, dy=-10) count: {sent_move}")
    print("  Analysis: SendInput generates genuine Raw Input (WM_INPUT) packets,")
    print("            which SDL2 relative mouse mode feeds directly into KEX yaw/pitch.")

    print("\n" + "=" * 65)

if __name__ == "__main__":
    test_async_keystate_comparison()
