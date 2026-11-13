"""Host weapon pickup notice + direct IP join, one PC: python -u -X utf8 smoke_weapon.py <install-root>

The host alone (re)loads TOMB. The client joins by a typed DIRECT address (PSE_JOIN_ADDRESS=localhost:28745, as `join.bat <address>`):
F7 opens on the address field, Enter parses "name:port" and resolves the name (no lobby list involved). Then PSE_DEBUG_GO_WEAPON puts the
host's own player on a map weapon it does not own: the P16 hook grants it (the weapon stays in the world for the others) and must now show
the host the stock pickup feedback (sound, HUD message with the weapon name, flash) - `p16_weapon_local_notice`, and the name must be ON
SCREEN (orange HUD-font pixels at the top centre of a screen grab). UPnP is off here (PSE_UPNP=0): a smoke must not change the router.

The pickup is at world tick 1000 (~15 s into the session): HUD messages added in the first seconds of a freshly loaded level were drawn by
the engine but never reached the screen in these runs (also the stock pickup at the client) - not what is tested here.
SMOKE_WEAPON_CLIENT=1: control, the CLIENT takes the weapon the stock way (its name must be on screen too).
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_weapon_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_UPNP', '0')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/TOMB.MAP')   # TOMB has map weapons a new game does not own (friend run 12.11: slot 1)
CONTROL = os.environ.get('SMOKE_WEAPON_CLIENT') == '1'   # control: the CLIENT takes the weapon the stock way (what its notice looks like)
os.environ.setdefault('PSE_DEBUG_GO_WEAPON_CLIENT' if CONTROL else 'PSE_DEBUG_GO_WEAPON', '1000')   # world tick
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


def shot(pid, name):
    subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(pid), str(out / name)], capture_output=True, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'})


def grab(pid, name):
    """What is on the screen over the window (PrintWindow returned a frozen frame of the D3D window)."""
    import ctypes
    from ctypes import wintypes
    from PIL import ImageGrab
    r = wintypes.RECT(); ctypes.windll.user32.GetWindowRect(act.hwnd_of(pid), ctypes.byref(r))
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(out / name)


def name_on_screen(name):
    """Orange HUD-font pixels in the message line (top centre); the brown walls of TOMB have none."""
    from PIL import Image
    im = Image.open(out / name).convert('RGB'); w, h = im.size
    box = im.crop((int(w * 0.34), int(h * 0.13), int(w * 0.66), int(h * 0.2)))
    return sum(1 for r, g, b in box.getdata() if r - b > 95 and r > 90)


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


def play(seconds):
    """Both worlds tick: the client's window in front, the host in the background."""
    end = time.time() + seconds
    while time.time() < end: act.focus(act.hwnd_of(client)); time.sleep(1.0)


def check(name, ok):
    results.append((name, bool(ok))); print(('PASS ' if ok else 'FAIL ') + name, flush=True)


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [host]
check('host alone goes to TOMB', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
sub = rows('client', 'p23_menu_submit')
print('   client submit:', sub[-1:])
check('client submitted the typed direct address (port 28745 parsed from "localhost:28745")', sub and sub[-1].get('port') == 28745)
check('client ready on TOMB', wait_for('client', 'client_input_domain_ready', 1, 180))
who = client if CONTROL else host
role = 'client' if CONTROL else 'host'
act.focus(act.hwnd_of(who)); end = time.time() + 90
while time.time() < end and not count(role, 'world_debug_go_weapon'): act.focus(act.hwnd_of(who)); time.sleep(0.1)   # in front: on screen
check(f'{role} went onto a map weapon', count(role, 'world_debug_go_weapon') >= 1)
print('   ', rows(role, 'world_debug_go_weapon')[-1:])
time.sleep(1.2); grab(who, f'1_{role}_weapon_name.png')   # after the pickup flash
pixels = name_on_screen(f'1_{role}_weapon_name.png')
print('   name pixels:', pixels)
check(f'{role}: the weapon name is on screen', pixels > 100)
if not CONTROL:
    check('host: weapon granted to its own player (slot 0)', wait_for('host', 'p16_weapon_only_granted', 1, 30, focus=[host]))
    print('   grants:', [r for r in rows('host', 'p16_weapon_only_granted') if r.get('slot') == 0][-2:], '| notices:', rows('host', 'p16_weapon_local_notice')[-2:])
    notice = rows('host', 'p16_weapon_local_notice')
    check('host: own pickup notice (sound, non-empty message, flash)', notice and notice[-1].get('value', 0) & 0xff == 7 and notice[-1]['value'] >> 8 > 0 and not count('host', 'p16_weapon_notice_skipped'))
up = rows('host', 'upnp_state')
print('   host upnp:', up[-1:])
check('host: UPnP stays off with PSE_UPNP=0 (router untouched)', up and up[-1].get('state') == 3 and 'PSE_UPNP=0' in up[-1].get('message', ''))
for role in ('host', 'client'):
    bad = sum(count(role, e) for e in ('blocked', 'native_fault', 'native_proxy_fault', 'seam_native_fault'))
    check(f'{role}: no block or fault', bad == 0)
print('RESULT', 'PASS' if all(ok for _, ok in results) else 'FAIL', sum(ok for _, ok in results), '/', len(results))
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
