"""Offline candidate evidence for P1; never imports a runner or touches a live process.

python -X utf8 -B experiments/pse_together/tools/review_runtime_p1.py \
  --host docs/evidence/m1/PSE_TOGETHER_P1/P1_HOST_02 \
  --client docs/evidence/m1/PSE_TOGETHER_P1/P1_CLIENT_02 \
  --bundle .../BUNDLE_RUN02_MOVEMENT.json --bundle .../BUNDLE_RUN02_EXIT.json \
  --integrity .../INTEGRITY_FINAL.json --out .../RUNTIME_CANDIDATE_02.json

Missing witnesses produce PARTIAL, contradictions produce FAIL. PASS is bounded to the
named check; no output is package acceptance. Raw REA records are selected by maximum
retained message count per session, never by the bundle's canonical record ordering.
"""
from __future__ import annotations

import argparse
import collections
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

EXPECTED_EXE = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'
FIELD_SIZES = dict(ankh_count=2, ankh_mask=2, artifacts=4, abilities=4, keys=2,
                   quest_items=2, team_dolls=4, current_weapon=4, pending_weapon=4,
                   owned_weapons=8, ammo=16, health=2)
HOOKS = {'puppet_tick', 'actor_tick', 'on_remove', 'pool_return', 'sweep', 'drain'}
KEEP = {'native_snapshot_captured', 'snapshot_worker_send', 'snapshot_worker_receive',
        'native_snapshot_received', 'native_snapshot_rejected', 'proxy_snapshot_routed',
        'proxy_tick_action', 'proxy_state', 'client_welcome', 'proxy_native_map_ready',
        'native_factory_call', 'factory_return', 'proxy_spawned', 'proxy_update',
        'native_remove_request', 'native_remove_postcondition', 'deferred_drain_entry',
        'deferred_drain_return', 'native_on_remove_entry', 'native_on_remove_return',
        'native_pool_return_entry', 'deferred_retired', 'native_proxy_stable', 'post_retire',
        'native_proxy_fault', 'native_proxy_controller_stopped', 'seam_native_fault',
        'native_fault', 'blocked', 'native_final_exit', 'client_native_final_exit',
        'native_proxy_hook_restored', 'client_control_restored', 'control_restore',
        'native_proxy_installed', 'seam_registered', 'p1_state_snapshot', 'p1_baseline',
        'world_exit_observed', 'client_worker_joined', 'attested_area', 'seam_detached',
        'seam_world_exit', 'proxy_disconnect', 'proxy_route_counts'}
OBS_KEEP = {'observer_loaded', 'observer_summary', 'observer_error', 'observation_refused',
            'p1_state_snapshot', 'p1_state_comparison', 'sprite_submission',
            'sprite_intermediate_submission', 'sprite_candidate_mismatch',
            'native_interpolation_sample', 'native_observation_phase', 'sprite_identity_unresolved'}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()


def bit(v):
    try: return struct.unpack('<I', struct.pack('<f', v))[0]
    except (ValueError, TypeError, OverflowError, struct.error): return None


def value(v):
    try: return struct.unpack('<f', struct.pack('<I', v))[0]
    except (ValueError, TypeError, OverflowError, struct.error): return None


def yaw(v):
    try: return bit(math.fmod(math.fmod(v + math.pi, 2 * math.pi) + 2 * math.pi, 2 * math.pi) - math.pi)
    except (ValueError, TypeError): return None


def ptr(v):
    try: return int(v, 16) if isinstance(v, str) else int(v)
    except (ValueError, TypeError): return None


def key(r): return r.get('slot'), r.get('snapshot_seq', r.get('sequence'))
def rows(j, e): return j.get(e, [])
def short(r): return {k: r[k] for k in ('_file', '_line', 'event', 'utc', 'tid', 'reason') if k in r}
def norm(p): return str(p).replace('\\', '/').casefold().rstrip('/')
def ref(r): return {k: r.get(k) for k in ('_file', '_line', 'utc', 'record', 'seq') if k in r}


