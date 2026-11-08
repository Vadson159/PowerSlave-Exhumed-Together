"""P27 / S9.6 offline contract: native write guard, shim v2 exports, pe-play launcher (no game started).

python -X utf8 -B experiments/pse_together/tools/p27.py offline auto
Every scenario is a real child process: guard_fixture.exe statically imports guard_probe.dll whose DllMain
installs the guard, exactly like the SDL2 shim v2 does for the game. All writes happen under a fresh
evidence directory. The only touch of a real user folder is SHGetKnownFolderPath(SavedGames, KF_FLAG_CREATE),
which opens the EXISTING folder; its mtime is compared before/after.
"""
from pathlib import Path
import ctypes
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'docs/evidence/m1/PSE_TOGETHER_P27'
def _latest_build():
    """Newest CMake build tree in the mod root that has both the loader (bN/p27) and pse_together.dll (bN/dll)."""
    found = [d for d in ROOT.glob('b[0-9]*') if (d / 'p27' / 'pe-together.dll').exists() and (d / 'dll' / 'pse_together.dll').exists()]
    return max(found, key=lambda d: min((d / 'p27' / 'pe-together.dll').stat().st_mtime, (d / 'dll' / 'pse_together.dll').stat().st_mtime), default=ROOT / 'b97')


BUILD_ROOT = _latest_build()
BUILD = Path(os.environ.get('PSE_P27_BUILD_DIR', str(BUILD_ROOT / 'p27'))).resolve()
EXE_NAME = 'anubis_Shipping_Steam_x64.exe'
EXE_SHA = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
HOST_COPY = ROOT / 'experiments/m0/game'
ACCESS_DENIED = 5


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def dump(path, obj):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise ValueError('Evidence outside P27')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf8') as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)


def known_saved_games():
    class GUID(ctypes.Structure):
        _fields_ = [('a', ctypes.c_uint32), ('b', ctypes.c_uint16), ('c', ctypes.c_uint16), ('d', ctypes.c_ubyte * 8)]
    # FOLDERID_SavedGames {4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4}
    g = GUID(0x4C5C32FF, 0xBB9D, 0x43B0, (ctypes.c_ubyte * 8)(0xB5, 0xB4, 0x2D, 0x72, 0xE5, 0x4E, 0xAA, 0xA4))
    out = ctypes.c_wchar_p()
    shell = ctypes.WinDLL('shell32')
    shell.SHGetKnownFolderPath.argtypes = [ctypes.POINTER(GUID), ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_wchar_p)]
    if shell.SHGetKnownFolderPath(ctypes.byref(g), 0, None, ctypes.byref(out)) != 0:
        return None
    value = out.value
    ctypes.WinDLL('ole32').CoTaskMemFree(ctypes.cast(out, ctypes.c_void_p))
    return value


def run_fixture(work, name, ops, env_extra=None, guard=True):
    env = {k: v for k, v in os.environ.items() if not k.startswith('PSE_GUARD_')}
    if guard:
        env.update(env_extra or {})
    scenario = work / 'scenarios'
    scenario.mkdir(exist_ok=True)
    r = subprocess.run([str(BUILD / 'guard_fixture.exe'), *ops], capture_output=True, env=env, timeout=120, cwd=str(scenario))
    (scenario / (name + '.stdout')).write_bytes(r.stdout)
    (scenario / (name + '.stderr')).write_bytes(r.stderr)
    lines = [json.loads(x) for x in r.stdout.decode('utf8', 'replace').splitlines() if x.strip().startswith('{')]
    return r.returncode, lines


