"""P24 local loader workflow. Mutations are restricted to two existing workspace copies.

baseline | offline <unique-tag> | stage <host|client> <new-run> | restore <role>
launch/bind/action use the accepted DOC-41/51 REA lane; no Frida DLL loader is used.
Never run launch outside the REA lane. No gameplay permit is generated here.
"""
from pathlib import Path
import ctypes
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import datetime

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P24'
BUILD = Path(os.environ.get('PSE_P24_BUILD_DIR', str(ROOT / 'b69/p24'))).resolve()
RUNTIME = ROOT / 'experiments/pse_together/runtime_p24'
EXE = 'anubis_Shipping_Steam_x64.exe'
SHA = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
SDL_SHA = 'f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0'
COPIES = {'host': ROOT / 'experiments/m0/game', 'client': ROOT / 'experiments/m1/native_second_instance/game'}
PAYLOAD = {
    'host': ROOT / 'docs/evidence/m1/PSE_TOGETHER_P1/P1_HOST_03/native_artifact/pse_m11_bootstrap.dll',
    'client': ROOT / 'docs/evidence/m1/PSE_TOGETHER_P1/P1_CLIENT_03/native_artifact/pse_native_client58b.dll',
}
PAYLOAD_SHA = {'host': '73b76e1f0698d314905f5991a478237557f50b5c7db4daeac2536a66a9303376',
               'client': '2366326a99ee05c4a9e4ec388431af901cbc15d99d9d427041a2e38c81758e97'}
ADDITIONS = ['SDL2_pse_original.dll', 'pe-together.dll', 'pse-runtime.dll', 'pe-together.ini']

def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def load(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def dump(path, obj):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()): raise ValueError('Evidence outside P24')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf8') as f: json.dump(obj, f, indent=2, ensure_ascii=False)

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value

def receipt(role, run=None):
    if role not in COPIES: raise ValueError('Invalid role')
    candidates = list(OUT.glob('STAGE_' + role + '*.json'))
    if run is not None: candidates = [p for p in candidates if load(p)['run'] == run]
    if not candidates: raise ValueError('No owned stage receipt')
    return load(max(candidates, key=lambda p: p.stat().st_mtime_ns))

def games():
    return module('p24_audit52', ROOT / 'experiments/m1/terminal_shutdown/audit.py').game_check()

def baseline():
    g = games()
    assert g['original_full_snapshot_equal'] and not g['errors'] and not g['client_copy_errors'], g
    dump(OUT / 'BASELINE.json', {'game': g, 'copies': {r: sha(p / EXE) for r, p in COPIES.items()}})
    print('P24 baseline: original 77 + both copies + protected profiles PASS')

def integrity():
    before = load(OUT / 'BASELINE.json')
    after = games()
    ok = after == before['game'] and all(sha(p / EXE) == SHA for p in COPIES.values())
    dump(OUT / 'INTEGRITY_FINAL.json', {'status': 'PASS' if ok else 'FAIL', 'game': after,
         'equals_baseline': after == before['game'], 'exe_hashes': {r: sha(p / EXE) for r, p in COPIES.items()}})
    assert ok
    print('P24 integrity PASS: original77, copies, protected profiles unchanged')

