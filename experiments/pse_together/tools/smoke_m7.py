"""Smoke of the 2026-11-11 (round 7) fixes on one PC: python -X utf8 smoke_m7.py <install-root>

1. The host, still alone, goes from TOMB to KARNAK (PSE_DEBUG_SOLO_MAP, like a save loaded there). The client starts a new game on another
   difficulty, joins from TOMB, follows to KARNAK and must rejoin the host's session there (it used to be rejected for good: "Session already started").
2. The client adopts the host's difficulty (one health bar, not two).
3. Both players stand in the water of KARNAK sector 156 next to ammo sphere 42: the proxies are drawn in water (proxy_water).
4. The host gives P2 the pistol (PSE_DEBUG_P2_GRANT), the client takes it in hand and walks into the sphere: the client asks, the host gives it
   to P2's body and removes it for everybody, the client shows the pickup (world_pickup_asked / world_pickup_take / world_pickup_applied mine).
5. Walking players show walk/idle poses on the other screen (p8_animation changed=true).
Screenshots and a summary go to <root>/smoke_m7_<time>/.
"""
import glob, json, os, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_m7_%Y%m%d_%H%M%S')
out.mkdir()
SPHERE = (5194.0, 384.0, -1488.0)
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/KARNAK.MAP')
os.environ.setdefault('PSE_DEBUG_P2_GRANT', '1,10')
# both in the water north of the sphere looking south (-y): the host 320 units away, the client between the host and the sphere
os.environ.setdefault('PSE_DEBUG_TELEPORT_CLIENT', f'{SPHERE[0] - 14},{SPHERE[1] + 176},3.14159,{SPHERE[2]}')
os.environ.setdefault('PSE_DEBUG_TELEPORT', f'{SPHERE[0] - 60},{SPHERE[1] + 320},3.14159,{SPHERE[2]}')
# round 8: an enemy of KARNAK (map id 26, near this water) is put 260 units in front of the client's body when the smoke says so;
# the host kills P2 late (world tick), the client respawns on E and must come back with the difficulty's health, not 200.
os.environ.setdefault('PSE_DEBUG_HIT_CLIENT', '26,50')   # the client's player "hits" map enemy 26 (40 hp) for 50
os.environ.setdefault('PSE_DEBUG_HIT_FILE', str(out / 'hit.flag'))
os.environ.setdefault('PSE_DEBUG_ENEMY', '26')   # both sides log probes of enemy 26 (hp, AI state)
os.environ.setdefault('PSE_DEBUG_KILL_P2_FILE', str(out / 'kill.flag'))


# New Game asks "overwrite this save?" (default No) once a slot exists: this test install starts every run with empty save slots.
for role in ('host', 'client'):
    saves = root / 'profile' / role / 'early_native_logs' / 'saves'
    if saves.exists() and any(saves.iterdir()): saves.rename(saves.with_name('saves_before_' + out.name)); saves.mkdir()


def pids():
    t = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq anubis_Shipping_Steam_x64.exe', '/FO', 'CSV', '/NH'], capture_output=True).stdout.decode('latin1')
    return {int(l.split(',')[1].strip('"')) for l in t.splitlines() if l.startswith('"anubis')}


def ensure_binds(role):
    cfg = root / 'profile' / role / 'early_native_logs' / 'kexengine.cfg'
    if not cfg.exists():
        cfg.parent.mkdir(parents=True, exist_ok=True); cfg.write_text((HERE / 'smoke_kexengine.cfg').read_text(encoding='utf8'), encoding='utf8')
    text = cfg.read_text(encoding='utf8')
    if 'bind f "attack"' not in text:
        cfg.write_text(text.rstrip() + chr(10) + 'bind f "attack"' + chr(10), encoding='utf8')


def launch(role):
    ensure_binds(role)
    before = pids()
    subprocess.Popen([str(root / 'pe-play.exe'), '--role', role], cwd=str(root), stdout=(out / f'{role}_stdout.txt').open('wb'), stderr=subprocess.STDOUT,
                     env={**os.environ, 'PSE_PLAYER_NAME': 'Host' if role == 'host' else 'Guest'})
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
        if a.startswith('mouse:'): act.focus(h); act.mouse_move(int(a[6:])); continue
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


MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'DOWN', 'wait=0.4', 'DOWN', 'wait=0.4', 'RETURN', 'wait=5', 'RETURN', 'wait=12')  # host: difficulty 2 (Pharaoh)
host = launch('host'); time.sleep(8)
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
print('host alone goes to KARNAK:', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)   # the load ticks only in the focused window
look(host, '0_host_karnak_alone.png', 1)
client = launch('client'); time.sleep(8)
CLIENT_MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')  # client: default difficulty 0
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *CLIENT_MENU)
both = [client, host]
print('client followed the host to KARNAK:', wait_for('client', 'client_follow_host_map', 1, 60, both))
print('client welcomed on KARNAK (2nd welcome):', wait_for('client', 'client_welcome', 2, 120, both))
print('host built P2:', wait_for('host', 'snapshot_entity_bindings', 1, 90, both))
print('client teleported:', wait_for('client', 'world_debug_teleport', 1, 60, both), '| host teleported:', wait_for('host', 'world_debug_teleport', 1, 60, both))
print('P2 pistol granted:', wait_for('host', 'debug_p2_grant', 1, 60, both))
time.sleep(2)
look(host, '1_host_sees_client_in_water.png', 2); look(client, '2_client_sees_host_in_water.png', 2)
for i in range(6):   # turn in place: the other player is found wherever the teleport left the view
    keys(host, 'mouse:330', 'wait=0.6'); look(host, f'1_host_sweep_{i}.png', 0.6)
    keys(client, 'mouse:330', 'wait=0.6'); look(client, f'2_client_sweep_{i}.png', 0.6)
