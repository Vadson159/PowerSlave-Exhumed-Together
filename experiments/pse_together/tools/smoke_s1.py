"""Smoke of the single host session (sync rework stage 1) on one PC: python -u -X utf8 smoke_s1.py <install-root>

The host's network session must live through every world change, the client must stay connected (exactly one WELCOME) and be
ready again in each new world:
  A. both start TOMB, the client joins;
  B. the host restarts the level from its pause menu (the client follows the order);
  C. the client restarts the level from ITS pause menu (any player's exit/restart moves everybody, or a re-ready if only it reloads);
  D. the host walks out of the level: the native map screen (state 2) on both, Enter on both -> the next level;
  E. the host exits to the title menu and loads the autosave from there (the client follows the host's level).
Screenshots and a summary go to <root>/smoke_s1_<time>/.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_s1_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_DEBUG_END', '2')                       # D: the host "walks out" (native Travel to the next map location)
os.environ.setdefault('PSE_DEBUG_MAP_FILE', str(out / 'exit.flag'))
for role in ('host', 'client'):   # New Game asks "overwrite?" (default No) once a slot exists: start with empty slots
    saves = root / 'profile' / role / 'early_native_logs' / 'saves'
    if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_' + out.name)); saves.mkdir()
results = []
STEPS = os.environ.get('SMOKE_S1_STEPS', 'ABCDE')   # e.g. AE: only the join and the title + load save


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
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
# A
check('A: client ready in TOMB', wait_for('client', 'client_input_domain_ready', 1, 120))
check('A: host built the client body', wait_for('host', 'snapshot_entity_bindings', 1, 90))
look(host, 'a_host.png', 1); look(client, 'a_client.png', 1)
pause_row = 0   # the pause menu remembers its last row (Resume=0 ... Restart Level=3, Exit To Title=4) per process
# B: host Restart Level
if 'B' in STEPS:
  pause_restart(host); pause_row = 3
  ready_again('B host restart', host_rebinds=1, client_ready=2)
  check('B: body rebuilt', wait_for('host', 'snapshot_entity_bindings', 2, 90))
  look(host, 'b_host.png', 1); look(client, 'b_client.png', 1)
# C: client Restart Level
if 'C' in STEPS:
  reready = count('host', 'network_reready'); rebinds = count('host', 'network_world_rebound'); readies = count('client', 'client_input_domain_ready')
  pause_restart(client)
  check('C: client ready again', wait_for('client', 'client_input_domain_ready', readies + 1, 180))
  time.sleep(3)
  print('   C: host followed (rebinds %d -> %d), re-ready %d -> %d' % (rebinds, count('host', 'network_world_rebound'), reready, count('host', 'network_reready')))
  ready_again('C client restart')
  look(host, 'c_host.png', 1); look(client, 'c_client.png', 1)
# D: the host leaves the level: native map screen on both, then Enter on both
if 'D' in STEPS:
  rebinds = count('host', 'network_world_rebound'); readies = count('client', 'client_input_domain_ready')
  (out / 'exit.flag').write_text('go')
  check('D: host went to the map screen', wait_for('host', 'debug_overworld', 1, 60))
  check('D: client followed to the map screen', wait_for('client', 'client_follow_state', 1, 60))
  for _ in range(4): act.focus(act.hwnd_of(host)); time.sleep(1.5); act.focus(act.hwnd_of(client)); time.sleep(1.5)
  look(host, 'd1_host_map.png', 1); look(client, 'd1_client_map.png', 1)
  for pid, tag in ((host, 'host'), (client, 'client')):
      keys(pid, 'RETURN', 'wait=2'); look(pid, f'd2_{tag}_after_enter.png', 0.5)
      keys(pid, 'RETURN', 'wait=2'); look(pid, f'd3_{tag}_after_enter2.png', 0.5)
  ready_again('D map screen -> next level', host_rebinds=rebinds + 1, client_ready=readies + 1)
  look(host, 'd4_host_next.png', 2); look(client, 'd4_client_next.png', 2)
# E: host exits to the title menu, then loads the autosave of this level (start -> Load Game -> first slot)
rebinds = count('host', 'network_world_rebound'); readies = count('client', 'client_input_domain_ready')
keys(host, 'ESC', 'wait=1.5', *(['DOWN'] * (4 - pause_row)), 'RETURN', 'wait=1'); look(host, 'e0_host_confirm.png', 0.3)
keys(host, 'LEFT', 'RETURN', 'wait=4'); look(host, 'e1_host_title.png', 0.5)
check('E: host at the title, session kept', count('host', 'network_started') == 1 and count('host', 'network_stop_begin') == 0)
keys(host, 'RETURN', 'wait=1.5'); look(host, 'e2_host_start.png', 0.3)
keys(host, 'DOWN', 'wait=0.5', 'RETURN', 'wait=2'); look(host, 'e3_host_load.png', 0.3)
keys(host, 'RETURN', 'wait=2'); look(host, 'e4_host_load2.png', 0.3)   # "Are you sure you wish to load this game?" Yes (left) / No
keys(host, 'LEFT', 'RETURN', 'wait=8'); look(host, 'e5_host_loaded.png', 1)
keys(host, 'RETURN', 'wait=2', 'RETURN', 'wait=10'); look(host, 'e5b_host_after_map.png', 1)   # a loaded save opens the map screen first: Enter there
ready_again('E title + load save', host_rebinds=rebinds + 1, client_ready=readies + 1, timeout=240)
look(host, 'e6_host.png', 2); look(client, 'e6_client.png', 2)
print('--- session')
show('host', 'network_started', 'network_world_left', 'network_world_rebound', 'network_reready', 'host_map_change_announced', 'host_follow_client_exit', 'debug_overworld', 'blocked', 'network_resync')
show('client', 'client_welcome', 'client_host_world_changed', 'client_follow_host_map', 'client_follow_map_received', 'client_follow_state', 'client_follow_map_change',
     'client_order_superseded', 'world_transition', 'client_input_domain_ready', 'client_exit_reported', 'blocked', 'client_session')
print('RESULT', 'PASS' if all(ok for _, ok in results) else 'FAIL', sum(ok for _, ok in results), '/', len(results))
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
