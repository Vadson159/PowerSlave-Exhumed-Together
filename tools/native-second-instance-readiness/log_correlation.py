#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Hardened Dual-Instance Log Correlation Engine
===========================================================================
Production-grade QA verification engine for cross-instance telemetry validation:
  - Game Process A (Host): host_native.jsonl (authoritative KEX engine host)
  - Game Process B (Client): client_native.jsonl (real KEX client / prototype)

Verifies the full causal integration chain:
  Client Local Input Sampled -> ENet Wire Dispatch -> Host ENet Acceptance ->
  Native KEX P2 Update -> Physics Motion Displacement -> Clean Shutdown.

Strict QA Factuality Enforcement:
  - Causal correlation by (slot, generation, input_seq), NEVER cross-machine wall clocks.
  - Unreliable packet drops permitted; single packet loss != failure.
  - Sticky walk detection (dropped release without neutral/timeout recovery -> FAIL).
  - Uncommanded P2 displacement detection (movement without client input -> FAIL).
  - P1 isolation corruption detection -> FAIL.
  - Thread affinity violation detection -> FAIL.
  - Headless/mock client detection -> INCONCLUSIVE (never false PASS for dual-game).
  - Missing or truncated telemetry -> INCONCLUSIVE.

Author: Senior QA Automation Engineer & Network Integration Validation Engineer
"""

import sys
import json
import argparse
from pathlib import Path
from typing import Dict, List, Any, Optional, Tuple, Set

EXPECTED_EXE_SHA256 = "b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d"


def load_jsonl(path: Path) -> Tuple[List[Dict[str, Any]], Optional[str]]:
    """Safely load records from a structured JSONL journal file."""
    records = []
    if not path.is_file():
        return records, f"File does not exist: {path}"
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for line_no, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                    rec["_line"] = line_no
                    records.append(rec)
                except json.JSONDecodeError:
                    return records, f"Malformed JSON on line {line_no} in {path}"
    except (PermissionError, OSError) as e:
        return records, f"Failed to read {path}: {e}"
    return records, None


def analyze_telemetry_correlation(host_records: List[Dict[str, Any]], 
                                   client_records: List[Dict[str, Any]],
                                   host_log_path: str = "host_native.jsonl",
                                   client_log_path: str = "client_native.jsonl") -> Dict[str, Any]:
    """
    Execute exhaustive contract validation across Host and Client telemetry streams.
    Returns structured scorecard with contracts, metrics, failure reasons, and verdict.
    """

    contracts = {
        "PROC_ISOLATION": {"status": "UNKNOWN", "detail": ""},
        "BUILD_INTEGRITY": {"status": "UNKNOWN", "detail": ""},
        "SESSION_LIFECYCLE": {"status": "UNKNOWN", "detail": ""},
        "INPUT_CORRELATION": {"status": "UNKNOWN", "detail": ""},
        "INPUT_MECHANICS": {"status": "UNKNOWN", "detail": ""},
        "CAUSAL_P2_MOVEMENT": {"status": "UNKNOWN", "detail": ""},
        "P1_ISOLATION": {"status": "UNKNOWN", "detail": ""},
        "THREAD_AFFINITY": {"status": "UNKNOWN", "detail": ""},
        "CLEAN_SHUTDOWN": {"status": "UNKNOWN", "detail": ""},
        "REAL_CLIENT_VERIFICATION": {"status": "UNKNOWN", "detail": ""}
    }

    failure_reasons = []
    inconclusive_reasons = []

    # -------------------------------------------------------------------------
    # 0. Basic Integrity & Empty Log Gate
    # -------------------------------------------------------------------------
    if not host_records or not client_records:
        if not host_records:
            inconclusive_reasons.append(f"Host journal empty or missing ({host_log_path})")
        if not client_records:
            inconclusive_reasons.append(f"Client journal empty or missing ({client_log_path})")
        return {
            "verdict": "INCONCLUSIVE",
            "summary": "Missing required telemetry stream(s)",
            "failure_reasons": failure_reasons,
            "inconclusive_reasons": inconclusive_reasons,
            "contracts": contracts,
            "metrics": {}
        }

    # Index events by type for both streams
    host_events: Dict[str, List[Dict[str, Any]]] = {}
    for r in host_records:
        host_events.setdefault(r.get("event", "unknown"), []).append(r)

    client_events: Dict[str, List[Dict[str, Any]]] = {}
    for r in client_records:
        client_events.setdefault(r.get("event", "unknown"), []).append(r)

    # -------------------------------------------------------------------------
    # 1. PROC_ISOLATION Contract
    # -------------------------------------------------------------------------
    host_pids = {r.get("pid") for r in host_records if r.get("pid") is not None}
    client_pids = {r.get("pid") for r in client_records if r.get("pid") is not None}
    host_tids = {r.get("tid") for r in host_records if r.get("tid") is not None}
    client_tids = {r.get("tid") for r in client_records if r.get("tid") is not None}

    if not host_pids or not client_pids:
        contracts["PROC_ISOLATION"]["status"] = "INCONCLUSIVE"
        contracts["PROC_ISOLATION"]["detail"] = "Process PID missing from one or both journals"
        inconclusive_reasons.append("Process PID metadata missing")
    else:
        pid_intersection = host_pids.intersection(client_pids)
        if pid_intersection:
            contracts["PROC_ISOLATION"]["status"] = "FAIL"
            contracts["PROC_ISOLATION"]["detail"] = f"Host and Client share identical PID(s): {pid_intersection}"
            failure_reasons.append("Host and Client running inside identical process ID (no isolation)")
        else:
            contracts["PROC_ISOLATION"]["status"] = "PASS"
            contracts["PROC_ISOLATION"]["detail"] = f"Host PID(s): {sorted(list(host_pids))}, Client PID(s): {sorted(list(client_pids))}"

    # -------------------------------------------------------------------------
    # 2. BUILD_INTEGRITY Contract
    # -------------------------------------------------------------------------
    dll_prep = host_events.get("dll_prepared", [])
    if dll_prep:
        host_bin_sha = dll_prep[0].get("binary_sha256", "")
        if host_bin_sha == EXPECTED_EXE_SHA256:
            contracts["BUILD_INTEGRITY"]["status"] = "PASS"
            contracts["BUILD_INTEGRITY"]["detail"] = f"Host binary SHA-256 verified: {host_bin_sha[:16]}..."
        else:
            contracts["BUILD_INTEGRITY"]["status"] = "FAIL"
            contracts["BUILD_INTEGRITY"]["detail"] = f"Host binary SHA mismatch: got {host_bin_sha}, expected {EXPECTED_EXE_SHA256}"
            failure_reasons.append(f"Host binary build mismatch: {host_bin_sha}")
    else:
        contracts["BUILD_INTEGRITY"]["status"] = "INCONCLUSIVE"
        contracts["BUILD_INTEGRITY"]["detail"] = "dll_prepared event missing from host journal"
        inconclusive_reasons.append("Host build integrity marker (dll_prepared) missing")

    # -------------------------------------------------------------------------
    # 3. SESSION_LIFECYCLE Contract
    # -------------------------------------------------------------------------
    net_started = host_events.get("network_started", [])
    net_session = host_events.get("network_session", [])
    net_ready = host_events.get("network_ready", [])
    client_welcomes = client_events.get("welcome", [])

    if net_started and (net_session or client_welcomes):
        assigned_slot = None
        assigned_gen = None
        if client_welcomes:
            assigned_slot = client_welcomes[0].get("slot")
            assigned_gen = client_welcomes[0].get("generation")
        elif net_session:
            assigned_slot = net_session[0].get("slot")
            assigned_gen = net_session[0].get("generation")

        if assigned_slot == 1:
            contracts["SESSION_LIFECYCLE"]["status"] = "PASS"
            contracts["SESSION_LIFECYCLE"]["detail"] = f"Session bound: slot={assigned_slot} (P2), generation={assigned_gen}"
        else:
            contracts["SESSION_LIFECYCLE"]["status"] = "FAIL"
            contracts["SESSION_LIFECYCLE"]["detail"] = f"Invalid slot assigned: slot={assigned_slot} (expected slot 1 / P2)"
            failure_reasons.append(f"Unexpected slot assignment: {assigned_slot}")
    else:
        contracts["SESSION_LIFECYCLE"]["status"] = "INCONCLUSIVE"
        contracts["SESSION_LIFECYCLE"]["detail"] = "Network handshake incomplete (missing network_started or welcome)"
        inconclusive_reasons.append("Network session handshake not fully established")

    # -------------------------------------------------------------------------
    # 4. INPUT_CORRELATION & Monotonicity Contract (Causal matching)
    # -------------------------------------------------------------------------
    client_sends = []
    client_sends_by_key = {}
    for r in client_records:
        evt = r.get("event")
        if evt in ("sent", "client_input_dispatched"):
            seq = r.get("input_seq") or r.get("sequence")
            gen = r.get("generation", 1)
            slot = r.get("slot", 1)
            fwd = r.get("forward", 0.0)
            if seq is not None:
                item = {"slot": slot, "gen": gen, "seq": seq, "forward": fwd, "rec": r}
                client_sends.append(item)
                client_sends_by_key[(slot, gen, seq)] = item

    host_accepts = []
    host_accepts_by_key = {}
    for r in host_events.get("network_input_accepted", []):
        seq = r.get("input_seq")
        gen = r.get("generation", 1)
        slot = r.get("slot", 1)
        fwd = r.get("forward", 0.0)
        btns = r.get("native_buttons", 0)
        tick = r.get("tick", 0)
        if seq is not None:
            item = {"slot": slot, "gen": gen, "seq": seq, "forward": fwd, "buttons": btns, "tick": tick, "rec": r}
            host_accepts.append(item)
            host_accepts_by_key[(slot, gen, seq)] = item

    host_rejects = host_events.get("network_input_rejected", [])

    matched_count = 0
    unmatched_sends = 0
    duplicate_accepted = False
    stale_accepted = False
    wrong_slot_accepted = False
    jump_accepted = False

    seen_host_keys = set()
    for h in host_accepts:
        key = (h["slot"], h["gen"], h["seq"])
        if key in seen_host_keys:
            duplicate_accepted = True
        seen_host_keys.add(key)
        
        if h["slot"] != 1:
            wrong_slot_accepted = True
        if key in client_sends_by_key:
            matched_count += 1

    for c in client_sends:
        key = (c["slot"], c["gen"], c["seq"])
        if key not in host_accepts_by_key:
            unmatched_sends += 1

    valid_generations = set()
    for r in client_welcomes + net_session:
        g = r.get("generation")
        if g is not None:
            valid_generations.add(g)

    # Check monotonicity of host accepted sequences and ingress bounds (DOC-50)
    monotonic_ok = True
    last_seq_by_gen = {}
    for h in host_accepts:
        s = h["seq"]
        gen = h["gen"]
        
        # Check slot
        if h["slot"] != 1:
            wrong_slot_accepted = True
            
        # Check generation match if session bound
        if valid_generations and gen not in valid_generations:
            stale_accepted = True
            
        # Check sequence progression within generation
        last_s = last_seq_by_gen.get(gen, -1)
        if last_s == -1 and s > 10800:
            jump_accepted = True
        elif last_s >= 0 and s <= last_s:
            monotonic_ok = False
        elif last_s >= 0 and (s - last_s) > 1800:
            jump_accepted = True
            
        last_seq_by_gen[gen] = s

    if duplicate_accepted:
        contracts["INPUT_CORRELATION"]["status"] = "FAIL"
        contracts["INPUT_CORRELATION"]["detail"] = "Host accepted duplicate sequence number(s)"
        failure_reasons.append("Duplicate sequence accepted as independent input")
    elif wrong_slot_accepted:
        contracts["INPUT_CORRELATION"]["status"] = "FAIL"
        contracts["INPUT_CORRELATION"]["detail"] = "Host accepted input for unauthorized slot != 1"
        failure_reasons.append("Unauthorized slot accepted")
    elif stale_accepted:
        contracts["INPUT_CORRELATION"]["status"] = "FAIL"
        contracts["INPUT_CORRELATION"]["detail"] = "Host accepted packet with stale/invalid generation"
        failure_reasons.append("Stale generation accepted by host")
    elif jump_accepted:
        contracts["INPUT_CORRELATION"]["status"] = "FAIL"
        contracts["INPUT_CORRELATION"]["detail"] = "Host accepted illegal forward sequence jump (>1800 or uninitialized UINT32_MAX)"
        failure_reasons.append("Sequence jump violation accepted by host")
    elif not monotonic_ok:
        contracts["INPUT_CORRELATION"]["status"] = "FAIL"
        contracts["INPUT_CORRELATION"]["detail"] = "Host accepted sequences out of monotonic order"
        failure_reasons.append("Host sequence progression non-monotonic")
    elif matched_count > 0:
        contracts["INPUT_CORRELATION"]["status"] = "PASS"
        contracts["INPUT_CORRELATION"]["detail"] = f"Matched {matched_count} commands (unmatched/dropped: {unmatched_sends})"
    elif client_sends and not host_accepts:
        contracts["INPUT_CORRELATION"]["status"] = "INCONCLUSIVE"
        contracts["INPUT_CORRELATION"]["detail"] = f"Client sent {len(client_sends)} commands, but Host accepted 0"
        inconclusive_reasons.append("Zero client commands accepted by host")
    else:
        contracts["INPUT_CORRELATION"]["status"] = "INCONCLUSIVE"
        contracts["INPUT_CORRELATION"]["detail"] = "No command activity in journals"
        inconclusive_reasons.append("No command streams found to correlate")

    # -------------------------------------------------------------------------
    # 5. INPUT_MECHANICS & Sticky Walk Detection
    # -------------------------------------------------------------------------
    hold_sent = any(c["forward"] > 0.5 for c in client_sends)
    release_sent = False
    neutral_stream_sent = False
    for i, c in enumerate(client_sends):
        if c["forward"] > 0.5:
            if i + 1 < len(client_sends) and client_sends[i + 1]["forward"] == 0.0:
                release_sent = True
            if i + 2 < len(client_sends) and all(client_sends[j]["forward"] == 0.0 for j in range(i + 1, min(i + 10, len(client_sends)))):
                neutral_stream_sent = True

    host_held = any(h["forward"] > 0.5 or h["buttons"] == 4 for h in host_accepts)
    host_neutralized = any(h["forward"] == 0.0 and h["buttons"] == 0 for h in host_accepts)
    host_timeouts = host_events.get("network_neutral", [])

    if hold_sent:
        # Check for sticky walk vulnerability
        # If client intended to stop, but host never received neutral or timeout
        last_client_fwd = client_sends[-1]["forward"] if client_sends else 1.0
        last_host_fwd = host_accepts[-1]["forward"] if host_accepts else 0.0
        last_host_btns = host_accepts[-1]["buttons"] if host_accepts else 0

        if last_client_fwd == 0.0 and (last_host_fwd > 0.5 or last_host_btns == 4) and not host_timeouts:
            contracts["INPUT_MECHANICS"]["status"] = "FAIL"
            contracts["INPUT_MECHANICS"]["detail"] = "Sticky walk detected: client released forward, but host P2 remained actively walking"
            failure_reasons.append("P2 sticky walk: forward motion active after client release without timeout recovery")
        elif release_sent and (host_neutralized or host_timeouts):
            contracts["INPUT_MECHANICS"]["status"] = "PASS"
            contracts["INPUT_MECHANICS"]["detail"] = "Hold -> Release -> Neutral cycle verified cleanly"
        elif not release_sent and hold_sent:
            contracts["INPUT_MECHANICS"]["status"] = "INCONCLUSIVE"
            contracts["INPUT_MECHANICS"]["detail"] = "Hold command sent without explicit release/neutral sequence"
            inconclusive_reasons.append("Release phase missing from client stream")
        else:
            contracts["INPUT_MECHANICS"]["status"] = "PASS"
            contracts["INPUT_MECHANICS"]["detail"] = "Neutral input stream sustained"
    else:
        contracts["INPUT_MECHANICS"]["status"] = "PASS"
        contracts["INPUT_MECHANICS"]["detail"] = "No active hold commands in scenario; neutral verified"

    # -------------------------------------------------------------------------
    # 6. CAUSAL_P2_MOVEMENT & Uncommanded Motion Gate
    # -------------------------------------------------------------------------
    host_motions = host_events.get("network_native_motion", [])
    total_displacement = 0.0
    for m in host_motions:
        b = m.get("before", [0.0, 0.0, 0.0])
        a = m.get("after", [0.0, 0.0, 0.0])
        dx = a[0] - b[0]
        dy = a[1] - b[1]
        step = (dx * dx + dy * dy) ** 0.5
        total_displacement += step

    if total_displacement > 5.0 and not hold_sent and not host_held:
        # Uncommanded displacement!
        contracts["CAUSAL_P2_MOVEMENT"]["status"] = "FAIL"
        contracts["CAUSAL_P2_MOVEMENT"]["detail"] = f"Causality violation: P2 moved {total_displacement:.3f} units without any forward command"
        failure_reasons.append(f"Uncommanded P2 displacement: {total_displacement:.3f} units")
    elif total_displacement > 10.0 and hold_sent and host_held:
        contracts["CAUSAL_P2_MOVEMENT"]["status"] = "PASS"
        contracts["CAUSAL_P2_MOVEMENT"]["detail"] = f"Native displacement verified: {total_displacement:.3f} XY units driven by client input"
    elif total_displacement <= 1.0 and not hold_sent:
        contracts["CAUSAL_P2_MOVEMENT"]["status"] = "PASS"
        contracts["CAUSAL_P2_MOVEMENT"]["detail"] = "Stationary state verified (displacement ~0.0)"
    else:
        contracts["CAUSAL_P2_MOVEMENT"]["status"] = "INCONCLUSIVE"
        contracts["CAUSAL_P2_MOVEMENT"]["detail"] = f"Displacement {total_displacement:.3f} units inconclusive for input sequence"
        inconclusive_reasons.append("P2 movement could not be causally correlated with command stream")

    # -------------------------------------------------------------------------
    # 7. P1_ISOLATION Contract
    # -------------------------------------------------------------------------
    p1_violations = 0
    host_updates = host_events.get("network_native_update", [])
    for u in host_updates:
        if u.get("unchanged") is False:
            p1_violations += 1
        b = u.get("p1_before", {})
        a = u.get("p1_after", {})
        if b and a:
            if b.get("health") != a.get("health") or b.get("weapon") != a.get("weapon"):
                p1_violations += 1

    if p1_violations > 0:
        contracts["P1_ISOLATION"]["status"] = "FAIL"
        contracts["P1_ISOLATION"]["detail"] = f"P1 state mutated during P2 update: {p1_violations} violation(s)"
        failure_reasons.append(f"Host P1 memory modified by P2 update ({p1_violations} times)")
    elif host_updates:
        contracts["P1_ISOLATION"]["status"] = "PASS"
        contracts["P1_ISOLATION"]["detail"] = f"P1 isolation verified across {len(host_updates)} native updates"
    else:
        contracts["P1_ISOLATION"]["status"] = "INCONCLUSIVE"
        contracts["P1_ISOLATION"]["detail"] = "No native update comparisons present"
        inconclusive_reasons.append("Host network_native_update records missing")

    # -------------------------------------------------------------------------
    # 8. THREAD_AFFINITY Contract
    # -------------------------------------------------------------------------
    foreign_calls = 0
    attested_tid = None
    if host_events.get("simulation_attested"):
        attested_tid = host_events["simulation_attested"][0].get("observed_tid")

    for u in host_updates:
        if u.get("foreign_calls", 0) > 0:
            foreign_calls += u.get("foreign_calls")
        if attested_tid and u.get("tid") != attested_tid:
            foreign_calls += 1

    if host_events.get("native_final_exit"):
        final_rec = host_events["native_final_exit"][0]
        if final_rec.get("foreign_calls", 0) > 0:
            foreign_calls += final_rec.get("foreign_calls")

    if foreign_calls > 0:
        contracts["THREAD_AFFINITY"]["status"] = "FAIL"
        contracts["THREAD_AFFINITY"]["detail"] = f"Thread affinity violation: {foreign_calls} foreign call(s) observed"
        failure_reasons.append(f"KEX executed outside primary simulation thread ({foreign_calls} foreign calls)")
    elif host_updates or host_events.get("simulation_attested"):
        contracts["THREAD_AFFINITY"]["status"] = "PASS"
        contracts["THREAD_AFFINITY"]["detail"] = f"All KEX calls executed strictly on attested simulation TID: {attested_tid}"
    else:
        contracts["THREAD_AFFINITY"]["status"] = "INCONCLUSIVE"
        contracts["THREAD_AFFINITY"]["detail"] = "Simulation TID attestation missing"
        inconclusive_reasons.append("Simulation thread attestation record missing")

    # -------------------------------------------------------------------------
    # 9. CLEAN_SHUTDOWN Contract
    # -------------------------------------------------------------------------
    quiesced = host_events.get("shutdown_quiesced", [])
    final_exit = host_events.get("native_final_exit", [])
    state_exit = host_events.get("state_exit_return", [])

    worker_lingering = False
    for r in host_records + client_records:
        if (r.get("worker_joined") is False or 
            r.get("worker_alive_at_exit") is True or 
            r.get("lingering_workers", 0) > 0 or 
            r.get("threads_active_after_shutdown", 0) > 0):
            worker_lingering = True
            break

    if worker_lingering:
        contracts["CLEAN_SHUTDOWN"]["status"] = "FAIL"
        contracts["CLEAN_SHUTDOWN"]["detail"] = "Worker thread or socket remained active/unjoined during process shutdown"
        failure_reasons.append("Lingering worker thread or unjoined socket on exit")
    elif quiesced or final_exit or state_exit:
        contracts["CLEAN_SHUTDOWN"]["status"] = "PASS"
        contracts["CLEAN_SHUTDOWN"]["detail"] = "Orderly shutdown verified (quiesced, worker joined, native exit)"
    else:
        contracts["CLEAN_SHUTDOWN"]["status"] = "INCONCLUSIVE"
        contracts["CLEAN_SHUTDOWN"]["detail"] = "Unconfirmed shutdown: process ended without proven quiescence"
        inconclusive_reasons.append("Shutdown quiescence marks absent from host journal")

    # -------------------------------------------------------------------------
    # 10. REAL_CLIENT_VERIFICATION Contract (Anti-False-PASS Gate)
    # -------------------------------------------------------------------------
    is_headless_mock = False
    for r in client_records:
        if r.get("headless") is True or "client_control.txt" in str(r):
            is_headless_mock = True
            break
    if not any("client_dll_prepared" in r.get("event", "") or "client_input_sampled" in r.get("event", "") for r in client_records):
        # Existing headless prototype
        is_headless_mock = True

    if is_headless_mock:
        contracts["REAL_CLIENT_VERIFICATION"]["status"] = "INCONCLUSIVE"
        contracts["REAL_CLIENT_VERIFICATION"]["detail"] = "Client stream originated from headless prototype (native KEX Process B pending)"
        inconclusive_reasons.append("Headless prototype client detected: real Native Game Process B required for milestone completion")
    else:
        contracts["REAL_CLIENT_VERIFICATION"]["status"] = "PASS"
        contracts["REAL_CLIENT_VERIFICATION"]["detail"] = "Client confirmed as genuine KEX Game Process B with local native input sampling"

    # -------------------------------------------------------------------------
    # Final Overall Verdict
    # -------------------------------------------------------------------------
    if failure_reasons:
        overall_verdict = "FAIL"
    elif inconclusive_reasons:
        overall_verdict = "INCONCLUSIVE"
    else:
        overall_verdict = "PASS"

    summary_text = (
        f"Verified {matched_count} commands, {total_displacement:.2f} XY units displacement. "
        f"Verdict: {overall_verdict}."
    )

    return {
        "verdict": overall_verdict,
        "summary": summary_text,
        "failure_reasons": failure_reasons,
        "inconclusive_reasons": inconclusive_reasons,
        "contracts": contracts,
        "metrics": {
            "client_commands_sent": len(client_sends),
            "host_commands_accepted": len(host_accepts),
            "matched_commands": matched_count,
            "unmatched_sends_dropped": unmatched_sends,
            "total_p2_displacement": round(total_displacement, 6),
            "p1_violations": p1_violations,
            "foreign_calls": foreign_calls
        }
    }


def main():
    parser = argparse.ArgumentParser(description="Hardened cross-instance telemetry correlation analyzer for dual KEX.")
    parser.add_argument("--host-log", type=Path, required=True, help="Path to host_native.jsonl")
    parser.add_argument("--client-log", type=Path, required=True, help="Path to client_native.jsonl")
    parser.add_argument("--json", action="store_true", help="Output summary in JSON format")
    args = parser.parse_args()

    host_records, host_err = load_jsonl(args.host_log)
    client_records, client_err = load_jsonl(args.client_log)

    if host_err or client_err:
        err_msg = host_err or client_err
        report = {
            "verdict": "INCONCLUSIVE",
            "summary": f"IO Error: {err_msg}",
            "failure_reasons": [],
            "inconclusive_reasons": [err_msg],
            "contracts": {},
            "metrics": {}
        }
        if args.json:
            print(json.dumps(report, indent=2))
        else:
            print(f"[INCONCLUSIVE] {err_msg}")
        sys.exit(2)

    analysis = analyze_telemetry_correlation(host_records, client_records, str(args.host_log), str(args.client_log))

    if args.json:
        print(json.dumps(analysis, indent=2))
    else:
        print("=" * 78)
        print("PowerSlave Exhumed Together — Hardened Dual-Instance Correlation Scorecard")
        print("=" * 78)
        print(f"VERDICT: {analysis['verdict']}")
        print(f"Summary: {analysis['summary']}")
        print("-" * 78)
        print("Contracts Evaluation:")
        for name, data in analysis["contracts"].items():
            status = data.get("status", "UNKNOWN")
            indicator = "[ OK ]" if status == "PASS" else f"[{status}]"
            print(f"  {indicator:16} {name}: {data.get('detail')}")
        
        if analysis["failure_reasons"]:
            print("-" * 78)
            print("FAILURE REASONS:")
            for r in analysis["failure_reasons"]:
                print(f"  [!] {r}")

        if analysis["inconclusive_reasons"]:
            print("-" * 78)
            print("INCONCLUSIVE REASONS:")
            for r in analysis["inconclusive_reasons"]:
                print(f"  [?] {r}")

        print("-" * 78)
        print("Metrics:")
        for k, v in analysis["metrics"].items():
            print(f"  {k:28}: {v}")
        print("=" * 78)

    # Exit codes: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
    if analysis["verdict"] == "PASS":
        sys.exit(0)
    elif analysis["verdict"] == "FAIL":
        sys.exit(1)
    else:
        sys.exit(2)


if __name__ == "__main__":
    main()
