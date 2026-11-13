"""Round 13 smoke on one PC (friend run 2026-11-13 evening): python -u -X utf8 smoke_r13.py <install-root>

  1. The host wounds a vase of TOMB (PSE_DEBUG_WOUND, 1 damage): the client shows the white hit flash on it (EvEnemyHurt for breakables).
  2. The host breaks it: broken at the client too; the client quits the game, starts again and rejoins: the vase is broken again for it
     (the host resends what broke in this level to a client that joins later).
  3. The host walks into the level exit (PSE_DEBUG_GOTO): the exit box does not stop the world (co-op) and keeps its buttons: the exit trigger
     asking again every tick is ignored while the box is open (screen grab 3_host_exit_box.png).
"""
import ctypes, glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

VASE = 61                                   # TOMB: a vase (broken by 15 in the friend runs)
EXIT = (1575.0, 3722.0, -891.0)             # TOMB: the host stood here when it left the level (friend run 2026-11-13)
root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_r13_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_UPNP', '0')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/TOMB.MAP')
os.environ.setdefault('PSE_DEBUG_WOUND', str(VASE))
os.environ.setdefault('PSE_DEBUG_WOUND_FILE', str(out / 'hurt.flag'))
os.environ.setdefault('PSE_DEBUG_WOUND_FILE2', str(out / 'break.flag'))
os.environ.setdefault('PSE_DEBUG_GOTO', '%g,%g,%g' % EXIT)
os.environ.setdefault('PSE_DEBUG_GOTO_FILE', str(out / 'goto.flag'))
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
    subprocess.Popen([str(root / 'pe-play.exe'), '--role', role], cwd=str(root), stdout=(out / f'{role}_stdout.txt').open('ab'), stderr=subprocess.STDOUT,
                     env={**os.environ, 'PSE_PLAYER_NAME': 'Host' if role == 'host' else 'Guest', **({'PSE_JOIN_ADDRESS': 'localhost:28745'} if role == 'client' else {})})
    for _ in range(60):
        time.sleep(1)
        new = pids() - before
        if new and act.hwnd_of(next(iter(new))): return next(iter(new))
    raise SystemExit(role + ' did not start')


def keys(pid, *seq):
    h = act.hwnd_of(pid); act.HW[0] = h
    for a in seq:
        if a.startswith('wait='): time.sleep(float(a[5:])); continue
        act.focus(h); act.tap(act.VK[a]); time.sleep(0.25)


def grab(pid, name):
    from PIL import ImageGrab
    from ctypes import wintypes
    r = wintypes.RECT(); ctypes.windll.user32.GetWindowRect(act.hwnd_of(pid), ctypes.byref(r))
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(out / name)


def logfile(role):
    fs = sorted(glob.glob(str(root / 'profile' / role / 'logs' / f'native_{role}_*.jsonl')), key=os.path.getmtime)
    return fs[-1] if fs else None


def rows(role, *names):
    f = logfile(role); res = []
    if not f: return res
    for l in open(f, encoding='utf8', errors='replace'):
        if l.strip().endswith('}'):
            try: r = json.loads(l)
            except ValueError: continue
            if not names or r.get('event') in names: res.append(r)
    return res


def count(role, name):
    f = logfile(role)
    return open(f, 'rb').read().count(b'"event":"' + name.encode() + b'"') if f else 0


def wait_for(role, name, n=1, timeout=90, focus=None):
    end = time.time() + timeout; turn = 0
    while time.time() < end:
        if count(role, name) >= n: return True
        targets = focus or both
        act.focus(act.hwnd_of(targets[turn % len(targets)])); turn += 1   # one PC: the client ticks only while focused, the host also in the background
        time.sleep(1.5)
    return False


def play(seconds, who=None):
    end = time.time() + seconds
    while time.time() < end: act.focus(act.hwnd_of(who or client)); time.sleep(1.0)


