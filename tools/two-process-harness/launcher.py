"""Two-Process Launcher, Isolation Binding, and Gate Verifier.

Reuses the proven M0 suspended launcher and M1 isolation contracts without hardcoding:
- Launches KEX processes in CREATE_SUSPENDED with fresh profiles and environment isolation
- Generates dynamic Frida loader scripts (load_native.js, permit_native.js, status.js, etc.)
- Injects client kexengine.cfg settings (cl_engineNoSleep, cl_engineFPS)
- Binds Frida adapter scripts and monitors process lifecycle
"""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
from typing import Any, Dict, List, Optional, Tuple

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session import TwoProcessConfig, compute_sha256, EXPECTED_EXE_SHA256
else:
    from .session import TwoProcessConfig, compute_sha256, EXPECTED_EXE_SHA256


class Launcher:
    """Manages suspended launching, DLL loading, and gate verification for host and client."""

    def __init__(self, config: Optional[TwoProcessConfig] = None):
        self.config = config or TwoProcessConfig()
        self.root = self.config.root

    def verify_prerequisites(self) -> Dict[str, Any]:
        targets = self.config.verify_targets()
        dlls = self.config.verify_dlls()
        return {'targets': targets, 'dlls': dlls}

    def launch(self, role: str) -> Dict[str, Any]:
        """Launches the specified role in suspended state with an isolated profile."""
        self.verify_prerequisites()
        run_name = self.config.get_run_name(role)
        run_dir = self.config.get_run_dir(role)
        if run_dir.exists():
            raise FileExistsError(f"Run directory already exists: {run_dir}")

        copy_dir = self.config.get_exe(role).parent
        profile_dir = self.root / 'experiments/m1/native_network_visual_proxy_58b/profile'

        # Import M0 launch logic safely
        m0_dir = self.root / 'experiments/m0'
        sys.path.insert(0, str(m0_dir))
        spec = importlib.util.spec_from_file_location('m0_prepare', m0_dir / 'prepare.py')
        prep_mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prep_mod)
        prep_mod.OUT = self.config.out_dir
        prep_mod.COPY = copy_dir
        prep_mod.PROFILE = profile_dir

        spec_launch = importlib.util.spec_from_file_location('m0_launch', m0_dir / 'launch.py')
        launch_mod = importlib.util.module_from_spec(spec_launch)
        spec_launch.loader.exec_module(launch_mod)

        old_argv = sys.argv
        sys.argv = [str(m0_dir / 'launch.py'), '--run', run_name, '--quiet']
        try:
            launch_mod.main()
        finally:
            sys.argv = old_argv

        launch_json_p = run_dir / 'launch.json'
        if not launch_json_p.is_file():
            raise RuntimeError(f"launch.json was not created at {launch_json_p}")

        return json.loads(launch_json_p.read_text(encoding='utf-8'))

    def bind(self, role: str) -> Dict[str, Any]:
        """Prepares native artifacts and Frida injection scripts for the suspended process."""
        run_dir = self.config.get_run_dir(role)
        launch_json_p = run_dir / 'launch.json'
        if not launch_json_p.is_file():
            raise FileNotFoundError(f"launch.json not found in {run_dir}")
        launch = json.loads(launch_json_p.read_text(encoding='utf-8'))

        artifact_dir = run_dir / 'native_artifact'
        artifact_dir.mkdir(parents=True, exist_ok=True)

        src_dll = self.config.get_dll(role)
        dest_dll = artifact_dir / src_dll.name
        shutil.copy2(src_dll, dest_dll)
        src_pdb = src_dll.with_suffix('.pdb')
        if src_pdb.is_file():
            shutil.copy2(src_pdb, artifact_dir / src_pdb.name)

        dll_sha = compute_sha256(dest_dll)
        cfg = {
            'pid': launch['pid'],
            'target': launch['target'],
            'primary_tid': launch['primary_thread_id'],
            'dll': str(dest_dll),
            'dll_name': src_dll.name,
            'dll_sha256': dll_sha,
            'native_log': str(run_dir / 'native.jsonl'),
            'role': role,
        }

        # Write NATIVE_ARTIFACT.json
        with open(run_dir / 'NATIVE_ARTIFACT.json', 'w', encoding='utf-8') as f:
            json.dump({'config': cfg, 'host_dll_is_frozen': role == 'host', 'new_injector': False}, f, indent=2)

        # Generate load_native.js
        loader_js = (
            f"const cfg = {json.dumps(cfg)};\n"
            r"""if(Process.id !== cfg.pid || Process.mainModule.path.toLowerCase() !== cfg.target.toLowerCase()) throw Error('target mismatch');
const load = new NativeFunction(Module.getGlobalExportByName('LoadLibraryW'), 'pointer', ['pointer'], 'win64');
if (load(Memory.allocUtf16String(cfg.dll)).isNull()) throw Error('LoadLibraryW failed');
const dll = Process.getModuleByName(cfg.dll_name);
if (dll.path.toLowerCase() !== cfg.dll.toLowerCase()) throw Error('Wrong DLL loaded');
const prepare = new NativeFunction(dll.getExportByName('M11Prepare'), 'uint32', ['pointer', 'uint32'], 'win64');
const res = prepare(Memory.allocUtf16String(cfg.native_log), cfg.primary_tid);
send({kind: 'm11_dll_loader', pid: Process.id, dll_path: dll.path, dll_sha256: cfg.dll_sha256, prepare_result: res, role: cfg.role});
if (res !== 0) throw Error('M11Prepare rejected: ' + res);
"""
        )
        (run_dir / 'load_native.js').write_text(loader_js, encoding='utf-8')

        # Generate early kexengine.cfg in profile
        profile = Path(launch['profile'])
        cfg_path = profile / 'early_native_logs/kexengine.cfg'
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text('seta cl_engineNoSleep "1"\nseta cl_engineFPS "180"\n', encoding='ascii')

        # Role-specific scripts
        if role == 'client':
            # Generate client control helpers
            head = (
                f"const c = {json.dumps(dict(pid=launch['pid'], target=launch['target'], dll=src_dll.name))};\n"
                r"if(Process.id !== c.pid || Process.mainModule.path.toLowerCase() !== c.target.toLowerCase()) throw Error('target');"
                r"const d = Process.getModuleByName(c.dll);" + "\n"
            )
            status_js = head + (
                "const st = new NativeFunction(d.getExportByName('PSE58B_Status'), 'uint32', [], 'win64')();\n"
                "send({kind: 'n58b_status', status: st, seam: st & 0xff, quiescent: (st >> 8) & 1, "
                "proxy_state_plus1: (st >> 16) & 0xff, utc: new Date().toISOString()});\n"
            )
            (run_dir / 'status.js').write_text(status_js, encoding='utf-8')

            shutdown_js = head + (
                "const r = new NativeFunction(d.getExportByName('PSE58B_RequestProxyShutdown'), 'uint32', [], 'win64')();\n"
                "send({kind: 'n58b_request_shutdown', result: r, utc: new Date().toISOString()});\n"
            )
            (run_dir / 'request_shutdown.js').write_text(shutdown_js, encoding='utf-8')

            detach_js = head + (
                "const r = new NativeFunction(d.getExportByName('PSE58B_RequestDetach'), 'uint32', [], 'win64')();\n"
                "send({kind: 'n58b_request_detach', result: r, utc: new Date().toISOString()});\n"
                + status_js
            )
            (run_dir / 'request_detach.js').write_text(detach_js, encoding='utf-8')

        return {'status': 'PASS', 'role': role, 'config': cfg}

    def generate_mailbox_script(self, role: str, command: int) -> Path:
        """Generates a mailbox command script for the client."""
        run_dir = self.config.get_run_dir(role)
        launch = json.loads((run_dir / 'launch.json').read_text(encoding='utf-8'))
        rows = [json.loads(line) for line in (run_dir / 'adapter.jsonl').read_text(encoding='utf-8').splitlines()]
        control = next(r for r in rows if r.get('event') == 'probe_loaded')['control']

        body = (
            f"const c = {json.dumps(dict(pid=launch['pid'], target=launch['target'], control=control, command=int(command)))};\n"
            r"""if (Process.id !== c.pid || Process.mainModule.path.toLowerCase() !== c.target.toLowerCase()) throw Error('mailbox target');
ptr(c.control).writeU32(c.command);
send({kind: 'n58b_mailbox', command: c.command, game_writes: 0, kex_calls: 0, utc: new Date().toISOString()});
"""
        )
        p = run_dir / f'mailbox_{command}.js'
        p.write_text(body, encoding='utf-8')
        return p
