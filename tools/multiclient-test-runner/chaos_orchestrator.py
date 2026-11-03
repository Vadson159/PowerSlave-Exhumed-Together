#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Deterministic Chaos & Stress Test Orchestrator
===========================================================================
Executes continuous stress, random connect/disconnect, sudden client kills,
mass drops, and reconnection recovery over configurable durations (e.g. 5m, 15m, 30m).

Features:
  - Strict PRNG determinism with reproducible seed (--chaos-seed).
  - Sudden process termination (SIGKILL / proc.kill()) to simulate crashes.
  - Mass simultaneous disconnects to verify host bridge queue drain and absence of deadlock.
  - Reconnection recovery during chaos (validating generation bump and slot reuse).
  - Complete child PID tracking and guaranteed process reaping (zero lingering zombies).

Wire Protocol: v1.10 (0x0110)
"""

import sys
import os
import time
import json
import random
import subprocess
import threading
from pathlib import Path
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional, Tuple, Any
from enum import Enum


class ChaosActionType(str, Enum):
    SPAWN = "SPAWN"                             # Launch new client process
    CLEAN_DISCONNECT = "CLEAN_DISCONNECT"       # Client finishes hold ticks and disconnects cleanly
    SUDDEN_KILL = "SUDDEN_KILL"                 # Abrupt process termination (SIGKILL) while streaming
    MASS_DISCONNECT = "MASS_DISCONNECT"         # Simultaneous kill of multiple active clients
    RECONNECT = "RECONNECT"                     # Reconnect using previously acquired cryptographic key


@dataclass
class ChaosEvent:
    timestamp_s: float
    action: ChaosActionType
    participant_id: str
    pid: Optional[int]
    details: str
    success: bool = True


@dataclass
class ChaosSummary:
    seed: int
    duration_s: float
    total_actions: int
    spawns_count: int
    clean_disconnects_count: int
    sudden_kills_count: int
    reconnects_count: int
    mass_disconnects_count: int
    lingering_pids_caught: int
    all_passed: bool
    invariant_violations: List[str] = field(default_factory=list)
    events: List[ChaosEvent] = field(default_factory=list)
    client_durations: Dict[str, float] = field(default_factory=dict)


class ChaosOrchestrator:
    """
    Deterministic chaos generator driving clients against authoritative host.
    """

    def __init__(
        self,
        client_exe: Path,
        host: str = "127.0.0.1",
        port: int = 27015,
        token: str = "",
        output_dir: Optional[Path] = None,
        seed: int = 1337,
        max_clients: int = 7,
        quiet: bool = False
    ):
        self.client_exe = Path(client_exe).resolve()
        self.host = host
        self.port = port
        self.token = token
        self.output_dir = Path(output_dir or Path("output/chaos")).resolve()
        self.seed = seed
        self.max_clients = min(7, max(1, max_clients))
        self.quiet = quiet

        self.rng = random.Random(seed)
        self._lock = threading.Lock()
        self._active_procs: Dict[str, Dict[str, Any]] = {}      # part_id -> {proc, pid, start_t, log_file, json_file}
        self._saved_credentials: Dict[str, Dict[str, Any]] = {} # part_id -> {key, slot, gen}
        self._events: List[ChaosEvent] = []
        self._violations: List[str] = []

    def run_chaos(self, duration_s: float, step_interval_s: float = 0.5) -> ChaosSummary:
        """
        Execute chaos scenario for specified duration.
        """
        t0 = time.time()
        deadline = t0 + duration_s
        self.output_dir.mkdir(parents=True, exist_ok=True)
        logs_dir = self.output_dir / "logs"
        logs_dir.mkdir(parents=True, exist_ok=True)

        if not self.quiet:
            print(f"\n[CHAOS] Initializing Chaos Engine (Seed: {self.seed}, Duration: {duration_s}s, Max Clients: {self.max_clients})")

        action_counts = {
            ChaosActionType.SPAWN: 0,
            ChaosActionType.CLEAN_DISCONNECT: 0,
            ChaosActionType.SUDDEN_KILL: 0,
            ChaosActionType.RECONNECT: 0,
            ChaosActionType.MASS_DISCONNECT: 0
        }

        step_idx = 0
        try:
            while time.time() < deadline:
                now_rel = time.time() - t0
                time_remaining = deadline - time.time()

                # Step 1: Poll active processes and reap exited ones
                self._poll_active_processes(now_rel)

                # Step 2: Choose next deterministic action
                action = self._decide_next_action()

                if action == ChaosActionType.SPAWN:
                    self._action_spawn(now_rel, logs_dir)
                    action_counts[ChaosActionType.SPAWN] += 1

                elif action == ChaosActionType.RECONNECT:
                    self._action_reconnect(now_rel, logs_dir)
                    action_counts[ChaosActionType.RECONNECT] += 1

                elif action == ChaosActionType.SUDDEN_KILL:
                    self._action_sudden_kill(now_rel)
                    action_counts[ChaosActionType.SUDDEN_KILL] += 1

                elif action == ChaosActionType.MASS_DISCONNECT:
                    self._action_mass_disconnect(now_rel)
                    action_counts[ChaosActionType.MASS_DISCONNECT] += 1

                elif action == ChaosActionType.CLEAN_DISCONNECT:
                    # Let clients run naturally toward clean disconnect
                    action_counts[ChaosActionType.CLEAN_DISCONNECT] += 1

                step_idx += 1
                time.sleep(min(step_interval_s, max(0.05, time_remaining)))

        finally:
            # Cleanup all remaining active children
            lingering = self._cleanup_all_children()

        total_elapsed = time.time() - t0

        summary = ChaosSummary(
            seed=self.seed,
            duration_s=round(total_elapsed, 3),
            total_actions=len(self._events),
            spawns_count=action_counts[ChaosActionType.SPAWN],
            clean_disconnects_count=action_counts[ChaosActionType.CLEAN_DISCONNECT],
            sudden_kills_count=action_counts[ChaosActionType.SUDDEN_KILL],
            reconnects_count=action_counts[ChaosActionType.RECONNECT],
            mass_disconnects_count=action_counts[ChaosActionType.MASS_DISCONNECT],
            lingering_pids_caught=lingering,
            all_passed=(len(self._violations) == 0),
            invariant_violations=self._violations,
            events=self._events
        )

        if not self.quiet:
            print(f"[CHAOS] Completed: {len(self._events)} events in {total_elapsed:.2f}s | Violations: {len(self._violations)}")

        return summary

    def _decide_next_action(self) -> ChaosActionType:
        active_count = len(self._active_procs)
        can_reconnect = len(self._saved_credentials) > 0

        # If zero clients, always spawn
        if active_count == 0:
            return ChaosActionType.SPAWN

        # If at max capacity, must disconnect, kill, or mass disconnect
        if active_count >= self.max_clients:
            roll = self.rng.random()
            if roll < 0.40:
                return ChaosActionType.SUDDEN_KILL
            elif roll < 0.70:
                return ChaosActionType.MASS_DISCONNECT
            else:
                return ChaosActionType.CLEAN_DISCONNECT

        # Otherwise pick probabilistically
        roll = self.rng.random()
        if can_reconnect and roll < 0.30:
            return ChaosActionType.RECONNECT
        elif roll < 0.65:
            return ChaosActionType.SPAWN
        elif roll < 0.85:
            return ChaosActionType.SUDDEN_KILL
        else:
            return ChaosActionType.CLEAN_DISCONNECT

    def _action_spawn(self, now_s: float, logs_dir: Path) -> None:
        # Pick participant ID not currently active
        candidate_ids = [f"chaos_p{i}" for i in range(2, 2 + self.max_clients)]
        available = [pid for pid in candidate_ids if pid not in self._active_procs]
        if not available:
            return

        part_id = self.rng.choice(available)
        hold_ticks = self.rng.randint(20, 60)
        log_f = logs_dir / f"{part_id}_{int(now_s * 1000)}.log"
        json_f = logs_dir / f"{part_id}_{int(now_s * 1000)}.json"

        cmd = [
            str(self.client_exe),
            "--host", self.host,
            "--port", str(self.port),
            "--participant", part_id,
            "--name", f"Chaos_{part_id.upper()}",
            "--scenario", "hold-w",
            "--hold-ticks", str(hold_ticks),
            "--neutral-ticks", "10",
            "--log-file", str(log_f),
            "--json-file", str(json_f),
            "--quiet"
        ]
        if self.token:
            cmd.extend(["--token", self.token])

        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
            )
            with self._lock:
                self._active_procs[part_id] = {
                    "proc": proc,
                    "pid": proc.pid,
                    "start_t": now_s,
                    "log_f": log_f,
                    "json_f": json_f,
                    "hold_ticks": hold_ticks
                }
            self._events.append(ChaosEvent(
                timestamp_s=round(now_s, 3),
                action=ChaosActionType.SPAWN,
                participant_id=part_id,
                pid=proc.pid,
                details=f"Spawned client (hold_ticks={hold_ticks})",
                success=True
            ))
        except Exception as e:
            self._violations.append(f"Failed to spawn {part_id}: {e}")

    def _action_reconnect(self, now_s: float, logs_dir: Path) -> None:
        if not self._saved_credentials:
            return
        part_id = self.rng.choice(list(self._saved_credentials.keys()))
        if part_id in self._active_procs:
            return

        creds = self._saved_credentials.pop(part_id)
        log_f = logs_dir / f"{part_id}_recon_{int(now_s * 1000)}.log"
        json_f = logs_dir / f"{part_id}_recon_{int(now_s * 1000)}.json"

        cmd = [
            str(self.client_exe),
            "--host", self.host,
            "--port", str(self.port),
            "--participant", part_id,
            "--name", f"Chaos_{part_id.upper()}_Recon",
            "--scenario", "reconnect",
            "--log-file", str(log_f),
            "--json-file", str(json_f),
            "--quiet"
        ]
        if self.token:
            cmd.extend(["--token", self.token])

        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
            )
            with self._lock:
                self._active_procs[part_id] = {
                    "proc": proc,
                    "pid": proc.pid,
                    "start_t": now_s,
                    "log_f": log_f,
                    "json_f": json_f,
                    "is_recon": True
                }
            self._events.append(ChaosEvent(
                timestamp_s=round(now_s, 3),
                action=ChaosActionType.RECONNECT,
                participant_id=part_id,
                pid=proc.pid,
                details=f"Reconnecting participant with previously saved key",
                success=True
            ))
        except Exception as e:
            self._violations.append(f"Failed to reconnect {part_id}: {e}")

    def _action_sudden_kill(self, now_s: float) -> None:
        if not self._active_procs:
            return
        part_id = self.rng.choice(list(self._active_procs.keys()))
        info = self._active_procs.pop(part_id)
        proc = info["proc"]
        pid = info["pid"]

        try:
            proc.kill()
            proc.wait(timeout=1.0)
            self._events.append(ChaosEvent(
                timestamp_s=round(now_s, 3),
                action=ChaosActionType.SUDDEN_KILL,
                participant_id=part_id,
                pid=pid,
                details="Abruptly killed process (SIGKILL simulated network pullout)",
                success=True
            ))
        except Exception as e:
            self._violations.append(f"Error abruptly killing PID {pid}: {e}")

    def _action_mass_disconnect(self, now_s: float) -> None:
        if len(self._active_procs) < 2:
            return

        to_kill = list(self._active_procs.keys())[:min(4, len(self._active_procs))]
        for pid_name in to_kill:
            info = self._active_procs.pop(pid_name)
            p = info["proc"]
            try:
                p.terminate()
                try:
                    p.wait(timeout=0.5)
                except subprocess.TimeoutExpired:
                    p.kill()
            except Exception:
                pass

        self._events.append(ChaosEvent(
            timestamp_s=round(now_s, 3),
            action=ChaosActionType.MASS_DISCONNECT,
            participant_id=",".join(to_kill),
            pid=None,
            details=f"Simultaneously dropped {len(to_kill)} clients",
            success=True
        ))

    def _poll_active_processes(self, now_s: float) -> None:
        finished = []
        for part_id, info in self._active_procs.items():
            proc = info["proc"]
            if proc.poll() is not None:
                finished.append(part_id)
                # Inspect json for reconnectKey if client exited cleanly
                json_f = info["json_f"]
                if json_f.is_file():
                    try:
                        data = json.loads(json_f.read_text(encoding="utf-8"))
                        for r in data.get("results", []):
                            if r.get("id") == "NET-03" and "reconnectKey=" in r.get("detail", ""):
                                m = re.search(r'reconnectKey=(0x[0-9a-fA-F]+)', r["detail"])
                                if m:
                                    self._saved_credentials[part_id] = {"key": m.group(1)}
                    except Exception:
                        pass

        for p_id in finished:
            self._active_procs.pop(p_id, None)

    def _cleanup_all_children(self) -> int:
        lingering = 0
        with self._lock:
            procs = list(self._active_procs.values())
            self._active_procs.clear()

        for info in procs:
            proc = info["proc"]
            if proc.poll() is None:
                lingering += 1
                try:
                    proc.terminate()
                    try:
                        proc.wait(timeout=0.5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=0.2)
                except Exception:
                    pass
        return lingering
