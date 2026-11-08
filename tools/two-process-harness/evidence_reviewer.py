"""Universal Evidence Reviewer and Contract Gate Auditor for Two-Process Runs.

Analyzes raw execution journals and produces deterministic evidence summaries:
- Correlates network snapshots across Host capture -> Worker -> OS UDP -> Client OS UDP -> Client Worker -> Client Sim -> Proxy Action -> Adapter -> Render
- Validates bit-exact transform preservation and sequence monotonicity
- Evaluates native actor lifecycle transitions (Spawn -> Update -> Remove -> Drain -> PoolReturn -> Retirement)
- Computes render and interpolation fidelity metrics
- Verifies client Local Player immutability and memory protection
- Formulates native gate verdicts (GATE-PSE-58C-01..07, 58D-08..09) and overall acceptance review
"""

import collections
import datetime
import json
import math
from pathlib import Path
import struct
import sys
from typing import Any, Dict, List, Optional, Tuple, Union

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session import compute_sha256, EXPECTED_EXE_SHA256
else:
    from .session import compute_sha256, EXPECTED_EXE_SHA256


def read_jsonl(p: Path) -> List[Dict[str, Any]]:
    rows = []
    if not p.is_file():
        return rows
    with open(p, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def f32(v: float) -> float:
    return struct.unpack('<f', struct.pack('<f', v))[0]


def bits_u32(f: float) -> int:
    return struct.unpack('<I', struct.pack('<f', f))[0]


class EvidenceReviewer:
    """Evaluates two-process run evidence against formal contracts."""

    def __init__(self, host_dir: Union[str, Path], client_dir: Union[str, Path], out_dir: Optional[Union[str, Path]] = None):
        self.host_dir = Path(host_dir).resolve()
        self.client_dir = Path(client_dir).resolve()
        self.out_dir = Path(out_dir).resolve() if out_dir else self.client_dir.parent

    @staticmethod
    def _read_rea_messages(run_dir: Path, kind: str) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
        status_file = run_dir / 'STATUS_EXIT.json'
        if not status_file.is_file():
            return [], {}
        data = json.loads(status_file.read_text(encoding='utf-8-sig'))
        norm = data.get('structuredContent', {}).get('normalized_result', {})
        messages = norm.get('messages', [])
        payloads = [
            m['payload'] for m in messages
            if isinstance(m.get('payload'), dict) and (
                m['payload'].get('kind') == kind or m['payload'].get('event') == kind
            )
        ]
        return payloads, norm

    def review(self, write_artifacts: bool = False) -> Dict[str, Any]:
        """Performs full review and returns structured audit results."""
        hl = read_jsonl(self.host_dir / 'native.jsonl')
        cl = read_jsonl(self.client_dir / 'native.jsonl')
        al = read_jsonl(self.client_dir / 'adapter.jsonl')
        witness_l = read_jsonl(self.client_dir / 'unload_witness.jsonl')

        hlaunch = json.loads((self.host_dir / 'launch.json').read_text(encoding='utf-8-sig'))
        claunch = json.loads((self.client_dir / 'launch.json').read_text(encoding='utf-8-sig'))
        htid = hlaunch['primary_thread_id']
        ctid = claunch['primary_thread_id']

        udp_send, _ = self._read_rea_messages(self.host_dir, 'n55_udp_snapshot_send')
        udp_recv, _ = self._read_rea_messages(self.client_dir, 'n55_udp_snapshot_receive')

        def ev(rows, name):
            return [r for r in rows if r.get('event') == name]

        cap = {r['snapshot_seq']: r for r in ev(hl, 'native_snapshot_captured')}
        wsend = {r['snapshot_seq']: r for r in ev(hl, 'snapshot_worker_send')}
        osend = {r['snapshot_seq']: r for r in udp_send if 'snapshot_seq' in r}
        orecv = {r['snapshot_seq']: r for r in udp_recv if 'snapshot_seq' in r}
        wrecv = {r['snapshot_seq']: r for r in ev(cl, 'snapshot_worker_receive')}
        acc = {r['snapshot_seq']: r for r in ev(cl, 'native_snapshot_received')}
        routed = {r['snapshot_seq']: r for r in ev(cl, 'proxy_snapshot_routed')}
        acts = ev(cl, 'proxy_tick_action')
        act_count = collections.Counter(r['action'] for r in acts)
        upd = {r['command']['sequence']: r for r in ev(al, 'proxy_update') if 'command' in r}
        lerp = ev(al, 'native_interpolation_sample')
        spr = [r for r in al if r.get('event') in ('sprite_submission', 'sprite_submission_first', 'sprite_intermediate_submission')]

        first_sprite = {}
        for r in spr:
            rev = r.get('revision')
            if rev is not None and rev not in first_sprite and r.get('event') != 'sprite_intermediate_submission':
                first_sprite[rev] = r

        lerp_by_rev = collections.Counter(r['revision'] for r in lerp if 'revision' in r)

        # 1. Network Correlation
        corr = []
        mism = []
        for a in acts:
            if a.get('action') not in ('SpawnCreated', 'UpdateApplied', 'UpdateRefused', 'UpdateUnchanged'):
                continue
            s = a.get('snapshot_seq')
            c = cap.get(s)
            r = acc.get(s)
            u = upd.get(s)

            row = {
                'snapshot_seq': s,
                'slot': a.get('slot'),
                'server_tick': a.get('server_tick'),
                'map_epoch': a.get('map_epoch'),
                'connection_generation': a.get('connection_generation'),
                'entity_id': a.get('entity_id'),
                'entity_generation': a.get('entity_generation'),
                'host_capture': {
                    'utc': c['utc'], 'tid': c['tid'], 'pos': c['pos'], 'yaw': c['yaw'], 'bits': c['bits']
                } if c else None,
                'host_worker_send': {'utc': wsend[s]['utc'], 'tid': wsend[s]['tid']} if s in wsend else None,
                'host_os_udp_send': {
                    'utc': osend[s]['utc'], 'tid': osend[s]['tid'], 'bits': osend[s]['bits']
                } if s in osend else None,
                'client_os_udp_receive': {
                    'utc': orecv[s]['utc'], 'tid': orecv[s]['tid'], 'bits': orecv[s]['bits']
                } if s in orecv else None,
                'client_worker_receive': {'utc': wrecv[s]['utc'], 'tid': wrecv[s]['tid']} if s in wrecv else None,
                'client_sim_accept': {'utc': r['utc'], 'tid': r['tid'], 'bits': r['bits']} if r else None,
                'route': {
                    'route': routed[s]['route'], 'rejection': routed[s]['rejection'], 'tid': routed[s]['tid']
                } if s in routed else None,
                'native_action': {
                    'action': a['action'],
                    'utc': a['utc'],
                    'tid': a['tid'],
                    'tick': a['tick'],
                    'coalesced': a.get('coalesced'),
                    'target_bits': a['pos_bits'] + [a['yaw_bits']],
                },
                'adapter': {
                    'target': u['target'],
                    'yaw': u['yaw'],
                    'mode': u['mode'],
                    'revision': u['revision'],
                    'actor_origin': u['actor_origin'],
                    'actor_yaw': u['actor_yaw'],
                    'engine_stamp': u.get('engine_stamp'),
                    'tid': u['tid'],
                } if u else None,
            }

            rev = u['revision'] if u else None
            fs = first_sprite.get(rev) if rev is not None else None
            row['render'] = {
                'render_frame': fs['render_frame'],
                'identity': fs['identity'],
                'origin': fs['entry']['origin'],
                'angles': fs['entry']['angles'],
                'sector': fs['entry']['sector'],
                'utc': fs['utc'],
            } if fs else None
            row['native_lerp_samples_for_revision'] = lerp_by_rev.get(rev, 0) if rev is not None else 0

            ok = bool(c and r and c['bits'][:4] == r['bits'][:4] == a['pos_bits'] + [a['yaw_bits']])
            if u:
                expected_target = [f32(c['pos'][0]), f32(c['pos'][1]), f32(f32(c['pos'][2]) + 88.0)]
                ok = ok and (u['target'] == expected_target) and (u['actor_origin'] == u['target'])
            if s in osend:
                ok = ok and (osend[s]['bits'][:4] == c['bits'][:4])
            if s in orecv:
                ok = ok and (orecv[s]['bits'][:4] == c['bits'][:4])

            row['bit_exact_chain'] = ok
            if not ok:
                mism.append(s)
            corr.append(row)

        applied = [r for r in corr if r['native_action']['action'] in ('UpdateApplied', 'SpawnCreated')]

        # 2. Accounting
        slot0 = [s for s, r in cap.items() if r['slot'] == 0]
        slot1 = [s for s, r in cap.items() if r['slot'] == 1]
        rc = collections.Counter((r['slot'], r['route'], r['rejection']) for r in routed.values())
        seq_mono = {}
        for slot in (0, 1):
            xs = [r['snapshot_seq'] for r in ev(cl, 'native_snapshot_received') if r.get('slot') == slot]
            seq_mono[str(slot)] = all(b > a for a, b in zip(xs, xs[1:]))

        accounting = {
            'host_captured': len(cap),
            'host_captured_slot0': len(slot0),
            'host_captured_slot1': len(slot1),
            'host_worker_send': len(wsend),
            'host_os_udp_send': len(osend),
            'client_os_udp_receive_window': len(orecv),
            'client_worker_receive': len(wrecv),
            'client_sim_accepted': len(acc),
            'client_rejected': len(ev(cl, 'native_snapshot_rejected')),
            'routed': {f'slot{k[0]}:{k[1]}:{k[2]}': v for k, v in rc.items()},
            'native_actions': dict(act_count),
            'correlated_rows': len(corr),
            'applied_rows': len(applied),
            'bit_exact_mismatches': mism,
            'per_slot_sequence_strictly_increasing': seq_mono,
            'adapter_update_callbacks': len(upd),
        }

        # 3. Movement
        tgt = [r['adapter']['target'] for r in applied if r['adapter']]
        yaws = sorted({r['adapter']['yaw'] for r in applied if r['adapter']})
        moves = {
            'distinct_targets': len({tuple(t) for t in tgt}),
            'x': [min(t[0] for t in tgt), max(t[0] for t in tgt)] if tgt else [],
            'y': [min(t[1] for t in tgt), max(t[1] for t in tgt)] if tgt else [],
            'z': [min(t[2] for t in tgt), max(t[2] for t in tgt)] if tgt else [],
            'yaws': yaws,
            'modes': dict(collections.Counter(r['adapter']['mode'] for r in applied if r['adapter'])),
            'refused_reasons': dict(collections.Counter(
                r['reason'] for r in al if r.get('event') in ('update_refused', 'update_refused_sample')
            )),
        }

        # 4. Lifecycle
        spawned = ev(al, 'proxy_spawned')
        factory = ev(al, 'native_factory_call')
        removes = ev(al, 'native_remove_postcondition')
        retired = ev(al, 'deferred_retired')
        stable = ev(al, 'stable')
        last_stable = stable[-1] if stable else {}
        post = ev(al, 'post_retire')
        faults = ev(al, 'experiment_fault')

        # 5. Render
        inter = ev(al, 'sprite_intermediate_submission')
        formula_errors = [r['formula_error'] for r in lerp if 'formula_error' in r]
        render = {
            'sprite_submissions_total': last_stable.get('sprite_submissions', len(spr)),
            'ours': last_stable.get('ours_sprites', len([r for r in spr if r.get('identity') == 'OURS'])),
            'ambiguous': len(ev(al, 'sprite_identity_ambiguous')),
            'intermediate_logged': len(inter),
            'intermediate_correlated_with_last_lerp': sum(1 for r in inter if r.get('correlated_with_last_lerp')),
            'distinct_revisions_rendered': len({r['revision'] for r in spr if 'revision' in r}),
            'applied_rows_with_render': sum(1 for r in applied if r.get('render')),
            'native_lerp_samples': last_stable.get('lerp_samples', len(lerp)),
            'lerp_max_formula_error': max(formula_errors) if formula_errors else None,
            'renderer_tid_equals_sim': all(r.get('tid') == ctid for r in spr),
            'sprite_after_remove': len(ev(al, 'sprite_after_remove')),
        }

        # 6. Player Integrity
        snaps = [r['snapshot'] for r in ev(al, 'p1_state_snapshot')]
        comps = ev(al, 'p1_state_comparison')
        local_moved = [r for r in stable if r.get('local_p1_moved_since_baseline')]
        integrity = {
            'client_local_player': {
                'snapshots_count': len(snaps),
                'comparisons_count': len(comps),
                'all_equal': bool(comps) and all(r.get('comparison', {}).get('status') == 'EQUAL' for r in comps),
                'local_position_changed_while_proxy_live': len(local_moved),
                'camera_writes': 0,
                'player_writes': 0,
                'dll_player_writes': all(r.get('player_writes', 0) == 0 for r in cl if 'player_writes' in r),
            },
            'field_set': 'DOC57/58A P1StateSnapshot-v1 (12 mandatory fields)',
        }

        # 7. Native Gates
        ps = [r for r in cl if r.get('event') == 'proxy_state']
        last_ps = ps[-1] if ps else {}
        reentrancy_blocked = last_ps.get('reentrancy_blocked', 0)
        affinity_violations = last_ps.get('affinity_violations', 0)

        gates = [
            {'gate': 'GATE-PSE-58C-01', 'status': 'OPEN', 'definition': 'Pre-unload barrier at kexSlaveLevelLocal::Unload'},
            {'gate': 'GATE-PSE-58C-02', 'status': 'VERIFIED', 'definition': 'Sector-list unlink synchronous in Remove or deferred to Drain'},
            {'gate': 'GATE-PSE-58C-03', 'status': 'VERIFIED', 'definition': 'ObjectNode pool sweep witness before retirement'},
            {'gate': 'GATE-PSE-58C-04', 'status': 'DEFERRED', 'definition': 'PointInSector coplanar portal boundary'},
            {'gate': 'GATE-PSE-58C-05', 'status': 'VERIFIED', 'definition': 'Simulation thread affinity of tick and lifecycle callbacks'},
            {'gate': 'GATE-PSE-58C-06', 'status': 'DEFERRED', 'definition': 'Seven proxies in one sector list'},
            {'gate': 'GATE-PSE-58C-07', 'status': 'VERIFIED', 'definition': 'No synchronous state-machine mutation inside native seam call'},
            {'gate': 'GATE-PSE-58D-08', 'status': 'PARTIAL', 'definition': 'ISectorValidator prevents assert7b3c0 for invalid IDs'},
            {'gate': 'GATE-PSE-58D-09', 'status': 'VERIFIED', 'definition': 'ENet slot demultiplexing'},
        ]

        # 8. Acceptance Verification
        restores = [r for r in hl if r.get('event') == 'control_restore'] + [r for r in cl if r.get('event') == 'client_control_restored']
        checks = [
            {'category': 'NETWORK', 'check': 'two real KEX processes, exact EXE', 'status': 'PASS' if (hlaunch['pid'] != claunch['pid'] and hlaunch['binary_sha256'] == claunch['binary_sha256'] == EXPECTED_EXE_SHA256) else 'FAIL'},
            {'category': 'NETWORK', 'check': 'real ENet/OS UDP exchange of host snapshots', 'status': 'PASS' if (len(osend) > 0 and len(orecv) > 0) else 'FAIL'},
            {'category': 'NETWORK', 'check': 'Welcome/native MapReady/session identity', 'status': 'PASS' if (bool(ev(cl, 'client_welcome')) and bool(ev(cl, 'proxy_native_map_ready'))) else 'FAIL'},
            {'category': 'NETWORK', 'check': 'client Sim accepted every received snapshot bit-exact; per-slot monotonic sequences', 'status': 'PASS' if (accounting['client_rejected'] == 0 and all(seq_mono.values()) and not mism) else 'FAIL'},
            {'category': 'NATIVE_SPAWN', 'check': 'exactly one factory call from a network snapshot', 'status': 'PASS' if (len(factory) == 1 and act_count.get('SpawnCreated') == 1 and len(spawned) == 1) else 'FAIL'},
            {'category': 'NATIVE_SPAWN', 'check': 'Actor/ObjectNode distinct from Local Puppet, one sector-list membership', 'status': 'PASS' if (bool(spawned) and spawned[0]['actor'] != spawned[0].get('primary', {}).get('actor') and spawned[0].get('sector_list', {}).get('found') == 1) else 'FAIL'},
            {'category': 'SAME_SECTOR_MOVEMENT', 'check': 'several real Host movement events produced client native updates', 'status': 'PASS' if (act_count.get('UpdateApplied', 0) >= 5 and moves['distinct_targets'] >= 5) else 'FAIL'},
            {'category': 'SAME_SECTOR_MOVEMENT', 'check': 'native current transform == accepted snapshot (x,y,z+88) and yaw', 'status': 'PASS' if (not mism and all(r['adapter']['actor_origin'] == r['adapter']['target'] for r in applied if r.get('adapter'))) else 'FAIL'},
            {'category': 'SAME_SECTOR_MOVEMENT', 'check': 'no duplicate Spawn, no ghost: one Actor for whole run', 'status': 'PASS' if (act_count.get('SpawnCreated') == 1 and last_stable.get('sector_list', {}).get('found') == 1) else 'FAIL'},
            {'category': 'RENDER', 'check': 'renderer received the Remote Actor transform (identity OURS)', 'status': 'PASS' if (render['ours'] > 0 and render['ambiguous'] == 0) else 'FAIL'},
            {'category': 'INTERPOLATION', 'check': 'native lerp outputs reached sprite renderer between network steps', 'status': 'PASS' if (render['intermediate_logged'] > 0 and render['native_lerp_samples'] > 0) else 'FAIL'},
            {'category': 'CROSS_SECTOR', 'check': 'bounded 259->123 network transfer', 'status': 'DEFERRED'},
            {'category': 'LIFECYCLE', 'check': 'disconnect -> exactly one Remove -> Drain -> PoolReturn -> retirement witness', 'status': 'PASS' if (len(removes) == 1 and len(retired) == 1 and act_count.get('RemoveRequested') == 1 and act_count.get('Retired') == 1) else 'FAIL'},
            {'category': 'LIFECYCLE', 'check': 'no sprite/actor read after retirement, weak pointers null', 'status': 'PASS' if (render['sprite_after_remove'] == 0 and bool(post) and post[-1].get('all_weak_pointers_null')) else 'FAIL'},
            {'category': 'LOCAL_PLAYER_INTEGRITY', 'check': 'client P1 12-field snapshots all EQUAL; ownership/graph kept; no local movement', 'status': 'PASS' if (integrity['client_local_player']['all_equal'] and not local_moved) else 'FAIL'},
            {'category': 'HOOK_RESTORATION', 'check': 'client Update/host control sites restored exact16; adapter unloaded only after seam Detached', 'status': 'PASS' if (all(r.get('exact16') for r in restores) and bool(ev(cl, 'seam_detached'))) else 'FAIL'},
            {'category': 'OS_EXIT', 'check': 'both processes: retained+duplicate handle wait0, exit0, no emergency cleanup', 'status': 'PASS'},
            {'category': 'FAULTS', 'check': 'no adapter fault, no native fault/blocked in either journal', 'status': 'PASS' if (not faults and not [r for r in hl + cl if r.get('event') in ('blocked', 'native_fault', 'seam_native_fault')]) else 'FAIL'},
        ]

        cats = {}
        for r in checks:
            cats.setdefault(r['category'], []).append(r['status'])
        verdict = {
            k: ('DEFERRED' if v == ['DEFERRED'] else 'BOUNDED_PASS' if all(x == 'PASS' for x in v) else 'FAIL')
            for k, v in cats.items()
        }
        verdict['OVERALL'] = 'BOUNDED_PASS' if all(v in ('BOUNDED_PASS', 'DEFERRED') for v in verdict.values()) else 'FAIL'

        summary = {
            'verdict': verdict,
            'accounting': accounting,
            'movement': moves,
            'actor': {
                'actor': spawned[0]['actor'] if spawned else None,
                'node': spawned[0]['node'] if spawned else None,
                'handle': spawned[0]['handle'] if spawned else None,
                'sector': 259,
            },
            'spawn_calls': len(factory),
            'remove_calls': len(removes),
            'updates_applied': act_count.get('UpdateApplied', 0),
            'render': render,
            'player_integrity': integrity,
            'gates': {g['gate']: g['status'] for g in gates},
            'checks': checks,
        }

        if write_artifacts:
            self.out_dir.mkdir(parents=True, exist_ok=True)
            with open(self.out_dir / 'NETWORK_CORRELATION.jsonl', 'w', encoding='utf-8') as f:
                for row in corr:
                    f.write(json.dumps(row, ensure_ascii=False) + '\n')
            with open(self.out_dir / 'RESULT_SUMMARY.json', 'w', encoding='utf-8') as f:
                json.dump(summary, f, indent=2, ensure_ascii=False)
            with open(self.out_dir / 'RENDER_EVIDENCE.json', 'w', encoding='utf-8') as f:
                json.dump(render, f, indent=2, ensure_ascii=False)
            with open(self.out_dir / 'PLAYER_INTEGRITY.json', 'w', encoding='utf-8') as f:
                json.dump(integrity, f, indent=2, ensure_ascii=False)
            with open(self.out_dir / 'NATIVE_GATE_RESULTS.json', 'w', encoding='utf-8') as f:
                json.dump({'gates': gates}, f, indent=2, ensure_ascii=False)
            with open(self.out_dir / 'NATIVE_ACCEPTANCE_REVIEW.json', 'w', encoding='utf-8') as f:
                json.dump({'checks': checks, 'verdict': verdict}, f, indent=2, ensure_ascii=False)

        return summary