class Audit:
    def __init__(self):
        self.checks, self.inputs, self.issues = [], {}, []

    def source(self, p):
        p = Path(p)
        if p.is_file(): self.inputs[str(p.resolve())] = dict(path=str(p.resolve()), size=p.stat().st_size, sha256=sha(p))

    def load(self, p, required=True):
        p = Path(p)
        if not p.is_file():
            if required: self.issues.append(dict(status='PARTIAL', file=str(p), error='missing file'))
            return None
        self.source(p)
        try: return json.loads(p.read_text(encoding='utf-8-sig'))
        except (OSError, UnicodeError, json.JSONDecodeError) as e:
            self.issues.append(dict(status='FAIL', file=str(p), error=str(e))); return None

    def journal(self, p, keep, required=True):
        p = Path(p); out = collections.defaultdict(list)
        if not p.is_file():
            if required: self.issues.append(dict(status='PARTIAL', file=str(p), error='missing journal'))
            return out
        self.source(p)
        try:
            with p.open(encoding='utf-8-sig') as f:
                for line, text in enumerate(f, 1):
                    if not text.strip(): continue
                    try:
                        r = json.loads(text)
                        if not isinstance(r, dict): raise ValueError('journal row is not an object')
                        if r.get('event') in keep:
                            r['_file'], r['_line'] = str(p), line
                            out[r['event']].append(r)
                    except (json.JSONDecodeError, ValueError) as e:
                        self.issues.append(dict(status='FAIL', file=str(p), line=line, error=str(e)))
        except (OSError, UnicodeError) as e: self.issues.append(dict(status='FAIL', file=str(p), error=str(e)))
        return out

    def check(self, name, present, ok, scope, **details):
        status = 'PARTIAL' if not present else 'PASS' if ok else 'FAIL'
        self.checks.append(dict(check=name, status=status, scope=scope, **details))


def rea(a, paths, launches, sessions):
    records = {}
    for p in paths:
        d = a.load(p)
        if not isinstance(d, dict): continue
        for i, r in enumerate(d.get('records', [])):
            if not isinstance(r, dict): continue
            n = r.get('normalized_result', {})
            if not isinstance(n, dict): continue
            identity = r.get('evidence_id') or (str(p), i)
            records[identity] = (r, str(p))
    selected, loads, summaries = {}, collections.defaultdict(list), []
    for role in ('host', 'client'):
        pid = launches[role].get('pid'); explicit = sessions.get(role)
        candidates = collections.defaultdict(list)
        for r, path in records.values():
            n = r.get('normalized_result', {}); sid = r.get('parameters', {}).get('session_id', n.get('session_id'))
            if r.get('operation') == 'frida_session_status' and (n.get('pid') == pid or explicit and sid == explicit):
                candidates[sid].append((r, path))
        for sid, rs in candidates.items():
            r, path = max(rs, key=lambda v: len(v[0].get('normalized_result', {}).get('messages', [])))
            n = r['normalized_result']; selected[sid] = role
            summaries.append(dict(role=role, session=sid, evidence_id=r.get('evidence_id'), bundle=path,
                                  messages=len(n.get('messages', [])), truncated=n.get('messages_truncated'),
                                  pid=n.get('pid'), pid_matches=n.get('pid') == pid,
                                  state_of_selected_capture=n.get('state')))
            for i, m in enumerate(n.get('messages', [])):
                if isinstance(m.get('payload'), dict):
                    q = dict(m['payload']); q.update(_file=path, _line=i + 1,
                                                     _evidence_id=r.get('evidence_id'), _script_id=m.get('script_id'))
                    loads[role].append(q)
        a.check(role + '_rea_complete_capture', bool(candidates),
                all(s['truncated'] is False and s['pid_matches'] for s in summaries if s['role'] == role),
                'largest retained full status per matching game session',
                captures=[s for s in summaries if s['role'] == role])
    source_rows = []
    for r, path in records.values():
        if r.get('operation') != 'load_frida_script': continue
        sid = r.get('parameters', {}).get('session_id')
        if sid not in selected: continue
        n = r['normalized_result']
        source_rows.append(dict(role=selected[sid], session=sid, bundle=path,
                                evidence_id=r.get('evidence_id'), **{k: n.get(k) for k in
                                ('script_id', 'source_kind', 'source_path', 'source_sha256', 'messages_truncated')}))
    return loads, source_rows, summaries


def numeric_equal(a, b):
    return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and [bit(x) for x in a] == [bit(x) for x in b]


def index(rs):
    out = collections.defaultdict(list)
    for r in rs: out[key(r)].append(r)
    return out


