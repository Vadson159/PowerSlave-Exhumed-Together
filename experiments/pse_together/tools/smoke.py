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
# A smoke run never touches the user's real Steam achievements: the mod's SetAchievement hook only logs (PSE_ACH_DRYRUN=1).
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
if os.environ.get('SMOKE_CAMPAIGN'):
    # Milestone 4: host "walks through the exit" (same native ChangeMap as the exit trigger) and the client follows to the next map.
    os.environ.setdefault('PSE_DEBUG_CHANGE_MAP', 'maps/KARNAK.MAP')
    os.environ.setdefault('PSE_DEBUG_MAP_FILE', str(out / 'go_next_map.flag'))
    os.environ.setdefault('PSE_DEBUG_P2_GRANT', '3,40')   # host P2 gets weapon 3 + 40 ammo: it must arrive on the next map
    os.environ.setdefault('PSE_DEBUG_ACH', '2')           # host earns ACH_TEAM_DOLL -> the client's game gets it too
    os.environ.setdefault('PSE_DEBUG_ACH_CLIENT', '5')    # client earns ACH_MASK -> the host's game gets it too
if os.environ.get('SMOKE_LATE'):
    os.environ.setdefault('PSE_DEBUG_P1_GRANT', '5,77')   # the host already owns weapon 5 + 77 ammo when the client joins
if os.environ.get('SMOKE_SCENE'):
    # Story scene of the level (TOMB script 1, King Ramses): both players stand in the chamber; the HOST crosses the trigger (script 1 via
    # the hook, as a root call) and the client must play the scene too. SMOKE_SCENE=client makes the CLIENT the one who crosses it.
    os.environ.setdefault('PSE_DEBUG_TELEPORT', '0,2800,0,-864')
    os.environ.setdefault('PSE_DEBUG_TELEPORT_CLIENT', '0,2800,0,-864')
    if os.environ['SMOKE_SCENE'] == 'client': os.environ.setdefault('PSE_DEBUG_SCRIPT_CLIENT', '1')
    else: os.environ.setdefault('PSE_DEBUG_SCRIPT', '1')
if os.environ.get('SMOKE_ENDING'):
    os.environ.setdefault('PSE_DEBUG_END', '7')
    os.environ.setdefault('PSE_DEBUG_MAP_FILE', str(out / 'go_ending.flag'))


def pids():
    t = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq anubis_Shipping_Steam_x64.exe', '/FO', 'CSV', '/NH'], capture_output=True).stdout.decode('latin1')
    return {int(l.split(',')[1].strip('"')) for l in t.splitlines() if l.startswith('"anubis')}


def ensure_binds(role):
    # Smoke-only keyboard attack (key F): SendInput mouse clicks depend on the OS cursor and are not reliable between two windows.
    cfg = root / 'profile' / role / 'early_native_logs' / 'kexengine.cfg'
    if not cfg.exists():
        cfg.parent.mkdir(parents=True, exist_ok=True); cfg.write_text((HERE / 'smoke_kexengine.cfg').read_text(encoding='utf8'), encoding='utf8')
    text = cfg.read_text(encoding='utf8')
    if 'bind f "attack"' not in text:
        cfg.write_text(text.rstrip() + chr(10) + 'bind f "attack"' + chr(10), encoding='utf8')


def launch(role):
    ensure_binds(role)
    before = pids()
    subprocess.Popen([str(root / 'pe-play.exe'), '--role', role], cwd=str(root), stdout=(out / f'{role}_stdout.txt').open('wb'), stderr=subprocess.STDOUT, env={**os.environ, **({} if os.environ.get('SMOKE_REALNAMES') else {'PSE_PLAYER_NAME': 'Host' if role == 'host' else 'Guest'})})
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
        if a == 'click': act.focus(h); act.cursor_to(h); act.click(); time.sleep(0.25); continue
        if a.startswith('fire:'): act.focus(h); act.cursor_to(h); act.fire(float(a[5:])); continue
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
    out_rows = []
    for l in open(f, encoding='utf8', errors='replace'):
        if l.strip().endswith('}'):
            try: out_rows.append(json.loads(l))
            except ValueError: pass  # a line cut by a concurrent write
    return out_rows


def seen(role, name):
    # Cheap substring probe of the live log (rows() parses every line and is far too slow to poll during a fight).
    fs = sorted(glob.glob(str(root / 'profile' / role / 'logs' / f'native_{role}_*.jsonl')))
    return bool(fs) and (b'"event":"' + name.encode() + b'"') in open(fs[-1], 'rb').read()


def events(role, *names):
    return [r for r in rows(role) if r['event'] in names]


