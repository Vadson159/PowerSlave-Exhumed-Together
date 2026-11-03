#!/usr/bin/env python3
"""
Unit, Safety, and Factuality Verification Tests for Multi-Client Test Runner (Phase 2)
======================================================================================
Tests runner-level logic and invariant guards:
  - Command line parameter enforcement (1..8 clients)
  - Anti-False-PASS guards (process crashes, timeouts, empty logs, missing JSON)
  - Concurrency Session Overlap Analyzer (detecting false simultaneity)
  - Authoritative Host Event Verifier (CONFIRMED vs CONTRADICTION vs UNATTACHED)
  - Native KEX Integration Adapter (strictly DEFERRED without native telemetry, NEVER false PASS)
  - Deterministic Chaos Engine PRNG seed repeatability
  - Markdown and JSON report generation integrity
"""

import unittest
import sys
import os
import json
import tempfile
from pathlib import Path

# Add runner dir to path
RUNNER_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(RUNNER_DIR))

from multiclient_runner import (
    MultiClientRunner,
    ClientConfig,
    ClientResult,
    SuiteResult,
    ContractResult,
    export_markdown_report,
    export_json_summary
)
from host_verifier import (
    HostEventTracker,
    HostConfirmationStatus,
    SessionOverlapAnalyzer
)
from kex_adapter import (
    NativeKexAdapter,
    KexStatus
)
from chaos_orchestrator import (
    ChaosOrchestrator,
    ChaosActionType
)


