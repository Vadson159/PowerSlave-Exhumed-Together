"""Round 14 smoke (2026-11-21) on one PC, KARNAK: python -u -X utf8 smoke_r14.py <install-root>

1. Late joiner: the host has Sandals + Mask and transmitter 1 before the client joins (PSE_DEBUG_P1_GEAR): the client's own player copies them
   (EvEquip -> world_equip_applied).
2. Weapon icons: both players in the water of sector 156 next to full ammo 42 (as smoke_m7); each sees the other's sprite with the icon of the
   weapon in hand (screen grabs; p8_resource_gate weapon_icon_resources 192, p8_animation resources *_w<N>).
3. Walking while firing: the client holds back + attack; the host's proxy of the client walks (no shoot pose while it moves).
4. Full ammo 42: the client walks into it -> removed for everyone -> back after 15 s at the host and at the client.
5. Kills: the host kills enemy 216 (PSE_DEBUG_KILL, as the host's player), the client enemy 26 (PSE_DEBUG_HIT_CLIENT): 1 : 1; Tab on both
   shows the kill table top right (kill_table_drawn, screen grabs).
6. Team items: the client takes ankh 13 (asked: the host gives it to everyone) and team doll 16 (taken there, reported): both players own them,
   the items are gone everywhere.
7. Death cry: the host kills the client's player: its death sound reaches the host and is played at the listener (world_death_sound_global).
Screen grabs and logs: <root>/smoke_r14_<time>/.
"""
import calendar, ctypes, glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

SPHERE = (5194.0, 384.0, -1488.0)   # KARNAK: full ammo (Item_Ammo_Full) map actor 42, in the water of sector 156
ANKH, DOLL = 13, 16                  # KARNAK: Item_Health_Ankah1, a team doll
root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_r14_%Y%m%d_%H%M%S')
out.mkdir()
env = {
    'PSE_ACH_DRYRUN': '1', 'PSE_UPNP': '0', 'PSE_DEBUG_SOLO_MAP': 'maps/KARNAK.MAP',
    'PSE_DEBUG_P1_GEAR': '3,0,1',                 # Sandals + Mask, no doll, transmitter 1
    'PSE_DEBUG_P2_GRANT': '1,10',                 # the client's body gets the pistol (the client holds it: icon w1)
    'PSE_DEBUG_TELEPORT_CLIENT': f'{SPHERE[0] - 14},{SPHERE[1] + 176},3.14159,{SPHERE[2]}',
    'PSE_DEBUG_TELEPORT': f'{SPHERE[0] - 60},{SPHERE[1] + 320},3.14159,{SPHERE[2]}',
    'PSE_DEBUG_KILL': '216', 'PSE_DEBUG_KILL_AMOUNT': '30000', 'PSE_DEBUG_KILL_BY_HOST': '1',
    'PSE_DEBUG_HIT_CLIENT': '26,1000', 'PSE_DEBUG_HIT_FILE': str(out / 'hit.flag'),
    'PSE_DEBUG_TOUCH_CLIENT': f'6,{ANKH};6,{DOLL}', 'PSE_DEBUG_TOUCH_FILE': str(out / 'touch.flag'),
    'PSE_DEBUG_KILL_P2_FILE': str(out / 'kill.flag'),
}
for k, v in env.items(): os.environ.setdefault(k, v)
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
    text = cfg.read_text(encoding='utf8')
    if 'bind f "attack"' not in text: cfg.write_text(text.rstrip() + '\nbind f "attack"\n', encoding='utf8')
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
        if a.startswith('mouse:'): act.focus(h); act.mouse_move(int(a[6:])); continue
        if a.startswith('hold:'):
            name, sec = a[5:].split('='); act.focus(h); act.post(act.VK[name], True); time.sleep(float(sec)); act.post(act.VK[name], False); continue
        act.focus(h); act.tap(act.VK[a]); time.sleep(0.25)


def hold(pid, names, seconds):
    h = act.hwnd_of(pid); act.HW[0] = h; act.focus(h)
    for n in names: act.post(act.VK[n], True)
    time.sleep(seconds)
    for n in names: act.post(act.VK[n], False)


def grab(pid, name, wait=1.0):
    from PIL import ImageGrab
    from ctypes import wintypes
    h = act.hwnd_of(pid); act.focus(h); time.sleep(wait)
    r = wintypes.RECT(); ctypes.windll.user32.GetWindowRect(h, ctypes.byref(r))
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


