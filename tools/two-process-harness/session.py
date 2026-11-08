"""Two-process session configuration and path management."""

from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
from typing import Any, Dict, Optional

EXPECTED_EXE_SHA256 = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'


def compute_sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def find_project_root() -> Path:
    # Walk upwards from current file to find directory containing AGENTS.md or experiments/m0
    p = Path(__file__).resolve().parent
    for parent in [p, *p.parents]:
        if (parent / 'AGENTS.md').is_file() and (parent / 'experiments').is_dir():
            return parent
    # Fallback to S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod
    return Path(r'S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod')


@dataclass
class TwoProcessConfig:
    root: Path = field(default_factory=find_project_root)
    host_run: str = 'N58B_HOST_02'
    client_run: str = 'N58B_CLIENT_02'
    out_dir: Optional[Path] = None
    host_exe: Optional[Path] = None
    client_exe: Optional[Path] = None
    host_dll: Optional[Path] = None
    client_dll: Optional[Path] = None
    expected_exe_sha256: str = EXPECTED_EXE_SHA256
    extra: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self):
        self.root = Path(self.root).resolve()
        if self.out_dir is None:
            self.out_dir = self.root / 'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B'
        else:
            self.out_dir = Path(self.out_dir).resolve()

        if self.host_exe is None:
            self.host_exe = self.root / 'experiments/m0/game/anubis_Shipping_Steam_x64.exe'
        else:
            self.host_exe = Path(self.host_exe).resolve()

        if self.client_exe is None:
            self.client_exe = self.root / 'experiments/m1/native_second_instance/game/anubis_Shipping_Steam_x64.exe'
        else:
            self.client_exe = Path(self.client_exe).resolve()

        if self.host_dll is None:
            # Default to frozen DOC-56 host artifact
            self.host_dll = self.root / 'docs/evidence/m1/NATIVE_MAP_READY_56/artifacts/pse_m11_bootstrap.dll'
        else:
            self.host_dll = Path(self.host_dll).resolve()

        if self.client_dll is None:
            # Default to DOC-58B client artifact
            self.client_dll = self.root / 'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/artifacts/pse_native_client58b.dll'
        else:
            self.client_dll = Path(self.client_dll).resolve()

    def get_run_name(self, role: str) -> str:
        role = role.lower()
        if role == 'host':
            return self.host_run
        elif role == 'client':
            return self.client_run
        else:
            raise ValueError(f"Unknown role: {role}")

    def get_run_dir(self, role: str) -> Path:
        return self.out_dir / self.get_run_name(role)

    def get_exe(self, role: str) -> Path:
        role = role.lower()
        if role == 'host':
            return self.host_exe
        elif role == 'client':
            return self.client_exe
        else:
            raise ValueError(f"Unknown role: {role}")

    def get_dll(self, role: str) -> Path:
        role = role.lower()
        if role == 'host':
            return self.host_dll
        elif role == 'client':
            return self.client_dll
        else:
            raise ValueError(f"Unknown role: {role}")

    def verify_targets(self) -> Dict[str, str]:
        results = {}
        for role, p in [('host', self.host_exe), ('client', self.client_exe)]:
            if not p.is_file():
                raise FileNotFoundError(f"Target executable not found for {role}: {p}")
            h = compute_sha256(p)
            if h != self.expected_exe_sha256:
                raise ValueError(f"Target {role} executable SHA-256 mismatch: got {h}, expected {self.expected_exe_sha256}")
            results[role] = h
        return results

    def verify_dlls(self) -> Dict[str, str]:
        results = {}
        for role, p in [('host', self.host_dll), ('client', self.client_dll)]:
            if not p.is_file():
                raise FileNotFoundError(f"Target DLL not found for {role}: {p}")
            results[role] = compute_sha256(p)
        return results

    def to_dict(self) -> Dict[str, Any]:
        return {
            'root': str(self.root),
            'host_run': self.host_run,
            'client_run': self.client_run,
            'out_dir': str(self.out_dir),
            'host_exe': str(self.host_exe),
            'client_exe': str(self.client_exe),
            'host_dll': str(self.host_dll),
            'client_dll': str(self.client_dll),
            'expected_exe_sha256': self.expected_exe_sha256,
            'extra': self.extra,
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> 'TwoProcessConfig':
        return cls(
            root=Path(d['root']) if 'root' in d else find_project_root(),
            host_run=d.get('host_run', 'N58B_HOST_02'),
            client_run=d.get('client_run', 'N58B_CLIENT_02'),
            out_dir=Path(d['out_dir']) if 'out_dir' in d else None,
            host_exe=Path(d['host_exe']) if 'host_exe' in d else None,
            client_exe=Path(d['client_exe']) if 'client_exe' in d else None,
            host_dll=Path(d['host_dll']) if 'host_dll' in d else None,
            client_dll=Path(d['client_dll']) if 'client_dll' in d else None,
            expected_exe_sha256=d.get('expected_exe_sha256', EXPECTED_EXE_SHA256),
            extra=d.get('extra', {}),
        )


class TwoProcessSession:
    """Manages runtime launch records and directory state for a two-process session."""

    def __init__(self, config: Optional[TwoProcessConfig] = None):
        self.config = config or TwoProcessConfig()

    def get_launch_record(self, role: str) -> Dict[str, Any]:
        p = self.config.get_run_dir(role) / 'launch.json'
        if not p.is_file():
            raise FileNotFoundError(f"launch.json not found for role '{role}' at {p}")
        return json.loads(p.read_text(encoding='utf-8-sig'))

    def get_pid(self, role: str) -> int:
        return self.get_launch_record(role)['pid']

    def get_primary_tid(self, role: str) -> int:
        return self.get_launch_record(role)['primary_thread_id']
