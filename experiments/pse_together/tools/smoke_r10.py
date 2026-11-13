"""Round 10 smoke on one PC (friend's run 2026-11-12): python -u -X utf8 smoke_r10.py <install-root>

The host alone goes to KARNAK, the client joins. Then, with the client's window focused (the host keeps ticking in the background):
  1. An Anubis (map enemy 21) is put in front of the client's body and "fires" fireballs the way an enemy does: the fireball appears inside
     it, owned by it. The client's mirror must get the same owner, and the Anubis must not be hit by its own shots (no hit goes to the host).
  2. The client's player hits that Anubis for more than its health: an extreme death at the host. The client must get the death at once
     (EvEnemyDeath) and play it natively, not see a living enemy vanish half a second later.
  3. The host removes three map enemies natively (their objects go back to the engine's pool), then the host spawns 3 enemies during play
     and the client 2 of its own. The engine hands out the pooled objects WITHOUT their constructor: the map number in +0x180 used to stay.
     Now every spawned enemy has -1 there: the host's are mirrored at the client, the client's own are removed, the enemy check is clean.

  4. A map trap (fireball spawner 1 of KARNAK) is switched off and on every tick for 240 ticks, as the engine does when one player stands in
     the sector that starts it and another in the sector that stops it. Each switch-on primes its timer, so it fired every tick (a solid
     line of fireballs). Now it fires no faster than its own period.

SMOKE_R10_KEEP=1 runs step 3 only with PSE_DEBUG_KEEP_POOL_ID=1 (the engine's own behaviour, as before the fix): the stale numbers must show.
SMOKE_R10_NOLIMIT=1 runs step 4 only with PSE_DEBUG_TRAP_NOLIMIT=1 (no limit, as before the fix): the trap must fire about every tick.
SMOKE_R10_NOOWNER=1 runs step 1 only with PSE_DEBUG_NO_MIRROR_OWNER=1 (the client's mirror has no owner, as before the fix): the mirror
bursts on its shooter at the client, and that hit must be dropped there instead of going to the host as the player's hit.
"""
import glob, json, os, subprocess, sys, time
from datetime import datetime
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ui_act as act  # noqa: E402

KEEP = os.environ.get('SMOKE_R10_KEEP') == '1'
NOLIMIT = os.environ.get('SMOKE_R10_NOLIMIT') == '1'
NOOWNER = os.environ.get('SMOKE_R10_NOOWNER') == '1'
TRAP = 1
ANUBIS, REMOVED = 21, (216, 204, 215)   # KARNAK: 21 is an Anubis on every difficulty the smokes ran on (239 is not), the others are small enemies
root = Path(sys.argv[1]).resolve()
out = root / time.strftime('smoke_r10_%Y%m%d_%H%M%S')
out.mkdir()
os.environ.setdefault('PSE_ACH_DRYRUN', '1')
os.environ.setdefault('PSE_DEBUG_SOLO_MAP', 'maps/KARNAK.MAP')
os.environ.setdefault('PSE_DEBUG_REMOVE', ','.join(map(str, REMOVED)))
os.environ.setdefault('PSE_DEBUG_REMOVE_FILE', str(out / 'remove.flag'))
os.environ.setdefault('PSE_DEBUG_SPAWN_AI', '3')
os.environ.setdefault('PSE_DEBUG_SPAWN_AI_CLIENT', '2')
os.environ.setdefault('PSE_DEBUG_SPAWN_AI_FILE', str(out / 'spawn.flag'))
os.environ.setdefault('PSE_DEBUG_TRAP_THRASH', str(TRAP))
os.environ.setdefault('PSE_DEBUG_TRAP_FILE', str(out / 'trap.flag'))
if KEEP:
    os.environ['PSE_DEBUG_KEEP_POOL_ID'] = '1'
elif NOLIMIT:
    os.environ['PSE_DEBUG_TRAP_NOLIMIT'] = '1'
