"""Enemy sync smoke on one PC: python -u -X utf8 smoke_mobs.py <install-root>

The host alone goes to KARNAK, the client joins and follows. Then the host spawns 3 enemies during play in front of the client's body
(type of the first map enemy, like a vase scorpion) and the client spawns 2 of its own. The host's must appear at the client as mirrors,
the client's own must be removed, and the client's enemy check (world_enemy_mismatch) must end up clean.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_mobs_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/KARNAK.MAP')
os.environ.setdefault('PSE_DEBUG_SPAWN_AI', '3')
os.environ.setdefault('PSE_DEBUG_SPAWN_AI_CLIENT', '2')
os.environ.setdefault('PSE_DEBUG_SPAWN_AI_FILE', str(out / 'spawn.flag'))
for prof_dir in (root / 'profile').glob('*'):
    saves = prof_dir / 'early_native_logs' / 'saves'
    if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_' + out.name)); saves.mkdir()
results = []


def pids():
    t = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq anubis_Shipping_Steam_x64.exe', '/FO', 'CSV', '/NH'], capture_output=True).stdout.decode('latin1')
    return {int(l.split(',')[1].strip('"')) for l in t.splitlines() if l.startswith('"anubis')}


def launch(role):
    cfg = root / 'profile' / role / 'early_native_logs' / 'kexengine.cfg'
    if not cfg.exists():
        cfg.parent.mkdir(parents=True, exist_ok=True); cfg.write_text((HERE / 'smoke_kexengine.cfg').read_text(encoding='utf8'), encoding='utf8')
    before = pids()
    subprocess.Popen([str(root / 'pe-play.exe'), '--role', role], cwd=str(root), stdout=(out / f'{role}_stdout.txt').open('wb'), stderr=subprocess.STDOUT,
                     env={**os.environ, 'PSE_PLAYER_NAME': 'Host' if role == 'host' else 'Guest'})
    for _ in range(60):
        time.sleep(1)
        new = pids() - before
        if new and act.hwnd_of(next(iter(new))): return next(iter(new))
    raise SystemExit(role + ' did not start')


def keys(pid, *seq):
    h = act.hwnd_of(pid); act.HW[0] = h
    for a in seq:
        if a.startswith('wait='): time.sleep(float(a[5:])); continue
        if a.startswith('hold:'):
            name, sec = a[5:].split('='); act.focus(h); act.post(act.VK[name], True); time.sleep(float(sec)); act.post(act.VK[name], False); continue
        act.focus(h); act.tap(act.VK[a]); time.sleep(0.25)


def shot(pid, name):
    subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(pid), str(out / name)], capture_output=True, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'})


def look(pid, name, wait=2.0):
    act.focus(act.hwnd_of(pid)); time.sleep(wait); shot(pid, name)


def logfile(role):
    fs = sorted(glob.glob(str(root / 'profile' / role / 'logs' / f'native_{role}_*.jsonl')), key=os.path.getmtime)
    return fs[-1] if fs else None


def rows(role):
    f = logfile(role); res = []
    if not f: return res
    for l in open(f, encoding='utf8', errors='replace'):
        if l.strip().endswith('}'):
            try: res.append(json.loads(l))
            except ValueError: pass
    return res


def count(role, name):
    f = logfile(role)
    return open(f, 'rb').read().count(b'"event":"' + name.encode() + b'"') if f else 0


def wait_for(role, name, n=1, timeout=90, focus=None):
    end = time.time() + timeout; turn = 0
    while time.time() < end:
        if count(role, name) >= n: return True
        targets = focus or both
        act.focus(act.hwnd_of(targets[turn % len(targets)])); turn += 1   # one PC: only the focused window ticks
        time.sleep(1.5)
    return False


def check(name, ok):
    results.append((name, bool(ok))); print(('PASS ' if ok else 'FAIL ') + name, flush=True)


def show(role, *names):
    for r in rows(role):
        if r['event'] in names: print(' ', role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns')})


def ready_again(tag, host_rebinds=None, client_ready=None, timeout=180):
    if host_rebinds is not None: check(f'{tag}: host session re-bound ({host_rebinds})', wait_for('host', 'network_world_rebound', host_rebinds, timeout))
    if client_ready is not None: check(f'{tag}: client ready again ({client_ready})', wait_for('client', 'client_input_domain_ready', client_ready, timeout))
    check(f'{tag}: still one welcome, no rejoin', count('client', 'client_welcome') == 1 and count('client', 'client_identity_reset') == 0)
    check(f'{tag}: host session never restarted', count('host', 'network_started') == 1)


def pause_restart(pid):
    # Pause menu: Resume, Options, Reload Last Checkpoint, Restart Level, Exit To Title, Quit; Restart asks to confirm (Yes is left).
    keys(pid, 'ESC', 'wait=1.5', 'DOWN', 'DOWN', 'DOWN', 'RETURN', 'wait=1', 'LEFT', 'RETURN', 'wait=2')


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [host]
check('host alone goes to KARNAK', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
check('client ready on KARNAK', wait_for('client', 'client_input_domain_ready', 1, 180))
for _ in range(4): act.focus(act.hwnd_of(host)); time.sleep(1.5); act.focus(act.hwnd_of(client)); time.sleep(1.5)
before = [r for r in rows('client') if r['event'] == 'world_enemy_mismatch']
print('   mismatches before the spawn:', len(before), [(r['local_only'], r['host_only'], r['local_dynamic'], r['host_dynamic_missing']) for r in before[-3:]])
spawned_before = count('client', 'world_dynamic_spawned')
(out / 'spawn.flag').write_text('go')
check('host spawned enemies', wait_for('host', 'world_debug_spawn_ai', 1, 60))
check('client spawned its own', wait_for('client', 'world_debug_spawn_ai', 1, 60))
for _ in range(6): act.focus(act.hwnd_of(host)); time.sleep(1.5); act.focus(act.hwnd_of(client)); time.sleep(1.5)
hs = [r for r in rows('host') if r['event'] == 'world_debug_spawn_ai'][-1:]
cs = [r for r in rows('client') if r['event'] == 'world_debug_spawn_ai'][-1:]
print('   host spawn:', hs, '| client spawn:', cs)
mirrors = count('client', 'world_dynamic_spawned') - spawned_before
check('client mirrored the host enemies', hs and mirrors >= hs[0]['spawned'])
check('client removed its own spawned enemies', count('client', 'world_dynamic_culled') >= 1)
look(client, '1_client.png', 1); look(host, '2_host.png', 1)
for _ in range(4): act.focus(act.hwnd_of(client)); time.sleep(2.5)   # let the client's check run a few times while it ticks
after = [r for r in rows('client') if r['event'] == 'world_enemy_mismatch']
last = [r for r in after if r['utc'] > max([x['utc'] for x in rows('client') if x['event'] == 'world_debug_spawn_ai'] or [''])]
print('   mismatches after:', [(r['utc'][11:19], r['local_only'], r['host_only'], r['local_dynamic'], r['host_dynamic_missing'], r['local_only_list'][:3], r['host_only_list'][:3]) for r in last[-4:]])
check("no enemy of the client's own and no missing host mirror at the end", not last or (last[-1]['local_dynamic'] == 0 and last[-1]['host_dynamic_missing'] == 0))
print('RESULT', 'PASS' if all(ok for _, ok in results) else 'FAIL', sum(ok for _, ok in results), '/', len(results))
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