host = launch('host'); time.sleep(8)
MENU = ('RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=1.5', 'RETURN', 'wait=5', 'RETURN', 'wait=12')  # start, New Game, slot, difficulty, (map screen) load
keys(host, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)  # the player name is the default (PSE_PLAYER_NAME below)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
shot(host, '1_host_start.png'); shot(client, '2_client_start.png')
print('p2 spawned:', bool(events('host', 'snapshot_entity_bindings')), '| spawn choice:', [ (r['spawn'], r['distance']) for r in events('host', 'p4_spawn_chosen')][-1:])
# 1) client walks forward -> host P2 (native Puppet) walks ahead of the host; the host window should show it
if not os.environ.get('SMOKE_NOWALK'): keys(client, 'hold:W=' + str(hold * 2), 'wait=1')
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
    for i in range(1, 3):
        keys(client, 'DOWN', 'wait=0.5'); shot(client, f'm_{i}.png')
    keys(client, 'F7', 'wait=1'); look(client, 'm_f8.png', 0.5); keys(client, 'F7', 'wait=0.5'); keys(client, 'ESC', 'wait=1')
    keys(host, 'ESC', 'wait=1.5'); keys(host, 'F7', 'wait=1'); look(host, 'm_host_f8.png', 0.5); keys(host, 'F7', 'wait=0.5'); keys(host, 'ESC', 'wait=1')   # the host's own lobby screen
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
if os.environ.get('SMOKE_INPUT'):
    # Full client input (v1.14): turn, strafe both ways, jump, use, attack, walk. The host must see them and mirror the pose on P2.
    keys(client, 'wait=2', 'hold:W=0.6', 'wait=0.5', 'E', 'wait=4')  # door in front (PSE_DEBUG_TELEPORT_CLIENT=0,-1250,3.14159): real E on a real door
    for role in ('host', 'client'):
        for r in rows(role):
            if r['event'] in ('world_mover_local', 'world_mover_applied', 'world_mover_busy'): print(role, {k: r[k] for k in r if k in ('event', 'class', 'type', 'sector', 'spawned')})
    keys(client, 'mouse:700', 'wait=0.7', 'hold:A=1.0', 'wait=0.5', 'hold:D=1.0', 'wait=0.5', 'SPACE', 'wait=0.5', 'click', 'wait=0.5', 'hold:W=1.0', 'wait=1')
    look(host, '7_host_after_input.png', 2); look(client, '8_client_after_input.png', 2)
    cs = [r for r in rows('client') if r['event'] == 'client_input_sampled' and r.get('valid')]
    hs = [r for r in rows('host') if r['event'] == 'network_input_accepted']
    pa = [r for r in rows('host') if r['event'] == 'p2_pose_applied']
    print('client samples:', len(cs), '| yaw range: %.3f..%.3f' % (min(r['yaw'] for r in cs), max(r['yaw'] for r in cs)) if cs else '', '| right axis:', sorted({r['right'] for r in cs}),
          '| wire buttons:', sorted({r.get('wire_buttons') for r in cs}), '| kex buttons:', sorted({r['kex_buttons'] for r in cs}))
    print('host accepted (sampled log):', len(hs), '| native buttons:', sorted({r['native_buttons'] for r in hs}), '| wire:', sorted({r.get('wire_buttons') for r in hs}), '| edges:', sorted({r['edges'] for r in hs}),
          '| yaw range: %.3f..%.3f' % (min(r['yaw'] for r in hs), max(r['yaw'] for r in hs)) if hs else '')
    for r in pa[-3:]: print('p2 pose:', {k: r[k] for k in ('tick', 'target', 'p2', 'yaw', 'p2_yaw', 'sector', 'p2_sector', 'teleports')})
