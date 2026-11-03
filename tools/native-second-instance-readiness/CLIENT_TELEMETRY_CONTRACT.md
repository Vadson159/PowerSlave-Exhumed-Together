# PowerSlave Exhumed Together — Client Telemetry Specification & Contract

**Document Version:** 1.0.0  
**Target Milestone:** Native Second-Instance Game Process B Client Integration  
**Role:** Senior QA Automation Engineer & Network Integration Validation Engineer  
**Status:** ACCEPTED SPECIFICATION  

---

## 1. Executive Summary & Purpose

This document establishes the **Client Telemetry Contract** required for the upcoming native client DLL (`pse_client.dll` / Game Process B).

In milestone M1.2a, network input was validated using a headless prototype client (`native_client.cpp` / `m12a_test_client.exe`). To graduate to genuine native co-op, Game Process B will run a real, unmodified PowerSlave Exhumed (2022) binary (`anubis_Shipping_Steam_x64.exe`), capture local player keyboard/mouse input through native KEX hooks/seams, and stream that input across the host-authoritative ENet transport to drive native P2.

To prevent false **PASS** evaluations and guarantee verifiable causal correlation, any future client DLL implementation **must** emit structured telemetry adhering to this specification.

---

## 2. Fundamental Architectural Rules

1. **Anti-False-PASS Gate:**
   - A headless test runner, mock script, or external tool reading `client_control.txt` will be classified as `INCONCLUSIVE` by `log_correlation.py`.
   - Only a genuine KEX process emitting `client_dll_prepared` and `client_input_sampled` from within `anubis_Shipping_Steam_x64.exe` (with verified binary hash and separate PID) can achieve a **PASS**.

2. **Causal Correlation Invariant:**
   - Telemetry validation **strictly prohibits** cross-machine or cross-process wall-clock comparisons.
   - All correlation is causal, keyed on the immutable triplet: `(slot, generation, input_seq)`.
   - Local durations and intervals are valid; inter-process latency is only inferred causally.

3. **Transport Drop Tolerance:**
   - The client streams player input over unreliable ENet UDP packets. Packet drops are statistically normal.
   - Telemetry analyzers do not fail on single packet drops, provided subsequent sequence progression remains monotonic and sticky movement is avoided.

4. **Bounded Disk I/O Cadence (Rate-Limiting):**
   - At 60 Hz simulation, unconstrained logging generates ~3,600 JSON lines per minute per process (~5 MB/min).
   - High-frequency events (`client_input_sampled`, `sent`) **must** support sampling, batching, or state-change logging (logging on input state transition, plus heartbeat every $N$ ticks) to prevent disk bottlenecks or framerate stutter in the KEX engine.

---

## 3. Required Journal Events & Schema Specification

All events must be appended as single-line JSON (`.jsonl`) objects to `client_native.jsonl`.

### 3.1. Process Manifest & Build Integrity (`client_dll_prepared`)

Emitted exactly once during client DLL initialization inside Game Process B before any network communication starts.

```json
{
  "event": "client_dll_prepared",
  "seq": 1,
  "utc": "2026-11-02T10:00:00.500Z",
  "pid": 10200,
  "tid": 20200,
  "binary_sha256": "b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d",
  "dll_version": "1.10.0",
  "headless": false,
  "kex_hook_installed": true
}
```

| Field | Type | Constraint / Description |
| :--- | :--- | :--- |
| `event` | string | Constant: `"client_dll_prepared"` |
| `pid` | integer | Win32 Process ID. **Must differ** from Host PID. |
| `tid` | integer | Win32 Thread ID of the main engine thread. |
| `binary_sha256` | string | SHA-256 of host EXE: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. |
| `dll_version` | string | Client DLL build version string (e.g. `"1.10.0"`). |
| `headless` | boolean | **Must be `false`**. `true` triggers `INCONCLUSIVE` gate. |
| `kex_hook_installed`| boolean | Confirmation that native KEX input interception hook is active. |

---

### 3.2. Session Handshake & Slot Binding (`welcome`)

Emitted upon receiving the server's authoritative `ServerWelcome` packet over ENet.

```json
{
  "event": "welcome",
  "seq": 2,
  "utc": "2026-11-02T10:00:04.000Z",
  "pid": 10200,
  "tid": 20200,
  "slot": 1,
  "generation": 1,
  "protocol": 272
}
```

| Field | Type | Constraint / Description |
| :--- | :--- | :--- |
| `event` | string | Constant: `"welcome"` |
| `slot` | integer | Assigned player slot. **Must be `1`** (P2) for the second instance. |
| `generation` | integer | Authoritative server session generation ($\ge 1$). Increments on reconnect. |
| `protocol` | integer | Wire Protocol magic version: `272` (0x0110 = v1.10). |

---

### 3.3. Native Input Sampling (`client_input_sampled`)

Emitted when the client hook intercepts the local player's input from the KEX engine before queueing for dispatch.

> [!NOTE]
> Logging every frame (60 Hz) is permitted during short acceptance tests, but for extended sessions, logging may be restricted to **state changes** (key pressed/released) plus a periodic 1-second cadence pulse.

