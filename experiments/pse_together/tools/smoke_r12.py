"""Round 12 smoke on one PC (friend run 2026-11-13): python -u -X utf8 smoke_r12.py <install-root>

  1. Direct join by clipboard: the host (in KARNAK) presses F6 = its address goes to the Windows clipboard; the client presses F6 on the
     title screen = the address is pasted into the F7 form, Enter joins. The typed address is kept in profile/client/pse_last_host.txt.
  2. No pause in co-op: the host opens the Esc menu (its world keeps ticking: P1 updates go on) and holds the weapon wheel Q (not slowed:
     120 ticks still take ~2 s).
  3. The host wounds an enemy (PSE_DEBUG_KILL with a small PSE_DEBUG_KILL_AMOUNT): the client gets the native white hit flash (EvEnemyHurt).
  4. A map (Item_Map, as a vase drops it) appears in front of the host: the client sees its mirror; the host takes it: the client's automap
     is revealed too (EvMapAll).
UPnP stays off (PSE_UPNP=0): a smoke never changes the router.
"""
import ctypes, glob, json, os, subprocess, sys, time
from datetime import datetime
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402
act.VK['F6'] = 0x75

ANUBIS = 21   # KARNAK: an Anubis on every difficulty the smokes ran on
root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_r12_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_UPNP', '0')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/KARNAK.MAP')
os.environ.setdefault('PSE_DEBUG_KILL', str(ANUBIS))
os.environ.setdefault('PSE_DEBUG_KILL_AMOUNT', '10')
os.environ.setdefault('PSE_DEBUG_ITEM', '144')
os.environ.setdefault('PSE_DEBUG_ITEM_FILE', str(out / 'item.flag'))
os.environ.setdefault('PSE_DEBUG_ITEM_TAKE_FILE', str(out / 'take.flag'))
os.environ.pop('PSE_JOIN_ADDRESS', None)
for prof_dir in (root / 'profile').glob('*'):
    saves = prof_dir / 'early_native_logs' / 'saves'
    if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_' + out.name)); saves.mkdir()
last_host = root / 'profile' / 'client' / 'pse_last_host.txt'
if last_host.exists(): last_host.unlink()
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
        act.focus(h); act.tap(act.VK[a]); time.sleep(0.25)


def hold(pid, vk, seconds):
    """A real key held down (SendInput): the game reads it as held, e.g. the weapon wheel."""
    h = act.hwnd_of(pid); act.focus(h)
    act.send(vk, True); end = time.time() + seconds
    while time.time() < end: time.sleep(0.1)
    act.send(vk, False)


def clipboard():
    u, k = ctypes.windll.user32, ctypes.windll.kernel32
    k.GlobalLock.restype = ctypes.c_void_p; u.GetClipboardData.restype = ctypes.c_void_p
    if not u.OpenClipboard(None): return ''
    try:
        h = u.GetClipboardData(13)
        if not h: return ''
        p = k.GlobalLock(ctypes.c_void_p(h)); text = ctypes.wstring_at(p) if p else ''; k.GlobalUnlock(ctypes.c_void_p(h)); return text
    finally: u.CloseClipboard()


def set_clipboard(text):
    u, k = ctypes.windll.user32, ctypes.windll.kernel32
    k.GlobalAlloc.restype = ctypes.c_void_p; k.GlobalLock.restype = ctypes.c_void_p
    data = (text + '\0').encode('utf-16-le')
    if not u.OpenClipboard(None): return False
    try:
        u.EmptyClipboard(); h = k.GlobalAlloc(2, len(data)); p = k.GlobalLock(ctypes.c_void_p(h)); ctypes.memmove(p, data, len(data)); k.GlobalUnlock(ctypes.c_void_p(h))
        return bool(u.SetClipboardData(13, ctypes.c_void_p(h)))
    finally: u.CloseClipboard()


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