if os.environ.get('SMOKE_ENEMY'):
    # Client stands near an enemy (PSE_DEBUG_TELEPORT_CLIENT + PSE_DEBUG_ENEMY=<spawn id>): host AI wakes up on P2, the client must mirror it.
    keys(client, 'wait=8'); look(client, '9_client_enemy.png', 2); keys(client, 'wait=2', 'click', 'wait=1', 'click', 'wait=1', 'click', 'wait=4'); look(client, '10_client_enemy_after_attack.png', 2); look(host, '11_host_enemy.png', 2)
    for role in ('host', 'client'):
        pr = [r for r in rows(role) if r['event'] == 'world_enemy_probe']
        print(role, 'enemy probes:', len(pr))
        for r in pr[::max(1, len(pr) // 8)]: print('  ', {k: r[k] for k in ('pos', 'yaw', 'hp', 'ai', 'frame', 'sector', 'replica')}, r['anim'])
    print('client enemy apply:', [ {k: r[k] for k in ('applied', 'moved', 'known', 'batch')} for r in events('client', 'world_enemy_apply')][-1:])
if os.environ.get('SMOKE_KEYS'):
    # Which native button bits does each kind of input produce on the client? (attack key F, left mouse, space)
    from datetime import datetime, timezone
    def now(): return datetime.now(timezone.utc).strftime('%H:%M:%S.%f')[:12]
    marks = []
    keys(client, 'wait=3')
    for name, seq in (('key F', ('hold:F=1.0',)), ('mouse left', ('fire:1.0',)), ('space', ('SPACE',)), ('key W', ('hold:W=0.5',))):
        t0 = now(); keys(client, *seq); keys(client, 'wait=0.5'); marks.append((name, t0, now()))
    for name, a, b in marks:
        sm = [r for r in rows('client') if r['event'] == 'client_input_sampled' and r.get('valid') and a <= r['utc'][11:23] <= b]
        print('input', name, 'samples', len(sm), 'kex_buttons', sorted({r['kex_buttons'] for r in sm}), 'wire', sorted({r['wire_buttons'] for r in sm}), 'held_forward max', max([r['held_forward'] for r in sm] or [0]))
if os.environ.get('SMOKE_COMBAT'):
    # Milestone 3 combat: the client stands in front of a mummy (PSE_DEBUG_TELEPORT_CLIENT + PSE_DEBUG_ENEMY=<spawn id>).
    # a) the mummy walks up, b) the client shoots (host P2 fires), c) the mummy hits the client: red flash + lower HP.
    if os.environ.get('PSE_DEBUG_PULL_FILE'): Path(os.environ['PSE_DEBUG_PULL_FILE']).write_text('go')
    deadline = time.time() + 60
    while not (seen('client', 'world_debug_teleport') or seen('host', 'world_debug_pull_enemy')) and time.time() < deadline: time.sleep(0.05)
    h = act.hwnd_of(client); act.HW[0] = h; act.focus(h)
    procs = []
    def shot_async(pid, name):  # window_shot takes ~1 s: never block the fight on it
        procs.append(subprocess.Popen([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(pid), str(out / name)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'}))
    shot_async(client, 'c1_mummy_approaches.png'); shot_async(host, 'c1_host_view.png')
    time.sleep(float(os.environ.get('SMOKE_APPROACH', '0.6')))
    if os.environ.get('PSE_DEBUG_CENSUS_FILE'): Path(os.environ['PSE_DEBUG_CENSUS_FILE']).write_text('go')
    # c) the mummy hits the client: a series of quick pictures catches the red flash and the lower HP bar
    for i in range(int(os.environ.get('SMOKE_HIT_SHOTS', '6'))):
        shot_async(client, f'c2_mummy_hits_{i:02d}.png'); time.sleep(0.3)
    shot_async(host, 'c2_host_sees_hit.png')
    # b) the client strikes back (F = attack): the mummy dies on both screens
    act.post(act.VK['F'], True)
    for i in range(int(os.environ.get('SMOKE_SHOTS', '6'))):
        time.sleep(0.4); shot_async(client, f'c3_client_attacks_{i:02d}.png')
        if i in (1, 3): shot_async(host, f'c3_host_sees_{i:02d}.png')
    act.post(act.VK['F'], False)
    for pr in procs: pr.wait(timeout=60)
    look(client, 'c3_after.png', 1.0); look(host, 'c3_host_after.png', 1.0)
    for role in ('host', 'client'):
        pr = [r for r in rows(role) if r['event'] == 'world_enemy_probe']
        print(role, 'enemy probes:', len(pr))
        for r in pr[::max(1, len(pr) // 12)]: print('  ', {k: r[k] for k in ('hp', 'ai', 'pos', 'sector', 'replica')})
    for r in events('client', 'world_own_health', 'world_own_damage_suppressed', 'client_player_dead'): print('client', {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'steady_ns')})
    for r in events('host', 'p16_death_observed', 'p16_respawn_committed'): print('host', {k: v for k, v in r.items() if k not in ('seq', 'tid')})
    hp = [(r['utc'][11:23], r['p2_health']) for r in events('host', 'network_native_motion')]
    print('host P2 hp samples:', hp[::max(1, len(hp) // 12)])
if os.environ.get('SMOKE_RESPAWN'):
    # The host kills P2 (PSE_DEBUG_KILL_P2=<host world tick>): the client dies, presses E (real key state), respawns and is alive again.
    deadline = time.time() + 90
    while not seen('client', 'client_player_dead') and time.time() < deadline: time.sleep(0.1)
    h = act.hwnd_of(client); act.HW[0] = h; act.focus(h); time.sleep(1.0); shot(client, 'r1_client_dead.png')
    for attempt in range(6):
        if seen('client', 'p16_client_respawn_committed'): break
        act.focus(h); act.send(act.VK['E'], True); time.sleep(0.5); act.send(act.VK['E'], False); time.sleep(1.5)
    time.sleep(2.0); look(client, 'r2_client_respawned.png', 1.0); look(host, 'r2_host_after.png', 1.0)
    last = None
    for r in [r for r in events('host', 'p7_native_scalars') if r['slot'] == 1]:
        k = (r['life_state'], r['weapon_mask'], tuple(r['ammo']))
        if k != last: print('host P2', r['utc'][14:23], 'life', r['life_state'], 'mask', r['weapon_mask'], 'ammo', r['ammo']); last = k
    for r in events('client', 'client_loadout_applied')[-3:]: print('client loadout', r['utc'][14:23], 'owned', r['owned'], 'ammo', r['ammo'])
    for role in ('host', 'client'):

        for r in events(role, 'p16_death_observed', 'p16_respawn_request', 'p16_respawn_committed', 'p16_E_request_sent', 'p16_generation_admitted', 'p16_client_confirm_native', 'p16_client_respawn_committed', 'blocked', 'world_own_health'):
            print(role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns')})
if os.environ.get('SMOKE_FIREBALL'):
    # Host-side Anubis fireball aimed at P2 (PSE_DEBUG_FIREBALL="dist,speed" + PSE_DEBUG_FIREBALL_FILE): the client must SEE it fly and then get hit.
    Path(os.environ['PSE_DEBUG_FIREBALL_FILE']).write_text('go')
    deadline = time.time() + 40
    while not seen('host', 'world_debug_fireball') and time.time() < deadline: time.sleep(0.05)
    h = act.hwnd_of(client); act.HW[0] = h; act.focus(h)
    procs = []
    for i in range(int(os.environ.get('SMOKE_SHOTS', '8'))):
        procs.append(subprocess.Popen([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(client), str(out / f'f_client_{i:02d}.png')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'}))
        time.sleep(0.2)
    for pr in procs: pr.wait(timeout=60)
    Path(os.environ['PSE_DEBUG_FIREBALL_FILE']).unlink()
    for role in ('host', 'client'):
        for r in events(role, 'world_projectile_defs', 'world_debug_fireball', 'world_projectile_spawned', 'client_loadout_applied', 'world_own_health')[:14]: print(role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns')})
if os.environ.get('SMOKE_WEAPON'):
    # Host P2 gets the pistol (PSE_DEBUG_P2_GRANT="1,33"): ammo + ownership reach the client, the client selects it (key 2) and P2 follows.
    time.sleep(6)
    keys(client, '2', 'wait=2'); look(client, 'w_client_pistol.png', 1.0)
    keys(client, 'hold:F=0.6', 'wait=1.5'); look(client, 'w_client_fired.png', 0.5)
    for r in events('host', 'debug_p2_grant'): print('host', {k: v for k, v in r.items() if k not in ('seq', 'tid')})
    for r in events('client', 'client_loadout_applied')[:4]: print('client', {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'steady_ns')})
    sc = [r for r in events('host', 'p7_native_scalars') if r['slot'] == 1]
    last = None
    for r in sc:
        k = (r['active_weapon'], r['pending_weapon'], r['weapon_mask'], tuple(r['ammo']))
        if k != last: print('host P2', r['utc'][14:23], 'active', r['active_weapon'], 'mask', r['weapon_mask'], 'ammo', r['ammo']); last = k
if os.environ.get('SMOKE_CAMPAIGN'):
    def wait_for(role, name, count=1, timeout=90):
        end = time.time() + timeout; turn = 0
        while time.time() < end:
            if len(events(role, name)) >= count: return True
            act.focus(act.hwnd_of(host if turn % 2 == 0 else client)); turn += 1   # one PC: only the focused window runs
            time.sleep(2)
        return False
    print('P2 grant seen:', wait_for('host', 'debug_p2_grant', 1, 60))
    time.sleep(3); look(host, 'k1_host_tomb.png', 2); look(client, 'k1_client_tomb.png', 2)
    print('achievements shared in TOMB: host->client applied:', [r['key'] for r in events('client', 'ach_applied')], '| client->host applied:', [r['key'] for r in events('host', 'ach_applied')])
    Path(os.environ['PSE_DEBUG_MAP_FILE']).write_text('go')
    print('host announced the change:', wait_for('host', 'host_map_change_announced', 1, 60), '| client follows:', wait_for('client', 'client_follow_map_received', 1, 20))
    print('host world rearmed:', wait_for('host', 'host_world_rearmed', 1, 60), '| client transition:', wait_for('client', 'world_transition', 1, 30))
    # the client must rejoin the NEW host session and both must have P2/proxy again on KARNAK
    print('second P2 spawn on host:', wait_for('host', 'snapshot_entity_bindings', 2, 150), '| second client_welcome:', wait_for('client', 'client_welcome', 2, 60))
    time.sleep(8); look(host, 'k2_host_karnak.png', 2); look(client, 'k2_client_karnak.png', 2)
    keys(client, 'hold:W=1.5', 'wait=1'); look(host, 'k3_host_sees_p2.png', 3)
    keys(host, 'hold:W=1.0', 'wait=1'); look(client, 'k3_client_sees_host.png', 3)
    for role, names in (('host', ('debug_change_map', 'host_map_change_announced', 'p2_travel_captured', 'state_exit_entry', 'host_world_rearmed', 'map_identity_bound', 'network_started', 'p2_travel_restored', 'proxy_world_transition', 'ach_local', 'ach_applied')),
                        ('client', ('client_follow_map_received', 'client_follow_map_change', 'world_transition', 'client_welcome', 'client_loadout_applied', 'ach_local', 'ach_applied'))):
        for r in events(role, *names): print(role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
if os.environ.get('SMOKE_LATE'):
    # Late joiner: P2 is created from a copy of the host's live inventory (no travel happened yet).
    end = time.time() + 60
    while time.time() < end and not events('host', 'p2_seeded_from_host'):
        act.focus(act.hwnd_of(host)); time.sleep(1); act.focus(act.hwnd_of(client)); time.sleep(1)
    for r in events('host', 'debug_p1_grant', 'p2_seeded_from_host', 'p2_travel_restored'): print('host', {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
    for r in events('client', 'client_loadout_applied')[:2]: print('client', {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
if os.environ.get('SMOKE_SCENE'):
    time.sleep(2)
    end = time.time() + 60
    while time.time() < end and not (events('host', 'script_call') and events('client', 'script_call')):
        act.focus(act.hwnd_of(host)); time.sleep(1); act.focus(act.hwnd_of(client)); time.sleep(1)
    for i in range(5):   # the scene lasts several seconds: camera lock, Ramses, dialogue
        act.focus(act.hwnd_of(client)); time.sleep(1.2); shot(client, f's_client_{i}.png')
        act.focus(act.hwnd_of(host)); time.sleep(1.2); shot(host, f's_host_{i}.png')
    for role in ('host', 'client'):
        for r in events(role, 'script_hook_ready', 'script_debug_trigger', 'script_call', 'script_applied', 'script_skipped', 'world_mover_local', 'world_mover_applied', 'world_mover_busy'):
            print(role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
if os.environ.get('SMOKE_ENDING'):
    # Story ending: the host asks game state 7 (EndGame(true)); the client must follow and play its own ending movie.
    print('P2 spawned:', bool(events('host', 'snapshot_entity_bindings')))
    time.sleep(5); look(host, 'e1_host_before.png', 1); look(client, 'e1_client_before.png', 1)
    Path(os.environ['PSE_DEBUG_MAP_FILE']).write_text('go')
    time.sleep(1.2)
    for i in range(6):   # movie frames on both windows
        act.focus(act.hwnd_of(client)); time.sleep(1.5); shot(client, f'e2_client_{i}.png')
        act.focus(act.hwnd_of(host)); time.sleep(1.5); shot(host, f'e2_host_{i}.png')
    for role, names in (('host', ('debug_end_game', 'host_map_change_announced', 'state_exit_entry', 'host_world_rearmed')), ('client', ('client_follow_map_received', 'client_follow_state', 'world_transition'))):
        for r in events(role, *names): print(role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
if os.environ.get('SMOKE_LOOK'):
    # Free-look at the PSE_DEBUG_TELEPORT[_CLIENT] spot: one picture per 90-degree turn (relative mouse), host and client.
    for pid, name in ((client, 'client'), (host, 'host')):
        for i in range(4):
            look(pid, f'l_{name}_{i}.png', 1.0); keys(pid, 'mouse:500', 'wait=0.5')
for role in ('host', 'client'):   # lobby: the host opens one by itself, the client finds it by the beacon and joins from the list
    for r in events(role, 'p23_lobby_auto_created', 'p23_lobbies_seen', 'p23_menu_submit'): print(role, {k: v for k, v in r.items() if k not in ('record', 'pid', 'tid', 'seq', 'steady_ns', 'utc')})
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

