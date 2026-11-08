"""P6 revision 2: reuse receipts and synthetic specification counterexamples.

No game, Ghidra, networking or native calls. Does not rerun accepted research.
Writes only docs/evidence/m1/P6_ADR_REWORK/{REUSE_CHECKS,CONTRACT_EXAMPLES}.json.
Examples are INFERRED arithmetic/serialization consequences, not KEX tests.
Run from any directory: python -X utf8 -I docs/research/p6_adr_rework_checks.py
"""
import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/evidence/m1/P6_ADR_REWORK'
TARGET = 'b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


def write(name, payload):
    (OUT / name).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


class Cases:
    def __init__(self):
        self.rows = []

    def equal(self, name, actual, expected, **witness):
        self.rows.append(dict(id=name, result='PASS' if actual == expected else 'FAIL',
                              actual=actual, expected=expected, **witness))

    def rejects(self, name, call):
        try:
            call()
        except ValueError as exc:
            self.equal(name, True, True, reason=str(exc))
        else:
            self.equal(name, False, True)

    def summary(self):
        return dict(checks=len(self.rows), passed=sum(r['result'] == 'PASS' for r in self.rows),
                    failed=[r['id'] for r in self.rows if r['result'] != 'PASS'])


def reuse():
    c = Cases()
    immutable = read_json('docs/evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/IMMUTABLE_INPUTS.json')
    for path, expected in immutable.items():
        c.equal('HASH:' + path, sha((ROOT / path).read_bytes()), expected)
    for path in ('docs/evidence/m1/P6_ADR_AUTHORITY_WORLD/MAP_SPAWN_IDENTITY_STATIC.json',
                 'docs/evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/MAP_SPAWN_IDENTITY_STATIC.json'):
        saved = read_json(path)
        c.equal('SAVED_SUMMARY:' + path, saved['summary'], dict(checks=25, **{'pass': 25, 'fail': []}))
        c.equal('SAVED_TARGET:' + path, saved['target_exe_sha256'], TARGET)
        for source, metadata in saved['inputs'].items():
            c.equal('SAVED_INPUT:' + path + ':' + source,
                    sha((ROOT / source).read_bytes()), metadata['sha256'])
            dossier = read_json(source)
            c.equal('DOSSIER_TARGET:' + source, dossier.get('binary_sha256'), TARGET)
            c.equal('DOSSIER_PROFILE:' + source,
                    {key: dossier.get(key) for key in ('ghidra_version', 'language', 'compiler_spec', 'image_base')},
                    dict(ghidra_version='12.2', language='x86:LE:64:default',
                         compiler_spec='windows', image_base='0x140000000'))
    author = read_json('docs/evidence/m1/P6_ADR_AUTHORITY_WORLD/MAP_SPAWN_IDENTITY_STATIC.json')
    exe_row = next(row for row in author['checks'] if row['id'] == 'EXE')
    for role, entry in exe_row['detail'].items():
        c.equal('CURRENT_EXE:' + role, sha(Path(entry['path']).read_bytes()), TARGET,
                path=entry['path'], access='read-only hash')
    # Exact accepted profile is stated separately in the static excerpts/coordinates.
    coords = read_json('docs/evidence/m1/P6_ACCEPTANCE_REVIEW_SOL/FUNCTION_COORDINATES.json')
    c.equal('COORDINATES_TARGET', coords['target_sha256'], TARGET)
    c.equal('IMAGE_BASE', coords['image_base'], '0x140000000')
    c.equal('STATIC_CHECKER_NOT_RERUN', author['runtime_executed'], False)
    wire_paths = sorted((ROOT / 'experiments/pse_together/src/wire111').rglob('*'))
    wire = {p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in wire_paths if p.is_file()}
    baseline_path = OUT / 'WIRE111_BASELINE.json'
    if not baseline_path.exists():
        prior = json.loads((OUT / 'REUSE_CHECKS.json').read_text(encoding='utf-8'))
        write('WIRE111_BASELINE.json', prior['wire111_receipt'])
    baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
    c.equal('WIRE111_UNCHANGED_SINCE_FIRST_REWORK_RECEIPT', wire, baseline)
    return dict(schema='p6-rework-reuse-v1', generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                runtime_executed=False, static_research_reexecuted=False,
                profile='Ghidra 12.2 / x86:LE:64:default / windows / image base 0x140000000; inherited from accepted dossiers',
                limitation='Hash/profile reuse only; no claim of new static or runtime acceptance. '
                           'Historical mutation 3/3 is not independently reproduced.',
                summary=c.summary(), checks=c.rows, wire111_receipt=wire)


def compensated(position, recorded_c, now_c):
    return position + now_c - recorded_c