else:
    if NOOWNER: os.environ['PSE_DEBUG_NO_MIRROR_OWNER'] = '1'
    os.environ.setdefault('PSE_DEBUG_PULL_ENEMY', f'{ANUBIS},300')
    os.environ.setdefault('PSE_DEBUG_PULL_FILE', str(out / 'pull.flag'))
    os.environ.setdefault('PSE_DEBUG_FIREBALL', '300,500')
    os.environ.setdefault('PSE_DEBUG_FIREBALL_OWNER', str(ANUBIS))
    os.environ.setdefault('PSE_DEBUG_FIREBALL_FILE', str(out / 'fire.flag'))
    os.environ.setdefault('PSE_DEBUG_ENEMY', str(ANUBIS))
    os.environ.setdefault('PSE_DEBUG_HIT_CLIENT', f'{ANUBIS},300')
    os.environ.setdefault('PSE_DEBUG_HIT_FILE', str(out / 'hit.flag'))
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


def shot(pid, name):
    subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'window_shot.py'), str(pid), str(out / name)], capture_output=True, env={**os.environ, 'PYTHONIOENCODING': 'utf-8'})


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
check('host alone goes to KARNAK', wait_for('host', 'debug_solo_map', 1, 90))
for _ in range(10): act.focus(act.hwnd_of(host)); time.sleep(2)
client = launch('client'); time.sleep(8)
keys(client, 'F7', 'wait=1', 'RETURN', 'wait=2', *MENU)
both = [client, host]
check('client ready on KARNAK', wait_for('client', 'client_input_domain_ready', 1, 180))
play(8)

if not KEEP and not NOLIMIT:
    # 1. An enemy's own shots.
    (out / 'pull.flag').write_text('go')
    check('Anubis put in front of the client body', wait_for('host', 'world_debug_pull_enemy', 1, 40, focus=[client]))
    print('  ', rows('host', 'world_debug_pull_enemy')[-1:])
    play(2)
    hits_before = [r for r in rows('host', 'world_hit_applied') if r['id'] == ANUBIS]
    (out / 'fire.flag').write_text('go')
    check('host fired as the Anubis', wait_for('host', 'world_debug_fireball', 2, 40, focus=[client]))
    play(4)
    (out / 'fire.flag').unlink()
    fired = rows('host', 'world_debug_fireball')
    owned = [r for r in rows('client', 'world_projectile_owner') if r['owner'] == ANUBIS]
    print('   host shots:', len(fired), fired[-1:], '| client mirrors with owner:', owned[-2:])
    check('the shots are owned by the Anubis at the host', fired and all(r.get('linked') for r in fired))
    if not NOOWNER: check('the client mirror got the same owner', any(r['linked'] for r in owned))
    self_hits = [r for r in rows('client', 'world_hit_local') if r['id'] == ANUBIS]
    host_hits = [r for r in rows('host', 'world_hit_applied') if r['id'] == ANUBIS][len(hits_before):]
    probes = [r for r in rows('host', 'world_enemy_probe') if r['id'] == ANUBIS]
    dropped = rows('client', 'world_foreign_hit_dropped')
    print('   client hits on the Anubis:', self_hits, '| host hits applied:', host_hits, '| host hp:', [r['hp'] for r in probes[-3:]],
          '| foreign hits dropped at the client:', [(r['id'], r['amount'], r['total']) for r in dropped])
    check('the Anubis is not hit by its own fireballs', not self_hits and not host_hits and (not probes or probes[-1]['hp'] == probes[0]['hp']))
    if NOOWNER: check('REPRODUCED: a mirror without an owner bursts on its shooter at the client; the hit is dropped there', any(r['id'] == ANUBIS for r in dropped))
    else: check('with the owner set the mirror does not touch its shooter at all', not dropped)
    shot(client, '1_client_fireballs.png')
if not KEEP and not NOLIMIT and not NOOWNER:
    # 2. The enemy dies at the host by the client's hit: the client plays the same death at once.
    (out / 'hit.flag').write_text('go')
    check('client hit the Anubis', wait_for('client', 'world_debug_hit', 1, 30, focus=[client]))
    check('host: the enemy died', wait_for('host', 'world_enemy_death', 1, 30, focus=[client]))
    check('client: the death arrived', wait_for('client', 'world_enemy_death_applied', 1, 30, focus=[client]))
    hd = [r for r in rows('host', 'world_enemy_death') if r['id'] == ANUBIS]; cd = [r for r in rows('client', 'world_enemy_death_applied') if r['id'] == ANUBIS]
    sd = [r for r in rows('client', 'world_enemy_death_snapshot') if r['id'] == ANUBIS]
    print('   host death:', hd[-1:], '| client event:', cd[-1:], '| client snapshot death:', sd[-1:])
    # The event plays the death; a snapshot collected in the very tick of the death may overtake the event by a tick and play it first.
    by_event = bool(hd and cd and hd[-1]['extreme'] == cd[-1]['extreme'] and cd[-1]['killed'])
    check('the client played the death natively (by the event, or a tick earlier by the snapshot)', by_event or bool(hd and cd and sd and not cd[-1]['killed']))
    play(3)
    stamp = lambda r: datetime.strptime(r['utc'], '%Y-%m-%dT%H:%M:%S.%fZ')
    late = [r for r in rows('client', 'world_enemy_probe') if r['id'] == ANUBIS and r['hp'] > 0 and hd and (stamp(r) - stamp(hd[-1])).total_seconds() > 1.0]
    check('no living copy of the dead enemy stays at the client', bool(hd) and not late)
    shot(client, '2_client_after_death.png')

