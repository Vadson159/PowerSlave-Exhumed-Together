"""Robust Window Manager & Foreground Activation for Two-Process KEX instances.

Reuses the proven Win32 foreground activation pattern from DOC-53 / DOC-58B:
- Enumerate windows for target PID (selecting largest visible window)
- ShowWindow(SW_RESTORE) + SetForegroundWindow
- AttachThreadInput fallback when Windows rejects simple foreground requests
- Polling & retry loop to handle the ~1.4s desktop focus-stealing quirk (Appendix A)
"""

from ctypes import wintypes as W
import ctypes as C
import datetime
import os
from pathlib import Path
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session import TwoProcessSession
else:
    from .session import TwoProcessSession

SW_RESTORE = 9
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def get_win32_apis():
    if os.name != 'nt':
        return None, None
    user32 = C.WinDLL('user32', use_last_error=True)
    kernel32 = C.WinDLL('kernel32', use_last_error=True)

    user32.GetForegroundWindow.restype = W.HWND
    user32.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user32.SetForegroundWindow.argtypes = [W.HWND]
    user32.ShowWindow.argtypes = [W.HWND, C.c_int]
    user32.IsWindowVisible.argtypes = [W.HWND]
    user32.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    user32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    user32.MapVirtualKeyW.argtypes = [W.UINT, W.UINT]
    user32.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
    user32.AttachThreadInput.restype = W.BOOL

    kernel32.GetCurrentThreadId.restype = W.DWORD
    kernel32.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel32.OpenProcess.restype = W.HANDLE
    kernel32.CloseHandle.argtypes = [W.HANDLE]
    kernel32.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]

    return user32, kernel32


class WindowManager:
    """Manages window discovery and reliable foreground activation."""

    def __init__(self, session: Optional[TwoProcessSession] = None):
        self.session = session or TwoProcessSession()
        self.user32, self.kernel32 = get_win32_apis()

    def get_foreground_info(self) -> Dict[str, Any]:
        if not self.user32:
            return {'hwnd': '0x0', 'pid': 0}
        hwnd = self.user32.GetForegroundWindow()
        pid = W.DWORD()
        self.user32.GetWindowThreadProcessId(hwnd, C.byref(pid))
        return {'hwnd': hex(hwnd or 0), 'pid': pid.value}

    def find_window_by_pid(self, pid: int, expected_target: Optional[Path] = None) -> int:
        if not self.user32 or not self.kernel32:
            raise RuntimeError("Win32 APIs not available on this platform")

        # Verify process image name if expected_target is provided
        if expected_target is not None:
            handle = self.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
            if not handle:
                raise RuntimeError(f"Cannot open process {pid}")
            buf = C.create_unicode_buffer(32768)
            size = W.DWORD(len(buf))
            ok = self.kernel32.QueryFullProcessImageNameW(handle, 0, buf, C.byref(size))
            self.kernel32.CloseHandle(handle)
            if not ok:
                raise RuntimeError(f"Cannot query process image name for {pid}")
            actual_path = Path(buf.value).resolve()
            if actual_path != Path(expected_target).resolve():
                raise RuntimeError(f"Process PID {pid} target mismatch: {actual_path} != {expected_target}")

        windows: List[Tuple[int, int]] = []
        WNDENUMPROC = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

        @WNDENUMPROC
        def enum_cb(hwnd, _):
            proc_id = W.DWORD()
            self.user32.GetWindowThreadProcessId(hwnd, C.byref(proc_id))
            if proc_id.value == pid and self.user32.IsWindowVisible(hwnd):
                rect = W.RECT()
                self.user32.GetClientRect(hwnd, C.byref(rect))
                area = rect.right * rect.bottom
                windows.append((area, hwnd))
            return True

        self.user32.EnumWindows.argtypes = [WNDENUMPROC, W.LPARAM]
        self.user32.EnumWindows(enum_cb, 0)
        if not windows:
            raise RuntimeError(f"No visible window found for PID {pid}")
        # Select window with largest client area
        return max(windows)[1]

    def find_window_for_role(self, role: str) -> Tuple[Dict[str, Any], int]:
        launch_record = self.session.get_launch_record(role)
        pid = launch_record['pid']
        target = Path(launch_record['target'])
        hwnd = self.find_window_by_pid(pid, target)
        return launch_record, hwnd

    def activate_foreground(self, hwnd: int, target_pid: int, timeout: float = 5.0, retry_interval: float = 0.25) -> Dict[str, Any]:
        """Brings the window to the actual foreground with verification and AttachThreadInput fallback."""
        if not self.user32 or not self.kernel32:
            raise RuntimeError("Win32 APIs not available on this platform")

        start_time = time.monotonic()
        attempts: List[Dict[str, Any]] = []

        while time.monotonic() - start_time < timeout:
            before = self.get_foreground_info()
            if before['pid'] == target_pid:
                return {
                    'status': 'PASS',
                    'target_pid': target_pid,
                    'hwnd': hex(hwnd),
                    'attempts': len(attempts),
                    'foreground': before,
                }

            self.user32.ShowWindow(hwnd, SW_RESTORE)
            ok = bool(self.user32.SetForegroundWindow(hwnd))

            if not ok or self.get_foreground_info()['pid'] != target_pid:
                # AttachThreadInput fallback
                fg_hwnd = self.user32.GetForegroundWindow()
                fg_pid = W.DWORD()
                fg_tid = self.user32.GetWindowThreadProcessId(fg_hwnd, C.byref(fg_pid))
                target_pid_dw = W.DWORD()
                target_tid = self.user32.GetWindowThreadProcessId(hwnd, C.byref(target_pid_dw))
                own_tid = self.kernel32.GetCurrentThreadId()

                attached: List[int] = []
                try:
                    for tid in dict.fromkeys([fg_tid, target_tid]):
                        if tid and tid != own_tid:
                            joined = bool(self.user32.AttachThreadInput(own_tid, tid, True))
                            attempts.append({
                                'operation': 'AttachThreadInput',
                                'own_tid': own_tid,
                                'target_tid': tid,
                                'joined': joined,
                            })
                            if joined:
                                attached.append(tid)
                    ok = bool(self.user32.SetForegroundWindow(hwnd))
                finally:
                    for tid in reversed(attached):
                        self.user32.AttachThreadInput(own_tid, tid, False)

            after = self.get_foreground_info()
            attempts.append({
                'operation': 'SetForegroundWindow',
                'target_hwnd': hex(hwnd),
                'before': before,
                'after': after,
                'result': ok,
            })

            if after['pid'] == target_pid:
                return {
                    'status': 'PASS',
                    'target_pid': target_pid,
                    'hwnd': hex(hwnd),
                    'attempts': len(attempts),
                    'foreground': after,
                }

            time.sleep(retry_interval)

        final_fg = self.get_foreground_info()
        raise TimeoutError(
            f"Failed to activate foreground for PID {target_pid} within {timeout}s. Current foreground: {final_fg}"
        )

    def focus_role(self, role: str, timeout: float = 5.0) -> Dict[str, Any]:
        """Focuses the specified role (host or client) with full foreground verification."""
        launch_record, hwnd = self.find_window_for_role(role)
        res = self.activate_foreground(hwnd, launch_record['pid'], timeout=timeout)
        res['role'] = role
        res['utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        return res
