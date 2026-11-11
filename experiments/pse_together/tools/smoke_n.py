"""N-player smoke on one PC: python -X utf8 smoke_n.py <install-root> [clients=3]

One host and <clients> clients (1..7) of the same install, each client with its own profile
(pe-play --role client --instance k). The host starts a game, every client joins from the lobby list / manual address and loads the same
map by the usual menu keys. Checks: every client gets its own slot, the host builds one body + one proxy per client, every client shows
the other (clients-1)+1 players as proxies, walking clients move their body on the host, and (SMOKE_N_CAMPAIGN=1) the host's map change
is followed by all clients with every player's inventory carried by identity. Screenshots + summary go to <root>/smoke_n_<time>/.
Only the focused KEX window ticks on one PC, so every wait rotates the focus through all windows.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
clients_n = int(sys.argv[2]) if len(sys.argv) > 2 else 3
assert 1 <= clients_n <= 7
out = root / time.strftime('smoke_n_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')   # never touch the user's real Steam achievements
if os.environ.get('SMOKE_N_CAMPAIGN'):
    os.environ.setdefault('PSE_DEBUG_CHANGE_MAP', 'maps/KARNAK.MAP')
    os.environ.setdefault('PSE_DEBUG_MAP_FILE', str(out / 'go_next_map.flag'))
    os.environ.setdefault('PSE_DEBUG_P2_GRANT', '3,40')

MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')


def pids():
    t = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq anubis_Shipping_Steam_x64.exe', '/FO', 'CSV', '/NH'], capture_output=True).stdout.decode('latin1')
    return {int(l.split(',')[1].strip('"')) for l in t.splitlines() if l.startswith('"anubis')}


def prof(role, inst): return role + (str(inst) if inst else '')


def ensure_binds(p):
    cfg = root / 'profile' / p / 'early_native_logs' / 'kexengine.cfg'
    if not cfg.exists():
        cfg.parent.mkdir(parents=True, exist_ok=True); cfg.write_text((HERE / 'smoke_kexengine.cfg').read_text(encoding='utf8'), encoding='utf8')
    text = cfg.read_text(encoding='utf8')
    if 'bind f "attack"' not in text:
        cfg.write_text(text.rstrip() + chr(10) + 'bind f "attack"' + chr(10), encoding='utf8')


def launch(role, inst=0, extra=None):
    p = prof(role, inst); ensure_binds(p)
    before = pids()
    args = [str(root / 'pe-play.exe'), '--role', role] + (['--instance', str(inst)] if inst else [])
    name = 'Host' if role == 'host' else f'Guest{inst}'
    subprocess.Popen(args, cwd=str(root), stdout=(out / f'{p}_stdout.txt').open('wb'), stderr=subprocess.STDOUT, env={**os.environ, 'PSE_PLAYER_NAME': name, **(extra or {})})
    for _ in range(60):
        time.sleep(1)
        new = pids() - before
        if new and act.hwnd_of(next(iter(new))):
            return next(iter(new))
    raise SystemExit(p + ' did not start')


def keys(pid, *seq):
    h = act.hwnd_of(pid); act.HW[0] = h
    for a in seq:
        if a.startswith('wait='): time.sleep(float(a[5:])); continue
        if a.startswith('hold:'):
            name, sec = a[5:].split('='); act.focus(h); act.post(act.VK[name], True); time.sleep(float(sec)); act.post(act.VK[name], False); continue
        act.focus(h); act.tap(act.VK[a]); time.sleep(0.25)


def shot(pid, name):
    subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(pid), str(out / name)], capture_output=True, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'})


def rows(p, role):
    fs = sorted(glob.glob(str(root / 'profile' / p / 'logs' / f'native_{role}_*.jsonl')))
    if not fs: return []
    res = []
    for l in open(fs[-1], encoding='utf8', errors='replace'):
        if l.strip().endswith('}'):
            try: res.append(json.loads(l))
            except ValueError: pass
    return res


def events(p, role, *names):
    return [r for r in rows(p, role) if r['event'] in names]


procs = {}   # profile name -> pid


def rotate(seconds):
    # one PC: only the focused window ticks, so give every window a turn
    end = time.time() + seconds
    while time.time() < end:
        for pid in procs.values():
            act.focus(act.hwnd_of(pid)); time.sleep(0.8)


def wait_for(cond, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if cond(): return True
        rotate(2)
    print('TIMEOUT waiting for', what)
    return False


procs['host'] = launch('host'); time.sleep(8)
keys(procs['host'], 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
for k in range(1, clients_n + 1):
    ex = {'PSE_DEBUG_CHANGE_MAP_CLIENT': 'maps/KARNAK.MAP', 'PSE_DEBUG_MAP_FILE_CLIENT': str(out / 'client_exit.flag')} if (os.environ.get('SMOKE_N_EXIT') == 'client' and k == 1) else None
    procs[prof('client', k)] = launch('client', k, ex); time.sleep(8)
    keys(procs[prof('client', k)], 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
    rotate(4)
    print(f'client {k} joined:', wait_for(lambda: len(events('host', 'host', 'snapshot_entity_bindings')) >= k, 60, f'host body for client {k}'))
rotate(10)

print('=== host ===')
print('bodies built:', [(r['slot'], r['p2_generation']) for r in events('host', 'host', 'snapshot_entity_bindings')])
print('proxies spawned on host:', len(events('host', 'host', 'proxy_spawned')), '| proxy faults:', [r.get('reason') for r in events('host', 'host', 'native_proxy_fault', 'blocked')])
print('p16 team:', [(r.get('slot'), r.get('generation')) for r in events('host', 'host', 'p16_team_connected')])
print('=== clients ===')
slots = []
for k in range(1, clients_n + 1):
    p = prof('client', k)
    w = events(p, 'client', 'client_welcome')
    slots.append(w[-1]['slot'] if w else None)
    sp = events(p, 'client', 'proxy_spawned')
    print(p, 'slot', slots[-1], '| proxies spawned:', len(sp), '(expected', clients_n, ') | faults:', [r.get('reason') for r in events(p, 'client', 'native_proxy_fault', 'blocked')])
print('distinct client slots:', len(set(slots)) == clients_n, slots)

# each client walks in turn: its body on the host must move
for k in range(1, clients_n + 1):
    keys(procs[prof('client', k)], 'hold:W=1.0', 'wait=0.5')
rotate(4)
for pid_name, pid in procs.items():
    shot(pid, f'{pid_name}.png')

if os.environ.get('SMOKE_N_FIRE'):
    # Shots of one player are heard by the others: client 1 holds the attack key (F) with the machete; host + client 2 must get the shot events / play the sound.
    keys(procs[prof('client', 1)], 'hold:F=3.0', 'wait=1'); rotate(4)
    print('host sent player sounds:', len(events('host', 'host', 'world_sound_sent')), '| client 2 played sounds:', len(events(prof('client', 2), 'client', 'world_sound_played')) if clients_n >= 2 else 'n/a (needs 2 clients)')
if os.environ.get('SMOKE_N_EXIT') == 'client':
    # Any player's exit moves everybody: client 1 walks into the exit (native ChangeMap on ITS game) -> the host and the other clients must follow.
    (out / 'client_exit.flag').write_text('go')
    print('host told by client 1:', wait_for(lambda: events('host', 'host', 'host_follow_client_exit'), 90, 'host follows client exit'))
    print('other clients ordered:', wait_for(lambda: all(events(prof('client', k), 'client', 'client_follow_map_received') or events(prof('client', k), 'client', 'client_follow_map_change') for k in range(2, clients_n + 1)), 90, 'other clients follow'))
    rotate(15)
    for pid_name, pid in procs.items(): shot(pid, f'{pid_name}_after_exit.png')
if os.environ.get('SMOKE_N_CAMPAIGN'):
    Path(os.environ['PSE_DEBUG_MAP_FILE']).write_text('go')
    ok = wait_for(lambda: all(events(prof('client', k), 'client', 'client_follow_map_received') for k in range(1, clients_n + 1)), 90, 'all clients told to follow')
    print('all clients received the map order:', ok)
    ok2 = wait_for(lambda: len(events('host', 'host', 'snapshot_entity_bindings')) >= 2 * clients_n, 180, 'all bodies rebuilt on the new map')
    print('bodies rebuilt on the next map:', ok2)
    rotate(10)
    for r in events('host', 'host', 'p2_travel_captured', 'p2_travel_restored', 'p2_seeded_from_host'): print('host', {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
    for pid_name, pid in procs.items():
        shot(pid, f'{pid_name}_next_map.png')
print('screens in', out)
