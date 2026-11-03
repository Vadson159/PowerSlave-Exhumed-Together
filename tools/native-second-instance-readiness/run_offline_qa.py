#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Offline Acceptance QA Master Runner
================================================================
Unified executable acceptance QA validation runner.
Executes purely offline without launching the game process:
  1. Executes precheck environment validation (SHA-256, UDP ports, profile isolation).
  2. Generates/refreshes 14 test scenario fixtures.
  3. Executes correlation analysis across 14 reference scenarios, negative filter tests,
     and historical M1.2a evidence (M12A_NATIVE_01).
  4. Generates comprehensive machine-readable report: QA_VALIDATION_RESULTS.json.
  5. Returns non-zero exit code on any assertion discrepancy.

Author: Senior QA Automation Engineer & Network Integration Validation Engineer
"""

import sys
import json
import datetime
from pathlib import Path

# Tool directory setup
tool_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(tool_dir))

from precheck_environment import run_environment_precheck
from test_log_correlation import run_all_tests


def run_offline_qa_suite():
    start_time = datetime.datetime.now(datetime.timezone.utc).isoformat()
    print("=" * 80)
    print("PowerSlave Exhumed Together — Dual-Instance Acceptance QA Runner")
    print(f"Timestamp: {start_time}")
    print("=" * 80)

    # 1. Environment Pre-check
    print("\n[PHASE 1] Executing Environment Isolation Pre-Check...")
    workspace_root = tool_dir.parents[1]
    precheck_report = run_environment_precheck(workspace_root)
    precheck_verdict = precheck_report.get("verdict", "FAIL")
    precheck_success = precheck_verdict in ("PASS", "WARNING")
    if precheck_success:
        print(f"[OK] Environment Pre-check verdict: {precheck_verdict}")
    else:
        print(f"[FAIL] Environment Pre-check verdict: {precheck_verdict}")

    # 2. Test Suite Execution (Fixtures + Scenarios + Negative Tests + M12A Evidence)
    print("\n[PHASE 2] Executing Offline Acceptance QA Test Suite...")
    suite_success, suite_results = run_all_tests()

    end_time = datetime.datetime.now(datetime.timezone.utc).isoformat()
    overall_suite_passed = precheck_success and suite_success

    # 3. Compile Master Results JSON
    report_data = {
        "metadata": {
            "suite_name": "PowerSlave Exhumed Together — Dual-Instance Acceptance QA",
            "runner": "run_offline_qa.py",
            "start_time": start_time,
            "end_time": end_time,
            "overall_status": "PASS" if overall_suite_passed else "FAIL",
            "target_milestone": "Native Second-Instance Game Process B Client Readiness",
            "author": "Senior QA Automation Engineer & Network Integration Validation Engineer"
        },
        "environment_precheck": precheck_report,
        "test_scenarios": suite_results,
        "summary": {
            "precheck_passed": precheck_success,
            "scenarios_total": len(suite_results),
            "scenarios_passed": sum(1 for r in suite_results if r.get("passed", False)),
            "all_assertions_met": overall_suite_passed
        }
    }

    results_file = tool_dir / "QA_VALIDATION_RESULTS.json"
    with results_file.open("w", encoding="utf-8") as f:
        json.dump(report_data, f, indent=2)

    print(f"\n[PHASE 3] Saved comprehensive QA validation results to:")
    print(f"  {results_file}")

    print("=" * 80)
    if overall_suite_passed:
        print("ALL ACCEPTANCE QA CHECKS PASSED. SYSTEM IS READY FOR SOL'S CLIENT INTEGRATION.")
        print("=" * 80)
        return 0
    else:
        print("ACCEPTANCE QA CHECKS FAILED. REVIEW QA_VALIDATION_RESULTS.JSON FOR DETAILS.")
        print("=" * 80)
        return 1


if __name__ == "__main__":
    sys.exit(run_offline_qa_suite())