stamp = lambda r: datetime.strptime(r['utc'], '%Y-%m-%dT%H:%M:%S.%fZ')
MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [host]
check('host alone goes to KARNAK', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(8): act.focus(act.hwnd_of(host)); time.sleep(2)

# 1. F6 copy at the host, F6 paste at the client, the address is remembered.
set_clipboard('nothing yet')
keys(host, 'F6', 'wait=1')
copied = clipboard()
print('   host copied:', repr(copied))
check('host F6 copied an address:port', ':' in copied and copied.rsplit(':', 1)[1] == '28745' and copied != 'nothing yet')
client = launch('client'); time.sleep(8)
keys(client, 'F6', 'wait=1.5', 'RETURN', 'wait=2', *MENU)
both = [client, host]
sub = rows('client', 'p23_menu_submit')
print('   client submit:', sub[-1:], '| remembered:', last_host.read_text() if last_host.exists() else None)
check('client F6 pasted it and joined with it (port 28745)', sub and sub[-1].get('port') == 28745)
check('the pasted address is remembered for the next start', last_host.exists() and last_host.read_text().strip() == copied)
check('client ready on KARNAK', wait_for('client', 'client_input_domain_ready', 1, 180))
check('client logged the address it connects to', count('client', 'client_connect_target') >= 1)
play(6)

# 2. No pause: the host's Esc menu and weapon wheel.
check('host: co-op no-pause patches on', any(r.get('on') for r in rows('host', 'world_nopause')))
keys(host, 'ESC', 'wait=1.0')
subprocess.run([sys.executable, '-X', 'utf8', '-c', 'import sys;sys.path.insert(0,r"%s");import ui_act as a;from PIL import ImageGrab;import ctypes;from ctypes import wintypes;r=wintypes.RECT();ctypes.windll.user32.GetWindowRect(a.hwnd_of(%d),ctypes.byref(r));ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom),all_screens=True).save(r"%s")' % (HERE, host, out / 'host_esc_menu.png')])
upd_before = [r for r in rows('host', 'background_policy_status')]
t0 = time.time()
for _ in range(6): act.focus(act.hwnd_of(host)); time.sleep(1)
in_menu = [r for r in rows('host', 'background_policy_status') if stamp(r).timestamp() >= 0][len(upd_before):]
print('   host while the Esc menu is open (menu stack, P1 updates):', [(r.get('menu_stack'), r.get('menu_current'), r['local_updates']) for r in in_menu])
check('host world ticks with the Esc menu open (P1 updates go on)', len(in_menu) >= 2 and all(r.get('menu_stack', 0) > 0 or r.get('menu_current') for r in in_menu) and in_menu[-1]['local_updates'] > in_menu[0]['local_updates'])
keys(host, 'ESC', 'wait=1')
before_wheel = len(rows('host', 'background_policy_status'))
hold(host, 0x51, 5.0)   # Q held: the weapon wheel
wheel = rows('host', 'background_policy_status')[before_wheel:]
gaps = [(stamp(b) - stamp(a)).total_seconds() for a, b in zip(wheel, wheel[1:])]
print('   120-tick intervals while Q is held:', [round(g, 2) for g in gaps])
check('weapon wheel does not slow the game (120 ticks in < 2.6 s)', gaps and max(gaps) < 2.6)
play(3)

# 3. The host wounds the Anubis: the client flashes it.
check('host wounded the Anubis', wait_for('host', 'world_enemy_hurt', 1, 60, focus=[client]))
print('   host hurt:', rows('host', 'world_enemy_hurt')[:2], '| client:', rows('client', 'world_enemy_hurt_applied')[:2])
check('client: the native hit flash on its Anubis', wait_for('client', 'world_enemy_hurt_applied', 1, 30, focus=[client]) and any(r.get('flash') for r in rows('client', 'world_enemy_hurt_applied')))

# 4. A map appears in front of the host: mirrored at the client, taken by the host, revealed for both.
(out / 'item.flag').write_text('go')
check('host spawned a map (Item_Map 144)', wait_for('host', 'world_debug_item', 1, 40, focus=[client, host]) and rows('host', 'world_debug_item')[-1].get('spawned'))
play(4)
mirrors = [r for r in rows('client', 'world_dynamic_spawned') if r.get('type') == 144]
print('   client mirrors of the map:', mirrors[:1])
check('client sees the host\'s map (mirror type 144)', bool(mirrors))
(out / 'take.flag').write_text('go')
check('host took the map', wait_for('host', 'world_map_taken', 1, 40, focus=[client, host]))
check('client automap revealed too (EvMapAll)', wait_for('client', 'world_map_reveal', 1, 30, focus=[client]) and rows('client', 'world_map_reveal')[-1].get('ok'))
play(3)

for role in ('host', 'client'):
    bad = sum(count(role, e) for e in ('blocked', 'native_fault', 'native_proxy_fault', 'seam_native_fault'))
    check(f'{role}: no block or fault', bad == 0)
print('RESULT', 'PASS' if all(ok for _, ok in results) else 'FAIL', sum(ok for _, ok in results), '/', len(results))
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('logs in', out)