def history_status(times, tau, uncertainty=0, sync_age=0, clock_jump=0, support_changed=False):
    """Illustrative scalar guards with synthetic limits, not production thresholds."""
    if uncertainty > 3 or sync_age > 50 or abs(clock_jump) > 2 or support_changed:
        return 'WAIT_ANCHOR'
    for left, right in zip(times, times[1:]):
        if left <= tau <= right:
            return 'COMPARE' if right - left <= 2 else 'WAIT_ANCHOR'
    return 'WAIT_ANCHOR'


def reset_projection(anchor, state):
    """Specification fixture: hard anchor discards post-sample local motion."""
    if anchor['token'] != state['token'] or anchor['disc'] != state['disc']:
        return dict(state, outcome='REJECT_IDENTITY')
    if state['phase'] != 'WAIT_ANCHOR':
        return dict(state, outcome='IGNORE_COMPLETED_TOKEN')
    if anchor['sequence'] <= state['applied_anchor']:
        return dict(state, outcome='IGNORE_DUPLICATE')
    if state['now'] - anchor['time'] > 5:
        return dict(state, outcome='WAIT_FRESH_ANCHOR')
    return dict(state, position=anchor['position'], velocity=anchor['velocity'], yaw=anchor['yaw'],
                queued_movement=0, mouse_delta=0, ledger=0, history=[],
                applied_anchor=anchor['sequence'], phase='ACTIVE', outcome='APPLIED')


def record_index(base, count, pointer):
    delta = pointer - base
    if delta < 0 or delta % 0x14 or delta // 0x14 >= count:
        raise ValueError('record pointer outside aligned current LoadMap array')
    return delta // 0x14


def active_binding(captured, current):
    return captured['source'] == 'map_factory' and all(
        captured[k] == current[k] for k in ('actor', 'node', 'allocation', 'activation', 'epoch'))


def manifest(total, difficulty, entries):
    rows = sorted((e for e in entries if e['type'] != 0), key=lambda e: e['index'])
    seen = set()
    for row in rows:
        if row['index'] in seen or not 0 <= row['index'] < total:
            raise ValueError('duplicate or out-of-range record index')
        if not row['provenance'] or not row['class'] or '\0' in row['class']:
            raise ValueError('unproven provenance or invalid class')
        seen.add(row['index'])
    data = b'P6-MAP-1\0' + struct.pack('<III', total, difficulty, len(rows))
    for row in rows:
        name = row['class'].encode('utf-8')
        data += struct.pack('<IiiI', row['index'], row['type'], row['sector'], len(name)) + name
    return data


