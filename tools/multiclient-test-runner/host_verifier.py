#!/usr/bin/env python3
"""
PowerSlave Exhumed Together — Host Fact Verifier & Telemetry Tracker
====================================================================
Cross-validates client-claimed outcomes against authoritative host-side
events to eliminate false PASS results from crashes, timeouts, or truncated logs.

Distinguishes between:
  1. Client-Side Observations (socket sent packets, local ENet buffer queue)
  2. Host-Side Confirmed Facts (host accepted handshake, assigned slot, recorded gen)

Wire Protocol: v1.10 (0x0110)
"""

import re
import time
import threading
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple, Any
from enum import Enum


class HostConfirmationStatus(str, Enum):
    CONFIRMED = "CONFIRMED"                     # Host event log strictly confirms client claim
    CONTRADICTION = "CONTRADICTION"             # Host log directly contradicts client claim (e.g. rejected or wrong slot)
    UNCONFIRMED = "UNCONFIRMED"                 # Host was running but did not log expected event for this client
    UNATTACHED = "UNATTACHED"                   # Host log was not available / unmanaged external host


@dataclass
class HostEvent:
    event_type: str                             # STARTED, SLOT_ASSIGNED, DISCONNECTED, REJECT, STATUS, STOPPED
    timestamp: float
    raw_text: str
    slot: Optional[str] = None                  # e.g. "P2"
    slot_id: Optional[int] = None               # e.g. 1
    participant_id: Optional[str] = None
    display_name: Optional[str] = None
    generation: Optional[int] = None
    session_id: Optional[int] = None
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class HostVerificationResult:
    participant_id: str
    status: HostConfirmationStatus
    detail: str
    host_slot: Optional[str] = None
    host_generation: Optional[int] = None
    host_session_id: Optional[int] = None
    disconnection_confirmed: bool = False


@dataclass
class SessionOverlapResult:
    is_overlapping: bool
    overlap_duration_s: float
    earliest_start_s: float
    latest_start_s: float
    earliest_end_s: float
    latest_end_s: float
    active_concurrent_peak: int
    detail: str


