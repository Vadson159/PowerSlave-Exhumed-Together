#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Acceptance QA Fixture Generator
=============================================================
Generates 14 test scenario fixture pairs (host_native.jsonl and client_native.jsonl)
under tools/native-second-instance-readiness/fixtures/ to validate the correlation engine.

Scenarios:
  01: scenario_01_valid_hold_release_neutral      -> PASS
  02: scenario_02_uncommanded_movement            -> FAIL
  03: scenario_03_undelivered_commands            -> INCONCLUSIVE
  04: scenario_04_dropped_release_sticky_walk     -> FAIL
  05: scenario_05_dropped_release_neutral_recovery-> PASS
  06: scenario_06_duplicate_sequence             -> PASS (rejected by ingress filter)
  07: scenario_07_stale_generation                -> PASS (rejected by ingress filter)
  08: scenario_08_wrong_slot                      -> PASS (rejected by ingress filter)
  09: scenario_09_pathological_uint32_max         -> PASS (rejected by ingress filter)
  10: scenario_10_thread_affinity_violation       -> FAIL
  11: scenario_11_p1_state_mutation               -> FAIL
  12: scenario_12_lingering_worker_on_exit        -> FAIL
  13: scenario_13_missing_telemetry_or_manifest   -> INCONCLUSIVE
  14: scenario_14_unconfirmed_shutdown_order      -> INCONCLUSIVE

