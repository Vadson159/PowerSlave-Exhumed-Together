"""Unified Command-Line Interface for the Two-Process Test Harness.

Usage:
  python harness.py launch <host|client> [options]
  python harness.py bind <host|client> [options]
  python harness.py focus <host|client> [options]
  python harness.py key <host|client> <W|S|A|D|E|RETURN|SPACE|ESCAPE> <hold_s> [--handoff] [options]
  python harness.py mouse <host|client> <dx> [options]
  python harness.py hold <seconds>
  python harness.py scenario <scenario_file.json> [--dry-run] [options]
  python harness.py extract <bundle.json> <run_name> <session_id> [status_tag] [options]
  python harness.py review [options]
  python harness.py reproduce-58b
"""

import argparse
import json
from pathlib import Path
import sys
from typing import Optional

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session import TwoProcessConfig, TwoProcessSession
    from launcher import Launcher
    from window_manager import WindowManager
    from input_controller import InputController
    from scenario_runner import ScenarioRunner
    from rea_extractor import ReaExtractor
    from evidence_reviewer import EvidenceReviewer
    from doc58b_reproduction import Doc58bReproduction
else:
    from .session import TwoProcessConfig, TwoProcessSession
    from .launcher import Launcher
    from .window_manager import WindowManager
    from .input_controller import InputController
    from .scenario_runner import ScenarioRunner
    from .rea_extractor import ReaExtractor
    from .evidence_reviewer import EvidenceReviewer
    from .doc58b_reproduction import Doc58bReproduction


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="PowerSlave Exhumed Together Two-Process Harness")
    sub = p.add_subparsers(dest="command", required=True)

    # Global options helper
    def add_common_opts(parser):
        parser.add_argument('--host-run', default=None, help='Host run identifier')
        parser.add_argument('--client-run', default=None, help='Client run identifier')
        parser.add_argument('--out-dir', default=None, help='Evidence output directory')
        parser.add_argument('--host-dll', default=None, help='Path to Host DLL')
        parser.add_argument('--client-dll', default=None, help='Path to Client DLL')

    # Launch
    p_launch = sub.add_parser('launch', help='Launch host or client in CREATE_SUSPENDED')
    p_launch.add_argument('role', choices=['host', 'client'])
    add_common_opts(p_launch)

    # Bind
    p_bind = sub.add_parser('bind', help='Prepare native artifact and Frida loader scripts')
    p_bind.add_argument('role', choices=['host', 'client'])
    add_common_opts(p_bind)

    # Focus
    p_focus = sub.add_parser('focus', help='Activate real foreground for host or client')
    p_focus.add_argument('role', choices=['host', 'client'])
    add_common_opts(p_focus)

    # Key
    p_key = sub.add_parser('key', help='Post key down/up to target role')
    p_key.add_argument('role', choices=['host', 'client'])
    p_key.add_argument('key', help='Key name (W, S, A, D, etc.)')
    p_key.add_argument('hold', type=float, help='Hold duration in seconds')
    p_key.add_argument('--handoff', action='store_true', help='Transfer focus to other role before release')
    add_common_opts(p_key)

    # Mouse
    p_mouse = sub.add_parser('mouse', help='Post relative mouse motion')
    p_mouse.add_argument('role', choices=['host', 'client'])
    p_mouse.add_argument('dx', type=int, help='Relative horizontal delta')
    add_common_opts(p_mouse)

    # Hold
    p_hold = sub.add_parser('hold', help='Pause for duration')
    p_hold.add_argument('seconds', type=float, help='Duration in seconds')

    # Scenario
    p_scen = sub.add_parser('scenario', help='Execute declarative input scenario')
    p_scen.add_argument('file', help='Path to scenario JSON file')
    p_scen.add_argument('--dry-run', action='store_true', help='Validate scenario without execution')
    add_common_opts(p_scen)

    # Extract
    p_ext = sub.add_parser('extract', help='Extract REA evidence bundle records')
    p_ext.add_argument('bundle', help='Path to REA bundle JSON')
    p_ext.add_argument('run_name', help='Target run folder name')
    p_ext.add_argument('session_id', help='Frida session ID')
    p_ext.add_argument('status_tag', nargs='?', default='STATUS_EXIT', help='Status tag')
    p_ext.add_argument('--out-dir', default=None, help='Output directory')

    # Review
    p_rev = sub.add_parser('review', help='Analyze raw journals and produce review evidence')
    p_rev.add_argument('--host-dir', default=None, help='Path to host run folder')
    p_rev.add_argument('--client-dir', default=None, help='Path to client run folder')
    p_rev.add_argument('--write', action='store_true', help='Write derived evidence files to out-dir')
    add_common_opts(p_rev)

    # Reproduce 58B
    sub.add_parser('reproduce-58b', help='Run full deterministic reproduction of DOC-58B Run 02')

    return p


