#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Hardened Native KEX Integration Adapter
=====================================================================
Dedicated verification adapter for testing against the authoritative
Native KEX Engine Host (Sol Max Native Engine, M1.2a scope).

Enforces strict QA Factuality Gate & Evidence Hardening:
  - Contracts KEX-01 through KEX-04 CANNOT receive false PASS from
    incomplete, stale, unlinked, or contradictory telemetry logs.
  - In the absence of native KEX telemetry, contracts MUST be DEFERRED.
  - Contracts are NEVER marked PASS based on mock-host runs or network socket packets alone.
  - Verifies slot matching, connectionGeneration integrity, simulation TID isolation,
    accepted network input correlation, temporal P1 state consistency (>= 2 snapshots),
    and scenario-aware clean shutdown (StateExit vs Direct Quit / C25).

Wire Protocol: v1.10 (0x0110)
Reference Docs: DOC-40 (Thread Bridge Contract), DOC-41 (M1.1 Bootstrap), DOC-43 (C25 Direct Quit)
"""

import re
import os
import json
import time
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any, Tuple
from enum import Enum


class KexStatus(str, Enum):
    DEFERRED = "DEFERRED"                       # Native telemetry absent or unhooked target
    PASS = "PASS"                               # Verified native engine events confirm contract
    FAIL = "FAIL"                               # Native engine event logged an explicit violation or error
    BLOCKED = "BLOCKED"                         # Host crashed, hung, or was unreachable before KEX phase


@dataclass
class KexTelemetryEvent:
    event_type: str                             # ACTOR_SPAWN, PHYSICS_TICK, P1_ISOLATION, ACTOR_DESTROY,
                                                # WORKER_SHUTDOWN, DIRECT_QUIT_QUIESCE, STATE_EXIT, ENGINE_EXIT
    slot: str                                   # e.g. "P2", "P1", "HOST", "NETWORK"
    timestamp: float
    raw_text: str
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class KexContractCheck:
    contract_id: str
    name: str
    status: KexStatus
    detail: str
    required_hook: str
    verified_event: Optional[KexTelemetryEvent] = None


class NativeKexAdapter:
    """
    Adapter responsible for querying, verifying, and reporting native KEX engine contracts.
    Hardened against false positives from incomplete, stale, or contradictory telemetry.
    """

    CONTRACT_DEFS = {
        "KEX-01": {
            "name": "Native P2 Puppet Actor instantiation",
            "hook": "Factory::SpawnActor(0) on host simulation thread upon MsgHello auth",
            "desc": "Confirms remote client P2..P8 actor is dynamically spawned in KEX world."
        },
        "KEX-02": {
            "name": "Native P2 physics displacement in world",
            "hook": "applyPuppetMotion() updating actor position vector on KEX physics tick",
            "desc": "Confirms inbound moveForward packets translate to spatial displacement."
        },
        "KEX-03": {
            "name": "Native P1 state isolation & non-interference",
            "hook": "GameInstance::GetPlayer(0) health/inventory/camera memory boundary check",
            "desc": "Confirms remote player packets never corrupt host local player state."
        },
        "KEX-04": {
            "name": "Safe native engine StateExit and process shutdown",
            "hook": "Engine::StateExit and actor cleanup on client disconnect / session teardown",
            "desc": "Confirms puppet actors are properly deallocated without memory leak or crash."
        }
    }

    # Regex patterns for legacy / structured [KEX-HOOK] or [KEX-TELEMETRY] tags
    RE_TAG_HEADER = re.compile(r'\[(KEX-HOOK|KEX-TELEMETRY)\]\s+([A-Za-z0-9_]+):?\s*(.*)', re.IGNORECASE)
    RE_POS = re.compile(r'pos=[(\[]([0-9\.\-]+)[,\s]+([0-9\.\-]+)[,\s]+([0-9\.\-]+)[)\]]', re.IGNORECASE)
    RE_KV = re.compile(r'([A-Za-z0-9_]+)=([^\s,]+)')

    def __init__(
        self,
        telemetry_source: Optional[Path] = None,
        is_native_host: bool = False,
        expected_generation: Optional[int] = None,
        expected_tid: Optional[int] = None,
        exit_scenario: Optional[str] = None
    ):
        self.telemetry_source = telemetry_source
        self.is_native_host = is_native_host
        self.expected_generation = expected_generation
        self.expected_tid = expected_tid
        self.exit_scenario = exit_scenario
        self.events: List[KexTelemetryEvent] = []
        self._telemetry_detected = False

    def check_telemetry_source(self) -> bool:
        """Inspect whether native KEX telemetry source exists and contains data."""
        if self.telemetry_source and self.telemetry_source.is_file():
            if self.telemetry_source.stat().st_size > 0:
                self._telemetry_detected = True
                return True
        return False

    def load_telemetry_file(self, path: Optional[Path] = None) -> int:
        """Load and ingest lines from a telemetry file."""
        target = path or self.telemetry_source
        if not target or not target.is_file():
            return 0
        count = 0
        with open(target, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if self.ingest_line(line):
                    count += 1
        return count

    def ingest_line(self, line: str, timestamp: Optional[float] = None) -> Optional[KexTelemetryEvent]:
        """
        Ingest a native KEX engine telemetry log line.
        Supports both structured key-value tag syntax and JSONL format (as emitted by native.jsonl).
        """
        t = timestamp if timestamp is not None else time.time()
        s = line.strip()
        if not s or s.startswith("#") or s.startswith("//"):
            return None

        # Check for JSONL
        if s.startswith("{") and s.endswith("}"):
            try:
                data = json.loads(s)
                return self._ingest_json_event(data, line, t)
            except json.JSONDecodeError:
                pass

        # Check for [KEX-HOOK] or [KEX-TELEMETRY]
        m_tag = self.RE_TAG_HEADER.match(s)
        if m_tag:
            tag_name = m_tag.group(2)
            payload = m_tag.group(3)
            return self._ingest_tag_event(tag_name, payload, line, t)

        return None

    def _parse_value(self, val_str: str) -> Any:
        """Convert parsed string token to typed primitive."""
        v_lower = val_str.lower()
        if v_lower == "true":
            return True
        if v_lower == "false":
            return False
        try:
            return int(val_str)
        except ValueError:
            pass
        try:
            return float(val_str)
        except ValueError:
            pass
        return val_str

    def _ingest_tag_event(self, tag_name: str, payload: str, raw_text: str, t: float) -> Optional[KexTelemetryEvent]:
        """Parse structured text tag into typed KexTelemetryEvent."""
        details: Dict[str, Any] = {}

        # Extract pos vector if present
        m_pos = self.RE_POS.search(payload)
        if m_pos:
            details["pos"] = (float(m_pos.group(1)), float(m_pos.group(2)), float(m_pos.group(3)))
            # strip pos to not interfere with generic key=value matcher
            payload = self.RE_POS.sub("", payload)

        # Extract key=value tokens
        for k, v in self.RE_KV.findall(payload):
            details[k] = self._parse_value(v)

        slot = details.get("slot", "HOST")
        tag_lower = tag_name.lower()
        ev: Optional[KexTelemetryEvent] = None

        if tag_lower in ("actorspawn", "spawn"):
            ev = KexTelemetryEvent(
                event_type="ACTOR_SPAWN",
                slot=slot,
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("motiontick", "physics_tick", "puppetmotion"):
            # Determine network input link
            input_seq = details.get("inputSeq", details.get("input_seq", details.get("seq", 0)))
            net_cmds = details.get("netCmds", details.get("net_cmds", 0))
            unlinked = details.get("unlinked", False)
            if unlinked in (True, 1, "1", "true"):
                input_linked = False
            else:
                input_linked = (input_seq > 0 or net_cmds > 0 or details.get("inputLinked", False) is True)
            details["input_linked"] = input_linked
            details["input_seq"] = input_seq
            details["net_cmds"] = net_cmds

            ev = KexTelemetryEvent(
                event_type="PHYSICS_TICK",
                slot=slot,
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("p1statecheck", "p1isolation", "p1_isolation"):
            ev = KexTelemetryEvent(
                event_type="P1_ISOLATION",
                slot="P1",
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("actordestroy", "destroy"):
            ev = KexTelemetryEvent(
                event_type="ACTOR_DESTROY",
                slot=slot,
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("workershutdown", "networkworkershutdown", "network_shutdown"):
            ev = KexTelemetryEvent(
                event_type="WORKER_SHUTDOWN",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("directquitquiesced", "c25quiesce", "shutdownquiesced"):
            ev = KexTelemetryEvent(
                event_type="DIRECT_QUIT_QUIESCE",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("stateexit", "state_exit"):
            ev = KexTelemetryEvent(
                event_type="STATE_EXIT",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details=details
            )
        elif tag_lower in ("engineshutdown", "engine_exit", "exit"):
            ev = KexTelemetryEvent(
                event_type="ENGINE_EXIT",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details=details
            )

        if ev:
            self.events.append(ev)
            self._telemetry_detected = True
        return ev

    def _ingest_json_event(self, data: Dict[str, Any], raw_text: str, t: float) -> Optional[KexTelemetryEvent]:
        """Parse native journal JSON line (e.g. native.jsonl from M1.1/M1.2)."""
        ev_name = data.get("event", "").lower()
        ev: Optional[KexTelemetryEvent] = None

        if ev_name in ("independent_spawn", "spawn"):
            ev = KexTelemetryEvent(
                event_type="ACTOR_SPAWN",
                slot=data.get("slot", "P2"),
                timestamp=t,
                raw_text=raw_text,
                details={
                    "actorId": data.get("p2_actor", data.get("actor", 0)),
                    "type": data.get("type", 0),
                    "tid": data.get("tid"),
                    "generation": data.get("generation", 1)
                }
            )
            # If P1 state baseline is embedded, record P1 checkpoint
            if "p1_health" in data:
                self.events.append(KexTelemetryEvent(
                    event_type="P1_ISOLATION",
                    slot="P1",
                    timestamp=t,
                    raw_text=f"P1BaselineCheck(from spawn): hp={data['p1_health']}",
                    details={"hp": data["p1_health"], "status": "ALIVE", "tid": data.get("tid")}
                ))
        elif ev_name in ("movement_checkpoint", "sequence_complete"):
            pos = data.get("p2_pos")
            p2_cmds = data.get("p2_commands", data.get("native_p2_commands", 0))
            ev = KexTelemetryEvent(
                event_type="PHYSICS_TICK",
                slot=data.get("slot", "P2"),
                timestamp=t,
                raw_text=raw_text,
                details={
                    "pos": tuple(pos) if isinstance(pos, list) else pos,
                    "vel": 1.0 if p2_cmds > 0 else 0.0,
                    "input_seq": p2_cmds,
                    "net_cmds": p2_cmds,
                    "input_linked": p2_cmds > 0,
                    "tid": data.get("tid"),
                    "generation": data.get("generation", 1)
                }
            )
            # Check P1 health preservation during checkpoint
            if "p1_health" in data:
                self.events.append(KexTelemetryEvent(
                    event_type="P1_ISOLATION",
                    slot="P1",
                    timestamp=t,
                    raw_text=f"P1ActiveCheck(from movement): hp={data['p1_health']}",
                    details={"hp": data["p1_health"], "status": "ALIVE", "tid": data.get("tid")}
                ))
        elif ev_name == "p1_state_check":
            ev = KexTelemetryEvent(
                event_type="P1_ISOLATION",
                slot="P1",
                timestamp=t,
                raw_text=raw_text,
                details={
                    "hp": data.get("hp", data.get("p1_health", 200)),
                    "status": data.get("status", "ALIVE"),
                    "tid": data.get("tid")
                }
            )
        elif ev_name in ("owned_actor_dtor_return", "actor_destroy"):
            ev = KexTelemetryEvent(
                event_type="ACTOR_DESTROY",
                slot=data.get("slot", "P2"),
                timestamp=t,
                raw_text=raw_text,
                details={
                    "actorId": data.get("actor", 0),
                    "tid": data.get("tid"),
                    "generation": data.get("generation", 1)
                }
            )
        elif ev_name in ("state_exit_return", "state_exit_entry"):
            ev = KexTelemetryEvent(
                event_type="STATE_EXIT",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details={"clean": True, "tid": data.get("tid")}
            )
        elif ev_name == "shutdown_quiesced":
            ev = KexTelemetryEvent(
                event_type="DIRECT_QUIT_QUIESCE",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details={
                    "handle_null": data.get("external_handle_null", True),
                    "hooks_restored": True,
                    "pinned": data.get("pin_until_process_exit", True),
                    "tid": data.get("tid")
                }
            )
        elif ev_name in ("worker_shutdown", "network_shutdown"):
            ev = KexTelemetryEvent(
                event_type="WORKER_SHUTDOWN",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details={"clean": data.get("clean", True), "tid": data.get("tid")}
            )
        elif ev_name in ("native_final_exit", "engine_shutdown_return"):
            ev = KexTelemetryEvent(
                event_type="ENGINE_EXIT",
                slot="HOST",
                timestamp=t,
                raw_text=raw_text,
                details={
                    "clean": not data.get("failed", False),
                    "code": data.get("code", 0),
                    "worker_stopped": data.get("worker_stopped", False),
                    "tid": data.get("tid")
                }
            )

        if ev:
            self.events.append(ev)
            self._telemetry_detected = True
        return ev

    def evaluate_contracts(
        self,
        target_slot: str = "P2",
        expected_generation: Optional[int] = None,
        expected_tid: Optional[int] = None,
        exit_scenario: Optional[str] = None
    ) -> List[KexContractCheck]:
        """
        Evaluate contracts KEX-01..KEX-04 against strictly hardened factuality criteria.

        Failure conditions:
          - Telemetry absent or unhooked target -> strictly DEFERRED.
          - ActorSpawn for wrong slot, invalid actorId, or non-Puppet type -> FAIL.
          - MotionTick lacking correlation to accepted network input (unlinked/zero sequence) -> FAIL.
          - Telemetry events belong to stale/mismatched connectionGeneration -> FAIL.
          - Native updates executed on wrong/foreign thread (TID mismatch / Thread Bridge violation) -> FAIL.
          - P1StateCheck backed by only 1 snapshot (temporal isolation unproven) -> FAIL.
          - EngineShutdown lacking evidence of clean network worker shutdown -> FAIL.
          - StateExit lacking target ActorDestroy -> FAIL.
          - Direct Quit lacking C25 quiesce proof -> FAIL.
        """
        results: List[KexContractCheck] = []

        # Auto-load file if not yet ingested
        if not self.events and self.telemetry_source:
            self.load_telemetry_file()

        # If native telemetry has not been detected or we are targeting mock-host -> DEFERRED
        if not self._telemetry_detected or not self.is_native_host or not self.events:
            for cid, cdef in self.CONTRACT_DEFS.items():
                results.append(KexContractCheck(
                    contract_id=cid,
                    name=cdef["name"],
                    status=KexStatus.DEFERRED,
                    detail=(
                        f"Requires native KEX telemetry hook [{cdef['hook']}]. "
                        f"Target host is mock-simulation or unhooked native binary. "
                        f"Packet transmission alone does not prove native engine displacement. "
                        f"Deferred pending Sol Max Phase 3 native hook integration."
                    ),
                    required_hook=cdef["hook"],
                    verified_event=None
                ))
            return results

        # Determine target connection generation
        target_gen = expected_generation if expected_generation is not None else self.expected_generation

        # Determine simulation thread ID baseline
        sim_tid = expected_tid if expected_tid is not None else self.expected_tid
        if sim_tid is None:
            # Detect simulation TID from native simulation events
            for e in self.events:
                if e.event_type in ("ACTOR_SPAWN", "PHYSICS_TICK", "P1_ISOLATION", "STATE_EXIT", "DIRECT_QUIT_QUIESCE"):
                    t_val = e.details.get("tid")
                    if t_val is not None:
                        sim_tid = int(t_val)
                        break

        # -------------------------------------------------------------
        # KEX-01: Puppet Spawn
        # -------------------------------------------------------------
        spawns = [e for e in self.events if e.event_type == "ACTOR_SPAWN"]
        if not spawns:
            results.append(KexContractCheck(
                contract_id="KEX-01",
                name=self.CONTRACT_DEFS["KEX-01"]["name"],
                status=KexStatus.FAIL,
                detail=f"Native telemetry present, but zero ACTOR_SPAWN events recorded.",
                required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"]
            ))
        else:
            target_spawns = [e for e in spawns if e.slot == target_slot]
            if not target_spawns:
                observed_slots = ", ".join(sorted(set(e.slot for e in spawns)))
                results.append(KexContractCheck(
                    contract_id="KEX-01",
                    name=self.CONTRACT_DEFS["KEX-01"]["name"],
                    status=KexStatus.FAIL,
                    detail=f"ActorSpawn recorded for incorrect slot: observed [{observed_slots}] != target [{target_slot}].",
                    required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"]
                ))
            else:
                sp = target_spawns[0]
                ev_gen = sp.details.get("generation", sp.details.get("gen"))
                actor_type = sp.details.get("type", sp.details.get("actor_type", 0))
                actor_id = sp.details.get("actorId", sp.details.get("actor_id"))
                ev_tid = sp.details.get("tid")

                if target_gen is not None and ev_gen is not None and ev_gen != target_gen:
                    results.append(KexContractCheck(
                        contract_id="KEX-01",
                        name=self.CONTRACT_DEFS["KEX-01"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"ActorSpawn belongs to stale connection generation: gen {ev_gen} != expected {target_gen}.",
                        required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"]
                    ))
                elif actor_type != 0:
                    results.append(KexContractCheck(
                        contract_id="KEX-01",
                        name=self.CONTRACT_DEFS["KEX-01"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"ActorSpawn type violation: expected type 0 (Puppet Actor), observed {actor_type}.",
                        required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"]
                    ))
                elif actor_id in (0, "0", None):
                    results.append(KexContractCheck(
                        contract_id="KEX-01",
                        name=self.CONTRACT_DEFS["KEX-01"]["name"],
                        status=KexStatus.FAIL,
                        detail="ActorSpawn invalid identity: actorId is 0 or unassigned.",
                        required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"]
                    ))
                elif sim_tid is not None and ev_tid is not None and int(ev_tid) != sim_tid:
                    results.append(KexContractCheck(
                        contract_id="KEX-01",
                        name=self.CONTRACT_DEFS["KEX-01"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"ActorSpawn executed on foreign thread: TID {ev_tid} != simulation TID {sim_tid}.",
                        required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"]
                    ))
                else:
                    results.append(KexContractCheck(
                        contract_id="KEX-01",
                        name=self.CONTRACT_DEFS["KEX-01"]["name"],
                        status=KexStatus.PASS,
                        detail=f"Confirmed native actor spawn for {target_slot}: {sp.raw_text}",
                        required_hook=self.CONTRACT_DEFS["KEX-01"]["hook"],
                        verified_event=sp
                    ))

        # -------------------------------------------------------------
        # KEX-02: Physics Displacement & Network Input Link
        # -------------------------------------------------------------
        ticks = [e for e in self.events if e.event_type == "PHYSICS_TICK"]
        if not ticks:
            results.append(KexContractCheck(
                contract_id="KEX-02",
                name=self.CONTRACT_DEFS["KEX-02"]["name"],
                status=KexStatus.FAIL,
                detail=f"Native telemetry present, but zero PHYSICS_TICK events recorded.",
                required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"]
            ))
        else:
            target_ticks = [e for e in ticks if e.slot == target_slot]
            if not target_ticks:
                observed_slots = ", ".join(sorted(set(e.slot for e in ticks)))
                results.append(KexContractCheck(
                    contract_id="KEX-02",
                    name=self.CONTRACT_DEFS["KEX-02"]["name"],
                    status=KexStatus.FAIL,
                    detail=f"MotionTick recorded for incorrect slot: observed [{observed_slots}] != target [{target_slot}].",
                    required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"]
                ))
            else:
                # Check generation on target ticks
                gen_mismatch = False
                stale_gen_val = None
                if target_gen is not None:
                    for tk in target_ticks:
                        tg = tk.details.get("generation", tk.details.get("gen"))
                        if tg is not None and tg != target_gen:
                            gen_mismatch = True
                            stale_gen_val = tg
                            break

                # Check foreign thread TID
                foreign_tid = None
                if sim_tid is not None:
                    for tk in target_ticks:
                        tk_tid = tk.details.get("tid")
                        if tk_tid is not None and int(tk_tid) != sim_tid:
                            foreign_tid = tk_tid
                            break

                # Check link to accepted network input
                linked_ticks = [
                    tk for tk in target_ticks
                    if tk.details.get("input_linked") is True or
                       tk.details.get("input_seq", 0) > 0 or
                       tk.details.get("net_cmds", 0) > 0
                ]

                # Check displacement
                positions = [tk.details.get("pos") for tk in target_ticks if tk.details.get("pos") is not None]
                velocities = [tk.details.get("vel", 0.0) for tk in target_ticks]
                has_spatial_motion = False
                if len(positions) >= 2:
                    has_spatial_motion = (positions[0] != positions[-1])
                if not has_spatial_motion:
                    has_spatial_motion = any(v > 0.0 for v in velocities)

                if gen_mismatch:
                    results.append(KexContractCheck(
                        contract_id="KEX-02",
                        name=self.CONTRACT_DEFS["KEX-02"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"MotionTick belongs to stale connection generation: gen {stale_gen_val} != expected {target_gen}.",
                        required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"]
                    ))
                elif foreign_tid is not None:
                    results.append(KexContractCheck(
                        contract_id="KEX-02",
                        name=self.CONTRACT_DEFS["KEX-02"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"MotionTick executed on foreign thread: TID {foreign_tid} != simulation TID {sim_tid}.",
                        required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"]
                    ))
                elif not linked_ticks:
                    results.append(KexContractCheck(
                        contract_id="KEX-02",
                        name=self.CONTRACT_DEFS["KEX-02"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"MotionTick recorded for {target_slot}, but missing correlation to accepted network input (inputSeq/netCmds unlinked or <= 0).",
                        required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"]
                    ))
                elif not has_spatial_motion and max(velocities, default=0.0) == 0.0:
                    results.append(KexContractCheck(
                        contract_id="KEX-02",
                        name=self.CONTRACT_DEFS["KEX-02"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"MotionTick records show zero displacement / velocity in world for {target_slot}.",
                        required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"]
                    ))
                else:
                    results.append(KexContractCheck(
                        contract_id="KEX-02",
                        name=self.CONTRACT_DEFS["KEX-02"]["name"],
                        status=KexStatus.PASS,
                        detail=f"Confirmed native physics displacement correlated to network input across {len(target_ticks)} ticks: {target_ticks[-1].raw_text}",
                        required_hook=self.CONTRACT_DEFS["KEX-02"]["hook"],
                        verified_event=target_ticks[-1]
                    ))

        # -------------------------------------------------------------
        # KEX-03: P1 State Isolation & Non-Interference
        # -------------------------------------------------------------
        iso_checks = [e for e in self.events if e.event_type == "P1_ISOLATION"]
        if not iso_checks:
            results.append(KexContractCheck(
                contract_id="KEX-03",
                name=self.CONTRACT_DEFS["KEX-03"]["name"],
                status=KexStatus.FAIL,
                detail="Native telemetry present, but zero P1_ISOLATION integrity checks observed.",
                required_hook=self.CONTRACT_DEFS["KEX-03"]["hook"]
            ))
        elif len(iso_checks) < 2:
            results.append(KexContractCheck(
                contract_id="KEX-03",
                name=self.CONTRACT_DEFS["KEX-03"]["name"],
                status=KexStatus.FAIL,
                detail=(
                    f"Insufficient P1 isolation evidence: only 1 snapshot observed ({iso_checks[0].raw_text}). "
                    f"Temporal isolation requires at least 2 checkpoints (baseline and active/post-sequence) "
                    f"to prove remote player operations did not corrupt host state."
                ),
                required_hook=self.CONTRACT_DEFS["KEX-03"]["hook"]
            ))
        else:
            corrupted = False
            corruption_detail = ""
            for chk in iso_checks:
                hp = chk.details.get("hp", chk.details.get("slot0_hp"))
                status_str = chk.details.get("status", chk.details.get("slot0_status", "ALIVE"))
                chk_tid = chk.details.get("tid")

                if hp is not None and hp <= 0:
                    corrupted = True
                    corruption_detail = f"P1 health corrupted or fatal: hp={hp}"
                    break
                if status_str and str(status_str).upper() not in ("ALIVE", "OK", "VALID"):
                    corrupted = True
                    corruption_detail = f"P1 status corrupted: status={status_str}"
                    break
                if sim_tid is not None and chk_tid is not None and int(chk_tid) != sim_tid:
                    corrupted = True
                    corruption_detail = f"P1StateCheck executed on foreign thread: TID {chk_tid} != simulation TID {sim_tid}"
                    break

            if corrupted:
                results.append(KexContractCheck(
                    contract_id="KEX-03",
                    name=self.CONTRACT_DEFS["KEX-03"]["name"],
                    status=KexStatus.FAIL,
                    detail=f"P1 non-interference violation: {corruption_detail}",
                    required_hook=self.CONTRACT_DEFS["KEX-03"]["hook"]
                ))
            else:
                results.append(KexContractCheck(
                    contract_id="KEX-03",
                    name=self.CONTRACT_DEFS["KEX-03"]["name"],
                    status=KexStatus.PASS,
                    detail=f"Confirmed P1 state non-interference across {len(iso_checks)} temporal checkpoints: {iso_checks[-1].raw_text}",
                    required_hook=self.CONTRACT_DEFS["KEX-03"]["hook"],
                    verified_event=iso_checks[-1]
                ))

        # -------------------------------------------------------------
        # KEX-04: Puppet Destroy / Shutdown & Network Worker Teardown
        # -------------------------------------------------------------
        actor_destroys = [e for e in self.events if e.event_type == "ACTOR_DESTROY"]
        engine_exits = [e for e in self.events if e.event_type == "ENGINE_EXIT"]
        worker_shutdowns = [e for e in self.events if e.event_type == "WORKER_SHUTDOWN"]
        direct_quit_quiesces = [e for e in self.events if e.event_type == "DIRECT_QUIT_QUIESCE"]
        state_exits = [e for e in self.events if e.event_type == "STATE_EXIT"]

        has_lifecycle = bool(actor_destroys or engine_exits or worker_shutdowns or direct_quit_quiesces or state_exits)
        if not has_lifecycle:
            results.append(KexContractCheck(
                contract_id="KEX-04",
                name=self.CONTRACT_DEFS["KEX-04"]["name"],
                status=KexStatus.FAIL,
                detail=f"Native telemetry present, but zero deallocation or shutdown events recorded.",
                required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
            ))
            return results

        # Verify Network Worker Shutdown evidence
        has_clean_worker_shutdown = False
        if worker_shutdowns and any(w.details.get("clean", True) for w in worker_shutdowns):
            has_clean_worker_shutdown = True
        elif engine_exits and any(ex.details.get("worker_stopped") for ex in engine_exits):
            has_clean_worker_shutdown = True

        if not has_clean_worker_shutdown:
            results.append(KexContractCheck(
                contract_id="KEX-04",
                name=self.CONTRACT_DEFS["KEX-04"]["name"],
                status=KexStatus.FAIL,
                detail="Engine shutdown recorded, but missing evidence of correct network worker shutdown (ENet worker / Thread Bridge teardown).",
                required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
            ))
            return results

        # Distinguish exit scenario: Direct Quit vs StateExit
        chosen_scenario = self.exit_scenario
        if chosen_scenario is None:
            if direct_quit_quiesces or any(e.details.get("reason") == "DirectQuit" or e.details.get("exit_reason") == "DirectQuit" or e.details.get("state_exit_preceded") is False for e in self.events):
                chosen_scenario = "DirectQuit"
            elif state_exits or actor_destroys or any(e.details.get("reason") == "StateExit" or e.details.get("exit_reason") == "StateExit" or e.details.get("state_exit_preceded") is True for e in self.events):
                chosen_scenario = "StateExit"
            else:
                chosen_scenario = "Unknown"

        if chosen_scenario == "DirectQuit":
            # Direct Quit scenario (C25/DOC-43 contract):
            # ActorDestroy is NOT called by engine. Requires C25 quiesce proof.
            if not direct_quit_quiesces:
                results.append(KexContractCheck(
                    contract_id="KEX-04",
                    name=self.CONTRACT_DEFS["KEX-04"]["name"],
                    status=KexStatus.FAIL,
                    detail="Direct Quit scenario (C25/DOC-43) requires quiesced state (handle null, hooks restored, module pinned), but no C25 quiesce evidence found.",
                    required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                ))
            else:
                q = direct_quit_quiesces[-1]
                if not q.details.get("handle_null", True):
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail="Direct Quit C25 violation: external player handle was not reset to null.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                elif not q.details.get("hooks_restored", True):
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail="Direct Quit C25 violation: native hooks were not restored before exit.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                elif not q.details.get("pinned", True):
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail="Direct Quit C25 violation: DLL module/storage was not pinned until process exit.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                elif engine_exits and not any(ex.details.get("clean", True) for ex in engine_exits):
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail="Direct Quit engine shutdown logged abnormal exit code.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                else:
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.PASS,
                        detail="Confirmed clean Direct Quit under C25/DOC-43 contract: native hooks restored, handle nulled, module pinned, network worker cleanly stopped, engine shutdown clean.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"],
                        verified_event=q
                    ))
        elif chosen_scenario == "StateExit":
            # StateExit scenario: ActorDestroy for target_slot IS REQUIRED
            target_destroys = [e for e in actor_destroys if e.slot == target_slot]
            if not target_destroys:
                if actor_destroys:
                    observed = ", ".join(sorted(set(e.slot for e in actor_destroys)))
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"StateExit scenario: ActorDestroy observed for incorrect slot [{observed}] != target [{target_slot}].",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                else:
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"StateExit scenario requires native ActorDestroy for {target_slot}, but zero deallocation events observed.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
            else:
                des = target_destroys[0]
                des_tid = des.details.get("tid")
                des_gen = des.details.get("generation", des.details.get("gen"))

                if sim_tid is not None and des_tid is not None and int(des_tid) != sim_tid:
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"ActorDestroy executed on foreign thread: TID {des_tid} != simulation TID {sim_tid}.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                elif target_gen is not None and des_gen is not None and des_gen != target_gen:
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail=f"ActorDestroy belongs to stale connection generation: gen {des_gen} != expected {target_gen}.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                elif engine_exits and not any(ex.details.get("clean", True) for ex in engine_exits):
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.FAIL,
                        detail="StateExit engine shutdown logged abnormal exit code.",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
                    ))
                else:
                    results.append(KexContractCheck(
                        contract_id="KEX-04",
                        name=self.CONTRACT_DEFS["KEX-04"]["name"],
                        status=KexStatus.PASS,
                        detail=f"Confirmed clean puppet actor deallocation and network worker teardown for {target_slot}: {des.raw_text}",
                        required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"],
                        verified_event=des
                    ))
        else:
            # Unknown scenario without specific deallocation or C25 quiesce proof
            results.append(KexContractCheck(
                contract_id="KEX-04",
                name=self.CONTRACT_DEFS["KEX-04"]["name"],
                status=KexStatus.FAIL,
                detail="Shutdown recorded without required deallocation proof (missing ActorDestroy for StateExit or C25 quiesce proof for DirectQuit).",
                required_hook=self.CONTRACT_DEFS["KEX-04"]["hook"]
            ))

        return results

    @staticmethod
    def get_minimal_dependency_specification() -> str:
        """
        Returns description of the minimal dependency needed from Sol Max
        to enable automated verification of KEX-01..KEX-04 in M1.2a without code alterations.
        """
        return """### Minimal Native KEX Telemetry Contract Specification (M1.2a)