def native_mapping(a, c):
    accepted = index(rows(c, 'native_snapshot_received')); routed = index(rows(c, 'proxy_snapshot_routed'))
    mismatches, checked, missing = [], 0, []
    for r in rows(c, 'proxy_update'):
        ks = (0, r.get('sequence')); src = accepted.get(ks, [])
        if not src: missing.append(ref(r)); continue
        s = src[-1]; bs = s.get('bits', [])
        if len(bs) != 5: missing.append(ref(s)); continue
        vals = [value(x) for x in bs[:4]]
        if any(x is None or not math.isfinite(x) for x in vals):
            mismatches.append(dict(reason='invalid source bits', source=ref(s))); continue
        target = [bs[0], bs[1], bit(vals[2] + 88.0)]
        good = [bit(x) for x in r.get('actor_origin', [])] == target and bit(r.get('actor_yaw')) == yaw(vals[3])
        good = good and [bit(x) for x in r.get('target', [])] == target and r.get('server_tick') == s.get('server_tick')
        checked += 1
        if not good: mismatches.append(dict(update=ref(r), source=ref(s), expected_target_bits=target,
                                             actual_origin_bits=[bit(x) for x in r.get('actor_origin', [])],
                                             expected_yaw_bits=yaw(vals[3]), actual_yaw_bits=bit(r.get('actor_yaw'))))
    # Factory yaw is deliberately zero; the first successful update applies the network yaw.
    spawn_missing, spawn_bad = [], []
    for r in rows(c, 'proxy_spawned'):
        src = accepted.get((0, r.get('sequence')), [])
        if not src: spawn_missing.append(ref(r)); continue
        b = src[-1].get('bits', [])
        if len(b) != 5: spawn_missing.append(ref(src[-1])); continue
        if [bit(x) for x in r.get('target', [])] != b[:2] + [bit(value(b[2]) + 88.0)]: spawn_bad.append(ref(r))
    a.check('client_snapshot_to_native_current_transform', bool(checked), not mismatches and not missing,
            'all logged Applied updates; source float bits, z+88 rounded once, literal JS yaw normalization',
            checked_updates=checked, missing_source=len(missing), mismatches=len(mismatches),
            mismatch_samples=mismatches[:12], missing_samples=missing[:12])
    a.check('spawn_target_from_snapshot', bool(rows(c, 'proxy_spawned')),
            not spawn_missing and not spawn_bad, 'Spawn uses snapshot XYZ+88; factory yaw is zero by design',
            missing=spawn_missing[:12], mismatches=spawn_bad[:12])
    routes = collections.Counter((r.get('slot'), r.get('route'), r.get('rejection')) for vs in routed.values() for r in vs)
    wrong = [short(r) for vs in routed.values() for r in vs if
             (r.get('slot') == 0 and r.get('route') not in ('Routed', 'ProxyRejected')) or
             (r.get('slot') == 1 and r.get('route') != 'LocalExcluded')]
    a.check('slot_demultiplexing', any(k[0] == 0 for k in routes) and any(k[0] == 1 for k in routes),
            not wrong and any(k[:2] == (0, 'Routed') for k in routes), 'slot0 remote; slot1 always LocalExcluded',
            counts={f'{k[0]}:{k[1]}:{k[2]}': v for k, v in routes.items()}, violations=wrong[:12])
    return accepted


def network(a, h, c, messages):
    cap = index(rows(h, 'native_snapshot_captured')); acc = index(rows(c, 'native_snapshot_received'))
    send = index([r for r in messages['host'] if r.get('kind') == 'n55_udp_snapshot_send'])
    recv = index([r for r in messages['client'] if r.get('kind') == 'n55_udp_snapshot_receive'])
    common = sorted(set(send) & set(recv), key=str); result, bad, missing = [], [], []
    for k in common:
        hs, cs = cap.get(k), acc.get(k)
        if not hs or not cs: missing.append(dict(key=k, host_capture=bool(hs), client_accept=bool(cs))); continue
        sources = [hs[-1], *send[k], *recv[k], cs[-1]]; bs = hs[-1].get('bits')
        ok = isinstance(bs, list) and len(bs) == 5 and all(r.get('bits') == bs for r in sources)
        for name in ('server_tick', 'map_epoch', 'entity_id', 'entity_generation'):
            ok = ok and all(name in r and r[name] == hs[-1].get(name) for r in sources)
        result.append(dict(slot=k[0], sequence=k[1], bits=bs, bit_exact=ok,
                           host_capture=ref(hs[-1]), host_os_send=[ref(r) for r in send[k]],
                           client_os_receive=[ref(r) for r in recv[k]], client_accept=ref(cs[-1])))
        if not ok: bad.append(result[-1])
    a.check('host_socket_client_bit_chain', bool(common), not bad and not missing,
            'intersection of the actual host-send/client-receive OS probe windows; no whole-run delivery claim',
            host_os_unique=len(send), client_os_unique=len(recv), common_keys=len(common),
            correlated_rows=len(result), mismatches=len(bad), missing_stage=len(missing),
            host_only_keys=len(set(send) - set(recv)), client_only_keys=len(set(recv) - set(send)),
            mismatch_samples=bad[:8], missing_samples=missing[:8])
    # Also check every logged OS datagram against its local endpoint's native record.
    for role, datagrams, native in [('host', send, cap), ('client', recv, acc)]:
        conflicts, absent = [], []
        for k, vs in datagrams.items():
            if k not in native: absent.append(k); continue
            if any(r.get('bits') != native[k][-1].get('bits') for r in vs): conflicts.append(k)
        a.check(role + '_os_local_native_bits', bool(datagrams), not conflicts,
                'observed OS samples with a matching native record; unmatched edge samples remain explicit',
                compared=len(datagrams) - len(absent), conflicts=conflicts[:12], unmatched=absent[:12],
                unmatched_count=len(absent))
    return result