def offline(tag):
    if tag == 'auto': tag = 'OFFLINE_' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    if not tag.replace('_', '').isalnum(): raise ValueError('Invalid tag')
    out = OUT / tag
    out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(ROOT / 'docs/research'))
    from inventory import PE
    checks = []
    def check(name, value, **detail):
        checks.append(dict(name=name, passed=bool(value), **detail))
    saved = load(ROOT / 'docs/evidence/E005_BINARY_MANIFEST.json')
    expected = saved['binaries'][1]['exports']
    exports = PE(BUILD / 'SDL2.dll').exports()
    byname = {e['name']: e for e in exports}
    check('SDL exports names+ordinals', {(e['name'], e['ordinal']) for e in expected} == {(e['name'], e['ordinal']) for e in exports}, count=len(exports))
    for e in expected:
        got = byname.get(e['name'], {})
        check('export ' + e['name'], got.get('ordinal') == e['ordinal'] and got.get('forwarder') ==
              (None if e['name'] == 'SDL_Init' else 'SDL2_pse_original.' + e['name']))
    imported = next(i for i in saved['binaries'][0]['imports'] if i['dll'].lower() == 'sdl2.dll')['symbols']
    check('all 99 EXE SDL imports covered', len(imported) == 99 and all(i['name'] in byname for i in imported))
    check('EXE imports SDL_Init', any(i.get('name') == 'SDL_Init' for i in imported))
    for name in ['SDL2.dll', 'pe-together.dll', 'pe-check.exe']:
        pe = PE(BUILD / name)
        imports = [i['dll'].lower() for i in pe.imports()]
        check(name + ' x64/no Frida dependency', pe.machine == 0x8664 and not any('frida' in i or 'python' in i for i in imports), imports=imports)
    damaged = out / 'changed.exe'
    shutil.copyfile(COPIES['host'] / EXE, damaged)
    with damaged.open('r+b') as f:
        f.seek(2048)
        value = f.read(1)
        f.seek(2048)
        f.write(bytes([value[0] ^ 1]))
    for label, path, code in [('valid', COPIES['host'] / EXE, 0), ('changed', damaged, 193), ('missing', out / 'missing.exe', 2)]:
        r = subprocess.run([str(BUILD / 'pe-check.exe'), str(path)], capture_output=True, timeout=30)
        message = r.stdout.decode('utf-16-le')
        (out / (label + '.txt')).write_text(message, encoding='utf8')
        check('checker ' + label, r.returncode == code and SHA in message and 'Actual SHA-256' in message,
              returncode=r.returncode, file_sha256=sha(path) if path.exists() else None)
    # Real DLL, foreign process refusal. Does not queue APC or create a journal.
    broker = ctypes.WinDLL(str(BUILD / 'pe-together.dll'))
    broker.PSELoaderStart.restype = ctypes.c_uint32
    check('broker foreign EXE refusal', broker.PSELoaderStart() == 193)
    check('broker repeated refusal stable', broker.PSELoaderStart() == 193)
    fn = broker.PSELoaderCheckFile
    fn.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
    fn.restype = ctypes.c_uint32
    buf = ctypes.create_unicode_buffer(32768)
    check('broker checks exact EXE without starting game', fn(str(COPIES['host'] / EXE), buf, len(buf)) == 0)
    check('broker rejects modified EXE', fn(str(damaged), buf, len(buf)) == 193)
    check('broker detects output truncation', fn(str(damaged), buf, 2) == 122)
    check('broker rejects null path', fn(None, buf, len(buf)) == 87)
    # Call forwarded APIs in a disposable Python child, without Frida or SDL_Init.
    fixture = out / 'forwarding'
    fixture.mkdir()
    shutil.copy2(BUILD / 'SDL2.dll', fixture / 'SDL2.dll')
    shutil.copy2(COPIES['host'] / 'SDL2.dll', fixture / 'SDL2_pse_original.dll')
    (fixture / 'exports.json').write_text(json.dumps(expected), encoding='utf8')
    script = """import ctypes,json,sys
from pathlib import Path
d=ctypes.CDLL(sys.argv[1]);v=(ctypes.c_ubyte*3)();d.SDL_GetVersion(ctypes.byref(v))
d.SDL_strlen.argtypes=[ctypes.c_char_p];d.SDL_strlen.restype=ctypes.c_size_t
d.SDL_sin.argtypes=[ctypes.c_double];d.SDL_sin.restype=ctypes.c_double
base=Path(sys.argv[1]).parent
o=ctypes.CDLL(str(base/'SDL2_pse_original.dll'));k=ctypes.WinDLL('kernel32')
k.GetProcAddress.argtypes=[ctypes.c_void_p,ctypes.c_void_p];k.GetProcAddress.restype=ctypes.c_void_p
same=ordinals=0
for e in json.loads((base/'exports.json').read_text()):
 name=ctypes.c_char_p(e['name'].encode())
 ptr=k.GetProcAddress(d._handle,ctypes.cast(name,ctypes.c_void_p))
 ptr0=k.GetProcAddress(o._handle,ctypes.cast(name,ctypes.c_void_p))
 byordinal=k.GetProcAddress(d._handle,e['ordinal'])
 ordinals+=bool(ptr and ptr==byordinal)
 same+=bool(ptr and (ptr!=ptr0 if e['name']=='SDL_Init' else ptr==ptr0))
print(json.dumps(dict(version=list(v),length=d.SDL_strlen(b'P24-forward'),sine=d.SDL_sin(0.5),resolved=same,ordinals=ordinals)))
"""
    r = subprocess.run([sys.executable, '-B', '-c', script, str(fixture / 'SDL2.dll')], capture_output=True, timeout=30)
    (out / 'forwarding_stdout.txt').write_bytes(r.stdout)
    (out / 'forwarding_stderr.txt').write_bytes(r.stderr)
    observed = json.loads(r.stdout) if not r.returncode else {}
    check('native SDL forwarding no Frida', r.returncode == 0 and observed.get('version') == [2, 0, 14]
          and observed.get('length') == 11 and abs(observed.get('sine', 0) - 0.479425538604203) < 1e-12
          and observed.get('resolved') == 738 and observed.get('ordinals') == 738, observed=observed)
    for role in COPIES:
        check('frozen P1 ' + role, sha(PAYLOAD[role]) == PAYLOAD_SHA[role])
    ok = all(c['passed'] for c in checks)
    dump(out / 'RESULT.json', dict(status='PASS' if ok else 'FAIL', checks=checks, passed=sum(c['passed'] for c in checks),
         total=len(checks), game_started=False, artifacts={n: sha(BUILD / n) for n in ['SDL2.dll', 'pe-together.dll', 'pe-check.exe']}))
    print(f"P24 offline {'PASS' if ok else 'FAIL'} {sum(c['passed'] for c in checks)}/{len(checks)}")
    for c in checks:
        if not c['passed']: print(c)
    assert ok