Author: Senior QA Automation Engineer & Network Integration Validation Engineer
"""

import json
from pathlib import Path

EXPECTED_EXE_SHA256 = "b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d"
HOST_PID = 10100
HOST_TID = 20100
CLIENT_PID = 10200
CLIENT_TID = 20200


def get_base_host_preamble(pid=HOST_PID, tid=HOST_TID, sha=EXPECTED_EXE_SHA256):
    return [
        {"event": "dll_prepared", "seq": 1, "utc": "2026-11-02T10:00:00.000Z", "tid": tid, "pid": pid, "binary_sha256": sha, "module_pinned": True},
        {"event": "bootstrap_ready", "seq": 2, "utc": "2026-11-02T10:00:01.000Z", "tid": tid, "hooks_installed_on_primary": True},
        {"event": "simulation_attested", "seq": 3, "utc": "2026-11-02T10:00:02.000Z", "tid": tid, "observed_tid": tid, "game_instance": "0000000140AF33A0"},
        {"event": "network_started", "seq": 4, "utc": "2026-11-02T10:00:03.000Z", "tid": tid, "port": 28745, "protocol": 272},
        {"event": "network_session", "seq": 5, "utc": "2026-11-02T10:00:04.000Z", "tid": tid, "slot": 1, "generation": 1, "tick": 100},
        {"event": "network_ready", "seq": 6, "utc": "2026-11-02T10:00:04.050Z", "tid": tid, "slot": 1, "generation": 1, "tick": 101},
        {"event": "independent_spawn", "seq": 7, "utc": "2026-11-02T10:00:04.100Z", "tid": tid, "p1_health": 200, "p2_health": 200, "generation": 1}
    ]


def get_base_client_preamble(pid=CLIENT_PID, tid=CLIENT_TID, sha=EXPECTED_EXE_SHA256):
    return [
        {"event": "client_dll_prepared", "seq": 1, "utc": "2026-11-02T10:00:00.500Z", "tid": tid, "pid": pid, "binary_sha256": sha, "headless": False},
        {"event": "welcome", "seq": 2, "utc": "2026-11-02T10:00:04.000Z", "tid": tid, "pid": pid, "slot": 1, "generation": 1, "protocol": 272},
        {"event": "client_ready", "seq": 3, "utc": "2026-11-02T10:00:04.050Z", "tid": tid, "pid": pid, "slot": 1, "generation": 1}
    ]


def get_base_host_shutdown(tid=HOST_TID, worker_joined=True, worker_alive=False):
    return [
        {"event": "network_stop_begin", "seq": 990, "utc": "2026-11-02T10:00:10.000Z", "tid": tid, "reason": "StateExit"},
        {"event": "network_joined", "seq": 991, "utc": "2026-11-02T10:00:10.050Z", "tid": tid, "worker_running": worker_alive, "worker_joined": worker_joined},
        {"event": "state_exit_return", "seq": 992, "utc": "2026-11-02T10:00:10.100Z", "tid": tid, "external_handle_null": True},
        {"event": "shutdown_quiesced", "seq": 993, "utc": "2026-11-02T10:00:10.150Z", "tid": tid, "external_handle_null": True, "pin_until_process_exit": True},
        {"event": "native_final_exit", "seq": 994, "utc": "2026-11-02T10:00:10.200Z", "tid": tid, "external_handle_null": True, "foreign_calls": 0, "worker_alive_at_exit": worker_alive}
    ]


def get_p1_state(health=200, weapon=0):
    return {"health": health, "weapon": weapon, "node": "00000000135D1EE8", "actor": "000000006BC65910"}


def write_jsonl(path: Path, records):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")


def generate_all_fixtures(fixtures_dir: Path):
    fixtures_dir.mkdir(parents=True, exist_ok=True)

    # -------------------------------------------------------------------------
    # Scenario 01: Valid Hold -> Release -> Neutral cycle (PASS)
    # -------------------------------------------------------------------------
    s01_dir = fixtures_dir / "scenario_01_valid_hold_release_neutral"
    client_recs = get_base_client_preamble()
    host_recs = get_base_host_preamble()

    # Client inputs: Hold W (seq 1..5), Release (seq 6), Neutral stream (seq 7..10)
    for seq in range(1, 6):
        client_recs.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        client_recs.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})

    client_recs.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 6, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    client_recs.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 6, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})

    for seq in range(7, 11):
        client_recs.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        client_recs.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})

    # Host inputs & motions
    pos_x = -7420.0
    for seq in range(1, 6):
        host_recs.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        host_recs.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        new_x = pos_x + 3.0
        host_recs.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [pos_x, -6775.0, 1152.0], "after": [new_x, -6775.0, 1152.0]})
        pos_x = new_x

    # Release and neutral
    for seq in range(6, 11):
        host_recs.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 0.0, "native_buttons": 0})
        host_recs.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        host_recs.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [pos_x, -6775.0, 1152.0], "after": [pos_x, -6775.0, 1152.0]})

    host_recs.extend(get_base_host_shutdown())
    write_jsonl(s01_dir / "client_native.jsonl", client_recs)
    write_jsonl(s01_dir / "host_native.jsonl", host_recs)

    # -------------------------------------------------------------------------
    # Scenario 02: Uncommanded Movement (FAIL)
    # -------------------------------------------------------------------------
    s02_dir = fixtures_dir / "scenario_02_uncommanded_movement"
    c02 = get_base_client_preamble()
    h02 = get_base_host_preamble()
    # Client sends ONLY neutral
    for seq in range(1, 6):
        c02.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        c02.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        h02.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 0.0, "native_buttons": 0})
        h02.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        # Uncommanded displacement of 20 units!
        h02.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0, -6775.0, 1152.0], "after": [-7400.0, -6775.0, 1152.0]})
    h02.extend(get_base_host_shutdown())
    write_jsonl(s02_dir / "client_native.jsonl", c02)
    write_jsonl(s02_dir / "host_native.jsonl", h02)

    # -------------------------------------------------------------------------
    # Scenario 03: Undelivered Commands (INCONCLUSIVE)
    # -------------------------------------------------------------------------
    s03_dir = fixtures_dir / "scenario_03_undelivered_commands"
    c03 = get_base_client_preamble()
    h03 = get_base_host_preamble()
    for seq in range(1, 6):
        c03.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c03.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    # Host accepts ZERO commands
    h03.extend(get_base_host_shutdown())
    write_jsonl(s03_dir / "client_native.jsonl", c03)
    write_jsonl(s03_dir / "host_native.jsonl", h03)

    # -------------------------------------------------------------------------
    # Scenario 04: Dropped Release Sticky Walk (FAIL)
    # -------------------------------------------------------------------------
    s04_dir = fixtures_dir / "scenario_04_dropped_release_sticky_walk"
    c04 = get_base_client_preamble()
    h04 = get_base_host_preamble()
    # Client sends Hold (1..5), Release (6), then stops sending
    for seq in range(1, 6):
        c04.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c04.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        h04.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h04.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h04.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Client sends release seq 6
    c04.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 6, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    c04.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 6, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    # Release dropped: host never receives seq 6, host never times out with network_neutral
    h04.extend(get_base_host_shutdown())
    write_jsonl(s04_dir / "client_native.jsonl", c04)
    write_jsonl(s04_dir / "host_native.jsonl", h04)

    # -------------------------------------------------------------------------
    # Scenario 05: Dropped Release Neutral Recovery (PASS)
    # -------------------------------------------------------------------------
    s05_dir = fixtures_dir / "scenario_05_dropped_release_neutral_recovery"
    c05 = get_base_client_preamble()
    h05 = get_base_host_preamble()
    for seq in range(1, 6):
        c05.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c05.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        h05.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h05.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h05.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Client sends release seq 6 (dropped)
    c05.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 6, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    c05.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 6, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    # Client follows with continuous neutral stream (seq 7..10)
    for seq in range(7, 11):
        c05.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        c05.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    # Host receives seq 7 (neutral recovery!)
    h05.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 7, "forward": 0.0, "native_buttons": 0})
    h05.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 7, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
    h05.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 7, "before": [-7405.0, -6775.0, 1152.0], "after": [-7405.0, -6775.0, 1152.0]})
    h05.extend(get_base_host_shutdown())
    write_jsonl(s05_dir / "client_native.jsonl", c05)
    write_jsonl(s05_dir / "host_native.jsonl", h05)

    # -------------------------------------------------------------------------
    # Scenario 06: Duplicate Sequence Rejected by Host Ingress Filter (PASS)
    # -------------------------------------------------------------------------
    s06_dir = fixtures_dir / "scenario_06_duplicate_sequence"
    c06 = get_base_client_preamble()
    h06 = get_base_host_preamble()
    # Client sends seq 1..5, plus duplicate seq 3
    for seq in (1, 2, 3, 3, 4, 5):
        c06.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c06.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    # Host accepts 1, 2, 3
    for seq in (1, 2, 3):
        h06.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h06.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h06.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Duplicate 3 is REJECTED by host ingress filter
    h06.append({"event": "network_input_rejected", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 3, "reason": "rejected_stale"})
    # Host accepts 4, 5
    for seq in (4, 5):
        h06.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h06.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h06.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Client sends Release (6) and Neutral (7)
    for seq in (6, 7):
        c06.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        c06.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        h06.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 0.0, "native_buttons": 0})
        h06.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h06.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7405.0, -6775.0, 1152.0], "after": [-7405.0, -6775.0, 1152.0]})
    h06.extend(get_base_host_shutdown())
    write_jsonl(s06_dir / "client_native.jsonl", c06)
    write_jsonl(s06_dir / "host_native.jsonl", h06)

    # -------------------------------------------------------------------------
    # Scenario 07: Stale Generation Rejected by Host (PASS)
    # -------------------------------------------------------------------------
    s07_dir = fixtures_dir / "scenario_07_stale_generation"
    c07 = get_base_client_preamble()
    h07 = get_base_host_preamble()
    # Rogue packet with generation 0
    c07.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 0, "forward": 1.0, "buttons": 4})
    c07.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 0, "forward": 1.0, "buttons": 4})
    # Host rejects stale generation
    h07.append({"event": "network_input_rejected", "tid": HOST_TID, "slot": 1, "generation": 0, "input_seq": 1, "reason": "rejected_generation"})
    # Normal packets gen 1
    for seq in range(1, 6):
        c07.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c07.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        h07.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h07.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h07.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Release and Neutral
    for seq in (6, 7):
        c07.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        c07.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        h07.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 0.0, "native_buttons": 0})
        h07.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h07.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7405.0, -6775.0, 1152.0], "after": [-7405.0, -6775.0, 1152.0]})
    h07.extend(get_base_host_shutdown())
    write_jsonl(s07_dir / "client_native.jsonl", c07)
    write_jsonl(s07_dir / "host_native.jsonl", h07)

    # -------------------------------------------------------------------------
    # Scenario 08: Wrong Slot Rejected by Host (PASS)
    # -------------------------------------------------------------------------
    s08_dir = fixtures_dir / "scenario_08_wrong_slot"
    c08 = get_base_client_preamble()
    h08 = get_base_host_preamble()
    # Packet claiming slot 99
    c08.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 99, "generation": 1, "forward": 1.0, "buttons": 4})
    c08.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 99, "generation": 1, "forward": 1.0, "buttons": 4})
    h08.append({"event": "network_input_rejected", "tid": HOST_TID, "slot": 99, "generation": 1, "input_seq": 1, "reason": "rejected_foreign"})
    for seq in range(1, 6):
        c08.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c08.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        h08.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h08.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h08.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Release and Neutral
    for seq in (6, 7):
        c08.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        c08.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        h08.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 0.0, "native_buttons": 0})
        h08.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h08.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7405.0, -6775.0, 1152.0], "after": [-7405.0, -6775.0, 1152.0]})
    h08.extend(get_base_host_shutdown())
    write_jsonl(s08_dir / "client_native.jsonl", c08)
    write_jsonl(s08_dir / "host_native.jsonl", h08)

    # -------------------------------------------------------------------------
    # Scenario 09: Pathological UINT32_MAX Rejected per DOC-50 (PASS)
    # -------------------------------------------------------------------------
    s09_dir = fixtures_dir / "scenario_09_pathological_uint32_max"
    c09 = get_base_client_preamble()
    h09 = get_base_host_preamble()
    # Client sends pathological UINT32_MAX
    c09.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 4294967295, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    c09.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 4294967295, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    # Host ingress filter detects jump > 1800 and rejects it without poisoning state
    h09.append({"event": "network_input_rejected", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 4294967295, "reason": "rejected_jump"})
    # Subsequent valid sequence 1..5 is accepted cleanly
    for seq in range(1, 6):
        c09.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        c09.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
        h09.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 1.0, "native_buttons": 4})
        h09.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h09.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7420.0 + (seq-1)*3, -6775.0, 1152.0], "after": [-7420.0 + seq*3, -6775.0, 1152.0]})
    # Release and Neutral
    for seq in (6, 7):
        c09.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        c09.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": seq, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
        h09.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "forward": 0.0, "native_buttons": 0})
        h09.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "unchanged": True, "foreign_calls": 0, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
        h09.append({"event": "network_native_motion", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": seq, "before": [-7405.0, -6775.0, 1152.0], "after": [-7405.0, -6775.0, 1152.0]})
    h09.extend(get_base_host_shutdown())
    write_jsonl(s09_dir / "client_native.jsonl", c09)
    write_jsonl(s09_dir / "host_native.jsonl", h09)

    # -------------------------------------------------------------------------
    # Scenario 10: Thread Affinity Violation (FAIL)
    # -------------------------------------------------------------------------
    s10_dir = fixtures_dir / "scenario_10_thread_affinity_violation"
    c10 = get_base_client_preamble()
    h10 = get_base_host_preamble()
    c10.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    c10.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    h10.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 1, "forward": 1.0, "native_buttons": 4})
    # Thread affinity violation: foreign_calls = 1 and executed on worker TID 20999!
    h10.append({"event": "network_native_update", "tid": 20999, "slot": 1, "generation": 1, "input_seq": 1, "unchanged": True, "foreign_calls": 1, "p1_before": get_p1_state(), "p1_after": get_p1_state()})
    h10.extend(get_base_host_shutdown())
    write_jsonl(s10_dir / "client_native.jsonl", c10)
    write_jsonl(s10_dir / "host_native.jsonl", h10)

    # -------------------------------------------------------------------------
    # Scenario 11: P1 State Mutation (FAIL)
    # -------------------------------------------------------------------------
    s11_dir = fixtures_dir / "scenario_11_p1_state_mutation"
    c11 = get_base_client_preamble()
    h11 = get_base_host_preamble()
    c11.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    c11.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 1.0, "buttons": 4})
    h11.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 1, "forward": 1.0, "native_buttons": 4})
    # P1 state mutated: unchanged = False and health dropped
    h11.append({"event": "network_native_update", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 1, "unchanged": False, "foreign_calls": 0, "p1_before": get_p1_state(200, 0), "p1_after": get_p1_state(150, 0)})
    h11.extend(get_base_host_shutdown())
    write_jsonl(s11_dir / "client_native.jsonl", c11)
    write_jsonl(s11_dir / "host_native.jsonl", h11)

    # -------------------------------------------------------------------------
    # Scenario 12: Lingering Worker on Exit (FAIL)
    # -------------------------------------------------------------------------
    s12_dir = fixtures_dir / "scenario_12_lingering_worker_on_exit"
    c12 = get_base_client_preamble()
    h12 = get_base_host_preamble()
    c12.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    c12.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    h12.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 1, "forward": 0.0, "native_buttons": 0})
    # Lingering worker: worker_joined = False, worker_alive_at_exit = True
    h12.extend(get_base_host_shutdown(worker_joined=False, worker_alive=True))
    write_jsonl(s12_dir / "client_native.jsonl", c12)
    write_jsonl(s12_dir / "host_native.jsonl", h12)

    # -------------------------------------------------------------------------
    # Scenario 13: Missing Telemetry or Manifest (INCONCLUSIVE)
    # -------------------------------------------------------------------------
    s13_dir = fixtures_dir / "scenario_13_missing_telemetry_or_manifest"
    # Host log is missing dll_prepared marker
    h13 = get_base_host_preamble()[1:]  # Dropped dll_prepared!
    c13 = get_base_client_preamble()
    write_jsonl(s13_dir / "client_native.jsonl", c13)
    write_jsonl(s13_dir / "host_native.jsonl", h13)

    # -------------------------------------------------------------------------
    # Scenario 14: Unconfirmed Shutdown Order (INCONCLUSIVE)
    # -------------------------------------------------------------------------
    s14_dir = fixtures_dir / "scenario_14_unconfirmed_shutdown_order"
    c14 = get_base_client_preamble()
    h14 = get_base_host_preamble()
    c14.append({"event": "client_input_sampled", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    c14.append({"event": "sent", "pid": CLIENT_PID, "tid": CLIENT_TID, "input_seq": 1, "slot": 1, "generation": 1, "forward": 0.0, "buttons": 0})
    h14.append({"event": "network_input_accepted", "tid": HOST_TID, "slot": 1, "generation": 1, "input_seq": 1, "forward": 0.0, "native_buttons": 0})
    # Abrupt end: NO shutdown_quiesced, NO native_final_exit
    write_jsonl(s14_dir / "client_native.jsonl", c14)
    write_jsonl(s14_dir / "host_native.jsonl", h14)

    print(f"[OK] Generated 14 test scenario fixtures in {fixtures_dir}")


if __name__ == "__main__":
    fixtures_path = Path(__file__).parent / "fixtures"
    generate_all_fixtures(fixtures_path)