def render(a, obs):
    sprites = rows(obs, 'sprite_submission') + rows(obs, 'sprite_intermediate_submission')
    bad, intermediates, uncorrelated = [], [], []
    for r in sprites:
        e, o = r.get('entry', {}), r.get('observation', {})
        good = e.get('origin_bits') == o.get('render_bits') and e.get('angles_bits') == o.get('angles_bits')
        good = good and e.get('frame') == o.get('frame_token') and e.get('sector') == o.get('sector')
        good = good and e.get('flags') == o.get('flags') and bit(e.get('scale')) == bit(o.get('scale'))
        good = good and len(e.get('origin_bits', [])) == 3 and len(e.get('angles_bits', [])) == 3
        if not good: bad.append(ref(r))
        if r.get('event') == 'sprite_intermediate_submission':
            intermediate = o.get('render_bits') != o.get('origin_bits') and o.get('render_bits') != o.get('previous_bits')
            lp = r.get('last_lerp') or {}
            correlated = intermediate and lp.get('output_bits') == e.get('origin_bits')
            correlated = correlated and lp.get('revision') == o.get('revision') and lp.get('sequence') == o.get('sequence')
            intermediates.append(r)
            if not correlated: uncorrelated.append(ref(r))
    a.check('real_sprite_entry_matches_native_dto', bool(sprites), not bad,
            'actual renderer entries equal DTO bits/frame/sector/flags/scale',
            logged_entries=len(sprites), mismatch_samples=bad[:12],
            identity_label='MATCHED_NATIVE_DTO',
            candidate_mismatch_records=len(rows(obs, 'sprite_candidate_mismatch')))
    census_bad=[ref(r) for r in sprites if r.get('identity')!='OURS' or not r.get('census',{}).get('complete') or
                r['census'].get('own_occurrences')!=1 or r['census'].get('other_matching_candidates')!=0]
    summaries=rows(obs,'observer_summary')
    ambiguous=max((r.get('ambiguous_sprites',0) for r in summaries),default=0)
    unknown=max((r.get('unknown_identity_sprites',0) for r in summaries),default=0)
    ours=max((r.get('ours_sprites',0) for r in summaries),default=0)
    a.check('exclusive_sprite_identity_census',bool(sprites) and bool(summaries),
            not census_bad and ambiguous==0 and unknown==0 and ours>0,
            'fresh valid Live DTO and complete bounded sector259 census excludes other same-frame same-origin candidates',
            ours_sprites=ours,ambiguous_sprites=ambiguous,unknown_identity_sprites=unknown,failures=census_bad[:12])
    a.check('intermediate_sprite_matches_own_lerp', bool(intermediates), not uncorrelated,
            'logged intermediate submissions vs own interpolant-filtered native lerp sample',
            intermediate_entries=len(intermediates), uncorrelated=len(uncorrelated), samples=uncorrelated[:12])
    lerps, lerp_bad = rows(obs, 'native_interpolation_sample'), []
    for r in lerps:
        if not isinstance(r.get('output_bits'), list) or len(r['output_bits']) != 3 or \
           r.get('identity') != 'OWN_DTO_OUTPUT_CHANGED_DURING_NATIVE_CALL' or \
           not isinstance(r.get('t'), (int, float)) or not 0 <= r['t'] <= 1 or \
           not isinstance(r.get('formula_error'), (int, float)) or not math.isfinite(r['formula_error']):
            lerp_bad.append(ref(r))
    a.check('own_native_lerp_observed', bool(lerps), not lerp_bad,
            'DTO output changed during the native call selected by equality-only interpolant filter; t is reconstructed',
            logged_samples=len(lerps), invalid_samples=lerp_bad[:12],
            max_formula_error=max((r.get('formula_error', 0) for r in lerps), default=None))


