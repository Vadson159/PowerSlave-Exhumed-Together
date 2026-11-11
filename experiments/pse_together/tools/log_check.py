"""Check the newest host/client journals of a test install: python -X utf8 log_check.py <root> [--since HH:MM:SS] [--all]

Reports per journal: problem events (blocked, native_fault, native_proxy_fault, seam_native_fault, journal_lines_dropped, ...) with their
reasons, and tick stalls: consecutive records that carry a simulation `tick` where the wall time grew more than Δtick/60 + 0.1 s while the
game was ticking (0 < Δtick <= 600). A window that is not focused on a one-PC smoke does not tick at all (Δtick == 0 there: not a stall).
"""
import glob, json, os, sys, time
from datetime import datetime
from pathlib import Path

PROBLEMS = ('blocked', 'native_fault', 'native_proxy_fault', 'seam_native_fault', 'journal_lines_dropped', 'host_block_kept', 'network_start_failed',
            'client_identity_reset', 'session_rejected', 'proxy_not_quiescent_at_stop')


TRANSITIONS = ('state_exit_entry', 'state_exit_return', 'network_world_left', 'world_transition', 'client_world_left', 'shutdown_entry', 'p14_hook_restore',
               'client_stop_begin', 'debug_solo_map', 'debug_change_map', 'debug_block_next_map', 'client_follow_map_change', 'client_follow_state',
               'host_follow_client_exit', 'client_native_final_exit', 'native_final_exit', 'engine_shutdown_entry', 'map_identity_bound', 'client_session',
               'debug_end_game', 'p16_respawn_committed', 'p16_client_respawn_committed', 'client_welcome', 'network_started', 'client_worker_started')


def rows(path):
    out = []
    for line in open(path, encoding='utf8', errors='replace'):
        line = line.strip()
        if line.endswith('}'):
            try: out.append(json.loads(line))
            except ValueError: pass
    return out


def stamp(r):
    if 'steady_ns' in r: return r['steady_ns'] / 1e9
    u = r.get('utc')
    if not u: return None
    return datetime.strptime(u, '%Y-%m-%dT%H:%M:%S.%fZ').timestamp()


def check(path, since=None):
    rs = rows(path)
    if since:
        rs = [r for r in rs if r.get('utc', '')[11:19] >= since]
    print('==', path, '| records', len(rs))
    bad = {}
    for r in rs:
        e = r.get('event', '')
        if e in PROBLEMS:
            key = (e, r.get('reason') or r.get('code') or r.get('count') or '')
            bad[key] = bad.get(key, 0) + 1
    for (e, why), n in sorted(bad.items(), key=lambda x: -x[1]):
        print(f'   PROBLEM {e} x{n}: {why}')
    if not bad: print('   problems: none')
    last = None; stalls = []
    # A level load, a world change or the game closing stalls the tick by design: such stalls are reported apart.
    marks = [stamp(r) for r in rs if r.get('event') in TRANSITIONS and stamp(r) is not None]
    def near_transition(s): return any(-1.0 <= s - m <= 4.0 for m in marks)
    for r in rs:
        t = r.get('tick')
        if not isinstance(t, int): continue
        s = stamp(r)
        if s is None: continue
        if last:
            dt, dtick = s - last[1], t - last[0]
            if 0 < dtick <= 600 and dt - dtick / 60.0 > 0.1:
                stalls.append((round(dt - dtick / 60.0, 3), dtick, last[2], r['event'], r.get('utc', '')[11:23], near_transition(s)))
        last = (t, s, r['event'])
    stalls.sort(reverse=True)
    play = [x for x in stalls if not x[5]]; load = [x for x in stalls if x[5]]
    print(f'   tick stalls > 0.1 s in play: {len(play)}' + ('' if not play else ' | worst: ' + '; '.join(f'{x[0]}s over {x[1]} ticks {x[2]}->{x[3]} at {x[4]}' for x in play[:6])))
    print(f'   (at world changes / loads / exit: {len(load)}' + ('' if not load else ', worst %.3fs' % load[0][0]) + ')')
    return bad, stalls


def main():
    root = Path(sys.argv[1]).resolve()
    since = sys.argv[sys.argv.index('--since') + 1] if '--since' in sys.argv else None
    every = '--all' in sys.argv
    for prof in sorted((root / 'profile').iterdir()):
        logs = sorted(glob.glob(str(prof / 'logs' / 'native_*.jsonl')), key=os.path.getmtime)
        if not logs: continue
        for path in (logs if every else logs[-1:]):
            check(path, since)


if __name__ == '__main__':
    main()