class HostEventTracker:
    """
    Parses and tracks host stdout/log stream in real time or post-run.
    Maintains the authoritative server state ground truth.
    """

    RE_STARTED = re.compile(r'\[Host\] Listening on ([0-9\.]+):(\d+) \(Max clients: (\d+)', re.IGNORECASE)
    RE_ASSIGNED = re.compile(r'\[Host\] Assigned slot (P\d+) to participant: (.+?) \[([^\]]+)\], gen=(\d+)', re.IGNORECASE)
    RE_DISCONNECTED = re.compile(r'\[Host\] Client disconnected from slot (P\d+) \(([^)]+)\)', re.IGNORECASE)
    RE_REJECTED = re.compile(r'\[Host\] Rejected (?:connection|respawn request).*?from (?:P\d+|[0-9\.:]+).*?code=(\d+)', re.IGNORECASE)
    RE_SESSION_STATUS = re.compile(r'--- Session Status \[SessionID: (\d+)\] ---', re.IGNORECASE)
    RE_STOPPED = re.compile(r'\[Host\] Stopped', re.IGNORECASE)

    def __init__(self):
        self.events: List[HostEvent] = []
        self.raw_lines: List[str] = []
        self.active_slots: Dict[str, Dict[str, Any]] = {}   # slot -> {participant_id, gen, display_name}
        self.historical_assignments: List[HostEvent] = []
        self.session_id: Optional[int] = None
        self.max_clients: int = 7
        self.is_running: bool = False
        self._lock = threading.Lock()

    def feed_line(self, line: str, timestamp: Optional[float] = None) -> Optional[HostEvent]:
        """Feed a single stdout line from the host process."""
        t = timestamp if timestamp is not None else time.time()
        line_clean = line.strip()
        if not line_clean:
            return None

        with self._lock:
            self.raw_lines.append(line_clean)
            ev: Optional[HostEvent] = None

            # 1. Host Listening Started
            m_start = self.RE_STARTED.search(line_clean)
            if m_start:
                bind_ip = m_start.group(1)
                port = int(m_start.group(2))
                max_c = int(m_start.group(3))
                self.max_clients = max_c
                self.is_running = True
                ev = HostEvent(
                    event_type="STARTED",
                    timestamp=t,
                    raw_text=line_clean,
                    details={"bind": bind_ip, "port": port, "max_clients": max_c}
                )

            # 2. Slot Assigned
            m_assign = self.RE_ASSIGNED.search(line_clean)
            if m_assign:
                slot = m_assign.group(1)
                d_name = m_assign.group(2)
                p_id = m_assign.group(3)
                gen = int(m_assign.group(4))
                m_slot_num = re.search(r'\d+', slot)
                s_id = (int(m_slot_num.group(0)) - 1) if m_slot_num else None

                ev = HostEvent(
                    event_type="SLOT_ASSIGNED",
                    timestamp=t,
                    raw_text=line_clean,
                    slot=slot,
                    slot_id=s_id,
                    participant_id=p_id,
                    display_name=d_name,
                    generation=gen
                )
                self.active_slots[slot] = {
                    "participant_id": p_id,
                    "display_name": d_name,
                    "generation": gen,
                    "assigned_at": t
                }
                self.historical_assignments.append(ev)

            # 3. Disconnection
            m_disc = self.RE_DISCONNECTED.search(line_clean)
            if m_disc:
                slot = m_disc.group(1)
                p_id = m_disc.group(2)
                ev = HostEvent(
                    event_type="DISCONNECTED",
                    timestamp=t,
                    raw_text=line_clean,
                    slot=slot,
                    participant_id=p_id
                )
                if slot in self.active_slots:
                    self.active_slots.pop(slot, None)

            # 4. Session Status
            m_sess = self.RE_SESSION_STATUS.search(line_clean)
            if m_sess:
                self.session_id = int(m_sess.group(1))
                ev = HostEvent(
                    event_type="STATUS",
                    timestamp=t,
                    raw_text=line_clean,
                    session_id=self.session_id
                )

            # 5. Rejected
            m_rej = self.RE_REJECTED.search(line_clean)
            if m_rej:
                ev = HostEvent(
                    event_type="REJECT",
                    timestamp=t,
                    raw_text=line_clean,
                    details={"code": int(m_rej.group(1))}
                )

            # 6. Stopped
            if self.RE_STOPPED.search(line_clean):
                self.is_running = False
                ev = HostEvent(
                    event_type="STOPPED",
                    timestamp=t,
                    raw_text=line_clean
                )

            if ev:
                self.events.append(ev)
            return ev

    def feed_text(self, text: str, timestamp: Optional[float] = None) -> None:
        """Feed full multiline string log."""
        for line in text.splitlines():
            self.feed_line(line, timestamp)

    def load_log_file(self, log_path: Path) -> None:
        """Load and parse host events from a saved log file."""
        if not log_path.is_file():
            return
        try:
            content = log_path.read_text(encoding="utf-8", errors="replace")
            self.feed_text(content)
        except Exception:
            pass

    def verify_client_facts(
        self,
        participant_id: str,
        claimed_slot: Optional[str],
        claimed_gen: Optional[int],
        claimed_connected: bool = True
    ) -> HostVerificationResult:
        """
        Cross-verify a client's reported connection against host-confirmed events.
        """
        with self._lock:
            # If no host events were tracked at all, return UNATTACHED
            if not self.raw_lines:
                return HostVerificationResult(
                    participant_id=participant_id,
                    status=HostConfirmationStatus.UNATTACHED,
                    detail="Host event telemetry was not captured for this execution (unmanaged/external host)."
                )

            # Search historical slot assignments for this participant
            matching_assignments = [
                ev for ev in self.historical_assignments
                if ev.participant_id == participant_id
            ]

            if not matching_assignments:
                if claimed_connected:
                    return HostVerificationResult(
                        participant_id=participant_id,
                        status=HostConfirmationStatus.UNCONFIRMED,
                        detail=f"Host event log contains no record of assigning a slot to participant '{participant_id}'."
                    )
                else:
                    return HostVerificationResult(
                        participant_id=participant_id,
                        status=HostConfirmationStatus.CONFIRMED,
                        detail=f"Host correctly did not assign any slot to '{participant_id}' (expected rejection)."
                    )

            # Take latest assignment
            latest = matching_assignments[-1]

            # Check slot match
            if claimed_slot and latest.slot != claimed_slot:
                return HostVerificationResult(
                    participant_id=participant_id,
                    status=HostConfirmationStatus.CONTRADICTION,
                    detail=f"Host assigned slot {latest.slot}, but client claimed slot {claimed_slot}!",
                    host_slot=latest.slot,
                    host_generation=latest.generation
                )

            # Check generation match
            if claimed_gen is not None and latest.generation != claimed_gen:
                return HostVerificationResult(
                    participant_id=participant_id,
                    status=HostConfirmationStatus.CONTRADICTION,
                    detail=f"Host recorded generation {latest.generation}, but client claimed generation {claimed_gen}!",
                    host_slot=latest.slot,
                    host_generation=latest.generation
                )

            # Check disconnection recorded
            disc_events = [
                ev for ev in self.events
                if ev.event_type == "DISCONNECTED" and ev.participant_id == participant_id
            ]
            disc_confirmed = (len(disc_events) >= len(matching_assignments))

            return HostVerificationResult(
                participant_id=participant_id,
                status=HostConfirmationStatus.CONFIRMED,
                detail=f"Host confirmed slot {latest.slot}, generation {latest.generation} for '{participant_id}'.",
                host_slot=latest.slot,
                host_generation=latest.generation,
                host_session_id=self.session_id,
                disconnection_confirmed=disc_confirmed
            )