def offline(tag):
    if tag == 'auto':
        tag = 'OFFLINE_' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    if not tag.replace('_', '').isalnum():
        raise ValueError('Invalid tag')
    out = OUT / tag
    out.mkdir(parents=True, exist_ok=False)
    work = out / 'work'
    work.mkdir()
    root = work / 'root'
    profile = root / 'profile'
    outside = work / 'outside'
    for d in [profile / 'early_native_logs', profile / 'gpu_cache/steam', profile / 'steam_overlay', outside]:
        d.mkdir(parents=True)
    (outside / 'existing.txt').write_text('keep', encoding='ascii')
    (outside / 'victim.txt').write_text('keep', encoding='ascii')
    (outside / 'doomed.txt').write_text('keep', encoding='ascii')
    (root / 'inside_read.txt').write_text('in', encoding='ascii')
    (profile / 'gpu_cache/steam/profile_only.bin').write_bytes(b'profile')
    checks = []

    def check(name, value, **detail):
        checks.append(dict(name=name, passed=bool(value), **detail))

    counter = [0]

    def env_for(log_name=None):
        counter[0] += 1
        log = root / (log_name or f'guard_{counter[0]}.jsonl')
        return {'PSE_GUARD_ROOT': str(root), 'PSE_GUARD_PROFILE': str(profile), 'PSE_GUARD_LOG': str(log)}, log

    def results(lines):
        return {(x['op'], x.get('path')): x for x in lines if 'path' in x}

    for n in ['guard_fixture.exe', 'guard_probe.dll', 'SDL2.dll', 'pe-play.exe']:
        check('built artifact exists ' + n, (BUILD / n).is_file())

    # 1. Guard disabled control: proves the denial scenarios below are sensitive.
    code, lines = run_fixture(work, 'control_noenv', ['write', str(outside / 'control.txt')], guard=False)
    r = results(lines)
    check('control without guard: outside write succeeds', code == 0 and r[('write', str(outside / 'control.txt'))]['ok'] and (outside / 'control.txt').exists())
    check('control without guard: install reported not configured', lines[0]['code'] == 21)

    # 2. Main matrix with the guard installed in DllMain before main().
    env, log = env_for('main.jsonl')
    steam = outside / 'steamapps/shadercache/1678430'
    junction = root / 'jn'
    subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(outside)], capture_output=True, check=True)
    (outside / 'jn_victim').mkdir()
    saved = known_saved_games()
    saved_mtime = os.stat(saved).st_mtime_ns if saved and os.path.isdir(saved) else None
    ops = ['write', str(root / 'inside.txt'),
           'append', str(root / 'inside.txt'),
           'read', str(root / 'inside_read.txt'),
           'mkdir', str(root / 'newdir'),
           'write', str(root / 'newdir/deep.txt'),
           'write', str(outside / 'blocked.txt'),
           'write', str(outside / 'blocked.txt'),
           'read', str(outside / 'existing.txt'),
           'delete', str(outside / 'victim.txt'),
           'deleteonclose', str(outside / 'doomed.txt'),
           'mkdir', str(outside / 'blockeddir'),
           'mkdir', str(outside),
           'write', str(root / '..' / 'outside' / 'traversal.txt'),
           'write', str(root / 'ads.txt:hidden'),
           'write', str(junction / 'via_junction.txt'),
           'list', str(outside),
           'read', str(steam / 'profile_only.bin'),
           'read', str(steam / 'absent.bin'),
           'write', str(steam / 'cache.bin'),
           'mkdir', str(steam / 'sub'),
           'write', str(steam / 'sub/nested.bin'),
           'write', str(outside / 'Steam/logs/gameoverlay_renderer_9.txt'),
           'write', '\\\\.\\NUL',
           'savedgames',
           'again',
           'stats']
    code, lines = run_fixture(work, 'main', ops, env)
    r = results(lines)
    by_op = {x['op']: x for x in lines if 'path' not in x}
    check('fixture exit 0', code == 0, returncode=code)
    check('guard installed in DllMain before main (code 0)', lines[0]['op'] == 'install' and lines[0]['code'] == 0)
    check('inside write allowed + file created', r[('write', str(root / 'inside.txt'))]['ok'] and (root / 'inside.txt').read_bytes() == b'xx')
    check('inside read allowed', r[('read', str(root / 'inside_read.txt'))]['ok'])
    check('inside mkdir + nested write allowed', r[('mkdir', str(root / 'newdir'))]['ok'] and (root / 'newdir/deep.txt').exists())
    blocked = r[('write', str(outside / 'blocked.txt'))]
    check('outside write denied with ACCESS_DENIED and no file', not blocked['ok'] and blocked['error'] == ACCESS_DENIED and not (outside / 'blocked.txt').exists())
    check('outside read of existing file allowed', r[('read', str(outside / 'existing.txt'))]['ok'])
    check('outside delete denied and file kept', not r[('delete', str(outside / 'victim.txt'))]['ok'] and (outside / 'victim.txt').exists())
    check('outside delete-on-close open denied and file kept', not r[('deleteonclose', str(outside / 'doomed.txt'))]['ok'] and (outside / 'doomed.txt').exists())
    check('outside mkdir denied', not r[('mkdir', str(outside / 'blockeddir'))]['ok'] and not (outside / 'blockeddir').exists())
    exists = r[('mkdir', str(outside))]
    check('mkdir of an EXISTING outside directory is idempotent (183, not ACCESS_DENIED)', not exists['ok'] and exists['error'] == 183, observed=exists)
    trav = r[('write', str(root / '..' / 'outside' / 'traversal.txt'))]
    check('.. traversal to outside denied', not trav['ok'] and not (outside / 'traversal.txt').exists())
    ads = r[('write', str(root / 'ads.txt:hidden'))]
    check('alternate data stream write denied (stricter than the Frida guard)', not ads['ok'])
    via = r[('write', str(junction / 'via_junction.txt'))]
    check('write through junction to outside denied', not via['ok'] and not (outside / 'via_junction.txt').exists())
    check('outside directory list (read open) allowed', r[('list', str(outside))]['ok'])
    check('read of a redirect-class path is served from the profile copy', r[('read', str(steam / 'profile_only.bin'))]['ok'] and not steam.exists())
    check('read of an absent redirect-class path fails (no fallthrough to the real location)', not r[('read', str(steam / 'absent.bin'))]['ok'])
    check('steam shadercache write redirected, nothing outside', r[('write', str(steam / 'cache.bin'))]['ok'] and (profile / 'gpu_cache/steam/cache.bin').exists() and not (steam / 'cache.bin').exists())
    check('steam shadercache subdir + nested file redirected', r[('write', str(steam / 'sub/nested.bin'))]['ok'] and (profile / 'gpu_cache/steam/sub/nested.bin').exists())
    overlay = outside / 'Steam/logs/gameoverlay_renderer_9.txt'
    check('Steam gameoverlay log write redirected to steam_overlay, nothing outside', r[('write', str(overlay))]['ok'] and
          (profile / 'steam_overlay/gameoverlay_renderer_9.txt').exists() and not (outside / 'Steam').exists())
    check('NUL device write exempt', r[('write', '\\\\.\\NUL')]['ok'])
    sg = next((x for x in lines if x['op'] == 'savedgames'), None)
    check('SHGetKnownFolderPath(SavedGames, KF_FLAG_CREATE) succeeds through the guard', sg is not None and sg['ok'], observed=sg)
    check('real Saved Games folder mtime unchanged', saved_mtime is None or os.stat(saved).st_mtime_ns == saved_mtime, path=saved)
    check('second install refused (ERROR_ALREADY_EXISTS 183)', by_op['again']['code'] == 183)
    stats = by_op['stats']
    records = [json.loads(x) for x in log.read_text(encoding='utf8').splitlines()]
    kinds = {}
    for rec in records:
        kinds[rec['kind']] = kinds.get(rec['kind'], 0) + 1
    check('JSONL ledger is unique per path (duplicate blocked.txt write logged once)',
          sum(1 for x in records if x['kind'] == 'denied' and x['path'].lower().endswith('\\blocked.txt')) == 1)
    check('JSONL redirected entries carry the target', all('to' in x and x['to'].lower().startswith('\\??\\' + str(profile).lower()) for x in records if x['kind'] == 'redirected'), kinds=kinds)
    check('stats: denied/redirected/unique match the ledger', stats['denied'] >= kinds.get('denied', 0) and stats['redirected'] >= kinds.get('redirected', 0) and stats['unique'] == len(records), stats=stats, kinds=kinds)
    check('stats: no native errors/overflow beyond the deliberately malformed ADS case', stats['overflow'] == 0 and stats['errors'] <= 1, stats=stats)

    # 2b. Concurrency: 8 threads x 400 iterations through the hooked NtCreateFile/NtOpenFile.
    stress_inside = root / 'stress'
    stress_inside.mkdir()
    stress_out = work / 'stress_outside'
    stress_out.mkdir()
    env, log = env_for('stress.jsonl')
    code, lines = run_fixture(work, 'stress', ['stress', str(stress_inside), str(stress_out), 'stats'], env)
    st = next(x for x in lines if x['op'] == 'stress')
    check('stress: 3200 inside writes allowed, 3200 reads, 3200 outside writes denied, 3200 redirected, no leak',
          code == 0 and st == dict(op='stress', inside_ok=3200, outside_denied=3200, outside_leaked=0, redirect_ok=3200, read_ok=3200), observed=st)
    check('stress: no file leaked outside, redirected files present', not any(stress_out.glob('leak*')) and not (stress_out / 'steamapps').exists() and
          sum(1 for _ in (profile / 'gpu_cache/steam').glob('r*.bin')) == 3200)
    sstats = next(x for x in lines if x['op'] == 'stats')
    check('stress: counters exact (denied 3200, redirected 3200, unique ledger bounded at 256)', sstats['denied'] == 3200 and sstats['redirected'] == 3200 and sstats['unique'] == 256 and sstats['overflow'] > 0, stats=sstats)

    # 3. Fail-closed configuration errors: no hook may remain.
    bad = {'profile outside root': {'PSE_GUARD_ROOT': str(root), 'PSE_GUARD_PROFILE': str(outside), 'PSE_GUARD_LOG': str(root / 'bad1.jsonl')},
           'relative root': {'PSE_GUARD_ROOT': 'relative\\root', 'PSE_GUARD_PROFILE': str(profile), 'PSE_GUARD_LOG': str(root / 'bad2.jsonl')},
           'log outside root': {'PSE_GUARD_ROOT': str(root), 'PSE_GUARD_PROFILE': str(profile), 'PSE_GUARD_LOG': str(outside / 'bad3.jsonl')}}
    for label, env_bad in bad.items():
        probe = outside / ('after_' + label.replace(' ', '_') + '.txt')
        code, lines = run_fixture(work, 'bad_' + label.replace(' ', '_'), ['write', str(probe)], env_bad)
        check(f'config error ({label}): install rejected, no hooks left', lines[0]['code'] != 0 and probe.exists(), install_code=lines[0]['code'])
    (root / 'exists.jsonl').write_text('x')
    code, lines = run_fixture(work, 'bad_log_exists', ['write', str(outside / 'after_log_exists.txt')],
                              {'PSE_GUARD_ROOT': str(root), 'PSE_GUARD_PROFILE': str(profile), 'PSE_GUARD_LOG': str(root / 'exists.jsonl')})
    check('existing log path rejected (no overwrite)', lines[0]['code'] == 80, install_code=lines[0]['code'])
    check('existing log not modified', (root / 'exists.jsonl').read_text() == 'x')

    # 4. Pure rule tables.
    p = 'C:\\root\\profile'
    rules = [('C:\\Users\\Bob\\Saved Games\\Nightdive Studios\\Powerslave EX\\save\\Slot1.sav', p + '\\early_native_logs\\save\\Slot1.sav'),
             ('C:\\Users\\Bob\\Saved Games\\Nightdive Studios\\Powerslave EX\\consoleHistory.txt', p + '\\early_native_logs\\consoleHistory.txt'),
             ('D:\\Program Files (x86)\\Steam\\logs\\gameoverlay_renderer64.txt', p + '\\steam_overlay\\gameoverlay_renderer64.txt'),
             ('E:\\SteamLibrary\\steamapps\\shadercache\\1678430\\fozpipelinesv6\\x.foz', p + '\\gpu_cache\\steam\\fozpipelinesv6\\x.foz'),
             ('C:\\.cache\\AMD\\DxcCache\\abc123', p + '\\gpu_cache\\abc123'),
             ('C:\\Users\\Bob\\Saved Games\\Nightdive Studios\\Powerslave EX', None),
             ('C:\\Users\\Bob\\Documents\\x.txt', None),
             ('C:\\Users\\Bob\\Saved Games\\Other Game\\x', None),
             ('E:\\SteamLibrary\\steamapps\\shadercache\\999\\x', None),
             ('C:\\Steam\\logs\\other.txt', None),
             ('C:\\.cache\\AMD\\DxcCache\\a\\b', None)]
    ops = []
    for src, _ in rules:
        ops += ['map', src, p]
    code, lines = run_fixture(work, 'rules', ops)
    mapped = [x for x in lines if x['op'] == 'map']
    for (src, expected), got in zip(rules, mapped):
        check('rule ' + src, (got['path'] if got['ok'] else None) == expected, observed=got['path'] if got['ok'] else None, expected=expected)
    canon = [('\\??\\C:\\A\\.\\b\\..\\C', (1, 'C:\\A\\C')), ('\\\\?\\c:\\x\\y', (1, 'c:\\x\\y')), ('C:/a//b', (1, 'C:\\a\\b')), ('C:\\a\\b.', (-1, '')),
             ('C:\\a\\b ', (-1, '')), ('C:\\a\\..\\..', (-1, '')), ('C:\\a\\f:s', (-1, '')), ('\\Device\\Null', (0, '')), ('relative\\x', (0, '')),
             ('\\??\\UNC\\srv\\share\\x', (0, ''))]
    code, lines = run_fixture(work, 'canon', sum([['canonical', s] for s, _ in canon], []))
    for (src, (status, text)), got in zip(canon, [x for x in lines if x['op'] == 'canonical']):
        check('canonical ' + src, got['status'] == status and (status != 1 or got['path'] == text), observed=got)

    # 5. Shim v2 export surface == accepted SDL2 export set (names, ordinals, forwarders), no Frida/python dependency.
    sys.path.insert(0, str(ROOT / 'docs/research'))
    from inventory import PE
    saved_manifest = json.loads((ROOT / 'docs/evidence/E005_BINARY_MANIFEST.json').read_text('utf-8-sig'))
    expected = saved_manifest['binaries'][1]['exports']
    pe = PE(BUILD / 'SDL2.dll')
    exports = pe.exports()
    byname = {e['name']: e for e in exports}
    check('shim v2 export names+ordinals = E005', {(e['name'], e['ordinal']) for e in expected} == {(e['name'], e['ordinal']) for e in exports}, count=len(exports))
    bad_forward = [e['name'] for e in expected if byname[e['name']].get('forwarder') != (None if e['name'] == 'SDL_Init' else 'SDL2_pse_original.' + e['name'])]
    check('shim v2 forwarders: 737 forward, SDL_Init local', not bad_forward, bad=bad_forward[:5])
    imports = [i['dll'].lower() for i in pe.imports()]
    check('shim v2 x64, no Frida/python import', pe.machine == 0x8664 and not any('frida' in i or 'python' in i for i in imports), imports=imports)

    # 6. pe-play launcher dry runs on a copy of the EXE (no game process).
    play_root = work / 'play'
    (play_root / 'game').mkdir(parents=True)
    r0 = subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--root', str(play_root)], capture_output=True, timeout=60)
    check('pe-play refuses root without game EXE', r0.returncode != 0 and not (play_root / 'profile').exists(), returncode=r0.returncode)
    shutil.copyfile(HOST_COPY / EXE_NAME, play_root / 'game' / EXE_NAME)
    r1 = subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--root', str(play_root)], capture_output=True, timeout=60)
    text = r1.stdout.decode('utf-16-le', 'replace')
    (out / 'play_dry_run.txt').write_text(text, encoding='utf8')
    pr = play_root / 'profile'
    check('pe-play dry run exits 0', r1.returncode == 0, returncode=r1.returncode)
    check('pe-play profile tree created', all((pr / d).is_dir() for d in ['roaming', 'local', 'temp', 'user', 'early_saved_games', 'early_native_logs', 'gpu_cache/steam/AMDv1', 'gpu_cache/steam/fozpipelinesv6', 'steam_overlay', 'logs']))
    check('pe-play wrote steam_appid.txt 1678430', (pr / 'steam_appid.txt').read_bytes() == b'1678430\n')
    check('pe-play command carries -basepath/-userpath of the install', '-basepath' in text and '-userpath' in text and str(play_root / 'game') in text and str(pr) in text)
    (play_root / 'game' / EXE_NAME).write_bytes((play_root / 'game' / EXE_NAME).read_bytes()[:2048] + b'\x00' + (play_root / 'game' / EXE_NAME).read_bytes()[2049:])
    r2 = subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--root', str(play_root)], capture_output=True, timeout=60)
    check('pe-play refuses a modified EXE (193) before touching the game', r2.returncode == 193, returncode=r2.returncode)

    # 7. S9.7: pe-play roles, portable installer, broker v2 gates (no game process).
    role_root = work / 'play_role'
    (role_root / 'game').mkdir(parents=True)
    shutil.copyfile(HOST_COPY / EXE_NAME, role_root / 'game' / EXE_NAME)
    rr = subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--role', 'host', '--root', str(role_root)], capture_output=True, timeout=60)
    rtext = rr.stdout.decode('utf-16-le', 'replace')
    rprofile = role_root / 'profile' / 'host'
    check('pe-play --role host: dry run exit 0, profile\\host created', rr.returncode == 0 and rprofile.is_dir() and 'role=host' in rtext, returncode=rr.returncode)
    cfg_bytes = (rprofile / 'early_native_logs/kexengine.cfg').read_bytes()
    check('pe-play --role host: profile config seeded with D3D11 + the 35 standard binds (W/A/S/D, mouse, Tab)', cfg_bytes.startswith(b'seta r_rhirenderfamily "d3d11"\n') and
          sum(1 for l in cfg_bytes.splitlines() if l.startswith(b'bind ')) == 35 and b'bind w "forward"' in cfg_bytes and b'bind mouse_left "attack"' in cfg_bytes and b'bind tab "automap"' in cfg_bytes)
    subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--role', 'host', '--root', str(role_root)], capture_output=True, timeout=60)
    check('pe-play config seeding is idempotent (second run changes nothing)', (rprofile / 'early_native_logs/kexengine.cfg').read_bytes() == cfg_bytes)
    # An engine-rewritten config from an earlier run (no binds) must get the binds appended, d3d11 kept once.
    (rprofile / 'early_native_logs/kexengine.cfg').write_bytes(b'seta snd_volume "0.85"\nseta r_rhirenderfamily "d3d11"\n')
    subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--role', 'host', '--root', str(role_root)], capture_output=True, timeout=60)
    repaired = (rprofile / 'early_native_logs/kexengine.cfg').read_bytes()
    check('pe-play repairs an existing profile config without binds (binds appended, no duplicate renderer line)',
          sum(1 for l in repaired.splitlines() if l.startswith(b'bind ')) == 35 and repaired.count(b'r_rhirenderfamily') == 1 and repaired.startswith(b'seta snd_volume'))
    check('pe-play prints a native log path inside the profile for the role', str(rprofile / 'logs') in rtext and 'native_host_' in rtext)
    rb = subprocess.run([str(BUILD / 'pe-play.exe'), '--dry-run', '--role', 'observer', '--root', str(role_root)], capture_output=True, timeout=60)
    check('pe-play rejects an unknown role', rb.returncode == 87, returncode=rb.returncode)
    # Broker v2 in a foreign process (python.exe): exports present, guard confirmation required, foreign EXE refused (193).
    broker = ctypes.WinDLL(str(BUILD / 'pe-together.dll'))
    check('broker v2 exports PSELoaderStartV2 + PSELoaderCheckFile', all(hasattr(broker, n) for n in ('PSELoaderStartV2', 'PSELoaderCheckFile')))
    def broker_start(flag):
        code = ("import ctypes,sys;d=ctypes.WinDLL(sys.argv[1]);f=d.PSELoaderStartV2;f.argtypes=[ctypes.c_uint32];f.restype=ctypes.c_uint32;"
                "a=f(int(sys.argv[2]));b=f(1);print(a,b)")
        r_ = subprocess.run([sys.executable, '-B', '-c', code, str(BUILD / 'pe-together.dll'), str(flag)], capture_output=True, timeout=60)
        return r_.stdout.decode().split()
    check('broker v2 refuses without the guard confirmation (ERROR_ACCESS_DENIED 5) and the refusal is latched', broker_start(0) == ['5', '5'])
    check('broker v2 refuses a foreign EXE even with the confirmation (193), latched', broker_start(1) == ['193', '193'])
    inst = work / 'install'
    ri = subprocess.run([sys.executable, '-X', 'utf8', '-B', str(Path(__file__).resolve()), 'install', str(inst)], capture_output=True, timeout=600, env={**os.environ, 'PSE_P27_BUILD_DIR': str(BUILD)})
    (out / 'install.stdout').write_bytes(ri.stdout + ri.stderr)
    check('installer succeeded', ri.returncode == 0, tail=ri.stdout.decode('utf8', 'replace')[-200:])
    if ri.returncode == 0:
        man = json.loads((inst / 'install_manifest.json').read_text(encoding='utf8'))
        ini = (inst / 'game/pe-together.ini').read_text(encoding='utf-16')
        check('installer: 77 verified files, EXE and original SDL SHA exact', man['files'] == 77 and man['hashes']['anubis_Shipping_Steam_x64.exe'] == EXE_SHA and man['hashes']['SDL2_pse_original.dll'] == 'f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0')
        check('installer: ini carries the exact payload SHA', 'sha256=' + man['hashes']['pse_together.dll'] in ini)
        check('installer: shim = built SDL2.dll, broker = built pe-together.dll', man['hashes']['SDL2.dll'] == sha(BUILD / 'SDL2.dll') and man['hashes']['pe-together.dll'] == sha(BUILD / 'pe-together.dll'))
        check('installer: host.bat / join.bat are ASCII and call pe-play with the right role',
              b'--role host' in (inst / 'host.bat').read_bytes() and b'--role client' in (inst / 'join.bat').read_bytes() and all(b < 128 for b in (inst / 'host.bat').read_bytes() + (inst / 'join.bat').read_bytes()))
        check('installer refuses an existing root', subprocess.run([sys.executable, '-X', 'utf8', '-B', str(Path(__file__).resolve()), 'install', str(inst)], capture_output=True, timeout=60, env={**os.environ, 'PSE_P27_BUILD_DIR': str(BUILD)}).returncode != 0)

    ok = all(c['passed'] for c in checks)
    result = dict(status='PASS' if ok else 'FAIL', passed=sum(c['passed'] for c in checks), total=len(checks), game_started=False,
                  artifacts={n: sha(BUILD / n) for n in ['SDL2.dll', 'pe-play.exe', 'guard_probe.dll', 'guard_fixture.exe']}, checks=checks)
    dump(out / 'RESULT.json', result)
    print(f"P27 offline {'PASS' if ok else 'FAIL'} {result['passed']}/{result['total']}")
    for c in checks:
        if not c['passed']:
            print('FAILED:', json.dumps(c, ensure_ascii=False))
    return 0 if ok else 1