```json
{
  "event": "client_input_sampled",
  "utc": "2026-11-02T10:00:04.100Z",
  "pid": 10200,
  "tid": 20200,
  "slot": 1,
  "generation": 1,
  "input_seq": 105,
  "forward": 1.0,
  "strafe": 0.0,
  "yaw": 0.0,
  "pitch": 0.0,
  "buttons": 4,
  "raw_key_w": true
}
```

| Field | Type | Constraint / Description |
| :--- | :--- | :--- |
| `event` | string | Constant: `"client_input_sampled"` |
| `input_seq` | integer | Strictly monotonic sequence number generated on client. Initial $\le 10800$. |
| `forward` | float | Movement axis: $[-1.0, 1.0]$. $1.0 = \text{Forward (W)}$, $-1.0 = \text{Back (S)}$. |
| `strafe` | float | Strafe axis: $[-1.0, 1.0]$. $1.0 = \text{Right (D)}$, $-1.0 = \text{Left (A)}$. |
| `buttons` | integer | Native KEX button bitmask (Bit 2 = 0x04 = Walk forward). |
| `raw_key_w` | boolean | Diagnostic flag confirming physical keyboard input capture. |

---

### 3.4. Wire Dispatch (`sent` / `client_input_dispatched`)

Emitted when the client's ENet worker thread hands the packet to the socket.

```json
{
  "event": "sent",
  "utc": "2026-11-02T10:00:04.102Z",
  "pid": 10200,
  "tid": 20205,
  "slot": 1,
  "generation": 1,
  "input_seq": 105,
  "forward": 1.0,
  "buttons": 4
}
```

| Field | Type | Constraint / Description |
| :--- | :--- | :--- |
| `event` | string | Constant: `"sent"` or `"client_input_dispatched"` |
| `tid` | integer | Network worker thread ID (distinct from simulation thread). |
| `input_seq` | integer | Matches sampled `input_seq`. |
| `forward` | float | Forward value serialized into wire packet. |
| `buttons` | integer | Buttons mask serialized into wire packet. |

---

### 3.5. Release & Neutral Safeguard (`client_neutral`)

Emitted when the local player releases all movement keys, or when a neutral failsafe pulse is dispatched.

```json
{
  "event": "client_neutral",
  "utc": "2026-11-02T10:00:05.200Z",
  "pid": 10200,
  "tid": 20200,
  "slot": 1,
  "generation": 1,
  "input_seq": 170,
  "reason": "key_release"
}
```

| Field | Type | Constraint / Description |
| :--- | :--- | :--- |
| `event` | string | Constant: `"client_neutral"` |
| `reason` | string | `"key_release"` or `"continuous_neutral_pulse"` |

> [!IMPORTANT]
> **Sticky Walk Prevention Rule:** Because ENet input packets are unreliable, a single release packet may be lost. The client DLL **must** continue to emit neutral packets (`forward=0.0, buttons=0`) for at least 10 consecutive ticks (or 150 ms) after key release, ensuring the host receives at least one neutral packet.

---

### 3.6. Clean Teardown & Lifecycle

Emitted during orderly process termination or session disconnect.

```json
{"event": "client_stop_begin", "utc": "2026-11-02T10:00:10.000Z", "pid": 10200, "tid": 20200, "reason": "UserQuit"}
{"event": "client_joined", "utc": "2026-11-02T10:00:10.050Z", "pid": 10200, "tid": 20200, "worker_running": false, "worker_joined": true}
{"event": "client_final_exit", "utc": "2026-11-02T10:00:10.100Z", "pid": 10200, "tid": 20200, "worker_alive_at_exit": false}
```

---

## 4. Host-Side Ingress Predicate Alignment (DOC-50 Verification)

The client implementation must strictly conform to the host ingress filter implemented by GPT-6.1 Sol in `50_M12B_INPUT_SEQUENCE_HARDENING.md`:

1. **Initial Sequence Bound:**  
   The first packet received after session start must have $\text{input\_seq} \le 10800$. Packets exceeding this are dropped with `rejected_jump`.
2. **Forward Jump Bound:**  
   Consecutive packets within the same generation cannot advance $\text{input\_seq}$ by more than $+1800$ (30 seconds at 60 Hz). Packets with $\Delta > 1800$ are dropped with `rejected_jump`.
3. **Monotonicity:**  
   Packets with $\text{input\_seq} \le \text{last\_seq}$ are dropped with `rejected_stale`.
4. **Generation Matching:**  
   Packets with $\text{generation} \ne \text{session\_generation}$ are dropped with `rejected_generation`.
5. **Slot Isolation:**  
   Packets with $\text{slot} \ne 1$ are dropped with `rejected_foreign`.

Telemetry emitted by the client will be validated directly against the host's `network_input_accepted` and `network_input_rejected` events.

---

## 5. Acceptance Checklist for Sol's Client DLL

Prior to submitting the native client DLL for acceptance testing, verify:

- [ ] Client emits `client_dll_prepared` with `headless: false` and matching game executable SHA-256.
- [ ] Client captures real keyboard input (`raw_key_w`) and emits `client_input_sampled`.
- [ ] Client runs on independent PID/TID from host.
- [ ] Release action emits continuous neutral stream (10 packets) to eliminate sticky walk.
- [ ] Client worker thread joins cleanly on exit (`worker_joined: true`).
- [ ] Running `run_offline_qa.py` against captured host/client journals yields overall verdict **PASS**.