def p1(a, c, obs):
    snaps = [r for r in rows(obs, 'p1_state_snapshot') if isinstance(r.get('snapshot'), dict)]
    bad, missing, identities, raw_sets, labels = [], [], [], [], []
    for r in snaps:
        s = r['snapshot']; fs = s.get('fields', {}); label = s.get('label', '')
        labels.append(label)
        if set(fs) != set(FIELD_SIZES): missing.append(dict(snapshot=ref(r), fields=sorted(fs))); continue
        raw = []
        for name, size in FIELD_SIZES.items():
            f = fs[name]; text = f.get('raw', '')
            if f.get('status') != 'OK' or not re.fullmatch('[0-9a-fA-F]{' + str(size * 2) + '}', text): bad.append(ref(r))
            raw.append(text.casefold())
        identity = {k: ptr(v) for k, v in s.get('identity', {}).items()}
        if set(identity) != {'player', 'node', 'actor', 'own_node', 'owner', 'vtable'} or not all(identity.values()):
            missing.append(dict(snapshot=ref(r), identity=identity))
        if s.get('status') != 'OK' or s.get('errors') or s.get('binary_sha256') != EXPECTED_EXE: bad.append(ref(r))
        identities.append(identity); raw_sets.append(''.join(raw))
    native = rows(c, 'p1_state_snapshot')
    native_raw = [r.get('fields_raw', '').casefold() for r in native]
    native_ok = bool(native) and all(r.get('fields') == 12 and re.fullmatch('[0-9a-f]{108}', r.get('fields_raw', '').casefold()) for r in native)
    identity_equal = bool(identities) and all(i == identities[0] for i in identities)
    raw_equal = bool(raw_sets) and all(x == raw_sets[0] for x in raw_sets + native_raw)
    positions=[r['snapshot'].get('position') for r in snaps]+[r.get('pos') for r in native]
    positions_equal=bool(positions) and all(isinstance(x,list) and len(x)==3 and numeric_equal(x,positions[0]) for x in positions)
    cross_identity = bool(identities) and all(all(ptr(r.get(k)) == identities[0].get(k) for k in ('actor', 'node', 'owner')) for r in native)
    groups = dict(before=any(x == 'BEFORE' or x.startswith('BEFORE_') for x in labels),
                  live=any(x == 'LIVE' or x.startswith('LIVE_') for x in labels),
                  after=any((x.startswith('AFTER') and not x.startswith('AFTER_SPAWN')) or x.startswith('POST_RETIREMENT') for x in labels))
    a.check('p1_twelve_mandatory_fields_and_identity', bool(snaps) and not missing and native_ok,
            not bad and identity_equal and raw_equal and cross_identity and positions_equal,
            'all full observer snapshots plus independent C++ 54-byte mandatory-field snapshots',
            full_observer_snapshots=len(snaps), native_snapshots=len(native), labels=collections.Counter(labels),
            identities_equal=identity_equal, field_bytes_equal=raw_equal, native_identity_matches=cross_identity, positions_equal=positions_equal,
            missing=missing[:12], invalid=bad[:12])
    a.check('p1_before_live_after_coverage', all(groups.values()), True,
            'full twelve-field observer snapshots at BEFORE/LIVE/after removal', groups=groups)


def strip_js(text):
    # Remove comments and literal contents for constructor-token inspection; preserve line positions.
    token = re.compile(r'//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|`(?:\\.|[^`\\])*`')
    return token.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), text)


def loaded_sources(a, sources, client):
    inspected, violations, missing, isolation = [], [], [], []
    for r in sources:
        path = r.get('source_path'); p = Path(path) if path else None
        if p is None or not p.is_file(): missing.append(r); continue
        a.source(p); text = p.read_text(encoding='utf-8-sig'); actual = sha(p)
        callbacks = len(re.findall(r'\bnew\s+NativeCallback\s*\(', strip_js(text)))
        is_isolation = p.name == 'generated_isolation.js'
        adapter = bool(re.search(r'adapter58b|movement_native|NativeVisualProxyMovementController|OneActor57C', p.name + '\n' + strip_js(text), re.I))
        row = dict(**r, actual_sha256=actual, sha256_matches=actual == r.get('source_sha256'),
                   native_callback_constructors=callbacks, accepted_isolation_os_callback=is_isolation and callbacks > 0,
                   proxy_adapter_detected=adapter)
        inspected.append(row)
        if is_isolation and callbacks: isolation.append(row)
        if adapter or callbacks and not is_isolation or not row['sha256_matches']: violations.append(row)
    a.check('loaded_sources_no_proxy_js_control', bool(sources) and not missing, not violations,
            'all game-session loaded source files, SHA-bound to REA; accepted isolation OS callback is explicitly allowed',
            inspected=inspected, missing=missing, violations=violations,
            blanket_no_nativecallback=False if isolation else None,
            accepted_os_callback_sources=[r['source_path'] for r in isolation])
    installed = rows(client, 'native_proxy_installed'); registered = rows(client, 'seam_registered')
    a.check('internal_cpp_seam_registered', bool(installed) and bool(registered),
            all(r.get('external_callbacks') is False for r in installed + registered) and
            all(r.get('owner') == 'native_cpp' and r.get('result') == 0 for r in registered),
            'runtime journal confirms internal C++ seam; source inventory supplies the separate JS control check')


