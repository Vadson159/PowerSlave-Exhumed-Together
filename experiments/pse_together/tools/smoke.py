"""Two-process smoke test on one PC: python -X utf8 smoke.py <install-root> [seconds-to-hold-W]

Needs an installed root (tools/p27.py install <root>). Starts host and client through pe-play, drives the menus with
ordinary window messages (ui_act.py), loads Tomb of Ramses on both, then moves the host player (client should see the
proxy) and the client player (host should see P2). Screenshots and a log summary go to <root>/smoke_<time>/.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
hold = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
out = root / time.strftime('smoke_%Y%m%d_%H%M%S')
out.mkdir()


def pids():
    t = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq anubis_Shipping_Steam_x64.exe', '/FO', 'CSV', '/NH'], capture_output=True).stdout.decode('latin1')
    return {int(l.split(',')[1].strip('"')) for l in t.splitlines() if l.startswith('"anubis')}


def launch(role):
    before = pids()
    subprocess.Popen([str(root / 'pe-play.exe'), '--role', role], cwd=str(root), stdout=(out / f'{role}_stdout.txt').open('wb'), stderr=subprocess.STDOUT)
    for _ in range(60):
        time.sleep(1)
        new = pids() - before
        if new and act.hwnd_of(next(iter(new))):
            return next(iter(new))
    raise SystemExit(role + ' did not start')


def keys(pid, *seq):
    h = act.hwnd_of(pid); act.HW[0] = h
    for a in seq:
        if a.startswith('wait='): time.sleep(float(a[5:])); continue
        if a.startswith('text:'): act.focus(h); act.text(a[5:]); continue
        if a.startswith('mouse:'): act.focus(h); act.mouse_move(int(a[6:])); continue
        if a.startswith('hold:'):
            name, sec = a[5:].split('='); act.focus(h); act.post(act.VK[name], True); time.sleep(float(sec)); act.post(act.VK[name], False); continue
        act.focus(h); act.tap(act.VK[a]); time.sleep(0.25)


def shot(pid, name):
    subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(pid), str(out / name)], capture_output=True, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'})


def look(pid, name, wait=3.0):
    # An unfocused KEX world does not tick: bring the window forward first so the picture is current.
    h = act.hwnd_of(pid); act.focus(h); time.sleep(wait); shot(pid, name)


def rows(role):
    f = sorted(glob.glob(str(root / 'profile' / role / 'logs' / f'native_{role}_*.jsonl')))[-1]
    return [json.loads(l) for l in open(f, encoding='utf8', errors='replace') if l.strip().endswith('}')]


def events(role, *names):
    return [r for r in rows(role) if r['event'] in names]


host = launch('host'); time.sleep(8)
MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')  # start, New Game, slot, difficulty, (map screen) load
keys(host, 'F8', 'wait=1', 'text:Host', 'RETURN', 'wait=2', *MENU)
client = launch('client'); time.sleep(8)
keys(client, 'F8', 'wait=1', 'text:Guest', 'RETURN', 'wait=2', *MENU)
shot(host, '1_host_start.png'); shot(client, '2_client_start.png')
print('p2 spawned:', bool(events('host', 'snapshot_entity_bindings')), '| spawn choice:', [ (r['spawn'], r['distance']) for r in events('host', 'p4_spawn_chosen')][-1:])
# 1) client walks forward -> host P2 (native Puppet) walks ahead of the host; the host window should show it
keys(client, 'hold:W=' + str(hold * 2), 'wait=1')
look(host, '3_host_sees_p2.png')
# 2) host walks forward -> client proxy of the host player appears ahead of the client
keys(host, 'hold:W=' + str(hold), 'wait=1')
look(client, '4_client_sees_host_proxy.png')
def sweep(pid, prefix, steps=4, turn=500):
    # Turn in place with relative mouse motion and photograph each view, so a player behind or beside is found too.
    for i in range(steps):
        keys(pid, f'mouse:{turn}', 'wait=0.8'); look(pid, f'{prefix}_{i}.png', 1.0)


if os.environ.get('SMOKE_SWEEP'):
    sweep(host, '5_host_sweep'); sweep(client, '6_client_sweep')
if os.environ.get('SMOKE_MENU'):
    # Explore the pause menu on the client: ESC, then DOWN a few times, one picture each.
    keys(client, 'ESC', 'wait=1.5'); look(client, 'm_0.png', 0.5)
    for i in range(1, 6):
        keys(client, 'DOWN', 'wait=0.5'); shot(client, f'm_{i}.png')
def restart(pid, name, role):
    # Pause menu: Resume, Options, Reload Last Checkpoint, Restart Level, Exit To Title, Quit.
    keys(pid, 'ESC', 'wait=1.5', 'DOWN', 'DOWN', 'DOWN', 'RETURN', 'wait=1', 'LEFT', 'RETURN', 'wait=2'); shot(pid, name + '_a.png')
    keys(pid, 'wait=10'); look(pid, name + '_b.png', 2)
    n = len(events(role, 'native_proxy_fault', 'blocked', 'proxy_world_exit', 'seam_world_exit'))
    print(name, '- alive:', pid in pids(), '| exit/fault events so far:', n)


if os.environ.get('SMOKE_RESTART'):
    restart(client, 'r_client', 'client')
    # The rejoined client and the host should see each other again in the new world.
    keys(client, 'hold:W=0.3', 'wait=1'); look(host, 'r_host_sees_p2.png', 3)
    keys(host, 'hold:W=0.1', 'wait=1'); look(client, 'r_client_sees_host.png', 3)
    sweep(host, 'r_host_sweep', 2)
sp = events('client', 'proxy_spawned'); ref = events('client', 'spawn_refused')
print('client proxy spawned:', len(sp), '| refused:', len(ref), sorted({r.get('reason') for r in ref}))
mot = [r for r in events('host', 'network_native_motion') if r['before'] != r['after']]
print('P2 moved steps:', len(mot))
bad = {}
for role in ('host', 'client'):
    for r in rows(role):
        if any(w in r['event'] for w in ('block', 'fault', 'fail', 'error', 'expired')): bad.setdefault(role, {}).setdefault(r['event'], r.get('reason'))
print('problems:', bad or 'none')
for pid in (client, host): keys(pid) if False else None
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
