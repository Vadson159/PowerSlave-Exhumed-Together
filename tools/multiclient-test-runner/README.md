# PowerSlave Exhumed Together — Multi-Client Test Runner (Phase 2)

Autonomous regression, stress, chaos, and integration test orchestration harness for **2 to 8 player cooperative multiplayer** (authoritative host + up to 7 remote clients) on Windows.

Created for **M1.2a QA Concurrency Validation, Stress Hardening & KEX Integration Readiness**, operating over **Wire Protocol v1.10 (`0x0110`)**.

---

## Architecture Overview

```text
tools/multiclient-test-runner/
├── multiclient_runner.py       # Master test orchestration CLI & suite runner
├── host_verifier.py            # Authoritative host event stream parser & fact reconciler
├── kex_adapter.py              # Native KEX Engine adapter (contracts KEX-01..KEX-04 & DEFERRED gate)
├── chaos_orchestrator.py       # Deterministic PRNG chaos generator (kill, reconnect, mass drop)
├── test_runner_unit.py         # 13 automated unit tests for invariant & safety validation
├── runner.bat                  # Windows batch launcher (passes CLI flags)
├── run.bat                     # Convenient launcher alias
├── README.md                   # Complete architectural & operational guide
├── TEST_REPORT.md              # Real test execution report with per-client breakdowns
├── summary.json                # Structured JSON matrix report for CI ingestion
└── output/                     # Isolated test logs, JSON summaries, and markdown reports
```

### Modular Components

1. **Host Event Tracker & Verifier (`host_verifier.py`):**
   - Monitors authoritative host output asynchronously in real time without blocking.
   - Cross-validates every client-reported outcome against host-side events (`CONFIRMED`, `CONTRADICTION`, or `UNATTACHED`).
   - Analyzes time-domain session intervals to prove **true concurrency overlap** ($\bigcap [t_{\text{start}}, t_{\text{end}}] > 0$).
2. **Native KEX Integration Adapter (`kex_adapter.py`):**
   - Evaluates contracts `KEX-01` through `KEX-04`.
   - **Strict QA Factuality Boundary:** In the absence of verified native KEX engine telemetry, all native contracts evaluate to **`DEFERRED`**; contracts are **NEVER marked `PASS`** based on mock-host runs or network socket packets alone.
   - Provides minimal telemetry hook dependency specification for future engine phases.
3. **Deterministic Chaos & Stress Engine (`chaos_orchestrator.py`):**
   - Drives continuous random connections, clean leaves, abrupt kills (`SIGKILL`), mass disconnects, and reconnected joins.
   - Seed-driven PRNG (`--chaos-seed`) guarantees exact scenario reproducibility.
   - Preset durations for 5m, 15m, and 30m endurance runs (`--stress 5m/15m/30m`).
   - Guarantees complete process tree tracking and child reaping (0 lingering zombie PIDs).
4. **Anti-False-PASS Invariant Engine (`multiclient_runner.py`):**
   - Eliminates false positive results from crashes, watchdog timeouts, empty logs, or truncated JSON.
   - Enforces slot uniqueness, `connectionGeneration` progression (1 -> 2), session ID uniformity, and log file isolation.

---

## Verified Protocol & KEX Contracts

| Contract | Type | Verification Level | Description |
|:--------:|:----:|:------------------:|:------------|
| `NET-01` | Socket | **Client & Host Confirmed** | ENet UDP socket transport establishment |
| `NET-02` | Protocol | **Client & Host Confirmed** | Wire Protocol v1.10 Handshake (`MsgHello` -> `MsgWelcome`) |
| `NET-03` | Slot | **Client & Host Confirmed** | Dynamic slot assignment (`P2` .. `P8`) and session ID binding |
| `NET-04` | Session | **Client & Host Confirmed** | Fresh join generation invariant (`connectionGeneration == 1`) |
| `NET-05` | Monotonicity | **Client Confirmed** | Outbound command sequence numbers strictly incrementing |
| `NET-06` | Input | **Client Confirmed** | Hold W movement command stream dispatch |
| `NET-07` | Input | **Client Confirmed** | Explicit W-release packet dispatch (`moveForward = 0.0f`) |
| `NET-08` | Input | **Client Confirmed** | Neutral input stream dispatch |
| `NET-09` | Disconnect | **Client & Host Confirmed** | Clean session termination (`LEAVE_USER_QUIT`) |
| `NET-10` | Reconnect | **Client & Host Confirmed** | Reconnection authentication using secret cryptographic key |
| `NET-11` | Slot | **Client & Host Confirmed** | Reconnection slot preservation (exact same player slot re-occupied) |
| `NET-12` | Session | **Client & Host Confirmed** | Generational advancement upon reconnect (`gen 1 -> 2`) |
| `NET-13` | Session | **Client Confirmed** | Stale session state prevention (zero packets carry old generation) |
| `NET-14` | Security | **Client & Host Confirmed** | Anti-hijack defense gate: instant rejection of forged key (`code 8`) |
| `CAP-01` | Capacity | **Client & Host Confirmed** | 7-client ceiling: 8th client strictly rejected (`REJECT_SERVER_FULL`) |
| `KEX-01` | Engine | **DEFERRED (Sol Max)** | Native Puppet Actor instantiation (`Factory::SpawnActor(0)`) |
| `KEX-02` | Engine | **DEFERRED (Sol Max)** | Native character physics displacement in engine world |
| `KEX-03` | Engine | **DEFERRED (Sol Max)** | Host local player (P1) memory isolation and non-interference |
| `KEX-04` | Engine | **DEFERRED (Sol Max)** | Clean engine actor deallocation on state exit and shutdown |

---