def lifecycle(a, c):
    count = {k: len(rows(c, k)) for k in ('native_factory_call', 'factory_return', 'proxy_spawned',
             'native_remove_request', 'native_remove_postcondition', 'deferred_retired')}
    a.check('one_spawn_one_remove', bool(rows(c, 'native_proxy_installed')), all(v == 1 for v in count.values()),
            'actual C++ call/return/identity journals; no FSM or exit-code substitution', counters=count)
    retired = rows(c, 'deferred_retired')
    a.check('complete_native_retirement', bool(retired), len(retired) == 1 and all(
        r.get('success') is True and r.get('payload_null') is True and r.get('refs') == 0 and
        r.get('in_use') == 0 and r.get('sector_occurrences') == 0 and r.get('all_weak_pointers_null') is True for r in retired),
        'post-PoolReturn node witness in controlled Sweep', witnesses=retired)
    pools = rows(c, 'native_pool_return_entry'); onremove = rows(c, 'native_on_remove_return')
    drain = rows(c, 'deferred_drain_return')
    a.check('deferred_drain_onremove_poolreturn_chain', bool(pools) and bool(onremove) and bool(drain),
            len(pools) == 1 and pools[0].get('valid_scope') is True and
            pools[0].get('all_actor_weak_pointers_null') is True and
            all(r.get('own_node_cleared') is True for r in onremove) and
            any(r.get('refs') == 0 and r.get('sector_occurrences') == 0 for r in drain),
            'controlled native lifecycle witnesses; no force-free or engine-retirement inference')
    samples = rows(c, 'native_proxy_stable')
    pairs = [(r.get('simulation_steps'), r.get('actor_ticks')) for r in samples]
    equality = bool(pairs) and all(x-y==1 for x,y in pairs)
    a.check('sampled_actor_ticks_vs_distinct_steps', bool(samples), equality,
            'Spawn tick counts as a live step before the first ActorTick: every logged sample must have steps=ticks+1',
            samples=len(samples), distinct_logged_engine_stamps=len({r.get('engine_stamp') for r in samples if 'engine_stamp' in r}),
            counter_deltas=dict(collections.Counter(str(x - y) for x, y in pairs if isinstance(x, int) and isinstance(y, int))),
            first_pairs=pairs[:4], last_pairs=pairs[-4:])
    # No claim of a complete per-step journal is made by this sampled counter check.


