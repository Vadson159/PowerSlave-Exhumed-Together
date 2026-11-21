"""Round 14 smoke B (KARNAK, one PC): python -u -X utf8 smoke_r14b.py <install-root>

1. The host, alone in KARNAK, touches ankh 234 (PSE_DEBUG_TOUCH_HOST: the hooked stock OnTouch entry): it takes it (P14 is not running
   without a client: the stock pickup; before 2026-11-21 a lone host could not take an ankh at all).
2. A client joins later: its own player gets that ankh (EvEquip ankh mask and the EvTeamItem replay) and the ankh is gone from its world.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

ANKH = (234, 3794.0, -389.0, 72.0)   # KARNAK Item_Health_Ankah3 (bit 2)
root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_r14b_%Y%m%d_%H%M%S')
out.mkdir()
for k, v in {'PSE_ACH_DRYRUN': '1', 'PSE_UPNP': '0', 'PSE_DEBUG_SOLO_MAP': 'maps/KARNAK.MAP',
             'PSE_DEBUG_TOUCH_HOST': f'6,{ANKH[0]}', 'PSE_DEBUG_TOUCH_HOST_FILE': str(out / 'touch.flag')}.items():
    os.environ.setdefault(k, v)
for prof in (root / 'profile').glob('*'):
    saves = prof / 'early_native_logs' / 'saves'
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
        act.focus(act.hwnd_of(targets[turn % len(targets)])); turn += 1
        time.sleep(1.5)
    return False


def check(name, ok, *detail):
    results.append((name, bool(ok))); print(('PASS ' if ok else 'FAIL ') + name, *detail, flush=True)


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [host]
check('host alone goes to KARNAK', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)
(out / 'touch.flag').write_text('go')
check('host touched the ankh (the stock entry, P14 hooked)', wait_for('host', 'world_debug_touch', 1, 40) and rows('host', 'world_debug_touch')[-1].get('found'), rows('host', 'world_debug_touch')[-1:])
check('host took it alone (team item recorded)', wait_for('host', 'world_team_item_taken', 1, 30) and any(r.get('id') == ANKH[0] and r.get('by_host_player') for r in rows('host', 'world_team_item_taken')),
      rows('host', 'world_team_item_taken', 'world_team_item_shared'))
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
check('client ready on KARNAK', wait_for('client', 'client_follow_host_map', 1, 90) and wait_for('client', 'client_input_domain_ready', 1, 180))
check('client got the host gear (ankh mask)', wait_for('client', 'world_equip_applied', 4, 60, focus=[client]) and any(r.get('kind') == 2 and r.get('mask') == 4 for r in rows('client', 'world_equip_applied')),
      rows('client', 'world_equip_applied'))
check('client: the ankh replay (gone there, owned)', wait_for('client', 'world_team_item_applied', 1, 60, focus=[client]) and any(r.get('id') == ANKH[0] and not r.get('live') for r in rows('client', 'world_team_item_applied')),
      rows('client', 'world_team_item_applied'))
gone = [r for r in rows('client', 'world_team_item_applied') if r.get('id') == ANKH[0]] + [r for r in rows('client', 'world_equip_applied') if r.get('kind') == 2]
check('client: ankh removed from its world', any(r.get('found') for r in gone) or any(r.get('items_removed') for r in gone), gone)
bad = {}
for role in ('host', 'client'):
    for r in rows(role):
        ev = str(r.get('event'))
        if any(w in ev for w in ('blocked', 'fault', 'fail', 'error')): bad.setdefault(role, {}).setdefault(ev, r.get('reason') or r.get('site'))
print('problems:', bad or 'none')
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print(f'{sum(1 for _, ok in results if ok)}/{len(results)} passed; logs in {out}')