def stage(role, run):
    if role not in COPIES or not run.replace('_', '').isalnum(): raise ValueError('Invalid role/run')
    copy = COPIES[role].resolve()
    assert copy.is_relative_to(ROOT.resolve()) and not copy.is_symlink()
    assert not (OUT / run).exists() and not (RUNTIME / 'profile' / run).exists()
    assert sha(copy / EXE) == SHA and sha(copy / 'SDL2.dll') == SDL_SHA
    original = Path(r'S:\Games\SteamLibrary\steamapps\common\PowerSlave Exhumed')
    for row in load(ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json')['independent_copy_files']:
        p = copy / row['relative_path']
        assert sha(p) == row['sha256'] and not os.path.samefile(p, original / row['relative_path'])
    assert all(not (copy / n).exists() for n in ADDITIONS)
    assert sha(PAYLOAD[role]) == PAYLOAD_SHA[role]
    # Recovery receipt exists before mutation. Restoration is idempotent and hash checked.
    receipt = {'role': role, 'run': run, 'copy': str(copy), 'sdl_before': SDL_SHA,
               'proxy_sha': sha(BUILD / 'SDL2.dll'), 'payload_sha': PAYLOAD_SHA[role],
               'broker_sha': sha(BUILD / 'pe-together.dll'), 'no_exe_patch': True}
    dump(OUT / ('STAGE_' + role + '_' + run + '.json'), receipt)
    shutil.copy2(copy / 'SDL2.dll', copy / 'SDL2_pse_original.dll')
    for source, dest in [(BUILD / 'pe-together.dll', 'pe-together.dll'), (PAYLOAD[role], 'pse-runtime.dll')]:
        shutil.copy2(source, copy / dest)
    (copy / 'pe-together.ini').write_text('[loader]\nrole=' + role + '\nnative_log=' + str(OUT / run / 'native.jsonl') + '\n', encoding='utf-16')
    shutil.copyfile(BUILD / 'SDL2.dll', copy / 'SDL2.dll')
    print('Staged P24', role, run, 'EXE unchanged', sha(copy / EXE))

def restore(role):
    stage_record = receipt(role)
    copy = COPIES[role].resolve()
    assert str(copy) == stage_record['copy'] and copy.is_relative_to(ROOT.resolve())
    assert not list(copy.glob('*.exe')) or sha(copy / EXE) == SHA
    launch_path = OUT / stage_record['run'] / 'launch.json'
    if launch_path.exists():
        launch = load(launch_path)
        k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        k.OpenProcess.restype = ctypes.c_void_p
        k.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        k.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = k.OpenProcess(0x100000, 0, launch['pid'])
        if handle:
            try:
                if k.WaitForSingleObject(handle, 0) != 0: raise RuntimeError('Process PID still alive; restore only after exit')
            finally: k.CloseHandle(handle)
    # The caller must terminate/wait its owned game before restoring mapped modules.
    backup = copy / 'SDL2_pse_original.dll'
    if backup.exists():
        assert sha(backup) == SDL_SHA
        assert sha(copy / 'SDL2.dll') in [SDL_SHA, stage_record['proxy_sha']]
        shutil.copyfile(backup, copy / 'SDL2.dll')
    assert sha(copy / 'SDL2.dll') == SDL_SHA
    for name in ADDITIONS:
        p = copy / name
        assert p.resolve().is_relative_to(copy)
        if p.exists(): p.unlink()
    dump(OUT / ('RESTORE_' + role + '_' + stage_record['run'] + '.json'), dict(status='PASS', sdl_sha256=sha(copy / 'SDL2.dll'), exe_sha256=sha(copy / EXE)))
    print('Restored P24', role)

def accepted(role, run=None):
    sys.path.insert(0, str(Path(__file__).parent))
    import runtime_p1
    runtime_p1.LAB = RUNTIME
    runtime_p1.OUT = OUT
    m = runtime_p1.accepted_runner(role)
    stage_record = receipt(role, run)
    def verify_staged():
        prep = load(ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json')
        for row in prep['independent_copy_files']:
            p = COPIES[role] / row['relative_path']
            expected = stage_record['proxy_sha'] if row['relative_path'].lower() == 'sdl2.dll' else row['sha256']
            assert sha(p) == expected and not os.path.samefile(p, m.prepare.SOURCE / row['relative_path'])
        assert sha(COPIES[role] / 'SDL2_pse_original.dll') == SDL_SHA
        assert sha(COPIES[role] / 'pe-together.dll') == stage_record['broker_sha']
        assert sha(COPIES[role] / 'pse-runtime.dll') == stage_record['payload_sha']
        return dict(files=77, intentional_proxy_replacement=True, stage=stage_record, original_never_written=True)
    m.verify_copy = verify_staged
    return m

def bind(role, run):
    m = accepted(role, run)
    m.owned_run(run)
    import importlib
    sys.argv = ['generate_observer', run]
    importlib.import_module('generate_observer').main()
    sys.argv = ['bind_probe', run, 'isolation']
    importlib.import_module('bind_probe').main()
    # No LoadLibrary/M11Prepare/Permit JS is generated or executed.
    dump(OUT / run / 'LOADER_TEST_SCOPE.json', dict(proxy_loader=True, frida_used_for='existing OS isolation and observation only',
         js_dll_loader=False, gameplay_permit=False, exe_sha256=sha(COPIES[role] / EXE)))

def inputs(args):
    import runtime_p1
    runtime_p1.OUT = OUT
    runtime_p1.RUNS = {role: receipt(role)['run'] for role in COPIES}
    runtime_p1.inputs(args)

if __name__ == '__main__':
    command = sys.argv[1]
    if command in ['baseline', 'integrity']: globals()[command]()
    elif command == 'offline': offline(sys.argv[2])
    elif command == 'stage': stage(sys.argv[2], sys.argv[3])
    elif command == 'restore': restore(sys.argv[2])
    elif command == 'launch': accepted(sys.argv[2], sys.argv[3]).launch(sys.argv[3])
    elif command == 'bind': bind(sys.argv[2], sys.argv[3])
    elif command == 'action': accepted(sys.argv[2], sys.argv[3]).action(sys.argv[3], sys.argv[4], sys.argv[5:])
    elif command == 'input': inputs(sys.argv[2:])
    else: raise ValueError(command)
