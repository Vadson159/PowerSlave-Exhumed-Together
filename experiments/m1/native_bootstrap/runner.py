"""M1.1 diagnostic harness. Reuse M0 isolation/OS helpers without editing them.

The loaded JS contains no KEX hook or game call. DLL code owns native integration.
Launch/action commands run through the existing REA provider on its Default desktop.
"""
from pathlib import Path
import ctypes
import datetime
import hashlib
import importlib
import json
import os
import shutil
import sys
import traceback

LAB = Path(__file__).resolve().parent
ROOT = LAB.parents[2]
OUT = ROOT / 'docs/evidence/m1'
IMPLEMENTATION = OUT / 'M11_IMPLEMENTATION_20261101'
sys.path.insert(0, str(ROOT / 'experiments/m0'))
import prepare
prepare.OUT = OUT
prepare.PROFILE = LAB / 'profile'


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def dump(p, obj):
    with p.open('x', encoding='utf8') as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)


def owned_run(name):
    if not name.replace('_', '').isalnum():
        raise ValueError('Invalid run')
    path = (OUT / name).resolve()
    if not path.is_relative_to(OUT.resolve()) or not (path / 'launch.json').is_file():
        raise ValueError('Unknown owned run')
    launch = json.loads((path / 'launch.json').read_text(encoding='utf8'))
    if Path(launch['target']).resolve() != (prepare.COPY / 'anubis_Shipping_Steam_x64.exe').resolve() or digest(Path(launch['target'])) != prepare.EXPECTED:
        raise ValueError('Build/target mismatch')
    return path, launch


