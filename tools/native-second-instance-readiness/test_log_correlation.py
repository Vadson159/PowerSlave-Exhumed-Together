#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Acceptance QA Test Suite
======================================================
Automated test suite verifying the behavior of the hardened log correlation engine
across the 14 reference scenarios, negative filter tests, and historical M1.2a evidence.

Verifies:
  1. All 14 scenarios yield exact expected verdicts (PASS, FAIL, INCONCLUSIVE).
  2. Negative tests: if host accepts duplicate / wrong slot / stale gen / uint32_max -> FAIL.
  3. Historical M12A_NATIVE_01 runs correctly evaluate REAL_CLIENT_VERIFICATION as INCONCLUSIVE
     (preventing false PASS on headless client prototypes).

Author: Senior QA Automation Engineer & Network Integration Validation Engineer
"""

import sys
import json
from pathlib import Path

# Add tool directory to path
tool_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(tool_dir))

from log_correlation import load_jsonl, analyze_telemetry_correlation
from generate_fixtures import generate_all_fixtures


def run_all_tests():
    fixtures_dir = tool_dir / "fixtures"
    generate_all_fixtures(fixtures_dir)

    scenario_expectations = {
        "scenario_01_valid_hold_release_neutral": ("PASS", []),
        "scenario_02_uncommanded_movement": ("FAIL", ["CAUSAL_P2_MOVEMENT"]),
        "scenario_03_undelivered_commands": ("INCONCLUSIVE", ["INPUT_CORRELATION"]),
        "scenario_04_dropped_release_sticky_walk": ("FAIL", ["INPUT_MECHANICS"]),
        "scenario_05_dropped_release_neutral_recovery": ("PASS", []),
        "scenario_06_duplicate_sequence": ("PASS", []),
        "scenario_07_stale_generation": ("PASS", []),
        "scenario_08_wrong_slot": ("PASS", []),
        "scenario_09_pathological_uint32_max": ("PASS", []),
        "scenario_10_thread_affinity_violation": ("FAIL", ["THREAD_AFFINITY"]),
        "scenario_11_p1_state_mutation": ("FAIL", ["P1_ISOLATION"]),
        "scenario_12_lingering_worker_on_exit": ("FAIL", ["CLEAN_SHUTDOWN"]),
        "scenario_13_missing_telemetry_or_manifest": ("INCONCLUSIVE", ["BUILD_INTEGRITY"]),
        "scenario_14_unconfirmed_shutdown_order": ("INCONCLUSIVE", ["CLEAN_SHUTDOWN"])
    }

    test_results = []
    total_passed = 0
    total_tests = 0

    print("=" * 80)
    print("PowerSlave Exhumed Together — Acceptance QA Test Suite (14 Scenarios)")
    print("=" * 80)

    for scenario_name, (expected_verdict, expected_failing_contracts) in scenario_expectations.items():
        total_tests += 1
        s_dir = fixtures_dir / scenario_name
        host_log = s_dir / "host_native.jsonl"
        client_log = s_dir / "client_native.jsonl"

        host_recs, h_err = load_jsonl(host_log)
        client_recs, c_err = load_jsonl(client_log)

        if h_err or c_err:
            print(f"[FAIL] {scenario_name}: Could not load logs: {h_err or c_err}")
            test_results.append({"scenario": scenario_name, "passed": False, "error": h_err or c_err})
            continue

        res = analyze_telemetry_correlation(host_recs, client_recs, str(host_log), str(client_log))
        verdict = res["verdict"]

        verdict_ok = (verdict == expected_verdict)
        contracts_ok = True
        for fc in expected_failing_contracts:
            contract_status = res["contracts"].get(fc, {}).get("status")
            if contract_status not in ("FAIL", "INCONCLUSIVE"):
                contracts_ok = False
                break

        passed = verdict_ok and contracts_ok
        if passed:
            total_passed += 1
            print(f"[PASS] {scenario_name:45} -> Verdict: {verdict:12} (Expected: {expected_verdict})")
        else:
            print(f"[FAIL] {scenario_name:45} -> Verdict: {verdict:12} (Expected: {expected_verdict}) Contracts: {res['contracts']}")

        test_results.append({
            "scenario": scenario_name,
            "passed": passed,
            "verdict": verdict,
            "expected_verdict": expected_verdict,
            "failure_reasons": res["failure_reasons"],
            "inconclusive_reasons": res["inconclusive_reasons"]
        })

    # -------------------------------------------------------------------------
    # Negative Ingress Filter Tests: Host Accepts Bad Packet -> Must FAIL
    # -------------------------------------------------------------------------
    print("-" * 80)
    print("Negative Tests: Rogue Host Accepts Invalid Packets -> Asserts FAIL")
    print("-" * 80)

    # Test N1: Host accepts duplicate sequence
    total_tests += 1
    h_recs, _ = load_jsonl(fixtures_dir / "scenario_01_valid_hold_release_neutral" / "host_native.jsonl")
    c_recs, _ = load_jsonl(fixtures_dir / "scenario_01_valid_hold_release_neutral" / "client_native.jsonl")
    # Inject duplicate accept into host
    h_recs.insert(10, {"event": "network_input_accepted", "tid": 20100, "slot": 1, "generation": 1, "input_seq": 1, "forward": 1.0, "native_buttons": 4})
    res_n1 = analyze_telemetry_correlation(h_recs, c_recs)
    n1_pass = (res_n1["verdict"] == "FAIL" and res_n1["contracts"]["INPUT_CORRELATION"]["status"] == "FAIL")
    if n1_pass:
        total_passed += 1
        print("[PASS] Negative Test N1 (Duplicate sequence accepted by host) -> Properly detected FAIL")
    else:
        print("[FAIL] Negative Test N1 failed to detect accepted duplicate sequence")
    test_results.append({"scenario": "negative_duplicate_accepted", "passed": n1_pass, "verdict": res_n1["verdict"]})

    # Test N2: Host accepts wrong slot (e.g. slot 99)
    total_tests += 1
    h_recs, _ = load_jsonl(fixtures_dir / "scenario_01_valid_hold_release_neutral" / "host_native.jsonl")
    c_recs, _ = load_jsonl(fixtures_dir / "scenario_01_valid_hold_release_neutral" / "client_native.jsonl")
    h_recs.insert(10, {"event": "network_input_accepted", "tid": 20100, "slot": 99, "generation": 1, "input_seq": 999, "forward": 1.0, "native_buttons": 4})
    res_n2 = analyze_telemetry_correlation(h_recs, c_recs)
    n2_pass = (res_n2["verdict"] == "FAIL" and res_n2["contracts"]["INPUT_CORRELATION"]["status"] == "FAIL")
    if n2_pass:
        total_passed += 1
        print("[PASS] Negative Test N2 (Wrong slot accepted by host) -> Properly detected FAIL")
    else:
        print("[FAIL] Negative Test N2 failed to detect accepted wrong slot")
    test_results.append({"scenario": "negative_wrong_slot_accepted", "passed": n2_pass, "verdict": res_n2["verdict"]})

    # Test N3: Host accepts forward sequence jump > 1800 (DOC-50)
    total_tests += 1
    h_recs, _ = load_jsonl(fixtures_dir / "scenario_01_valid_hold_release_neutral" / "host_native.jsonl")
    c_recs, _ = load_jsonl(fixtures_dir / "scenario_01_valid_hold_release_neutral" / "client_native.jsonl")
    h_recs.insert(10, {"event": "network_input_accepted", "tid": 20100, "slot": 1, "generation": 1, "input_seq": 5000, "forward": 1.0, "native_buttons": 4})
    res_n3 = analyze_telemetry_correlation(h_recs, c_recs)
    n3_pass = (res_n3["verdict"] == "FAIL" and res_n3["contracts"]["INPUT_CORRELATION"]["status"] == "FAIL")
    if n3_pass:
        total_passed += 1
        print("[PASS] Negative Test N3 (Illegal jump >1800 accepted by host) -> Properly detected FAIL")
    else:
        print("[FAIL] Negative Test N3 failed to detect accepted sequence jump")
    test_results.append({"scenario": "negative_sequence_jump_accepted", "passed": n3_pass, "verdict": res_n3["verdict"]})

    # -------------------------------------------------------------------------
    # Historical Evidence Verification: M12A_NATIVE_01 Gate
    # -------------------------------------------------------------------------
    print("-" * 80)
    print("Historical Evidence Test: M12A_NATIVE_01 (Headless client gate)")
    print("-" * 80)

    total_tests += 1
    m12a_host = tool_dir.parents[1] / "docs" / "evidence" / "m1" / "M12A_NATIVE_01" / "native.jsonl"
    m12a_client = tool_dir.parents[1] / "docs" / "evidence" / "m1" / "M12A_NATIVE_01" / "client.jsonl"

    if m12a_host.is_file() and m12a_client.is_file():
        h_m12a, _ = load_jsonl(m12a_host)
        c_m12a, _ = load_jsonl(m12a_client)
        res_m12a = analyze_telemetry_correlation(h_m12a, c_m12a, str(m12a_host), str(m12a_client))

        # Must NOT evaluate to PASS because client was headless prototype!
        # REAL_CLIENT_VERIFICATION must be INCONCLUSIVE, overall verdict INCONCLUSIVE
        # But host transport and displacement contracts should PASS.
        m12a_pass = (
            res_m12a["verdict"] == "INCONCLUSIVE" and
            res_m12a["contracts"]["REAL_CLIENT_VERIFICATION"]["status"] == "INCONCLUSIVE" and
            res_m12a["contracts"]["CAUSAL_P2_MOVEMENT"]["status"] == "PASS" and
            res_m12a["contracts"]["BUILD_INTEGRITY"]["status"] == "PASS"
        )
        if m12a_pass:
            total_passed += 1
            print(f"[PASS] M12A_NATIVE_01: Correctly flagged as INCONCLUSIVE due to headless client prototype (anti-false-PASS confirmed)")
        else:
            print(f"[FAIL] M12A_NATIVE_01: Unexpected verdict {res_m12a['verdict']}, contracts: {res_m12a['contracts']}")
        test_results.append({"scenario": "m12a_historical_evidence", "passed": m12a_pass, "verdict": res_m12a["verdict"]})
    else:
        print(f"[SKIP] M12A_NATIVE_01 files not found at expected location ({m12a_host})")

    print("=" * 80)
    print(f"Summary: {total_passed} / {total_tests} tests passed.")
    print("=" * 80)

    return total_passed == total_tests, test_results


if __name__ == "__main__":
    success, results = run_all_tests()
    sys.exit(0 if success else 1)