keys(client, '2', 'wait=2')                       # pistol in hand (the sphere gives pistol ammo)
look(client, '3_client_pistol.png', 0.5)
keys(client, 'hold:W=2.2', 'wait=0.3')            # into the sphere (slow under water)
look(client, '4_client_after_pickup.png', 0.3)
print('pickup asked:', wait_for('client', 'world_pickup_asked', 1, 10, both), '| host took:', wait_for('host', 'world_pickup_take', 1, 20, both),
      '| client applied:', wait_for('client', 'world_pickup_applied', 1, 20, both))
look(host, '5_host_after_pickup.png', 1.5)
# walking animations seen from the other window
keys(client, 'hold:S=1.2', 'wait=0.2'); look(host, '6_host_sees_client_walk.png', 0.1)
keys(host, 'hold:A=1.2', 'wait=0.2'); look(client, '7_client_sees_host_walk.png', 0.1)
keys(host, 'hold:D=1.2', 'wait=0.2'); look(client, '8_client_sees_host_walk_back.png', 0.1)
# combat: the enemy in front of the client, the client fires (F) — its hits go to the host, the enemy dies on both screens
(out / 'hit.flag').write_text('go')
print('client hit:', wait_for('client', 'world_debug_hit', 1, 30, both), '| host applied:', wait_for('host', 'world_hit_applied', 1, 30, both))
for _ in range(3): act.focus(act.hwnd_of(client)); time.sleep(1.5); act.focus(act.hwnd_of(host)); time.sleep(1.0)
# death and respawn of the client's player (PSE_DEBUG_KILL_P2): health after the respawn = the cap of the host's difficulty
(out / 'kill.flag').write_text('go')
print('client died:', wait_for('client', 'client_player_dead', 1, 60, both))
h = act.hwnd_of(client)
for attempt in range(8):
    if seen('client', 'p16_client_respawn_committed'): break
    act.focus(h); time.sleep(0.5); act.send(act.VK['E'], True); time.sleep(0.5); act.send(act.VK['E'], False); time.sleep(1.5)
print('client respawned:', wait_for('client', 'p16_client_respawn_committed', 1, 30, both))
for _ in range(4): act.focus(act.hwnd_of(client)); time.sleep(1.5); act.focus(act.hwnd_of(host)); time.sleep(1.0)
look(client, '12_client_after_respawn.png', 1.0)
time.sleep(2)
print('--- combat')
show('client', 'world_debug_hit', 'world_hit_local', 'world_enemy_gone_applied', 'p16_client_respawn_committed', 'world_health_capped')
show('host', 'world_hit_applied', 'world_remote_hit_muted', 'world_debug_pull_enemy', 'world_health_capped', 'p16_respawn_committed')
for role in ('host', 'client'):
    pr = [r for r in rows(role) if r['event'] == 'world_enemy_probe']
    print(' ', role, 'enemy 26 probes:', len(pr), [(r['utc'][14:23], r.get('hp'), r.get('ai')) for r in pr[-6:]])
print('--- session / identity')
show('client', 'client_welcome', 'client_identity_reset', 'blocked', 'client_follow_host_map')
print('--- difficulty / health')
show('host', 'world_difficulty_sent')
show('client', 'world_difficulty_adopted')
own = [r for r in rows('client') if r['event'] == 'world_own_health'][:6]
for r in own: print('  client own health', {k: r[k] for k in ('host', 'before', 'after', 'silent')})
print('--- pickups')
show('client', 'sphere_hook_ready', 'world_pickup_asked', 'world_pickup_applied', 'client_loadout_applied')
show('host', 'world_pickup_take', 'world_pickup_sent', 'p14_ammo_for_held_weapon', 'world_hud_sink_ready')
print('--- water')
show('host', 'proxy_water'); show('client', 'proxy_water')
print('--- animation changes')
for role in ('host', 'client'):
    ch = [(r['utc'][14:23], r['slot'], r['pose']) for r in rows(role) if r['event'] == 'p8_animation' and r.get('changed')]
    print(' ', role, len(ch), ch[:24])
bad = {}
for role in ('host', 'client'):
    for r in rows(role):
        if any(w in str(r['event']) for w in ('block', 'fault', 'fail', 'error')): bad.setdefault(role, {}).setdefault(r['event'], r.get('reason'))
print('problems:', bad or 'none')
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
