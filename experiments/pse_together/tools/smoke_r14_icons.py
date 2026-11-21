"""Visual check of the weapon icons on dry land (TOMB), one PC: python -u -X utf8 smoke_r14_icons.py <install-root>

The host and the client stand 240 units apart facing each other (the Ramses spot of TOMB, as smoke.py), the host holds the M60, the client
the pistol, then the machete. Screen grabs of both views go to <root>/smoke_r14_icons_<time>/ (no pass/fail: look at them).
"""
import ctypes, glob, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_r14_icons_%Y%m%d_%H%M%S')
out.mkdir()
for k, v in {'PSE_ACH_DRYRUN': '1', 'PSE_UPNP': '0', 'PSE_DEBUG_P1_GRANT': '2,40', 'PSE_DEBUG_P2_GRANT': '1,10'}.items():
    os.environ.setdefault(k, v)
for prof in (root / 'profile').glob('*'):
    saves = prof / 'early_native_logs' / 'saves'
    if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_' + out.name)); saves.mkdir()


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


def grab(pid, name, wait=1.0):
    from PIL import ImageGrab
    from ctypes import wintypes
    h = act.hwnd_of(pid); act.focus(h); time.sleep(wait)
    r = wintypes.RECT(); ctypes.windll.user32.GetWindowRect(h, ctypes.byref(r))
    ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True).save(out / name)


def count(role, name):
    fs = sorted(glob.glob(str(root / 'profile' / role / 'logs' / f'native_{role}_*.jsonl')), key=os.path.getmtime)
    return open(fs[-1], 'rb').read().count(b'"event":"' + name.encode() + b'"') if fs else 0


def wait_for(role, name, timeout=90):
    end = time.time() + timeout; turn = 0
    while time.time() < end:
        if count(role, name): return True
        act.focus(act.hwnd_of([client, host][turn % 2])); turn += 1
        time.sleep(1.5)
    return False


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
for _ in range(6): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
print('client ready:', wait_for('client', 'client_input_domain_ready', 180))
print('host built the body:', wait_for('host', 'snapshot_entity_bindings', 90))
keys(host, '3', 'wait=2')      # M60
keys(client, '2', 'wait=2')    # pistol
# both start on the same spot facing the same way: the host walks ahead, the client looks at its back
h = act.hwnd_of(host); act.HW[0] = h; act.focus(h); act.post(act.VK['W'], True); time.sleep(0.9); act.post(act.VK['W'], False)
for _ in range(3): act.focus(act.hwnd_of(client)); time.sleep(1); act.focus(act.hwnd_of(host)); time.sleep(1)
grab(client, '1_client_sees_host_m60.png', 2)
for i in range(6):   # the host turns in place until the client is in view
    act.focus(h); act.mouse_move(330); time.sleep(0.6); grab(host, f'2_host_sweep_pistol_{i}.png', 0.6)
keys(client, '1', 'wait=2')    # machete
for _ in range(2): act.focus(act.hwnd_of(client)); time.sleep(1); act.focus(act.hwnd_of(host)); time.sleep(1)
for i in range(6):
    act.focus(h); act.mouse_move(330); time.sleep(0.6); grab(host, f'3_host_sweep_machete_{i}.png', 0.6)
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