def threads_and_faults(a, h, c, obs, launches):
    tid = launches['client'].get('primary_thread_id'); htid = launches['host'].get('primary_thread_id')
    own = [r for name, rs in c.items() for r in rs if name.startswith(('proxy_', 'native_proxy_', 'seam_')) or name in {
        'native_factory_call', 'factory_return', 'native_remove_request', 'native_remove_postcondition',
        'deferred_retired', 'deferred_drain_entry', 'deferred_drain_return', 'native_on_remove_entry',
        'native_on_remove_return', 'native_pool_return_entry', 'p1_state_snapshot', 'world_exit_observed'}]
    bad = [short(r) for r in own if r.get('tid') != tid]
    a.check('own_operations_simulation_affinity', bool(own) and tid is not None, not bad,
            'actual TID of every retained own-operation journal entry', client_sim_tid=tid,
            observed_tids=sorted({r.get('tid') for r in own}, key=str), violations=bad[:12], events=len(own))
    cap = rows(h, 'native_snapshot_captured')
    a.check('host_native_capture_affinity', bool(cap) and htid is not None,
            all(r.get('tid') == htid for r in cap), 'host capture journal only; worker/socket TIDs are separate', host_sim_tid=htid)
    faults = [short(r) for j in (h, c) for name in ('blocked', 'native_fault', 'native_proxy_fault',
             'native_proxy_controller_stopped', 'seam_native_fault') for r in rows(j, name)]
    finals = rows(c, 'client_native_final_exit') + rows(h, 'native_final_exit')
    ps = rows(c, 'proxy_state')
    a.check('native_faults_and_foreign_calls', len(finals) >= 2 and bool(ps), not faults and all(
        r.get('failed') is False and r.get('foreign_calls') == 0 for r in finals) and
        all(r.get('affinity_violations') == 0 for r in ps),
        'actual fault events, both final foreign/failed counters and FSM affinity counters; stable literal0 is excluded',
        faults=faults[:20], final_events=finals, fsm_affinity=[r.get('affinity_violations') for r in ps])
    summaries = rows(obs, 'observer_summary')
    obs_errors = rows(obs, 'observer_error') + rows(obs, 'observation_refused')
    summaries_by_file = collections.defaultdict(list)
    for r in summaries: summaries_by_file[r['_file']].append(r)
    tails = [vs[-1] for vs in summaries_by_file.values()]
    a.check('observer_errors_and_foreign_callbacks', bool(tails), not obs_errors and all(
        r.get('errors') == 0 and r.get('foreign_callbacks') == 0 for r in tails),
        'last counters per saved observer journal; timer summary TID is not a simulation callback',
        observer_errors=[short(r) for r in obs_errors[:12]], final_summaries=tails)