# 3. Pooled objects of removed map enemies handed out again.
if not NOLIMIT and not NOOWNER:
    (out / 'remove.flag').write_text('go')
    check('host removed three map enemies', wait_for('host', 'world_debug_remove', len(REMOVED), 40, focus=[client]))
    play(4)
    spawned_before = count('client', 'world_dynamic_spawned')
    (out / 'spawn.flag').write_text('go')
    check('host spawned enemies', wait_for('host', 'world_debug_spawn_ai', 1, 60, focus=[client]))
    check('client spawned its own', wait_for('client', 'world_debug_spawn_ai', 1, 60, focus=[client]))
    play(8)
    hs = rows('host', 'world_debug_spawn_ai')[-1:]; cs = rows('client', 'world_debug_spawn_ai')[-1:]
    print('   host spawn:', hs, '| client spawn:', cs)
    stale = [n for r in hs + cs for n in r.get('numbers', []) if n >= 0]
    mirrors = count('client', 'world_dynamic_spawned') - spawned_before
    after = [r for r in rows('client', 'world_enemy_mismatch') if r['utc'] > max([x['utc'] for x in rows('client', 'world_debug_spawn_ai')] or [''])]
    print('   mirrors at the client:', mirrors, '| culled events:', count('client', 'world_dynamic_culled'), '| mismatches after:',
          [(r['utc'][11:19], r['local_only'], r['host_only'], r['local_dynamic'], r['host_dynamic_missing'], r['local_only_list'][:3], r['host_only_list'][:3]) for r in after[-4:]])
    shot(client, '3_client_spawned.png')
    if KEEP:
        check('REPRODUCED: the engine hands out pooled objects with the old map number', bool(stale))
        print('   stale numbers:', stale)
    else:
        check('every enemy spawned during play has no map number (-1)', hs and cs and hs[0]['spawned'] >= 1 and not stale)
        check('client mirrored the host enemies', hs and mirrors >= hs[0]['spawned'])
        check('client removed its own spawned enemies', count('client', 'world_dynamic_culled') >= 1)
        check('the enemy check is clean at the end', not after or all(after[-1][k] == 0 for k in ('local_only', 'host_only', 'local_dynamic', 'host_dynamic_missing')))

# 4. A map trap switched off and on every tick.
if not KEEP and not NOOWNER:
    (out / 'trap.flag').write_text('go')
    check('host toggled the trap every tick (240 ticks)', wait_for('host', 'world_debug_trap', 4, 60, focus=[client]))
    trap = rows('host', 'world_debug_trap')
    print('   trap:', [(r.get('left'), r.get('shots'), r.get('skipped'), r.get('limit')) for r in trap], '| skipped events:', count('host', 'world_trap_refire_skipped'))
    if NOLIMIT:
        check('REPRODUCED: a trap switched off and on every tick fires about every tick', trap and trap[-1].get('shots', 0) >= 100)
    else:
        check('the toggled trap fired no faster than its own period', trap and 1 <= trap[-1].get('shots', 0) <= 4 and trap[-1].get('skipped', 0) >= 100)
    play(2)
for role in ('host', 'client'):
    bad = sum(count(role, e) for e in ('blocked', 'native_fault', 'native_proxy_fault', 'seam_native_fault'))
    check(f'{role}: no block or fault', bad == 0)
print('RESULT', 'PASS' if all(ok for _, ok in results) else 'FAIL', sum(ok for _, ok in results), '/', len(results))
for pid in (client, host):
    h = act.hwnd_of(pid)
    if h: act.u.PostMessageW(h, 0x10, 0, 0); time.sleep(5)
print('screens in', out)