def verify_copy():
    manifest = json.loads((ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json').read_text(encoding='utf8'))
    checked = []
    for record in manifest['independent_copy_files']:
        rel = record['relative_path']
        original = prepare.SOURCE / rel
        copy = prepare.COPY / rel
        if os.path.samefile(original, copy) or digest(original) != record['sha256'] or digest(copy) != record['sha256']:
            raise ValueError('Original/copy integrity or alias: ' + rel)
        checked.append(rel)
    return dict(files=len(checked), original_and_copy='unchanged', hardlinks_to_original=False)


def launch(name):
    # Existing M0 launcher: CREATE_SUSPENDED, fresh CWD/profile, env/path isolation,
    # protected before/after snapshots, process wait and retained native exit status.
    integrity = verify_copy()
    OUT.mkdir(exist_ok=True)
    # M0 launch.py resolves its read-only preparation manifest relative to OUT.
    # Retain a byte-identical evidence copy when relocating its output directory.
    preparation = ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json'
    relocated = OUT / preparation.name
    if not relocated.exists():
        shutil.copy2(preparation, relocated)
    if digest(relocated) != digest(preparation):
        raise ValueError('Relocated preparation manifest differs')
    prefix = OUT / ('M11_' + name + '_provider')
    dump(prefix.with_suffix('.json'), dict(stage='before isolated launcher', utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), helper_pid=os.getpid(), integrity=integrity))
    with prefix.with_suffix('.log').open('x', encoding='utf8') as log:
        sys.stdout = log
        sys.stderr = log
        sys.argv = [str(ROOT / 'experiments/m0/launch.py'), '--run', name, '--quiet']
        try:
            importlib.import_module('launch').main()
        except BaseException:
            traceback.print_exc()
            raise


def action(name, helper, args):
    run, _ = owned_run(name)
    allowed = ['resume', 'ui', 'ui_start', 'window_state', 'ui_sequence']
    if helper not in allowed:
        raise ValueError('Unsupported helper')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    prefix = run / f'provider_{helper}_{stamp}'
    record = dict(stage='starting', helper_pid=os.getpid(), helper=helper, arguments=args, harness_sha256=digest(Path(__file__)))
    with prefix.with_suffix('.log').open('x', encoding='utf8') as log:
        sys.stdout = log
        sys.stderr = log
        sys.argv = [helper, name] + args
        try:
            module = importlib.import_module(helper)
            if hasattr(module, 'main'):
                module.main()
            record.update(stage='completed')
        except BaseException as e:
            traceback.print_exc()
            record.update(stage='failed', error=str(e))
        dump(prefix.with_suffix('.json'), record)


def bind(name):
    run, launch_record = owned_run(name)
    sys.argv = ['generate_observer', name]
    importlib.import_module('generate_observer').main()
    sys.argv = ['bind_probe', name, 'isolation']
    importlib.import_module('bind_probe').main()
    # Do not load observer_bound.js: only generated_isolation.js is used.
    frozen = run / 'native_artifact'
    frozen.mkdir()
    dll = LAB / 'build/Release/pse_m11_bootstrap.dll'
    shutil.copy2(dll, frozen / dll.name)
    pdb = dll.with_suffix('.pdb')
    if pdb.exists():
        shutil.copy2(pdb, frozen / pdb.name)
    sources = []
    for p in sorted(LAB.rglob('*')):
        if not p.is_file() or 'build' in p.parts or 'profile' in p.parts or '__pycache__' in p.parts:
            continue
        dest = frozen / 'source' / p.relative_to(LAB)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dest)
        sources.append(dict(path=str(p.relative_to(ROOT)), sha256=digest(p)))
    dll_path = frozen / dll.name
    cfg = dict(pid=launch_record['pid'], target=launch_record['target'], primary_tid=launch_record['primary_thread_id'],
               dll=str(dll_path), dll_sha256=digest(dll_path), native_log=str(run / 'native.jsonl'))
    dump(run / 'NATIVE_ARTIFACT.json', dict(config=cfg, sources=sources, reused_m0_isolation=['observer.js (path slice only)', 'isolation_guard.js'],
                                        no_frida_kex_hooks=True))
    source = 'const cfg = ' + json.dumps(cfg) + ';\n' + r'''
if (Process.id !== cfg.pid || Process.mainModule.path.toLowerCase() !== cfg.target.toLowerCase()) throw Error('M11 loader target mismatch');
const load = new NativeFunction(Module.getGlobalExportByName('LoadLibraryW'), 'pointer', ['pointer'], 'win64');
const handle = load(Memory.allocUtf16String(cfg.dll));
if (handle.isNull()) throw Error('LoadLibraryW failed');
const dll = Process.getModuleByName('pse_m11_bootstrap.dll');
if (dll.path.toLowerCase() !== cfg.dll.toLowerCase()) throw Error('Wrong DLL loaded');
const prepare = new NativeFunction(dll.getExportByName('M11Prepare'), 'uint32', ['pointer','uint32'], 'win64');
const code = prepare(Memory.allocUtf16String(cfg.native_log), cfg.primary_tid);
send({kind:'m11_dll_loader',pid:Process.id,dll_path:dll.path,dll_base:dll.base.toString(),dll_sha256_host_verified:cfg.dll_sha256,prepare_result:code,kex_calls:0,frida_game_hooks:0});
if (code !== 0) throw Error('Native bootstrap preparation rejected: '+code);
'''
    (run / 'load_native.js').write_text(source, encoding='utf8')
    print(json.dumps(dict(run=str(run), cfg=cfg)))


def payloads(run):
    records, errors, truncation = [], [], False
    for p in sorted(run.glob('*.json')):
        obj = json.loads(p.read_text(encoding='utf-8-sig'))
        if not isinstance(obj, dict):
            continue
        result = obj.get('structuredContent', {}).get('normalized_result', {})
        truncation |= result.get('messages_truncated', False)
        for message in result.get('messages', []):
            if message.get('type') == 'error':
                errors.append(message)
            if isinstance(message.get('payload'), dict):
                records.append(message['payload'])
    return records, errors, truncation