SDL_SHA = 'f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0'
PAYLOAD = Path(os.environ.get('PSE_PAYLOAD_DLL', str(BUILD.parent / 'dll' / 'pse_together.dll')))  # one DLL, both roles


def install(root_arg, source_arg=None):
    """Build a portable install root (D15a: a COPY of the 77 verified game files; the Steam install is never touched).

    python p27.py install <new-root-dir> [<game-dir-with-the-77-files>]
    The source defaults to the verified lab copy experiments/m0/game; files are checked against M0_001_PREPARATION by SHA.
    """
    root = Path(root_arg).resolve()
    source = Path(source_arg).resolve() if source_arg else HOST_COPY
    if root.exists():
        raise SystemExit('install root must not exist yet: ' + str(root))
    prep = json.loads((ROOT / 'docs/evidence/m0/M0_001_PREPARATION.json').read_text('utf-8-sig'))
    game = root / 'game'
    for row in prep['independent_copy_files']:
        src = source / row['relative_path']
        if sha(src) != row['sha256']:
            raise SystemExit('source file differs from the verified manifest: ' + row['relative_path'])
    for row in prep['independent_copy_files']:
        dest = game / row['relative_path']
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / row['relative_path'], dest)
        assert sha(dest) == row['sha256']
    assert sha(game / 'SDL2.dll') == SDL_SHA
    shutil.copyfile(game / 'SDL2.dll', game / 'SDL2_pse_original.dll')
    shutil.copyfile(BUILD / 'SDL2.dll', game / 'SDL2.dll')
    shutil.copyfile(BUILD / 'pe-together.dll', game / 'pe-together.dll')
    shutil.copyfile(PAYLOAD, game / 'pse_together.dll')
    # Third-person player sprites (P8): KEX auto-mounts mods/*.kpf. Host P2 and the client proxy of the host use them.
    (game / 'mods').mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'experiments/pse_together/assets/coop_sprites_john.kpf', game / 'mods/coop_sprites_john.kpf')
    ini = '[payload]\nsha256=' + sha(game / 'pse_together.dll') + '\n'
    (game / 'pe-together.ini').write_text(ini, encoding='utf-16')
    shutil.copyfile(BUILD / 'pe-play.exe', root / 'pe-play.exe')
    for name, role in (('host', 'host'), ('join', 'client')):
        (root / (name + '.bat')).write_bytes(('@echo off\r\ncd /d "%~dp0"\r\n"%~dp0pe-play.exe" --role ' + role + '\r\nset "pse_result=%ERRORLEVEL%"\r\n'
            'if not "%pse_result%"=="0" echo Failed with code %pse_result%\r\npause\r\nexit /b %pse_result%\r\n').encode('ascii'))
    manifest = dict(root=str(root), source=str(source), files=len(prep['independent_copy_files']),
                    hashes={n: sha(game / n) for n in ['SDL2.dll', 'SDL2_pse_original.dll', 'pe-together.dll', 'pse_together.dll', 'anubis_Shipping_Steam_x64.exe']},
                    play=sha(root / 'pe-play.exe'), note='payload = pse_together.dll (host and client in one DLL, role from PSE_ROLE)')
    (root / 'install_manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    print('installed', root)
    print(json.dumps(manifest['hashes'], indent=2))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'offline':
        sys.exit(offline(sys.argv[2]))
    if sys.argv[1:2] == ['install'] and len(sys.argv) in (3, 4):
        install(*sys.argv[2:])
        sys.exit(0)
    raise SystemExit('usage: p27.py offline <tag|auto> | install <new-root> [<source-game-dir>]')