## Quick Start Commands

### 1. Execute Full Automated QA Phase 2 Matrix (10 Suites)

Runs the complete 10-suite regression, lifecycle, capacity, fault-injection, and chaos verification suite, generating `TEST_REPORT.md` and `summary.json`:

```cmd
runner.bat --run-matrix
```

### 2. Run Endurance Stress Tests (5, 15, or 30 Minutes)

Spawns a managed host and executes continuous deterministic chaos testing:

```cmd
:: 5-minute endurance stress run:
runner.bat --stress 5m --spawn-host

:: 15-minute endurance stress run:
runner.bat --stress 15m --spawn-host

:: 30-minute endurance stress run:
runner.bat --stress 30m --spawn-host
```

### 3. Run Reproducible Chaos Testing with Fixed Seed

Run custom duration chaos with specific seed for deterministic replay:

```cmd
runner.bat --chaos-duration 30 --chaos-seed 4242 --spawn-host
```

### 4. Run Maximum Full-Capacity Multi-Client Test (7 Remote Clients)

Launches 7 independent clients simultaneously against managed host:

```cmd
runner.bat --clients 7 --spawn-host
```

### 5. Run Reconnect & Generational Bump Test

Verifies disconnect, session reset, and reconnect with generation bump `1 -> 2`:

```cmd
runner.bat --clients 1 --scenario reconnect --spawn-host
```

### 6. Run Anti-Hijack Defense Test

Verifies rejection of forged cryptographic reconnect key:

```cmd
runner.bat --clients 1 --scenario anti-hijack --spawn-host
```

### 7. Run Unit & Invariant Tests

Executes 13 runner-level unit tests verifying invariant detectors, overlap analyzers, host reconciler, and PID cleanup:

```cmd
python test_runner_unit.py
```

---

## CLI Reference Guide

| Flag | Type | Default | Description |
|:-----|:----:|:-------:|:------------|
| `--run-matrix` | `flag` | `False` | Run complete QA Phase 2 verification matrix (10 suites). |
| `--clients`, `-n` | `int` | `3` | Number of clients to spawn (`1` to `8`). |
| `--host` | `string` | `127.0.0.1` | Target host IPv4 address. |
| `--port` | `int` | `27015` | Target host UDP port. |
| `--scenario` | `choice` | `hold-w` | Scenario: `hold-w`, `reconnect`, `anti-hijack`, `full`. |
| `--mode` | `choice` | `simultaneous` | Execution mode: `simultaneous` or `sequential`. |
| `--stagger-ms` | `int` | `0` | Delay in ms between consecutive client spawns. |
| `--hold-ticks` | `int` | `30` | Duration (ticks) to hold W (+1.0 forward). |
| `--neutral-ticks` | `int` | `15` | Duration (ticks) of neutral input. |
| `--tick-rate` | `int` | `60` | Client simulation rate in Hz. |
| `--timeout-ms` | `int` | `5000` | Network timeout per client in ms. |
| `--global-timeout` | `float` | `30.0` | Global watchdog timeout in seconds. |
| `--spawn-host` | `flag` | `False` | Automatically spawn and manage local host process. |
| `--stress` | `choice` | `None` | Long-duration stress preset: `5m`, `15m`, `30m`. |
| `--chaos-duration`| `float` | `None` | Duration (seconds) for chaos stress run. |
| `--chaos-seed` | `int` | `1337` | Random seed for deterministic chaos replay. |
| `--host-type` | `choice` | `mock` | Target host architecture: `mock` or `kex`. |
| `--kex-telemetry-file`| `path` | `None` | Path to native KEX telemetry log file. |
| `--early-disconnect` | `int` | `None` | Index of client that exits early (`1`..`N`). |
| `--output-dir` | `path` | `output` | Directory for logs, json, and reports. |
| `--report-file` | `path` | Auto | Output Markdown report file path. |
| `--json-file` | `path` | Auto | Output JSON summary file path. |
| `--quiet`, `-q` | `flag` | `False` | Suppress verbose output; print summary table only. |
| `--verbose`, `-v` | `flag` | `False` | Stream raw client logs to console. |

---

## Architectural & QA Observations

### 1. Factuality Boundary & Anti-False-PASS
- A client is only marked `PASS` when **all** of the following hold:
  1. Process exit code is `0` (clean exit, no crash, no unhandled exception).
  2. Process did not time out.
  3. Individual log file (`client_N.log`) exists and is non-empty.
  4. Structured report (`client_N.json`) exists and records `failCount == 0`.
  5. Authoritative host event log confirms slot assignment, generation, and lack of rejection.
- If any condition is violated, the result is marked **`FAIL`**.

### 2. Host Slot Retention Policy in `pse_host.exe`
- When a client disconnects cleanly (`LEAVE_USER_QUIT`), `pse_host.exe` sets `connected = false` but retains the `participantId` and secret `reconnectKey`.
- Reconnection with the **same** `participantId` and `reconnectKey` succeeds, re-occupies the slot, and advances `connectionGeneration: 1 -> 2`.
- A **new** client with a different `participantId` is assigned the next unoccupied slot. Once all 7 client slots are allocated or reserved, subsequent new clients receive `REJECT_SERVER_FULL`. Dynamic slot eviction for new participant IDs is not supported in the mock host and requires host session reset.

### 3. Native KEX Readiness & Minimal Dependency
- In the absence of native KEX telemetry hooks, contracts `KEX-01` through `KEX-04` are reported as **`DEFERRED`**.
- When targeting native KEX host with telemetry output (`[KEX-HOOK] ActorSpawn`, `[KEX-HOOK] MotionTick`, etc.), `NativeKexAdapter` validates native events automatically.