class TestMultiClientRunnerPhase2(unittest.TestCase):

    def test_slot_collision_detector(self):
        """Invariant validator must detect when two clients are assigned the same slot ID."""
        runner = MultiClientRunner()
        cfg1 = ClientConfig(1, "p2", "P2", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, Path("c1.log"), Path("c1.json"), Path("c1.md"))
        cfg2 = ClientConfig(2, "p3", "P3", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, Path("c2.log"), Path("c2.json"), Path("c2.md"))
        suite = SuiteResult(
            suite_name="Slot Collision Test",
            timestamp="2026-11-01 23:00:00",
            host="127.0.0.1",
            port=27015,
            client_count=2,
            mode="simultaneous",
            stagger_ms=0,
            scenario="hold-w",
            clients=[
                ClientResult(index=1, participant_id="p2", display_name="P2", pid=1001, exit_code=0, passed=True, assigned_slot="P2", slot_id=1, session_id=999, connection_generation=1, log_file="c1.log"),
                ClientResult(index=2, participant_id="p3", display_name="P3", pid=1002, exit_code=0, passed=True, assigned_slot="P2", slot_id=1, session_id=999, connection_generation=1, log_file="c2.log")  # COLLISION!
            ]
        )

        runner._validate_invariants(suite, [cfg1, cfg2])
        self.assertFalse(suite.all_passed)
        self.assertTrue(len(suite.slot_collisions) > 0)
        self.assertIn("CRITICAL SLOT COLLISION", suite.slot_collisions[0])

    def test_generation_invariant_detector(self):
        """Invariant validator must flag any fresh client whose connectionGeneration != 1."""
        runner = MultiClientRunner()
        cfg = ClientConfig(1, "p2", "P2", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, Path("c1.log"), Path("c1.json"), Path("c1.md"))
        suite = SuiteResult(
            suite_name="Generation Invariant Test",
            timestamp="2026-11-01 23:00:00",
            host="127.0.0.1",
            port=27015,
            client_count=1,
            mode="simultaneous",
            stagger_ms=0,
            scenario="hold-w",
            clients=[
                ClientResult(index=1, participant_id="p2", display_name="P2", pid=1001, exit_code=0, passed=True, assigned_slot="P2", slot_id=1, session_id=999, connection_generation=3, log_file="c1.log")
            ]
        )

        runner._validate_invariants(suite, [cfg])
        self.assertFalse(suite.all_passed)
        self.assertTrue(len(suite.generation_violations) > 0)
        self.assertIn("GENERATION INVARIANT VIOLATION", suite.generation_violations[0])

    def test_reconnect_generation_bump_detector(self):
        """Invariant validator must require connectionGeneration >= 2 for reconnected scenario."""
        runner = MultiClientRunner()
        cfg = ClientConfig(1, "p2", "P2", "127.0.0.1", 27015, "", "reconnect", 30, 15, 60, 5000, Path("c1.log"), Path("c1.json"), Path("c1.md"))
        suite = SuiteResult(
            suite_name="Reconnect Generation Test",
            timestamp="2026-11-01 23:00:00",
            host="127.0.0.1",
            port=27015,
            client_count=1,
            mode="simultaneous",
            stagger_ms=0,
            scenario="reconnect",
            clients=[
                ClientResult(index=1, participant_id="p2", display_name="P2", pid=1001, exit_code=0, passed=True, assigned_slot="P2", slot_id=1, session_id=999, connection_generation=1, log_file="c1.log")  # FAILED: reconnect must be gen >= 2
            ]
        )

        runner._validate_invariants(suite, [cfg])
        self.assertFalse(suite.all_passed)
        self.assertTrue(len(suite.generation_violations) > 0)
        self.assertIn("GENERATION INVARIANT VIOLATION", suite.generation_violations[0])

    def test_session_id_mismatch_detector(self):
        """Simultaneous clients must receive the identical host session ID."""
        runner = MultiClientRunner()
        cfg1 = ClientConfig(1, "p2", "P2", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, Path("c1.log"), Path("c1.json"), Path("c1.md"))
        cfg2 = ClientConfig(2, "p3", "P3", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, Path("c2.log"), Path("c2.json"), Path("c2.md"))
        suite = SuiteResult(
            suite_name="Session Inconsistency Test",
            timestamp="2026-11-01 23:00:00",
            host="127.0.0.1",
            port=27015,
            client_count=2,
            mode="simultaneous",
            stagger_ms=0,
            scenario="hold-w",
            clients=[
                ClientResult(index=1, participant_id="p2", display_name="P2", pid=1001, exit_code=0, passed=True, assigned_slot="P2", slot_id=1, session_id=111111, connection_generation=1, log_file="c1.log"),
                ClientResult(index=2, participant_id="p3", display_name="P3", pid=1002, exit_code=0, passed=True, assigned_slot="P3", slot_id=2, session_id=222222, connection_generation=1, log_file="c2.log")  # MISMATCH!
            ]
        )

        runner._validate_invariants(suite, [cfg1, cfg2])
        self.assertFalse(suite.all_passed)
        self.assertTrue(len(suite.session_mismatches) > 0)

    def test_session_overlap_analyzer_pass(self):
        """SessionOverlapAnalyzer confirms positive overlap window when sessions run simultaneously."""
        sessions = [
            (100.0, 101.5),  # Client 1: 0.0 -> 1.5
            (100.2, 101.4),  # Client 2: 0.2 -> 1.4
            (100.1, 101.6)   # Client 3: 0.1 -> 1.6
        ]
        res = SessionOverlapAnalyzer.analyze_overlap(sessions)
        self.assertTrue(res.is_overlapping)
        self.assertAlmostEqual(res.overlap_duration_s, 1.2, places=2)
        self.assertEqual(res.active_concurrent_peak, 3)

    def test_session_overlap_analyzer_fail(self):
        """SessionOverlapAnalyzer flags non-overlapping sessions as concurrency defect."""
        sessions = [
            (100.0, 100.5),  # Client 1 terminates at 100.5
            (100.6, 101.2)   # Client 2 only starts at 100.6
        ]
        res = SessionOverlapAnalyzer.analyze_overlap(sessions)
        self.assertFalse(res.is_overlapping)
        self.assertEqual(res.overlap_duration_s, 0.0)
        self.assertEqual(res.active_concurrent_peak, 1)
        self.assertIn("CONCURRENCY DEFECT", res.detail)

    def test_anti_false_pass_process_crash(self):
        """Non-zero process exit code must always result in passed=False."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_p = Path(tmp_dir)
            log_f = tmp_p / "client.log"
            json_f = tmp_p / "client.json"
            log_f.write_text("dummy log", encoding="utf-8")
            json_f.write_text(json.dumps({"passCount": 9, "failCount": 0, "results": []}), encoding="utf-8")

            cfg = ClientConfig(1, "p2", "P2", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, log_f, json_f, tmp_p / "rep.md")
            runner = MultiClientRunner()
            # Crash exit code 0xC0000005 (-1073741819)
            res = runner._parse_client_output(cfg, pid=9999, exit_code=-1073741819, timed_out=False, duration_s=0.5, stdout="", stderr="", t_start=0.0)
            self.assertFalse(res.passed)
            self.assertIn("abnormally", res.error_message)

    def test_anti_false_pass_empty_log(self):
        """Empty client log file must fail even if JSON claims passes."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_p = Path(tmp_dir)
            log_f = tmp_p / "empty.log"
            json_f = tmp_p / "client.json"
            log_f.write_text("", encoding="utf-8")  # 0 bytes!
            json_f.write_text(json.dumps({"passCount": 9, "failCount": 0, "results": []}), encoding="utf-8")

            cfg = ClientConfig(1, "p2", "P2", "127.0.0.1", 27015, "", "hold-w", 30, 15, 60, 5000, log_f, json_f, tmp_p / "rep.md")
            runner = MultiClientRunner()
            res = runner._parse_client_output(cfg, pid=9999, exit_code=0, timed_out=False, duration_s=0.5, stdout="", stderr="", t_start=0.0)
            self.assertFalse(res.passed)
            self.assertIn("missing or empty", res.error_message)

    def test_host_verifier_confirmed_vs_contradiction(self):
        """HostEventTracker must confirm authentic allocations and flag contradictions."""
        tracker = HostEventTracker()
        tracker.feed_line("[Host] Listening on 127.0.0.1:27015 (Max clients: 7, Tick: 60 Hz, Snapshots: 30 Hz)")
        tracker.feed_line("[Host] Assigned slot P2 to participant: Runner_P2 [runner_p2], gen=1")
        tracker.feed_line("[Host] Client disconnected from slot P2 (runner_p2)")

        # Case 1: Matching facts -> CONFIRMED
        r1 = tracker.verify_client_facts("runner_p2", claimed_slot="P2", claimed_gen=1)
        self.assertEqual(r1.status, HostConfirmationStatus.CONFIRMED)
        self.assertTrue(r1.disconnection_confirmed)

        # Case 2: Claimed wrong slot -> CONTRADICTION
        r2 = tracker.verify_client_facts("runner_p2", claimed_slot="P4", claimed_gen=1)
        self.assertEqual(r2.status, HostConfirmationStatus.CONTRADICTION)

        # Case 3: Claimed wrong generation -> CONTRADICTION
        r3 = tracker.verify_client_facts("runner_p2", claimed_slot="P2", claimed_gen=2)
        self.assertEqual(r3.status, HostConfirmationStatus.CONTRADICTION)

        # Case 4: Ghost participant never registered on host -> UNCONFIRMED
        r4 = tracker.verify_client_facts("ghost_p9", claimed_slot="P2", claimed_gen=1)
        self.assertEqual(r4.status, HostConfirmationStatus.UNCONFIRMED)

    def test_kex_adapter_strictly_deferred_without_native_telemetry(self):
        """NativeKexAdapter must strictly mark KEX-01..KEX-04 as DEFERRED when targeting mock host or no telemetry."""
        adapter = NativeKexAdapter(telemetry_source=None, is_native_host=False)
        checks = adapter.evaluate_contracts(target_slot="P2")
        self.assertEqual(len(checks), 4)
        for c in checks:
            self.assertEqual(c.status, KexStatus.DEFERRED, f"Contract {c.contract_id} must be DEFERRED without telemetry!")
            self.assertIn("Requires native KEX telemetry hook", c.detail)

    def test_kex_adapter_verifies_real_telemetry_state_exit(self):
        """NativeKexAdapter marks PASS when complete, hardened native telemetry markers are fed for StateExit."""
        adapter = NativeKexAdapter(telemetry_source=None, is_native_host=True)
        adapter.ingest_line("[KEX-HOOK] ActorSpawn: slot=P2 actorId=201 type=0 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] MotionTick: slot=P2 pos=(120.0,45.0,0.0) vel=1.0 inputSeq=1 netCmds=1 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] MotionTick: slot=P2 pos=(125.0,48.0,0.0) vel=1.0 inputSeq=2 netCmds=2 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] P1StateCheck: tick=10 slot0_hp=200 slot0_status=ALIVE gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] P1StateCheck: tick=20 slot0_hp=200 slot0_status=ALIVE gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] ActorDestroy: slot=P2 actorId=201 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] StateExit: clean=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] WorkerShutdown: clean=1 tid=44120")
        adapter.ingest_line("[KEX-HOOK] EngineShutdown: clean=1 code=0 workerStopped=1 tid=37472")

        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-01"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-02"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-03"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-04"], KexStatus.PASS)

    def test_kex_adapter_verifies_direct_quit_c25(self):
        """NativeKexAdapter marks PASS under Direct Quit C25 contract without requiring ActorDestroy."""
        adapter = NativeKexAdapter(telemetry_source=None, is_native_host=True)
        adapter.ingest_line("[KEX-HOOK] ActorSpawn: slot=P2 actorId=201 type=0 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] MotionTick: slot=P2 pos=(120.0,45.0,0.0) vel=1.0 inputSeq=1 netCmds=1 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] MotionTick: slot=P2 pos=(125.0,48.0,0.0) vel=1.0 inputSeq=2 netCmds=2 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] P1StateCheck: tick=10 slot0_hp=200 slot0_status=ALIVE gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] P1StateCheck: tick=20 slot0_hp=200 slot0_status=ALIVE gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] DirectQuitQuiesced: handleNull=1 hooksRestored=1 pinned=1 gen=1 tid=37472")
        adapter.ingest_line("[KEX-HOOK] WorkerShutdown: clean=1 tid=44120")
        adapter.ingest_line("[KEX-HOOK] EngineShutdown: clean=1 code=0 workerStopped=1 tid=37472")

        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-01"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-02"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-03"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-04"], KexStatus.PASS)
        self.assertIn("C25/DOC-43", [c.detail for c in checks if c.contract_id == "KEX-04"][0])

    def test_kex_adapter_rejects_legacy_incomplete_log(self):
        """NativeKexAdapter must REJECT old unhardened 4-line log (never false PASS)."""
        adapter = NativeKexAdapter(telemetry_source=None, is_native_host=True)
        adapter.ingest_line("[KEX-HOOK] ActorSpawn: slot=P2 actorId=201 type=0")
        adapter.ingest_line("[KEX-HOOK] MotionTick: slot=P2 pos=(120.0,45.0,0.0) vel=1.0")
        adapter.ingest_line("[KEX-HOOK] P1StateCheck: slot0_hp=200 slot0_status=ALIVE")
        adapter.ingest_line("[KEX-HOOK] ActorDestroy: slot=P2 actorId=201")

        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertNotEqual(statuses["KEX-02"], KexStatus.PASS, "KEX-02 must FAIL when unlinked from network input")
        self.assertNotEqual(statuses["KEX-03"], KexStatus.PASS, "KEX-03 must FAIL when backed by only 1 snapshot")
        self.assertNotEqual(statuses["KEX-04"], KexStatus.PASS, "KEX-04 must FAIL without network worker shutdown")
        self.assertEqual(statuses["KEX-02"], KexStatus.FAIL)
        self.assertEqual(statuses["KEX-03"], KexStatus.FAIL)
        self.assertEqual(statuses["KEX-04"], KexStatus.FAIL)

    def test_negative_01_actorspawn_wrong_slot(self):
        """Negative Gate 01: ActorSpawn for wrong slot (P3 != P2) must result in FAIL, NEVER PASS."""
        log_file = RUNNER_DIR / "test_data" / "synthetic_kex" / "01_actorspawn_wrong_slot.log"
        adapter = NativeKexAdapter(telemetry_source=log_file, is_native_host=True)
        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-01"], KexStatus.FAIL)
        self.assertNotEqual(statuses["KEX-01"], KexStatus.PASS)
        detail = [c.detail for c in checks if c.contract_id == "KEX-01"][0]
        self.assertIn("incorrect slot", detail)

    def test_negative_02_motiontick_unlinked_input(self):
        """Negative Gate 02: MotionTick without accepted network input link must result in FAIL, NEVER PASS."""
        log_file = RUNNER_DIR / "test_data" / "synthetic_kex" / "02_motiontick_unlinked_network_input.log"
        adapter = NativeKexAdapter(telemetry_source=log_file, is_native_host=True)
        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-02"], KexStatus.FAIL)
        self.assertNotEqual(statuses["KEX-02"], KexStatus.PASS)
        detail = [c.detail for c in checks if c.contract_id == "KEX-02"][0]
        self.assertIn("missing correlation to accepted network input", detail)

    def test_negative_03_stale_connection_generation(self):
        """Negative Gate 03: Telemetry from old connectionGeneration must result in FAIL, NEVER PASS."""
        log_file = RUNNER_DIR / "test_data" / "synthetic_kex" / "03_stale_connection_generation.log"
        # Test runner expects generation 2 (e.g. after reconnection), but log has gen 1
        adapter = NativeKexAdapter(telemetry_source=log_file, is_native_host=True, expected_generation=2)
        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-01"], KexStatus.FAIL)
        self.assertNotEqual(statuses["KEX-01"], KexStatus.PASS)
        detail = [c.detail for c in checks if c.contract_id == "KEX-01"][0]
        self.assertIn("stale connection generation", detail)

    def test_negative_04_wrong_thread_execution(self):
        """Negative Gate 04: Native updates executed on wrong thread (TID mismatch) must result in FAIL, NEVER PASS."""
        log_file = RUNNER_DIR / "test_data" / "synthetic_kex" / "04_wrong_thread_execution.log"
        # Simulation thread is 37472, but log operations executed on worker thread 44120
        adapter = NativeKexAdapter(telemetry_source=log_file, is_native_host=True, expected_tid=37472)
        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-01"], KexStatus.FAIL)
        self.assertNotEqual(statuses["KEX-01"], KexStatus.PASS)
        detail = [c.detail for c in checks if c.contract_id == "KEX-01"][0]
        self.assertIn("foreign thread", detail)

    def test_negative_05_p1_single_snapshot(self):
        """Negative Gate 05: P1StateCheck backed by only 1 snapshot must result in FAIL, NEVER PASS."""
        log_file = RUNNER_DIR / "test_data" / "synthetic_kex" / "05_p1_single_snapshot.log"
        adapter = NativeKexAdapter(telemetry_source=log_file, is_native_host=True)
        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-03"], KexStatus.FAIL)
        self.assertNotEqual(statuses["KEX-03"], KexStatus.PASS)
        detail = [c.detail for c in checks if c.contract_id == "KEX-03"][0]
        self.assertIn("only 1 snapshot observed", detail)

    def test_negative_06_missing_worker_shutdown(self):
        """Negative Gate 06: EngineShutdown without network worker shutdown proof must result in FAIL, NEVER PASS."""
        log_file = RUNNER_DIR / "test_data" / "synthetic_kex" / "06_engine_shutdown_missing_worker_shutdown.log"
        adapter = NativeKexAdapter(telemetry_source=log_file, is_native_host=True)
        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-04"], KexStatus.FAIL)
        self.assertNotEqual(statuses["KEX-04"], KexStatus.PASS)
        detail = [c.detail for c in checks if c.contract_id == "KEX-04"][0]
        self.assertIn("missing evidence of correct network worker shutdown", detail)

    def test_synthetic_files_valid_state_exit_and_direct_quit(self):
        """Synthetic valid files 07 and 08 must pass evaluation cleanly."""
        file_07 = RUNNER_DIR / "test_data" / "synthetic_kex" / "07_valid_state_exit_pass.log"
        a7 = NativeKexAdapter(telemetry_source=file_07, is_native_host=True)
        c7 = a7.evaluate_contracts(target_slot="P2")
        for c in c7:
            self.assertEqual(c.status, KexStatus.PASS, f"File 07 contract {c.contract_id} failed: {c.detail}")

        file_08 = RUNNER_DIR / "test_data" / "synthetic_kex" / "08_valid_direct_quit_c25_pass.log"
        a8 = NativeKexAdapter(telemetry_source=file_08, is_native_host=True)
        c8 = a8.evaluate_contracts(target_slot="P2")
        for c in c8:
            self.assertEqual(c.status, KexStatus.PASS, f"File 08 contract {c.contract_id} failed: {c.detail}")

    def test_kex_adapter_jsonl_ingestion(self):
        """NativeKexAdapter supports JSON lines format matching Sol's native.jsonl."""
        adapter = NativeKexAdapter(telemetry_source=None, is_native_host=True)
        adapter.ingest_line(json.dumps({
            "event": "independent_spawn",
            "tid": 37472,
            "generation": 1,
            "type": 0,
            "p2_actor": "0000000063241B70",
            "p1_health": 200
        }))
        adapter.ingest_line(json.dumps({
            "event": "movement_checkpoint",
            "tid": 37472,
            "generation": 1,
            "p2_pos": [-7418.87, -6563.54, 1152.0],
            "p2_commands": 120,
            "p1_health": 200
        }))
        adapter.ingest_line(json.dumps({
            "event": "owned_actor_dtor_return",
            "tid": 37472,
            "generation": 1,
            "actor": "0000000063241B70"
        }))
        adapter.ingest_line(json.dumps({
            "event": "worker_shutdown",
            "tid": 44120,
            "clean": True
        }))
        adapter.ingest_line(json.dumps({
            "event": "native_final_exit",
            "tid": 37472,
            "code": 0,
            "failed": False
        }))

        checks = adapter.evaluate_contracts(target_slot="P2")
        statuses = {c.contract_id: c.status for c in checks}
        self.assertEqual(statuses["KEX-01"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-02"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-03"], KexStatus.PASS)
        self.assertEqual(statuses["KEX-04"], KexStatus.PASS)

    def test_chaos_orchestrator_seed_determinism(self):
        """ChaosOrchestrator must produce the exact same sequence of decisions given the same seed."""
        ch1 = ChaosOrchestrator(client_exe=Path("dummy.exe"), seed=9999, max_clients=7, quiet=True)
        ch2 = ChaosOrchestrator(client_exe=Path("dummy.exe"), seed=9999, max_clients=7, quiet=True)

        seq1 = [ch1._decide_next_action() for _ in range(10)]
        seq2 = [ch2._decide_next_action() for _ in range(10)]
        self.assertEqual(seq1, seq2, "Decisions across identical seeds must be completely deterministic")

    def test_markdown_and_json_export_phase2(self):
        """Export functions must emit complete Markdown tables, badges, and valid JSON."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir)
            md_path = tmp_path / "TEST_REPORT.md"
            json_path = tmp_path / "summary.json"

            overlap = SessionOverlapAnalyzer.analyze_overlap([(0.0, 1.0), (0.1, 1.1)])

            suite = SuiteResult(
                suite_name="Unit Export Test",
                timestamp="2026-11-01 23:00:00",
                host="127.0.0.1",
                port=27015,
                client_count=2,
                mode="simultaneous",
                stagger_ms=0,
                scenario="hold-w",
                all_passed=True,
                total_duration_s=1.234,
                session_overlap=overlap,
                clients=[
                    ClientResult(
                        index=1,
                        participant_id="runner_p2",
                        display_name="Runner_P2",
                        pid=1234,
                        exit_code=0,
                        passed=True,
                        assigned_slot="P2",
                        slot_id=1,
                        session_id=777,
                        connection_generation=1,
                        pass_count=9,
                        fail_count=0,
                        host_confirmation="CONFIRMED",
                        host_detail="Host confirmed slot P2",
                        contracts=[
                            ContractResult("NET-01", "ENet transport", "PASS", 1.5, "OK")
                        ]
                    )
                ]
            )

            self.assertTrue(export_markdown_report([suite], md_path))
            self.assertTrue(export_json_summary([suite], json_path))

            self.assertTrue(md_path.is_file())
            self.assertTrue(json_path.is_file())

            md_text = md_path.read_text(encoding="utf-8")
            self.assertIn("PowerSlave Exhumed Together", md_text)
            self.assertIn("Multi-Client QA Phase 2 Report", md_text)
            self.assertIn("CONFIRMED", md_text)
            self.assertIn("KEX-01", md_text)
            self.assertIn("DEFERRED", md_text)

            json_data = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertTrue(json_data["allSuitesPassed"])
            self.assertEqual(len(json_data["suites"]), 1)
            self.assertEqual(json_data["suites"][0]["clients"][0]["host_confirmation"], "CONFIRMED")


if __name__ == "__main__":
    unittest.main()
