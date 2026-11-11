"""Smoke of the round-9 fixes on one PC: python -u -X utf8 smoke_m9.py <install-root>

1. The host, alone, goes to KARNAK; the client joins from TOMB and follows (rejoin).
2. Scarabs, one per player: the client stands west of scarab 235 and walks into it, the host west of scarab 236 and walks into it
   (world_checkpoint_local / p16_checkpoint slot 1 + slot 0).
3. Environment damage of the client's player (sourceless Damage, like drowning) goes to the host (world_self_damage -> world_self_damage_applied).
4. The client's player dies (PSE_DEBUG_KILL_P2_FILE), presses E and must come back beside ITS scarab (p16_respawn_checkpoint).
5. A world-level fault of the host (PSE_DEBUG_BLOCK_FILE) and then the host goes on to SANCTUAR: co-op comes back without any restart
   (host_recovered_after_block, the client rejoins on SANCTUAR).
Screenshots and a summary go to <root>/smoke_m9_<time>/.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_m9_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/KARNAK.MAP')
# KARNAK scarabs (census): 235 at (5270,3410,-378) sector 69, 236 at (3524,3040,192) sector 307. Both players 170 units west, looking east (+x).
os.environ.setdefault('PSE_DEBUG_TELEPORT_CLIENT', '5100,3410,1.5708,-378')
os.environ.setdefault('PSE_DEBUG_TELEPORT', '5440,3410,-1.5708,-378')   # the host east of the same scarab 235, looking west
os.environ.setdefault('PSE_DEBUG_SELF_DAMAGE_FILE', str(out / 'self.flag'))
os.environ.setdefault('PSE_DEBUG_KILL_P2_FILE', str(out / 'kill.flag'))
os.environ.setdefault('PSE_DEBUG_BLOCK_FILE', str(out / 'block.flag'))
os.environ.setdefault('PSE_DEBUG_BLOCK_NEXT_MAP', 'maps/SANCTUAR.MAP')
for role in ('host', 'client'):   # New Game asks "overwrite?" (default No) once a slot exists: start with empty slots
    saves = root / 'profile' / role / 'early_native_logs' / 'saves'
    if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_' + out.name)); saves.mkdir()


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
    fs = sorted(glob.glob(str(root / 'profile' / role / 'logs' / f'native_{role}_*.jsonl')))
    return fs[-1] if fs else None


def rows(role):
    f = logfile(role); res = []
    if not f: return res
    for l in open(f, encoding='utf8', errors='replace'):
        if l.strip().endswith('}'):
            try: res.append(json.loads(l))
            except ValueError: pass
    return res


def seen(role, name, count=1):
    f = logfile(role)
    return bool(f) and open(f, 'rb').read().count(b'"event":"' + name.encode() + b'"') >= count


def wait_for(role, name, count=1, timeout=90, focus=None):
    end = time.time() + timeout; turn = 0
    while time.time() < end:
        if seen(role, name, count): return True
        targets = focus or [host]
        act.focus(act.hwnd_of(targets[turn % len(targets)])); turn += 1   # one PC: only the focused window ticks
        time.sleep(1.5)
    return False


def show(role, *names):
    for r in rows(role):
        if r['event'] in names: print(' ', role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns')})


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
print('host alone goes to KARNAK:', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
print('client followed to KARNAK:', wait_for('client', 'client_follow_host_map', 1, 60, both), '| welcomed:', wait_for('client', 'client_welcome', 1, 120, both))
(out / 'self.flag').write_text('go')   # environment damage first: enemies at the scarab may kill the client soon
print('environment damage: client', wait_for('client', 'world_debug_self_damage', 1, 60, both), '| host applied', wait_for('host', 'world_self_damage_applied', 1, 30, both))
print('teleported: client', wait_for('client', 'world_debug_teleport', 1, 60, both), '| host', wait_for('host', 'world_debug_teleport', 1, 60, both))
time.sleep(2)
look(client, '1_client_before_scarab.png', 1); look(host, '2_host_before_scarab.png', 1)
keys(client, 'hold:W=1.6', 'wait=0.5'); look(client, '3_client_at_scarab.png', 0.5)
keys(host, 'hold:W=1.6', 'wait=0.5'); look(host, '4_host_at_scarab.png', 0.5)
print('scarabs: client local', wait_for('client', 'world_checkpoint_local', 1, 20, both), '| host has slot1+slot0:', wait_for('host', 'p16_checkpoint', 2, 30, both))
# the client walks away from its scarab before it dies, so the respawn spot proves the scarab
keys(client, 'hold:S=1.5', 'wait=0.5')
(out / 'kill.flag').write_text('go')
print('client died:', wait_for('client', 'client_player_dead', 1, 60, both))
h = act.hwnd_of(client)
for attempt in range(8):
    if seen('client', 'p16_client_respawn_committed'): break
    act.focus(h); time.sleep(0.5); act.send(act.VK['E'], True); time.sleep(0.5); act.send(act.VK['E'], False); time.sleep(1.5)
print('client respawned:', wait_for('client', 'p16_client_respawn_committed', 1, 30, both))
for _ in range(3): act.focus(act.hwnd_of(client)); time.sleep(1.5); act.focus(act.hwnd_of(host)); time.sleep(1.0)
look(client, '5_client_after_respawn.png', 1.0)
pose = [r for r in rows('client') if r['event'] == 'client_input_sampled' and r.get('valid')][-1:]
print('client pose after respawn:', [p['pose'] for p in pose], '(scarab 235 at 5270,3410)')
# world fault on the host, then the host goes on: co-op must come back on SANCTUAR without a restart
(out / 'block.flag').write_text('go')
print('host blocked:', wait_for('host', 'blocked', 1, 30, both), '| next map:', wait_for('host', 'debug_block_next_map', 1, 40, both))
print('host recovered:', wait_for('host', 'host_recovered_after_block', 1, 90, both))
print('client rejoined on SANCTUAR:', wait_for('client', 'client_welcome', 3, 180, both))
time.sleep(4); look(host, '6_host_sanctuar.png', 1); look(client, '7_client_sanctuar.png', 1)
print('--- scarabs / respawn')
show('client', 'checkpoint_hook_ready', 'world_checkpoint_local', 'p16_client_respawn_committed', 'world_health_capped')
show('host', 'world_checkpoint_local', 'p16_checkpoint', 'p16_checkpoint_unknown', 'p16_respawn_checkpoint', 'p16_respawn_committed')
print('--- environment damage')
show('client', 'world_debug_self_damage', 'world_self_damage', 'world_own_health')
show('host', 'world_self_damage_applied', 'world_remote_self_damage_muted')
print('--- recovery')
show('host', 'blocked', 'debug_block_next_map', 'host_recovered_after_block', 'host_block_kept', 'map_identity_bound', 'host_world_rearmed')
show('client', 'client_welcome', 'client_follow_host_map', 'client_recovered_after_block', 'blocked', 'client_identity_reset')
bad = {}
for role in ('host', 'client'):
    for r in rows(role):
        if any(w in str(r['event']) for w in ('fault', 'fail', 'error', 'dropped')): bad.setdefault(role, {}).setdefault(r['event'], r.get('reason'))
print('problems:', bad or 'none')
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