def restores_os(a, h, c, runs, integrity):
    restored = rows(c, 'native_proxy_hook_restored')
    seen = {r.get('site') for r in restored}
    a.check('six_native_hooks_restored', HOOKS <= seen,
            all(r.get('exact16') is True and r.get('status') == 0 for r in restored),
            'all six installed native adapter hooks restored to exact16', observed_sites=sorted(seen), rows=restored)
    cr, hr = rows(c, 'client_control_restored'), rows(h, 'control_restore')
    hsites = {r.get('site') for r in hr}
    a.check('client_update_and_host_controls_restored', bool(cr) and {'Update', 'Turning', 'Commands', 'Getter'} <= hsites,
            all(r.get('exact16') is True and r.get('status', 0) == 0 for r in cr + hr),
            'client Update plus four native host control hooks', client=cr, host=hr)
    for role, run in runs.items():
        launch = a.load(run / 'launch.json') or {}; ident = a.load(run / 'OS_HANDLE_IDENTITY.json') or {}
        sig = a.load(run / 'OS_SIGNALED.json') or {}; closed = a.load(run / 'OBSERVER_CLOSED.json') or {}
        primary, duplicate = ident.get('primary', {}), ident.get('duplicate', {})
        identity_ok = ident.get('compare_same_kernel_object') is True and primary.get('pid') == duplicate.get('pid') == launch.get('pid')
        identity_ok = identity_ok and primary.get('creation_filetime') == duplicate.get('creation_filetime')
        identity_ok = identity_ok and norm(primary.get('path', '')) == norm(launch.get('target', '')) == norm(duplicate.get('path', ''))
        ok = identity_ok and sig.get('wait') == 0 and sig.get('duplicate_wait') == 0 and sig.get('exit_query_ok') is True
        ok = ok and sig.get('exit_code') == 0 and sig.get('no_emergency_cleanup') is True
        ok = ok and closed.get('close_primary') is True and closed.get('close_duplicate') is True
        a.check(role + '_retained_duplicate_exit', bool(ident) and bool(sig) and bool(closed), ok,
                'retained + duplicate kernel handles, exact PID/path/creation identity, waits0/exit0/closed handles', signal=sig, identity=ident)
        before = a.load(run / 'protected_before.json'); after = a.load(run / 'protected_after.json')
        a.check(role + '_protected_profiles_unchanged', before is not None and after is not None,
                before == after, 'exact saved protected-profile before/after manifests')
    baseline = a.load(runs['client'].parent / 'BASELINE.json', required=False) or {}
    if integrity:
        d = a.load(integrity) or {}
        a.check('original_77_files_integrity', bool(d) and bool(baseline.get('game')),
                d.get('game_equals_baseline') is True and baseline['game'].get('files') == 77 and
                baseline['game'].get('original_full_snapshot_equal') is True,
                'accepted saved integrity audit of original installation; overall workspace audit status is reported separately',
                original_files=baseline.get('game', {}).get('files'), whole_workspace_audit_status=d.get('status'),
                whole_workspace_unexpected=d.get('unexpected'), added_outside_task_count=len(d.get('added_outside_s03', [])))
    else: a.check('original_77_files_integrity', False, False, 'pass --integrity with the completed saved integrity audit')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', type=Path, required=True); ap.add_argument('--client', type=Path, required=True)
    ap.add_argument('--bundle', type=Path, action='append', required=True)
    ap.add_argument('--host-session'); ap.add_argument('--client-session'); ap.add_argument('--integrity', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args(argv); out = args.out.resolve(); corr_out = out.with_name(out.stem + '_CORRELATION.jsonl')
    if out.exists() or corr_out.exists(): ap.error('Output already exists; choose a new --out path')
    a = Audit(); runs = dict(host=args.host.resolve(), client=args.client.resolve())
    launches = {role: a.load(p / 'launch.json') or {} for role, p in runs.items()}
    h = a.journal(runs['host'] / 'native.jsonl', KEEP); c = a.journal(runs['client'] / 'native.jsonl', KEEP)
    obs = collections.defaultdict(list)
    for p in sorted(runs['client'].glob('observer_*.jsonl')):
        for k, vs in a.journal(p, OBS_KEEP).items(): obs[k].extend(vs)
    a.check('two_exact_build_processes', all(launches.values()),
            launches['host'].get('pid') != launches['client'].get('pid') and
            all(d.get('binary_sha256') == EXPECTED_EXE for d in launches.values()),
            'the two explicitly named run directories', processes=launches)
    for role, run in runs.items():
        artifact = a.load(run / 'NATIVE_ARTIFACT.json') or {}; cfg = artifact.get('config', {})
        dll = Path(cfg['dll']) if cfg.get('dll') else None
        actual = sha(dll) if dll and dll.is_file() else None
        if dll and dll.is_file(): a.source(dll)
        a.check(role + '_frozen_dll_copy', bool(cfg) and actual is not None,
                actual == cfg.get('dll_sha256'), 'run-specific copied DLL matches the saved artifact manifest',
                path=str(dll) if dll else None, actual_sha256=actual, declared_sha256=cfg.get('dll_sha256'))
    messages, sources, captures = rea(a, args.bundle, launches, dict(host=args.host_session, client=args.client_session))
    native_mapping(a, c); correlation = network(a, h, c, messages)
    lifecycle(a, c); render(a, obs); p1(a, c, obs)
    threads_and_faults(a, h, c, obs, launches); loaded_sources(a, sources, c)
    restores_os(a, h, c, runs, args.integrity)
    if a.issues:
        a.check('input_completeness', not any(x['status'] == 'PARTIAL' for x in a.issues),
                not any(x['status'] == 'FAIL' for x in a.issues), 'parsing and required saved artifacts', issues=a.issues)
        if any(x['status'] == 'FAIL' for x in a.issues):
            a.checks[-1]['status'] = 'FAIL'
    counts = collections.Counter(x['status'] for x in a.checks)
    status = 'FAIL' if counts['FAIL'] else 'PARTIAL' if counts['PARTIAL'] else 'BOUNDED_PASS_CANDIDATE'
    report = dict(schema='P1-runtime-candidate-v1', utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                  status=status, package_status='REVIEW', acceptance_authority='NOT_PACKAGE_ACCEPTANCE',
                  runs={k: str(v) for k, v in runs.items()}, checks=a.checks, check_counts=dict(counts),
                  input_manifest=list(a.inputs.values()), selected_rea_captures=captures,
                  correlation_file=str(corr_out), correlation_rows=len(correlation),
                  limitations=['Single Actor1007, TOMB sector259, z+88; no reconnect/map-transition/anti-ABA acceptance.',
                               'Sprite identity is bounded to a fresh Live DTO and a complete current sector259 census.',
                               'Sampled live counters are not a complete per-step ActorTick witness.',
                               'OS probe correlation is bounded to the explicitly reported common capture window.',
                               'Accepted isolation NativeCallback for OS path redirection is separate from forbidden proxy control callbacks.',
                               'Closed game handles do not by themselves prove every REA session has been closed.'])
    out.parent.mkdir(parents=True, exist_ok=True)
    with corr_out.open('x', encoding='utf8') as f:
        for row in correlation: f.write(json.dumps(row, ensure_ascii=False) + '\n')
    with out.open('x', encoding='utf8') as f: json.dump(report, f, ensure_ascii=False, indent=2)
    print(json.dumps(dict(status=status, checks=dict(counts), output=str(out), correlation=str(corr_out)), ensure_ascii=False))
    return 1 if status == 'FAIL' else 2 if status == 'PARTIAL' else 0


if __name__ == '__main__': sys.exit(main())
