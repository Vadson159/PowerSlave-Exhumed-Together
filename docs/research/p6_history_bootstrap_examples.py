"""P6 ADR revision 3: synthetic composition checks for A1, not KEX implementation.

No game/network/native calls. Immutable historical evidence is read only.
Run: python -X utf8 -I docs/research/p6_history_bootstrap_examples.py
"""
import hashlib
import json
import math
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/evidence/m1/P6_ADR_REV3'


class HistoryFixture:
    """Scalar specification fixture, not native prediction or physical simulation."""
    def __init__(self, capacity=64, warmup=250, pending_timeout=100, gap=20, drift=0):
        self.capacity, self.warmup, self.pending_timeout, self.gap, self.drift = capacity, warmup, pending_timeout, gap, drift
        self.phase, self.token, self.disc = 'WAIT_ANCHOR', 1, 5
        self.resets, self.log, self.history, self.pending = 0, [], [], None
        self.seen, self.compared = 0, 0
        self.step_compared = False

    def result(self, outcome, **detail):
        self.log.append(dict(outcome=outcome, phase=self.phase, token=self.token, **detail))
        return outcome

    def install(self, now, position=0):
        self.floor = now
        self.deadline = now + self.warmup
        self.history = [(now, position)]
        self.pending = None
        self.seen = self.compared = 0
        self.step_compared = False
        self.phase = 'WARMUP_HISTORY'
        return self.result('SEEDED_AT_REAL_INSTALL', seed_time=now)

    def reset(self, reason):
        self.phase = 'WAIT_ANCHOR'
        self.token += 1
        self.resets += 1
        self.history, self.pending = [], None
        return self.result('RESET', reason=reason)

    def close(self, reason):
        self.phase, self.pending = 'CLOSED', None
        return self.result('CLOSED', reason=reason)

    def deadlines(self, now):
        if self.phase == 'WARMUP_HISTORY' and now >= self.deadline:
            return self.close('HISTORY_BOOTSTRAP_TIMEOUT')
        if self.pending and now - self.pending['arrival'] >= self.pending_timeout:
            return self.close('HISTORY_PENDING_TIMEOUT')
        return None

    def tick(self, now, position=0):
        if self.phase not in ('WARMUP_HISTORY', 'TRACKING'):
            return self.result('IGNORED_PHASE')
        expired = self.deadlines(now)
        if expired:
            return expired
        if now > self.history[-1][0]:
            self.step_compared = False
            self.history.append((now, position))
            self.history = self.history[-self.capacity:]
        if self.pending:
            sample, self.pending = self.pending, None
            return self.evaluate(sample, now)
        return self.result('HISTORY_RECORDED')

    def ingest(self, seq, tau, now, uncertainty=0, position=0, token=None, disc=None):
        if self.phase not in ('WARMUP_HISTORY', 'TRACKING'):
            return self.result('IGNORED_PHASE')
        expired = self.deadlines(now)
        if expired:
            return expired
        if token is not None and token != self.token:
            return self.result('DROP_IDENTITY')
        if disc is not None and disc != self.disc:
            if disc < self.disc:
                return self.result('DROP_OLD_DISCONTINUITY')
            self.disc = disc
            return self.reset('NEW_DISCONTINUITY')
        if not all(math.isfinite(v) for v in (tau, uncertainty, position)) or uncertainty < 0:
            return self.result('DROP_NONFINITE_OR_INVALID')
        if seq <= self.seen:
            return self.result('DROP_ORDER')
        self.seen = seq
        if self.pending:
            return self.result('PENDING_OCCUPIED', rejected_seq=seq, retained_seq=self.pending['seq'])
        return self.evaluate(dict(seq=seq, tau=tau, uncertainty=uncertainty,
                                  position=position, arrival=now), now)

    def evaluate(self, sample, now):
        u = sample['uncertainty'] + self.drift * (now - sample['arrival'])
        left, right = sample['tau'] - u, sample['tau'] + u
        if left < self.floor:
            return self.result('BEFORE_PREDICTION_GENERATION', seq=sample['seq'])
        if left < self.history[0][0]:
            return (self.reset('HISTORY_EVICTED') if self.phase == 'TRACKING'
                    else self.close('HISTORY_BOOTSTRAP_EVICTED'))
        relevant = [(a, b) for a, b in zip(self.history, self.history[1:])
                    if a[0] < right and b[0] > left]
        if any(b[0] - a[0] > self.gap for a, b in relevant):
            return self.reset('HISTORY_DISCONTINUOUS')
        if right > self.history[-1][0] or len(self.history) < 2:
            self.pending = sample
            return self.result('PENDING_RIGHT_BOUNDARY', seq=sample['seq'])
        pair = next(((a, b) for a, b in zip(self.history, self.history[1:])
                     if a[0] <= sample['tau'] <= b[0] and a[0] < b[0]), None)
        if pair is None:
            raise AssertionError('Coverage without a pair is inconsistent in the fixture')
        if sample['seq'] <= self.compared:
            return self.result('DROP_ALREADY_COMPARED')
        if self.step_compared:
            return self.result('STEP_COMPARISON_BUDGET', seq=sample['seq'])
        a, b = pair
        alpha = (sample['tau'] - a[0]) / (b[0] - a[0])
        predicted = a[1] + alpha * (b[1] - a[1])
        error = sample['position'] - predicted
        self.compared, self.phase = sample['seq'], 'TRACKING'
        self.step_compared = True
        return self.result('COMPARED', seq=sample['seq'], error=error,
                           bracket=[a[0], b[0]], uncertainty_interval=[left, right])


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows, traces = [], {}

    def equal(name, actual, expected):
        rows.append(dict(id=name, actual=actual, expected=expected,
                         result='PASS' if actual == expected else 'FAIL'))

    # Directly reuse the independent review's timings; do not rerun accepted static research.
    review = json.loads((ROOT / 'docs/evidence/m1/P6_INDEPENDENT_REVIEW_ASTRA/RESET_HISTORY_COUNTEREXAMPLE.json').read_text(encoding='utf-8'))
    first = review['cycles'][0]
    m = HistoryFixture()
    m.install(first['anchor_install_time'])
    equal('PRE_ACK_SEED_USES_INSTALL_NOT_HOST_TIME', m.history[0][0], 40)
    for t in first['history_times_at_snapshot_receive']:
        m.tick(t)
    equal('PRE_ACK_SAMPLE_BEFORE_SEED_DROPPED', m.ingest(1, 32, 72, uncertainty=40), 'BEFORE_PREDICTION_GENERATION')
    for t in (88, 104):
        m.tick(t)
    equal('UNCERTAINTY_CROSSING_SEED_DROPPED', m.ingest(2, 64, 104, uncertainty=40), 'BEFORE_PREDICTION_GENERATION')
    for t in (120, 136):
        m.tick(t)
    equal('PRE_ACK_TRACE_REACHES_COMPARE', m.ingest(3, 96, 136, uncertainty=40), 'COMPARED')
    equal('PRE_ACK_TRACE_ZERO_ERROR', m.log[-1]['error'], 0)
    equal('PRE_ACK_TRACE_NO_NEW_TOKEN', [m.token, m.resets, m.phase], [1, 0, 'TRACKING'])
    traces['independent_pre_ack_trace_fixed'] = m.log

    p = HistoryFixture()
    p.install(1)
    p.tick(17)
    equal('POST_ACK_LOW_LATENCY_FIRST_SAMPLE_COMPARES', p.ingest(1, 16, 17, uncertainty=1), 'COMPARED')
    equal('POST_ACK_LOW_LATENCY_PAIR', p.log[-1]['bracket'], [1, 17])
    equal('POST_ACK_LOW_LATENCY_NO_RESET', p.resets, 0)
    traces['independent_post_ack_trace_fixed'] = p.log

    p = HistoryFixture()
    p.install(103)
    p.tick(106)
    p.tick(107)
    equal('POST_ACK_BEFORE_FIRST_STEP_USES_SEED', p.ingest(1, 104.2, 107, uncertainty=0.5), 'COMPARED')
    traces['author_post_ack_trace_fixed'] = p.log

    p = HistoryFixture()
    p.install(0)
    p.tick(0)
    equal('DUPLICATE_TIMESTAMP_IS_NOT_SECOND_RECORD', len(p.history), 1)
    equal('SINGLE_SEED_WAITS', p.ingest(1, 0, 0), 'PENDING_RIGHT_BOUNDARY')
    equal('SECOND_REAL_TIMESTAMP_COMPARE', p.tick(1), 'COMPARED')

    p = HistoryFixture()
    p.install(0)
    p.tick(1)
    equal('FUTURE_RIGHT_BOUNDARY_PENDING', p.ingest(1, 2, 1), 'PENDING_RIGHT_BOUNDARY')
    equal('NEWER_SAMPLE_DOES_NOT_REPLACE_PENDING', p.ingest(2, 3, 1.2), 'PENDING_OCCUPIED')
    equal('PENDING_OLDER_THAN_SEEN_STILL_COMPARES_ONCE', p.tick(2), 'COMPARED')
    equal('SEPARATE_WATERMARKS', [p.seen, p.compared], [2, 1])
    equal('DUPLICATE_PENDING_NOT_REAPPLIED', p.ingest(1, 2, 2), 'DROP_ORDER')
    equal('LATER_SNAPSHOT_CAN_WAIT', p.ingest(3, 2.5, 2), 'PENDING_RIGHT_BOUNDARY')
    equal('LATER_SNAPSHOT_REACHES_HISTORY', p.tick(3), 'COMPARED')
    traces['bounded_pending_without_starvation'] = p.log

    p = HistoryFixture()
    p.install(0)
    p.tick(1)
    p.ingest(1, 0.5, 1)
    equal('SECOND_COMPARISON_SAME_STEP_DROPPED', p.ingest(2, 0.75, 1), 'STEP_COMPARISON_BUDGET')
    equal('STEP_BUDGET_DOES_NOT_APPLY_OR_RESET', [p.compared, p.resets, p.token], [1, 0, 1])
    p.tick(2)
    equal('NEXT_LOCAL_STEP_CAN_COMPARE', p.ingest(3, 1.5, 2), 'COMPARED')
    traces['one_comparison_per_local_step'] = p.log

    p = HistoryFixture(warmup=10)
    p.install(100)
    for seq, now in enumerate((101, 104, 109), 1):
        p.tick(now)
        p.ingest(seq, 99, now)
    equal('EARLY_TRAFFIC_DOES_NOT_EXTEND_WARMUP', p.deadline, 110)
    equal('WARMUP_TIMEOUT_CLOSES', p.tick(110), 'CLOSED')
    equal('WARMUP_TIMEOUT_DOES_NOT_RESET', [p.token, p.resets], [1, 0])
    equal('CLOSED_CANNOT_RESTART_ON_ORDINARY_SNAPSHOT', p.ingest(9, 120, 120), 'IGNORED_PHASE')
    traces['bounded_warmup_timeout'] = p.log

    p = HistoryFixture(pending_timeout=5)
    p.install(0)
    p.ingest(1, 10, 0)
    p.ingest(2, 11, 4)
    equal('PENDING_TIMEOUT_CLOSES', p.tick(5), 'CLOSED')
    equal('PENDING_TIMEOUT_DOES_NOT_RESET', [p.token, p.resets], [1, 0])
    traces['bounded_pending_timeout'] = p.log

    p = HistoryFixture(capacity=3)
    p.install(0)
    p.tick(1)
    p.ingest(1, 0.5, 1)
    p.tick(2)
    p.tick(3)
    equal('TRUE_HISTORY_EVICTION_RESETS_TRACKING', p.ingest(2, 0.75, 3), 'RESET')
    equal('TRUE_EVICTION_NEW_TOKEN', [p.token, p.resets], [2, 1])
    traces['true_eviction_is_not_warmup'] = p.log

    p = HistoryFixture(capacity=2)
    p.install(0)
    p.tick(1)
    p.tick(2)
    equal('EVICTION_DURING_BOOTSTRAP_CLOSES', p.ingest(1, 0.5, 2), 'CLOSED')
    equal('BOOTSTRAP_EVICTION_NO_TOKEN_LOOP', p.resets, 0)

    p = HistoryFixture()
    p.install(10)
    equal('OLD_TOKEN_DOES_NOT_ADVANCE_WATERMARK', p.ingest(99, 20, 11, token=0), 'DROP_IDENTITY')
    equal('OLD_TOKEN_SEEN_UNCHANGED', p.seen, 0)
    equal('OLD_DISCONTINUITY_DROPPED', p.ingest(99, 20, 11, disc=4), 'DROP_OLD_DISCONTINUITY')
    equal('VALID_NEW_DISCONTINUITY_RESETS', p.ingest(1, 20, 11, disc=6), 'RESET')
    equal('VALID_NEW_DISCONTINUITY_GENERATION', [p.token, p.disc], [2, 6])

    p = HistoryFixture(gap=2)
    p.install(0)
    p.tick(5)
    equal('INTERNAL_HISTORY_GAP_IS_NOT_RIGHT_BOUNDARY_WAIT', p.ingest(1, 3, 5), 'RESET')
    p = HistoryFixture(gap=2)
    p.install(0)
    p.tick(1)
    p.tick(8)
    equal('KNOWN_GAP_BEFORE_FUTURE_RIGHT_BOUNDARY_WAIT', p.ingest(1, 5, 8, uncertainty=4), 'RESET')
    p = HistoryFixture(drift=0.1)
    p.install(0)
    p.tick(1)
    p.ingest(1, 3, 1, uncertainty=0.5)
    equal('PENDING_UNCERTAINTY_GROWS_WITH_WAIT', p.tick(3.5), 'PENDING_RIGHT_BOUNDARY')
    equal('PENDING_EVENTUALLY_COVERS_DRIFT_INTERVAL', p.tick(4), 'COMPARED')
    equal('DRIFT_INTERVAL_USED', p.log[-1]['uncertainty_interval'], [2.2, 3.8])

    summary = dict(checks=len(rows), passed=sum(r['result'] == 'PASS' for r in rows),
                   failed=[r['id'] for r in rows if r['result'] != 'PASS'])
    payload = dict(schema='p6-adr3-bootstrap-examples-v1', label='INFERRED scalar specification fixtures, not KEX/native acceptance',
                   runtime_executed=False, summary=summary, checks=rows, traces=traces,
                   source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   reviewed_adr_sha256=hashlib.sha256((ROOT / 'docs/62_P6_ADR_AUTHORITY_AND_CLIENT_WORLD.md').read_bytes()).hexdigest())
    (OUT / 'BOOTSTRAP_EXAMPLES.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    # Only read executor pure reuse checks; never call its main()/write historical output.
    ns = runpy.run_path(str(ROOT / 'docs/research/p6_adr_rework_checks.py'), run_name='p6_adr3_read_only_reuse')
    receipt = ns['reuse']()
    (OUT / 'REUSE_CHECKS.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(examples=summary, reuse=receipt['summary'], runtime_executed=False)))
    return int(bool(summary['failed'] or receipt['summary']['failed']))


if __name__ == '__main__':
    raise SystemExit(main())
