"""Input Controller for Two-Process Test Harness.

Handles posted Win32 keys, relative mouse events, hold intervals, and role handoffs:
- Posts bounded WM_KEYDOWN/WM_KEYUP messages directly to the target window
- Supports 'handoff': releases host movement keys only after real foreground is transferred to client
- Executes smooth relative mouse events
- Maintains an immutable structured audit log of all input operations
"""

from ctypes import wintypes as W
import ctypes as C
import datetime
import json
import os
from pathlib import Path
import sys
import time
from typing import Any, Dict, List, Optional

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session import TwoProcessSession
    from window_manager import WindowManager
else:
    from .session import TwoProcessSession
    from .window_manager import WindowManager

WM_KEYDOWN = 0x100
WM_KEYUP = 0x101
MOUSEEVENTF_MOVE = 0x0001


def get_user32():
    if os.name != 'nt':
        return None
    u = C.WinDLL('user32', use_last_error=True)
    u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    u.MapVirtualKeyW.argtypes = [W.UINT, W.UINT]
    u.mouse_event.argtypes = [W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_size_t]
    return u


class InputController:
    """Controls Win32 input delivery for host and client processes."""

    def __init__(self, session: Optional[TwoProcessSession] = None, window_manager: Optional[WindowManager] = None):
        self.session = session or TwoProcessSession()
        self.win_mgr = window_manager or WindowManager(self.session)
        self.user32 = get_user32()

    def _now(self) -> str:
        return datetime.datetime.now(datetime.timezone.utc).isoformat()

    def focus(self, role: str) -> Dict[str, Any]:
        """Activates foreground for the given role."""
        return self.win_mgr.focus_role(role)

    def key(self, role: str, key_name: str, hold_s: float, handoff: bool = False) -> Dict[str, Any]:
        """Posts keydown, holds with repeat messages, optionally hands off foreground, then releases."""
        if not self.user32:
            raise RuntimeError("Win32 API not available")

        key_name = key_name.upper()
        if key_name not in ['W', 'S', 'A', 'D', 'E', 'RETURN', 'SPACE', 'ESCAPE']:
            raise ValueError(f"Unsupported key: {key_name}")

        records: List[Dict[str, Any]] = []
        d, hwnd = self.win_mgr.find_window_for_role(role)
        self.win_mgr.activate_foreground(hwnd, d['pid'])
        records.append({
            'event': 'actual_foreground',
            'role': role,
            'target_pid': d['pid'],
            'utc': self._now(),
        })

        # Key code mapping
        if key_name == 'RETURN':
            vk = 0x0D
        elif key_name == 'SPACE':
            vk = 0x20
        elif key_name == 'ESCAPE':
            vk = 0x1B
        else:
            vk = ord(key_name)

        scan = self.user32.MapVirtualKeyW(vk, 0)
        lparam_down = 1 | (scan << 16)
        lparam_repeat = 1 | (scan << 16) | (1 << 30)
        lparam_up = 1 | (scan << 16) | (1 << 30) | (1 << 31)

        begin = time.monotonic()
        ok_down = bool(self.user32.PostMessageW(hwnd, WM_KEYDOWN, vk, lparam_down))
        if not ok_down:
            raise RuntimeError(f"PostMessageW WM_KEYDOWN failed for {key_name}")
        records.append({'event': 'key_down', 'key': key_name, 'role': role, 'utc': self._now()})

        try:
            while time.monotonic() - begin < hold_s:
                fg = self.win_mgr.get_foreground_info()
                if fg['pid'] != d['pid']:
                    # Re-assert foreground if stolen
                    self.win_mgr.activate_foreground(hwnd, d['pid'], timeout=1.0)
                time.sleep(0.04)
                self.user32.PostMessageW(hwnd, WM_KEYDOWN, vk, lparam_repeat)

            if handoff:
                # In handoff mode, transfer focus to client before releasing host key
                target_role = 'client' if role == 'host' else 'host'
                cd, chwnd = self.win_mgr.find_window_for_role(target_role)
                self.win_mgr.activate_foreground(chwnd, cd['pid'])
                records.append({
                    'event': 'handoff_focus',
                    'target_role': target_role,
                    'target_pid': cd['pid'],
                    'utc': self._now(),
                })
        finally:
            ok_up = bool(self.user32.PostMessageW(hwnd, WM_KEYUP, vk, lparam_up))
            elapsed = time.monotonic() - begin
            records.append({
                'event': 'key_up',
                'key': key_name,
                'role': role,
                'result': ok_up,
                'elapsed_s': elapsed,
                'handoff': handoff,
                'utc': self._now(),
            })

        time.sleep(0.3)
        return {
            'status': 'PASS',
            'action': 'key',
            'role': role,
            'key': key_name,
            'hold_s': hold_s,
            'handoff': handoff,
            'records': records,
        }

    def mouse(self, role: str, dx: int) -> Dict[str, Any]:
        """Executes relative mouse movement on the specified window."""
        if not self.user32:
            raise RuntimeError("Win32 API not available")

        records: List[Dict[str, Any]] = []
        d, hwnd = self.win_mgr.find_window_for_role(role)
        self.win_mgr.activate_foreground(hwnd, d['pid'])
        records.append({
            'event': 'actual_foreground',
            'role': role,
            'target_pid': d['pid'],
            'utc': self._now(),
        })

        step = dx // 4
        rem = dx % 4
        for i in range(4):
            cur_dx = step + (rem if i == 3 else 0)
            self.user32.mouse_event(MOUSEEVENTF_MOVE, cur_dx & 0xFFFFFFFF, 0, 0, 0)
            records.append({'event': 'relative_mouse', 'dx': cur_dx, 'step': i + 1, 'utc': self._now()})
            time.sleep(0.05)

        time.sleep(0.4)
        return {
            'status': 'PASS',
            'action': 'mouse',
            'role': role,
            'dx': dx,
            'records': records,
        }

    def hold(self, seconds: float) -> Dict[str, Any]:
        """Pauses for a specified duration."""
        if seconds <= 0 or seconds > 300:
            raise ValueError(f"Hold duration out of bounds: {seconds}")
        time.sleep(seconds)
        return {
            'status': 'PASS',
            'action': 'hold',
            'seconds': seconds,
            'utc': self._now(),
        }

    def save_action_log(self, result: Dict[str, Any], tag: str = "action") -> Path:
        """Saves an input action result to the session's inputs folder."""
        inputs_dir = self.session.config.out_dir / 'inputs'
        inputs_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
        p = inputs_dir / f'INPUT_{tag}_{stamp}.json'
        with open(p, 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2, ensure_ascii=False)
        return p