def check(name, ok):
    results.append((name, bool(ok))); print(('PASS ' if ok else 'FAIL ') + name, flush=True)


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [host]
check('host alone goes to TOMB', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(8): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
check('client ready on TOMB', wait_for('client', 'client_input_domain_ready', 1, 180))
play(6)

# 1. A wounded vase flashes at the client.
(out / 'hurt.flag').write_text('go')
check('host hit the vase for 1', wait_for('host', 'world_debug_wound', 1, 40))
print('   ', rows('host', 'world_debug_wound')[-1:], '| host hurt:', [r for r in rows('host', 'world_enemy_hurt') if r['id'] == VASE][:1])
check('client: the white hit flash on the vase', wait_for('client', 'world_enemy_hurt_applied', 1, 30, focus=[client]) and any(r['id'] == VASE and r.get('flash') for r in rows('client', 'world_enemy_hurt_applied')))

# 2. Broken, and broken again after the client restarts the game and rejoins.
(out / 'break.flag').write_text('go')
check('host broke the vase', wait_for('host', 'world_debug_wound', 2, 40))
check('client: the vase broke there too', wait_for('client', 'world_break_applied', 1, 30, focus=[client]) and any(r['id'] == VASE for r in rows('client', 'world_break_applied')))
play(2)
first_log = logfile('client')
act.u.PostMessageW(act.hwnd_of(client), 0x10, 0, 0); time.sleep(8)
both = [host]
for _ in range(5): act.focus(act.hwnd_of(host)); time.sleep(2)
resent_before = len(rows('host', 'world_removals_resent'))
saves = root / 'profile' / 'client' / 'early_native_logs' / 'saves'   # the first session saved: New Game would ask to overwrite
if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_rejoin_' + out.name)); saves.mkdir()
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
check('client rejoined (a new game process)', logfile('client') != first_log and wait_for('client', 'client_input_domain_ready', 1, 180))
play(6)
resent = rows('host', 'world_removals_resent')[resent_before:]
print('   host resent:', resent[-1:], '| client breaks after rejoin:', rows('client', 'world_break_applied')[:3])
check('host resent the broken vase to the rejoined client', bool(resent) and resent[-1].get('breaks', 0) >= 1)
play(4)
print('   client breaks after rejoin:', [r for r in rows('client', 'world_break_applied') if r['id'] == VASE])
check('client: the vase is broken again after the rejoin', any(r['id'] == VASE and r.get('alive_after') is False for r in rows('client', 'world_break_applied')))

# 3. The level exit box stops the world and has its buttons.
(out / 'goto.flag').write_text('go')
check('host went into the level exit', wait_for('host', 'world_debug_goto', 1, 40, focus=[client]) and rows('host', 'world_debug_goto')[-1].get('ok'))
end = time.time() + 20
while time.time() < end and not any(r.get('menu') for r in rows('host', 'background_policy_status')[-2:]): act.focus(act.hwnd_of(host)); time.sleep(1)
for _ in range(7): act.focus(act.hwnd_of(host)); time.sleep(1)
grab(host, '3_host_exit_box.png')
boxed = [r for r in rows('host', 'background_policy_status') if r.get('menu')]
print('   host while the box is open (menu, P1 updates):', [(r['menu'], r['local_updates']) for r in boxed[-4:]])
check('the exit box is open at the host (GI+0x20c)', len(boxed) >= 2)
check('the world runs on under the exit box (co-op: no pause)', len(boxed) >= 2 and boxed[-1]['local_updates'] > boxed[0]['local_updates'])
print('   reopen requests ignored:', count('host', 'world_level_window_reopen_skipped'))
check('the box was opened once (not rebuilt every tick)', len([r for r in rows('host', 'world_level_window_open') if r.get('no_pause')]) == 1)
check('the world pass under the level window ran', count('host', 'world_level_window_world_pass') >= 1)
check('no-pause patches are on (the client is connected)', any(r.get('on') for r in rows('host', 'world_nopause')))

exits = count('host', 'state_exit_entry')
act.focus(act.hwnd_of(host))
from ctypes import wintypes
r = wintypes.RECT(); act.u.GetWindowRect(act.hwnd_of(host), ctypes.byref(r)); w, h = r.right - r.left, r.bottom - r.top
# After any mouse event (here: the focus changes between the two windows) the box takes its selection from the game's cursor, as in the
# stock game: move the cursor onto Yes with real relative mouse motion and click it.
time.sleep(1.0)
act.mouse_move(-160, 75, steps=30); time.sleep(0.5)
grab(host, '4a_host_cursor_on_yes.png')
act.click(); time.sleep(0.5)
grab(host, '4_host_after_yes.png')
check('Yes in the box takes the host on (StateExit)', wait_for('host', 'state_exit_entry', exits + 1, 30, focus=[host]))
play(5, host)
for role in ('host', 'client'):
    bad = sum(count(role, e) for e in ('blocked', 'native_fault', 'native_proxy_fault', 'seam_native_fault'))
    check(f'{role}: no block or fault', bad == 0)
print('RESULT', 'PASS' if all(ok for _, ok in results) else 'FAIL', sum(ok for _, ok in results), '/', len(results))
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('logs in', out)