def create_config_from_args(args) -> TwoProcessConfig:
    cfg = TwoProcessConfig()
    if getattr(args, 'host_run', None):
        cfg.host_run = args.host_run
    if getattr(args, 'client_run', None):
        cfg.client_run = args.client_run
    if getattr(args, 'out_dir', None):
        cfg.out_dir = Path(args.out_dir).resolve()
    if getattr(args, 'host_dll', None):
        cfg.host_dll = Path(args.host_dll).resolve()
    if getattr(args, 'client_dll', None):
        cfg.client_dll = Path(args.client_dll).resolve()
    return cfg


def main():
    parser = build_parser()
    args = parser.parse_args()

    if args.command == 'reproduce-58b':
        repro = Doc58bReproduction()
        res = repro.run_reproduction()
        print(json.dumps(res, indent=2))
        sys.exit(0 if res['status'] == 'PASS' else 1)

    cfg = create_config_from_args(args)
    session = TwoProcessSession(cfg)

    if args.command == 'launch':
        launcher = Launcher(cfg)
        res = launcher.launch(args.role)
        print(json.dumps(res, indent=2))

    elif args.command == 'bind':
        launcher = Launcher(cfg)
        res = launcher.bind(args.role)
        print(json.dumps(res, indent=2))

    elif args.command == 'focus':
        wm = WindowManager(session)
        res = wm.focus_role(args.role)
        print(json.dumps(res, indent=2))

    elif args.command == 'key':
        ctrl = InputController(session)
        res = ctrl.key(args.role, args.key, args.hold, handoff=args.handoff)
        ctrl.save_action_log(res, tag=f'key_{args.key}')
        print(json.dumps(res, indent=2))

    elif args.command == 'mouse':
        ctrl = InputController(session)
        res = ctrl.mouse(args.role, args.dx)
        ctrl.save_action_log(res, tag=f'mouse_{args.dx}')
        print(json.dumps(res, indent=2))

    elif args.command == 'hold':
        ctrl = InputController(session)
        res = ctrl.hold(args.seconds)
        print(json.dumps(res, indent=2))

    elif args.command == 'scenario':
        runner = ScenarioRunner(session)
        res = runner.execute_scenario(Path(args.file), dry_run=args.dry_run)
        print(json.dumps(res, indent=2))
        sys.exit(0 if res['status'] in ('PASS', 'DRY_RUN_PASS') else 1)

    elif args.command == 'extract':
        out_base = Path(args.out_dir) if args.out_dir else cfg.out_dir
        run_folder = out_base / args.run_name
        ext = ReaExtractor()
        res = ext.extract(args.bundle, run_folder, args.session_id, status_tag=args.status_tag)
        print(json.dumps(res, indent=2))

    elif args.command == 'review':
        host_dir = Path(args.host_dir) if args.host_dir else cfg.get_run_dir('host')
        client_dir = Path(args.client_dir) if args.client_dir else cfg.get_run_dir('client')
        rev = EvidenceReviewer(host_dir, client_dir, out_dir=cfg.out_dir)
        res = rev.review(write_artifacts=args.write)
        print(json.dumps({'status': 'PASS', 'verdict': res['verdict'], 'accounting': res['accounting']}, indent=2))


if __name__ == '__main__':
    main()