class SessionOverlapAnalyzer:
    """
    Computes time-domain overlap across concurrent client sessions.
    Guarantees that 'simultaneous' tests truly overlapped on the wire.
    """

    @staticmethod
    def analyze_overlap(sessions: List[Tuple[float, float]]) -> SessionOverlapResult:
        """
        sessions: list of (t_start, t_end) timestamps for each client.
        """
        if not sessions:
            return SessionOverlapResult(
                is_overlapping=False,
                overlap_duration_s=0.0,
                earliest_start_s=0.0,
                latest_start_s=0.0,
                earliest_end_s=0.0,
                latest_end_s=0.0,
                active_concurrent_peak=0,
                detail="No sessions provided for overlap analysis."
            )

        if len(sessions) == 1:
            dur = max(0.0, sessions[0][1] - sessions[0][0])
            return SessionOverlapResult(
                is_overlapping=True,
                overlap_duration_s=dur,
                earliest_start_s=sessions[0][0],
                latest_start_s=sessions[0][0],
                earliest_end_s=sessions[0][1],
                latest_end_s=sessions[0][1],
                active_concurrent_peak=1,
                detail=f"Single client session duration: {dur:.3f}s"
            )

        earliest_start = min(s[0] for s in sessions)
        latest_start = max(s[0] for s in sessions)
        earliest_end = min(s[1] for s in sessions)
        latest_end = max(s[1] for s in sessions)

        # The global intersection window where ALL clients were concurrently active
        overlap_s = max(0.0, earliest_end - latest_start)
        is_overlapping = (overlap_s > 0.0)

        # Compute max concurrent active clients at any point in time
        time_points: List[Tuple[float, int]] = []
        for s in sessions:
            time_points.append((s[0], +1))
            time_points.append((s[1], -1))

        # Sort timepoints (+1 before -1 if identical time)
        time_points.sort(key=lambda x: (x[0], -x[1]))

        current_active = 0
        peak_active = 0
        for _, delta in time_points:
            current_active += delta
            if current_active > peak_active:
                peak_active = current_active

        if is_overlapping:
            detail = (
                f"Full concurrency verified: all {len(sessions)} clients concurrently active "
                f"for {overlap_s:.3f}s (peak concurrent: {peak_active})."
            )
        else:
            gap = latest_start - earliest_end
            detail = (
                f"CONCURRENCY DEFECT: Sessions did not concurrently overlap! "
                f"Earliest client disconnected {gap:.3f}s before latest client connected (peak concurrent: {peak_active})."
            )

        return SessionOverlapResult(
            is_overlapping=is_overlapping,
            overlap_duration_s=round(overlap_s, 4),
            earliest_start_s=earliest_start,
            latest_start_s=latest_start,
            earliest_end_s=earliest_end,
            latest_end_s=latest_end,
            active_concurrent_peak=peak_active,
            detail=detail
        )
