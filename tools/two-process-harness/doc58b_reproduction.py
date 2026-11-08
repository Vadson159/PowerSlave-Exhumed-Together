"""DOC-58B Run 02 Exact Reproduction and Acceptance Gate.

Validates that the reusable two-process harness deterministically reproduces
the exact metrics, verdicts, accounting, and lifecycle observations of DOC-58B Run 02
from the raw accepted journals (N58B_HOST_02 / N58B_CLIENT_02).
"""

import json
from pathlib import Path
import sys
from typing import Any, Dict, List, Optional, Tuple

if __package__ is None or __package__ == '':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from evidence_reviewer import EvidenceReviewer
    from scenario_runner import ScenarioRunner
    from session import find_project_root
else:
    from .evidence_reviewer import EvidenceReviewer
    from .scenario_runner import ScenarioRunner
    from .session import find_project_root


class Doc58bReproduction:
    """Verifies that the reusable harness reproduces DOC-58B Run 02 with bit-exact fidelity."""

    def __init__(self, root: Optional[Path] = None):
        self.root = root or find_project_root()
        self.evidence_dir = self.root / 'docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B'
        self.host_dir = self.evidence_dir / 'N58B_HOST_02'
        self.client_dir = self.evidence_dir / 'N58B_CLIENT_02'
        self.frozen_summary_path = self.evidence_dir / 'RESULT_SUMMARY.json'

    def run_reproduction(self) -> Dict[str, Any]:
        if not self.host_dir.is_dir() or not self.client_dir.is_dir():
            raise FileNotFoundError(f"DOC-58B Run 02 directories not found: {self.host_dir}, {self.client_dir}")

        # 1. Run the universal EvidenceReviewer
        reviewer = EvidenceReviewer(self.host_dir, self.client_dir)
        review_result = reviewer.review(write_artifacts=False)

        # 2. Load the frozen DOC-58B result summary
        frozen_summary = json.loads(self.frozen_summary_path.read_text(encoding='utf-8-sig'))

        # 3. Assertions and comparisons
        discrepancies: List[str] = []

        # Compare Verdicts
        for cat, expected_verdict in frozen_summary['verdict'].items():
            actual_verdict = review_result['verdict'].get(cat)
            if actual_verdict != expected_verdict:
                discrepancies.append(f"Verdict mismatch for {cat}: got {actual_verdict}, expected {expected_verdict}")

        # Compare Accounting metrics
        f_acc = frozen_summary['accounting']
        r_acc = review_result['accounting']
        accounting_keys = [
            'host_captured',
            'host_captured_slot0',
            'host_captured_slot1',
            'host_worker_send',
            'host_os_udp_send',
            'client_os_udp_receive_window',
            'client_worker_receive',
            'client_sim_accepted',
            'client_rejected',
            'correlated_rows',
            'applied_rows',
            'adapter_update_callbacks',
        ]
        for key in accounting_keys:
            if r_acc.get(key) != f_acc.get(key):
                discrepancies.append(f"Accounting mismatch for {key}: got {r_acc.get(key)}, expected {f_acc.get(key)}")

        # Check bit exact mismatches
        if r_acc.get('bit_exact_mismatches') != []:
            discrepancies.append(f"Bit-exact mismatches found: {r_acc.get('bit_exact_mismatches')}")

        # Compare Render metrics
        f_ren = frozen_summary['render']
        r_ren = review_result['render']
        for rk in ['ours', 'intermediate_logged', 'native_lerp_samples', 'distinct_revisions_rendered', 'applied_rows_with_render']:
            if r_ren.get(rk) != f_ren.get(rk):
                discrepancies.append(f"Render metric mismatch for {rk}: got {r_ren.get(rk)}, expected {f_ren.get(rk)}")

        # Compare Native Gates
        f_gates = frozen_summary.get('gates', {})
        r_gates = review_result['gates']
        for gate_id, expected_status in f_gates.items():
            if r_gates.get(gate_id) != expected_status:
                discrepancies.append(f"Gate mismatch for {gate_id}: got {r_gates.get(gate_id)}, expected {expected_status}")

        # Compare Actor details
        if review_result['spawn_calls'] != frozen_summary['spawn_calls']:
            discrepancies.append(f"Spawn calls mismatch: got {review_result['spawn_calls']}, expected {frozen_summary['spawn_calls']}")
        if review_result['remove_calls'] != frozen_summary['remove_calls']:
            discrepancies.append(f"Remove calls mismatch: got {review_result['remove_calls']}, expected {frozen_summary['remove_calls']}")
        if review_result['updates_applied'] != frozen_summary['updates_applied']:
            discrepancies.append(f"Updates applied mismatch: got {review_result['updates_applied']}, expected {frozen_summary['updates_applied']}")

        # 4. Validate declarative scenario definition
        scenario = ScenarioRunner.build_doc58b_run02_scenario()
        runner = ScenarioRunner()
        runner.validate_scenario(scenario)

        status = 'PASS' if not discrepancies else 'FAIL'
        return {
            'status': status,
            'task': 'S0.5 Acceptance: DOC-58B Run 02 Reproduction',
            'overall_verdict': review_result['verdict']['OVERALL'],
            'discrepancies': discrepancies,
            'accounting': review_result['accounting'],
            'verdict': review_result['verdict'],
            'render': review_result['render'],
            'gates': review_result['gates'],
            'scenario_validation': 'PASS',
        }


def main():
    repro = Doc58bReproduction()
    res = repro.run_reproduction()
    print(json.dumps(res, indent=2))
    if res['status'] != 'PASS':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