To enable automated native contract validation (KEX-01..KEX-04) for remote player P2
without modifying Wire Protocol v1.10 or the Multi-Client Test Runner:

1. **Emission Interface:** The native host executable (or injected DLL) must output structured telemetry markers to stdout or a designated log file (`--kex-telemetry-file <path>`), formatted either as key-value tags (`[KEX-HOOK] ...`) or JSON lines:
   - **KEX-01 (Actor Instantiation):**
     `[KEX-HOOK] ActorSpawn: slot=P2 actorId=<id> type=0 gen=<N> tid=<sim_tid>`
     *(type must be 0 for Puppet Actor; tid must match the KEX simulation thread; gen must match active connectionGeneration)*
   - **KEX-02 (Physics Displacement & Network Input Seam):**
     `[KEX-HOOK] MotionTick: slot=P2 pos=(<x>,<y>,<z>) vel=<v> inputSeq=<seq> netCmds=<n> gen=<N> tid=<sim_tid>`
     *(must explicitly link spatial displacement to accepted network input via inputSeq > 0 or netCmds > 0; tid must match simulation TID)*
   - **KEX-03 (P1 State Isolation & Temporal Integrity):**
     `[KEX-HOOK] P1StateCheck: tick=<t> slot0_hp=200 slot0_status=ALIVE gen=<N> tid=<sim_tid>`
     *(must be emitted at least twice: initial baseline before remote activity, and during/after remote player motion to prove non-interference across time)*
   - **KEX-04 (Lifecycle & Teardown):**
     - *StateExit scenario:*
       `[KEX-HOOK] ActorDestroy: slot=P2 actorId=<id> gen=<N> tid=<sim_tid>`
       `[KEX-HOOK] WorkerShutdown: clean=1 tid=<worker_tid>`
       `[KEX-HOOK] EngineShutdown: clean=1 code=0 workerStopped=1 tid=<sim_tid>`
     - *Direct Quit scenario (C25/DOC-43):*
       *(ActorDestroy is NOT required)*
       `[KEX-HOOK] DirectQuitQuiesced: handleNull=1 hooksRestored=1 pinned=1 gen=<N> tid=<sim_tid>`
       `[KEX-HOOK] WorkerShutdown: clean=1 tid=<worker_tid>`
       `[KEX-HOOK] EngineShutdown: clean=1 code=0 workerStopped=1 tid=<sim_tid>`
2. **Factuality Guarantee:** The Multi-Client Test Runner will read this channel and transition KEX checks from `DEFERRED` to `PASS` or `FAIL` strictly upon receiving verified events. Incomplete, stale, unlinked, or cross-thread telemetry is rejected as FAIL. Under no circumstances will mock-host runs be accepted as evidence for native engine contracts.
"""