def utc(r):
    s = r['utc']; return calendar.timegm(time.strptime(s[:19], '%Y-%m-%dT%H:%M:%S')) + int(s[20:23]) / 1000.0


def check(name, ok, *detail):
    results.append((name, bool(ok))); print(('PASS ' if ok else 'FAIL ') + name, *detail, flush=True)


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [host]
check('host alone goes to KARNAK', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
check('client followed to KARNAK', wait_for('client', 'client_follow_host_map', 1, 90))
check('client ready on KARNAK', wait_for('client', 'client_input_domain_ready', 1, 180))
check('host built the client body', wait_for('host', 'snapshot_entity_bindings', 1, 90))

# 1. the host's gear for the late joiner
check('host gear set before the join', count('host', 'debug_p1_gear') >= 1)
check('host sent its gear to the client', wait_for('host', 'world_equip_sent', 1, 60))
ok = wait_for('client', 'world_equip_applied', 4, 60, focus=[client])
eq = {r['kind']: r for r in rows('client', 'world_equip_applied')}
check('client copied artifacts 3 and transmitter 1', ok and eq.get(1, {}).get('mask') == 3 and eq.get(1, {}).get('changed') and eq.get(4, {}).get('mask') == 1, eq)

# 2. weapon icons
check('both teleported', wait_for('client', 'world_debug_teleport', 1, 60) and wait_for('host', 'world_debug_teleport', 1, 60))
check('P2 pistol granted', wait_for('host', 'debug_p2_grant', 1, 60))
keys(client, '2', 'wait=2')                       # pistol in hand: icon w1 above the client's sprite
play(3)
gates = {r['event'] + ':' + str(r.get('weapon_icon_resources')): r for role in ('host', 'client') for r in rows(role, 'p8_resource_gate')}
check('icon resources found (host and client)', all(r.get('weapon_icon_resources') == 192 and r.get('weapon_icons') for r in rows('host', 'p8_resource_gate') + rows('client', 'p8_resource_gate'))
      and rows('host', 'p8_resource_gate') and rows('client', 'p8_resource_gate'), [(r.get('weapon_icon_resources'), r.get('weapon_icons')) for r in rows('host', 'p8_resource_gate') + rows('client', 'p8_resource_gate')])
grab(host, '1_host_sees_client.png', 2)

# 4. full ammo 42 (176 units in front of the client): taken -> gone everywhere -> back after 15 s (checked after the kills)
keys(client, 'hold:W=2.4', 'wait=0.3')
check('host gave the full ammo to the client', wait_for('host', 'world_pickup_take', 1, 20) and any(r.get('id') == 42 and r.get('result') == 'taken' for r in rows('host', 'world_pickup_take')), rows('host', 'world_pickup_take')[-2:])
check('host scheduled its return', wait_for('host', 'world_respawn_scheduled', 1, 20) and any(r.get('id') == 42 for r in rows('host', 'world_respawn_scheduled')))
check('client removed it', wait_for('client', 'world_pickup_applied', 1, 20, focus=[client]))
taken_at = time.time()

# 3. walking while firing: the host's proxy of the client walks
t0 = time.time()
hold(client, ['S', 'F'], 1.6)
t1 = time.time()
play(2)
poses = [(utc(r), r['pose'], r['resource']) for r in rows('host', 'p8_animation') if r.get('slot') == 1 and r.get('changed')]
during = [p for p in poses if t0 + 0.3 <= p[0] <= t1]
check('client walking + firing is drawn walking at the host', any(p[1] == 'walk' for p in during) and not any(p[1] == 'shoot' for p in during), during[:8])
grab(host, '1b_host_sees_client_walking.png', 0.2)
for i in range(6):   # the client turns in place: the host is behind it
    keys(client, 'mouse:330', 'wait=0.6'); grab(client, f'2_client_sweep_{i}.png', 0.6)
icons = [r['resource'] for role in ('host', 'client') for r in rows(role, 'p8_animation') if '_w' in r.get('resource', '')]
check('proxies bound with a weapon icon', icons, sorted(set(icons))[:8])

# 5. kills (during the 15 s): the host's kill at its world tick 500, the client's by its hit; then the kill table on Tab
(out / 'hit.flag').write_text('go')
check('host killed enemy 216 as its player', wait_for('host', 'world_kill', 1, 60) and any(r.get('slot') == 0 for r in rows('host', 'world_kill')), rows('host', 'world_kill', 'world_debug_kill')[-3:])
check('client killed enemy 26 (ReqHit)', wait_for('host', 'world_kill', 2, 60) and any(r.get('slot') == 1 and r.get('id') == 26 for r in rows('host', 'world_kill')), rows('host', 'world_kill')[-3:])
play(2)
keys(client, 'TAB', 'wait=1.0')
grab(client, '3_client_kill_table.png', 0.5)
check('client drew the kill table', wait_for('client', 'kill_table_drawn', 1, 20, focus=[client]))
keys(client, 'TAB', 'wait=0.5')
keys(host, 'TAB', 'wait=1.0')
grab(host, '4_host_kill_table.png', 0.5)
check('host drew the kill table', wait_for('host', 'kill_table_drawn', 1, 20, focus=[host]))
keys(host, 'TAB', 'wait=0.5')

# 4b. the full ammo is back after 15 s
left = 15 - (time.time() - taken_at)
if left > 0: play(left + 1)
check('host put the full ammo back', wait_for('host', 'world_item_respawned', 1, 30) and any(r.get('id') == 42 and r.get('result') == 'ok' for r in rows('host', 'world_item_respawned')), rows('host', 'world_item_respawned')[-2:])
check('client put it back too', wait_for('client', 'world_item_back_applied', 1, 30, focus=[client]) and any(r.get('id') == 42 and r.get('result') == 'ok' for r in rows('client', 'world_item_back_applied')), rows('client', 'world_item_back_applied')[-2:])
grab(client, '5_client_ammo_back.png', 1.0)

# 6. team items: ankh 13 (asked) and doll 16 (taken there, reported)
(out / 'touch.flag').write_text('go')
check('client touched the ankh and the doll', wait_for('client', 'world_debug_touch', 2, 30, focus=[client]), rows('client', 'world_debug_touch'))
check('host shared both', wait_for('host', 'world_team_item_shared', 2, 40), rows('host', 'world_team_item_shared', 'world_touch_applied')[-4:])
check('client applied both', wait_for('client', 'world_team_item_applied', 2, 40, focus=[client]), rows('client', 'world_team_item_applied'))
sh = rows('host', 'world_team_item_shared')
check('ankh: given to the host player too (P14 team grant)', any(r.get('kind') == 1 and r.get('id') == ANKH for r in sh) and count('host', 'p14_ankh_granted') >= 2)
check('doll: the host player got it', any(r.get('kind') == 2 and r.get('id') == DOLL and r.get('host_player_got_it') for r in sh))
ap = rows('client', 'world_team_item_applied')
check('client owns the ankh (granted here) and the doll (its own pickup)', any(r.get('kind') == 1 and r.get('granted') for r in ap) and any(r.get('kind') == 2 for r in ap))
keys(client, 'ESC', 'wait=1')                     # the doll's inventory screen of the stock pickup

# 7. the death cry of the client reaches the host at the listener
(out / 'kill.flag').write_text('go')
check('client died', wait_for('client', 'client_player_dead', 1, 60))
check('client sent its death sound', wait_for('client', 'world_sound_sent', 1, 20, focus=[client]) and any('death' in r.get('path', '') for r in rows('client', 'world_sound_sent')), [r.get('path') for r in rows('client', 'world_sound_sent')][-4:])
check('host played it at the listener', wait_for('host', 'world_death_sound_global', 1, 30), rows('host', 'world_death_sound_global')[-2:])

bad = {}
for role in ('host', 'client'):
    for r in rows(role):
        ev = str(r.get('event'))
        if any(w in ev for w in ('block', 'fault', 'fail', 'error', 'mismatch')) and ev not in ('world_enemy_mismatch',): bad.setdefault(role, {}).setdefault(ev, r.get('reason') or r.get('site'))
print('problems:', bad or 'none')
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
passed = sum(1 for _, ok in results if ok)
print(f'{passed}/{len(results)} passed; screens and logs in {out}')
(out / 'summary.json').write_text(json.dumps({'results': results, 'problems': bad}, indent=1), encoding='utf8')