def descriptor(d):
    # Inputs below are fixtures already constrained to the versioned descriptor schema.
    return json.dumps(d, sort_keys=True, ensure_ascii=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def examples():
    c = Cases()
    # Time units and coordinates here are arbitrary scalar fixture units, not KEX units/ABI.
    sync = dict(c0=100, h1=112, h2=113, c3=105)
    interval = [sync['h2'] - sync['c3'], sync['h1'] - sync['c0']]
    c.equal('F1_OFFSET_INTERVAL', interval, [8, 12], exchange=sync)
    offset = sum(interval) / 2
    c.equal('F1_OFFSET_UNCERTAINTY', [offset, (interval[1] - interval[0]) / 2], [10, 2])
    history = {10: 10, 11: 11, 12: 12, 13: 12.5}
    held = [dict(stateTime=t + offset, stateTick=20 + t - 10,
                 seq=100, position=history[t]) for t in (10, 11, 12)]
    c.equal('F1_HELD_TIME_ERRORS', [h['position'] - history[h['stateTime'] - offset] for h in held],
            [0, 0, 0], host=held, old_seq_formula_errors=[0, 1, 2])
    c.equal('F1_COALESCED_SEQ_NOT_HISTORY_KEY', history[12] - history[22 - offset], 0,
            received_sequences=[100, 101, 102], applied_sequence=102, stateTime=22)
    c.equal('F1_NEUTRAL_WITHOUT_NEW_SEQ', 12.5 - history[23 - offset], 0,
            applied_sequence=100, mode='NEUTRAL', world_or_inertia_displacement=0.5)
    c.equal('F1_INTERPOLATE_SAME_TIME', 11.5 - (history[11] + history[12]) / 2, 0,
            stateTime=21.5, tau=11.5)
    c.equal('F1_TWO_OLD_SAMPLES_FULL_CORRECTION',
            [10 - compensated(0, 0, 0), 11 - compensated(1, 0, 10)], [10, 0],
            actual_applied_first_shift=10)
    c.equal('F1_TWO_OLD_SAMPLES_PARTIAL_CORRECTION',
            [10 - compensated(0, 0, 0), 11 - compensated(1, 0, 5)], [10, 5],
            second_target='replace remaining error with 5; do not add another 10')
    c.equal('F1_NEW_RECORD_ALREADY_CONTAINS_SHIFT', compensated(11, 10, 10), 11)
    c.equal('F1_ONLY_ACTUAL_SHIFT_IN_LEDGER', 10 - compensated(0, 0, 3), 7,
            requested_shift=5, actual_shift=3)
    c.equal('F1_REBASE_BEFORE_INTERPOLATION',
            (compensated(0, 0, 10) + compensated(11, 10, 10)) / 2, 10.5)
    for name, sample_tick, watermark in [('DUPLICATE', 42, 42), ('REORDERED', 41, 42)]:
        c.equal('F1_DROP_' + name, sample_tick > watermark, False)
    c.equal('F1_TIME_BRACKET_ACCEPTED', history_status([10, 11, 12], 11.5), 'COMPARE')
    for reason, times, tau, options in [
        ('MISSING_HISTORY', [10, 11], 9, {}),
        ('PAUSE_GAP', [10, 20], 15, {}),
        ('SYNC_TOO_OLD', [10, 11], 10.5, dict(sync_age=51)),
        ('UNCERTAINTY_TOO_HIGH', [10, 11], 10.5, dict(uncertainty=4)),
        ('CLOCK_JUMP', [10, 11], 10.5, dict(clock_jump=3)),
        ('SUPPORT_CHANGED', [10, 11], 10.5, dict(support_changed=True)),
    ]:
        c.equal('F1_' + reason + '_CANNOT_SOFT_CORRECT',
                history_status(times, tau, **options), 'WAIT_ANCHOR',
                history_times=times, target_time=tau, options=options,
                limitation='fixture limits are synthetic, not runtime calibration')
    uncertainty, drift_bound, elapsed = 2, 0.01, 100
    c.equal('F1_DRIFT_EXPANDS_UNCERTAINTY', uncertainty + drift_bound * elapsed, 3)
    c.equal('F1_OLD_DISCONTINUITY_REJECTED', (4, 9) == (5, 10), False,
            fields=['discontinuitySequence', 'resetToken'])
    anchor = dict(position=100, velocity=2, yaw=90, token=10, disc=5, sequence=60, time=100)
    state = dict(position=80, velocity=1, yaw=30, token=10, disc=5, queued_movement=7,
                 mouse_delta=20, ledger=10, history=[1, 2, 3], applied_anchor=59, now=103, phase='WAIT_ANCHOR')
    reset = reset_projection(anchor, state)
    c.equal('F1_ANCHOR_DISCARDS_POST_SAMPLE_MOTION',
            [reset['position'], reset['queued_movement'], reset['ledger'], reset['history']], [100, 0, 0, []],
            before=state, anchor=anchor, forbidden_target=107)
    c.equal('F1_OLD_TOKEN_ANCHOR_REJECTED', reset_projection(dict(anchor, token=9), state)['outcome'], 'REJECT_IDENTITY')
    c.equal('F1_OLD_DISC_ANCHOR_REJECTED', reset_projection(dict(anchor, disc=4), state)['outcome'], 'REJECT_IDENTITY')
    c.equal('F1_OLD_TIME_ANCHOR_REJECTED', reset_projection(dict(anchor, time=90), state)['outcome'], 'WAIT_FRESH_ANCHOR')
    c.equal('N3_AUTHORITATIVE_YAW_THEN_NEW_INPUT', (reset['yaw'] + 5) % 360, 95,
            old_yaw=state['yaw'], discarded_pre_anchor_mouse_delta=state['mouse_delta'])
    turned = dict(reset, yaw=95)
    duplicate = reset_projection(anchor, turned)
    c.equal('N3_DUPLICATE_ANCHOR_DOES_NOT_TURN_AGAIN',
            [duplicate['outcome'], duplicate['yaw']], ['IGNORE_COMPLETED_TOKEN', 95])
    later_anchor = reset_projection(dict(anchor, sequence=61, yaw=180), turned)
    c.equal('N3_NEW_SEQUENCE_FOR_COMPLETED_TOKEN_DOES_NOT_RESET',
            [later_anchor['outcome'], later_anchor['yaw']], ['IGNORE_COMPLETED_TOKEN', 95])

    # Synthetic record-array addresses are integers for arithmetic, never native addresses.
    c.equal('F2_RECORD_POINTER_VALID', record_index(1000, 3, 1040), 2)
    c.rejects('F2_RECORD_POINTER_UNALIGNED', lambda: record_index(1000, 3, 1041))
    c.rejects('F2_RECORD_POINTER_OUTSIDE_ARRAY', lambda: record_index(1000, 3, 1060))
    frames = []
    frames.append(dict(index=0, actor='outer-fixture'))
    frames.append(dict(index=2, actor='inner-fixture'))
    inner, outer = frames.pop(), frames.pop()
    c.equal('F2_NESTED_FACTORY_FRAMES', [outer['index'], inner['index']], [0, 2])
    capture = dict(actor='actor-fixture', node='node-fixture', allocation=10,
                   activation=1, epoch=2, source='map_factory')
    c.equal('F2_MATCHING_ACTIVATION', active_binding(capture, capture), True)
    for key, replacement in [('activation', 2), ('allocation', 11), ('node', 'new-node-fixture'), ('epoch', 3)]:
        current = dict(capture, **{key: replacement})
        c.equal('F2_STALE_' + key.upper(), active_binding(capture, current), False)
    c.equal('F2_SCAN_ONLY_IS_NOT_PROVENANCE', active_binding(dict(capture, source='scan'), capture), False)
    c.equal('F2_CHILD_DOES_NOT_INHERIT_MAP_ID', active_binding(dict(capture, source='by_type_child'), capture), False)

    entries = [dict(index=2, type=1007, sector=259, **{'class': 'kexSlaveAI'}, provenance=True),
               dict(index=1, type=0, sector=0, **{'class': 'kexSlavePuppet'}, provenance=True)]
    m = manifest(3, 1, entries)
    c.equal('F3_MANIFEST_CANONICAL_ORDER', sha(m), sha(manifest(3, 1, list(reversed(entries)))))
    c.equal('F3_TYPE_ZERO_EXCLUDED', sha(m), sha(manifest(3, 1, entries[:1])))
    c.rejects('F2_DUPLICATE_MAP_INDEX', lambda: manifest(3, 1, [entries[0], entries[0]]))
    c.rejects('F2_UNKNOWN_PROVENANCE', lambda: manifest(3, 1, [dict(entries[0], provenance=False)]))
    c.equal('F3_DIFFICULTY_IN_MANIFEST', sha(m) == sha(manifest(3, 2, entries)), False)
    d = dict(schema='P6-CONTENT-1', exe_sha256=TARGET, map_name='TOMB',
             psm_sha256=sha(b'SYNTHETIC PSM fixture: origin=0 wall=10'),
             mounts=[['base.kpf', sha(b'SYNTHETIC archive: defs speed=1')],
                     ['coop.kpf', sha(b'SYNTHETIC coop archive')]],
             loose=[], gameplay_config={'profile': 'closed-p9-fixture'}, difficulty=1)
    for name, patch in [
        ('MAP_COORDINATES', dict(psm_sha256=sha(b'SYNTHETIC PSM fixture: origin=100 wall=10'))),
        ('MAP_GEOMETRY', dict(psm_sha256=sha(b'SYNTHETIC PSM fixture: origin=0 wall=50'))),
        ('DEFS', dict(mounts=[['base.kpf', sha(b'SYNTHETIC archive: defs speed=2')], d['mounts'][1]])),
        ('MOUNT_ORDER', dict(mounts=list(reversed(d['mounts'])))),
        ('LOOSE_OVERRIDE', dict(loose=[['defs/actor.def', sha(b'SYNTHETIC override')]])),
        ('DIFFICULTY', dict(difficulty=2)),
    ]:
        changed = dict(d, **patch)
        c.equal('F3_CONTENT_REJECTS_' + name, sha(descriptor(d)) == sha(descriptor(changed)), False,
                manifest_unchanged=name != 'DIFFICULTY', content_hash=sha(descriptor(changed)))
    changed_entry = [dict(entries[0], **{'class': 'otherClass'})]
    c.equal('F3_EQUAL_CONTENT_DIFFERENT_BINDING', sha(m) == sha(manifest(3, 1, changed_entry)), False,
            content_hash=sha(descriptor(d)), outcome='MANIFEST_MISMATCH')
    c.equal('F3_CONTENT_KEY_ORDER_CANONICAL', descriptor(d), descriptor(dict(reversed(list(d.items())))))
    # JSON output must be serializable; retain bytes as a readable canonical vector.
    for row in c.rows:
        for key in ('actual', 'expected'):
            if isinstance(row[key], bytes):
                row[key] = row[key].decode('utf-8')
    return dict(schema='p6-rework-contract-examples-v1', runtime_executed=False,
                label='INFERRED synthetic specification examples; no native ABI, physics or lifetime acceptance',
                summary=c.summary(), checks=c.rows,
                canonical_vectors=dict(manifest_hex=m.hex(), manifest_sha256=sha(m),
                                       content_utf8=descriptor(d).decode('utf-8'), content_sha256=sha(descriptor(d))))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    receipt, cases = reuse(), examples()
    write('REUSE_CHECKS.json', receipt)
    write('CONTRACT_EXAMPLES.json', cases)
    print(json.dumps(dict(reuse=receipt['summary'], examples=cases['summary'], runtime_executed=False)))
    return int(bool(receipt['summary']['failed'] or cases['summary']['failed']))


if __name__ == '__main__':
    sys.exit(main())