def gate(name):
    run, launch_record = owned_run(name)
    records, errors, truncated = payloads(run)
    native = [json.loads(s) for s in (run / 'native.jsonl').read_text(encoding='utf8').splitlines()]
    summaries = [p for p in records if p.get('kind') == 'isolation_write_guard_summary']
    guard = max(summaries, key=lambda p: p.get('write_opens', 0), default={})
    expected_tid = launch_record['primary_thread_id']
    tick = [p for p in native if p.get('event') == 'tick_status' and p.get('state') == 3 and p.get('pending') == 0 and p.get('local_updates', 0) > 0 and p.get('local_this_tick') == 1 and p['tid'] == expected_tid]
    attested = any(p.get('event') == 'simulation_attested' and p.get('observed_tid') == expected_tid for p in native)
    faults = [p for p in native if p['event'] in ['blocked', 'native_fault', 'hook_error', 'hook_create_error']]
    outside = [p for p in records if p.get('kind') == 'file_write_open_observed' and p.get('succeeded') and not prepare.ROOT.resolve() in Path(p.get('path', '')).resolve().parents]
    loaded = any(p.get('kind') == 'isolation_write_guard_loaded' for p in records)
    loader = any(p.get('kind') == 'm11_dll_loader' and p.get('pid') == launch_record['pid'] and p.get('prepare_result') == 0 for p in records)
    passed = bool(loaded and loader and guard.get('allowed_disk', 0) and attested and tick and not errors and not faults and not outside and not truncated and not guard.get('errors') and not guard.get('unique_overflow'))
    result = dict(status='PASS' if passed else 'PENDING', pid=launch_record['pid'], binary_sha256=prepare.EXPECTED,
                  guard=guard, native_simulation_tid=expected_tid, native_gameplay_tick_observed=bool(tick), native_log_tick_witness=tick,
                  dll_loader_observed=loader, errors=errors, native_faults=faults, successful_outside_write_opens=outside, truncated=truncated,
                  scope='Bounded current-run NtCreateFile/NtOpenFile + CreateFileW and native simulation observation; not an OS sandbox.')
    path = run / ('ISOLATION_GATE.json' if passed else 'ISOLATION_PENDING_' + datetime.datetime.now(datetime.timezone.utc).strftime('%H%M%S_%f') + '.json')
    dump(path, result)
    if passed:
        cfg = json.loads((run / 'NATIVE_ARTIFACT.json').read_text(encoding='utf8'))['config']
        if digest(Path(cfg['dll'])) != cfg['dll_sha256']:
            raise ValueError('Frozen DLL changed')
        source = 'const cfg = ' + json.dumps(cfg) + ';\n' + r'''
if(Process.id!==cfg.pid||Process.mainModule.path.toLowerCase()!==cfg.target.toLowerCase())throw Error('permit PID/target mismatch');
const dll=Process.getModuleByName('pse_m11_bootstrap.dll');
const permit=new NativeFunction(dll.getExportByName('M11PermitTest'),'uint32',[],'win64');
send({kind:'m11_permit_mailbox',result:permit(),kex_calls:0,dll_sha256:cfg.dll_sha256});
'''
        (run / 'permit_native.js').write_text(source, encoding='utf8')
    print(json.dumps(result))
    raise SystemExit(not passed)


def preflight():
    # A disposable Python process is deliberately NOT a game. The real DLL must
    # reject it before any hook, file open, KEX call or bootstrap APC is created.
    dll = LAB / 'build/Release/pse_m11_bootstrap.dll'
    module = ctypes.WinDLL(str(dll))
    fn = module.M11Prepare
    fn.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32]
    fn.restype = ctypes.c_uint32
    logfile = IMPLEMENTATION / 'MUST_NOT_BE_CREATED.jsonl'
    result = fn(str(logfile), ctypes.windll.kernel32.GetCurrentThreadId())
    ok = result == 193 and not logfile.exists()
    report = dict(status='PASS' if ok else 'FAIL', dll_sha256=digest(dll), wrong_process_return=result,
                  expected='ERROR_BAD_EXE_FORMAT (193)', no_log_created=not logfile.exists(), game_started=False, kex_calls=0,
                  copy_integrity=verify_copy())
    dump(IMPLEMENTATION / ('PREFLIGHT_' + datetime.datetime.now(datetime.timezone.utc).strftime('%H%M%S_%f') + '.json'), report)
    print(json.dumps(report))
    raise SystemExit(not ok)


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'launch':
        launch(sys.argv[2])
    elif cmd == 'action':
        action(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == 'bind':
        bind(sys.argv[2])
    elif cmd == 'gate':
        gate(sys.argv[2])
    elif cmd == 'preflight':
        preflight()
    else:
        raise ValueError('Unknown action')
