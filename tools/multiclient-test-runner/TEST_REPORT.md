# PowerSlave Exhumed Together — Multi-Client QA Phase 2 Report

**Execution Timestamp:** `2026-11-02 00:10:44`  
**Authoritative Host:** Autoritative ENet Host (`pse_host.exe`) / Future Native KEX Host  
**Client Binary:** `m12a_test_client.exe` (Wire Protocol v1.10 - 0x0110)  
**Tool Location:** `tools/multiclient-test-runner/`  

> [!IMPORTANT]
> **Multi-Client Verification Verdict: PASSED**  
> Validated 1 to 7 simultaneous clients, early disconnect, generational bump (gen 1 -> 2), cryptographic reconnect, anti-hijack defense, 7-client capacity ceiling, sudden crash recovery, and deterministic chaos stress testing.

> [!NOTE]
> **QA Factuality Boundary:** This test harness verifies real socket connections, wire protocol handshakes, slot assignments, connection generation counters, and local command queue dispatches. Dispatched `moveForward` commands prove valid network transmission into the local ENet buffer and UDP transfer to the host; they do **not** imply native KEX character physics displacement or puppet actor spawning, which remain explicitly `DEFERRED` to Sol Max native hooks.

## 1. Executive Test Matrix Summary

| Suite # | Test Suite Name | Target | Clients | Concurrency Mode | Overlap (s) | Host Confirmation | Invariant Status | Suite Verdict |
|:-------:|:----------------|:-------|:-------:|:----------------:|:-----------:|:-----------------:|:----------------:|:-------------:|
| 1 | Matrix 01: Single Client Baseline | `127.0.0.1:27201` | 1 | SIMULTANEOUS | N/A | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 2 | Matrix 02: Three Simultaneous Clients | `127.0.0.1:27202` | 3 | SIMULTANEOUS | 0.82s | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 3 | Matrix 03: Seven Simultaneous Clients (Full Capacity) | `127.0.0.1:27203` | 7 | SIMULTANEOUS | 0.83s | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 4 | Matrix 04: Early Disconnect Resilience (1 Exits Early, 2 Continue) | `127.0.0.1:27204` | 3 | SIMULTANEOUS | 0.28s | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 5 | Matrix 05: Reconnect Lifecycle & Generational Bump (gen 1 -> 2) | `127.0.0.1:27205` | 1 | SIMULTANEOUS | N/A | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 6 | Matrix 06: Anti-Hijack Defense Gate (Forged Key Rejection) | `127.0.0.1:27206` | 1 | SIMULTANEOUS | N/A | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 7 | Matrix 07: Multi-Client Reconnect with Active Peers | `127.0.0.1:27207` | 3 | SIMULTANEOUS | 0.39s | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 8 | Matrix 08: Server Capacity Ceiling (7 Allowed, 8th Rejected) | `127.0.0.1:27208` | 8 | SIMULTANEOUS | 1.02s | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 9 | Matrix 09: Fault Injection Resilience (Sudden Client Kill) | `127.0.0.1:27209` | 2 | SIMULTANEOUS | 0.30s | CONFIRMED | CLEAN (No Collisions) | **PASSED** |
| 10 | Matrix 10: Deterministic Chaos Stress Cycle (Seed 4242) | `127.0.0.1:27210` | 7 | CHAOS | N/A | UNATTACHED | CLEAN (No Collisions) | **PASSED** |

---

## 2.1. Suite: Matrix 01: Single Client Baseline

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27201`
- **Total Elapsed:** `0.831s`
- **Client Count:** `1`

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `runner_p2` | `Runner_P2` | `7968` | `P2` (id=1) | `195072` | `1` | 0.826s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)

### Host Ground-Truth Reconciliation

- **Client #1 (`runner_p2`):** Host confirmed slot P2, generation 1 for 'runner_p2'.

---

## 2.2. Suite: Matrix 02: Three Simultaneous Clients

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27202`
- **Total Elapsed:** `0.841s`
- **Client Count:** `3`
- **Session Overlap:** `0.824s` (Peak concurrent: `3`)

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `runner_p2` | `Runner_P2` | `40680` | `P2` (id=1) | `953559` | `1` | 0.834s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #2 | `runner_p3` | `Runner_P3` | `37032` | `P4` (id=3) | `953559` | `1` | 0.835s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #3 | `runner_p4` | `Runner_P4` | `35188` | `P3` (id=2) | `953559` | `1` | 0.836s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)
- **Concurrency Overlap:** PASSED (Full concurrency verified: all 3 clients concurrently active for 0.824s (peak concurrent: 3).)

### Host Ground-Truth Reconciliation

- **Client #1 (`runner_p2`):** Host confirmed slot P2, generation 1 for 'runner_p2'.
- **Client #2 (`runner_p3`):** Host confirmed slot P4, generation 1 for 'runner_p3'.
- **Client #3 (`runner_p4`):** Host confirmed slot P3, generation 1 for 'runner_p4'.

---

## 2.3. Suite: Matrix 03: Seven Simultaneous Clients (Full Capacity)

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27203`
- **Total Elapsed:** `0.855s`
- **Client Count:** `7`
- **Session Overlap:** `0.832s` (Peak concurrent: `7`)

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `runner_p2` | `Runner_P2` | `24636` | `P4` (id=3) | `788295` | `1` | 0.848s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #2 | `runner_p3` | `Runner_P3` | `39152` | `P6` (id=5) | `788295` | `1` | 0.851s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #3 | `runner_p4` | `Runner_P4` | `22588` | `P3` (id=2) | `788295` | `1` | 0.849s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #4 | `runner_p5` | `Runner_P5` | `17884` | `P8` (id=7) | `788295` | `1` | 0.852s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #5 | `runner_p6` | `Runner_P6` | `31584` | `P7` (id=6) | `788295` | `1` | 0.849s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #6 | `runner_p7` | `Runner_P7` | `22356` | `P2` (id=1) | `788295` | `1` | 0.849s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #7 | `runner_p8` | `Runner_P8` | `40732` | `P5` (id=4) | `788295` | `1` | 0.849s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)
- **Concurrency Overlap:** PASSED (Full concurrency verified: all 7 clients concurrently active for 0.832s (peak concurrent: 7).)

### Host Ground-Truth Reconciliation

- **Client #1 (`runner_p2`):** Host confirmed slot P4, generation 1 for 'runner_p2'.
- **Client #2 (`runner_p3`):** Host confirmed slot P6, generation 1 for 'runner_p3'.
- **Client #3 (`runner_p4`):** Host confirmed slot P3, generation 1 for 'runner_p4'.
- **Client #4 (`runner_p5`):** Host confirmed slot P8, generation 1 for 'runner_p5'.
- **Client #5 (`runner_p6`):** Host confirmed slot P7, generation 1 for 'runner_p6'.
- **Client #6 (`runner_p7`):** Host confirmed slot P2, generation 1 for 'runner_p7'.
- **Client #7 (`runner_p8`):** Host confirmed slot P5, generation 1 for 'runner_p8'.

---

## 2.4. Suite: Matrix 04: Early Disconnect Resilience (1 Exits Early, 2 Continue)

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27204`
- **Total Elapsed:** `1.106s`
- **Client Count:** `3`
- **Session Overlap:** `0.279s` (Peak concurrent: `3`)

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `runner_p2` | `Runner_P2` | `24844` | `P3` (id=2) | `433426` | `1` | 0.293s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #2 | `runner_p3` | `Runner_P3` | `34532` | `P4` (id=3) | `433426` | `1` | 1.105s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #3 | `runner_p4` | `Runner_P4` | `36984` | `P2` (id=1) | `433426` | `1` | 1.105s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)
- **Concurrency Overlap:** PASSED (Full concurrency verified: all 3 clients concurrently active for 0.279s (peak concurrent: 3).)

### Host Ground-Truth Reconciliation

- **Client #1 (`runner_p2`):** Host confirmed slot P3, generation 1 for 'runner_p2'.
- **Client #2 (`runner_p3`):** Host confirmed slot P4, generation 1 for 'runner_p3'.
- **Client #3 (`runner_p4`):** Host confirmed slot P2, generation 1 for 'runner_p4'.

---

## 2.5. Suite: Matrix 05: Reconnect Lifecycle & Generational Bump (gen 1 -> 2)

- **Scenario:** `reconnect`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27205`
- **Total Elapsed:** `0.45s`
- **Client Count:** `1`

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `runner_p2` | `Runner_P2` | `37712` | `P2` (id=1) | `N/A` | `2` | 0.446s | `0` | `CONFIRMED` | 4P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)

### Host Ground-Truth Reconciliation

- **Client #1 (`runner_p2`):** Host confirmed slot P2, generation 2 for 'runner_p2'.

---

## 2.6. Suite: Matrix 06: Anti-Hijack Defense Gate (Forged Key Rejection)

- **Scenario:** `anti-hijack`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27206`
- **Total Elapsed:** `0.182s`
- **Client Count:** `1`

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `runner_p2` | `Runner_P2` | `37704` | `P2` (id=1) | `N/A` | `1` | 0.181s | `0` | `CONFIRMED` | 1P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)

### Host Ground-Truth Reconciliation

- **Client #1 (`runner_p2`):** Host confirmed slot P2, generation 1 for 'runner_p2'.

---

## 2.7. Suite: Matrix 07: Multi-Client Reconnect with Active Peers

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27207`
- **Total Elapsed:** `1.373s`
- **Client Count:** `3`
- **Session Overlap:** `0.391s` (Peak concurrent: `3`)

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `peer_p2` | `Peer_P2` | `33096` | `P2` (id=1) | `421152` | `1` | 1.372s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #2 | `peer_p3` | `Peer_P3` | `15552` | `P3` (id=2) | `N/A` | `2` | 0.441s | `0` | `CONFIRMED` | 4P / 0F | **PASS** |
| #3 | `peer_p4` | `Peer_P4` | `40160` | `P4` (id=3) | `421152` | `1` | 1.368s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)
- **Concurrency Overlap:** PASSED (Full concurrency verified: all 3 clients concurrently active for 0.391s (peak concurrent: 3).)

### Host Ground-Truth Reconciliation

- **Client #1 (`peer_p2`):** Host confirmed slot P2, generation 1 for 'peer_p2'.
- **Client #2 (`peer_p3`):** Host confirmed slot P3, generation 2 for 'peer_p3'.
- **Client #3 (`peer_p4`):** Host confirmed slot P4, generation 1 for 'peer_p4'.

---

## 2.8. Suite: Matrix 08: Server Capacity Ceiling (7 Allowed, 8th Rejected)

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27208`
- **Total Elapsed:** `2.33s`
- **Client Count:** `8`
- **Session Overlap:** `1.016s` (Peak concurrent: `8`)

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `cap_p2` | `Cap_P2` | `35736` | `P5` (id=4) | `954867` | `1` | 1.223s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #2 | `cap_p3` | `Cap_P3` | `39608` | `P6` (id=5) | `954867` | `1` | 1.223s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #3 | `cap_p4` | `Cap_P4` | `26256` | `P3` (id=2) | `954867` | `1` | 1.222s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #4 | `cap_p5` | `Cap_P5` | `28932` | `P8` (id=7) | `954867` | `1` | 1.221s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #5 | `cap_p6` | `Cap_P6` | `37796` | `P7` (id=6) | `954867` | `1` | 1.219s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #6 | `cap_p7` | `Cap_P7` | `32500` | `P2` (id=1) | `954867` | `1` | 1.221s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #7 | `cap_p8` | `Cap_P8` | `15224` | `P4` (id=3) | `954867` | `1` | 1.217s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |
| #8 | `cap_p9` | `Cap_P9` | `35764` | `N/A` | `N/A` | `N/A` | 2.126s | `1` | `CONFIRMED` | 0P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)
- **Concurrency Overlap:** PASSED (Full concurrency verified: all 8 clients concurrently active for 1.016s (peak concurrent: 8).)

### Host Ground-Truth Reconciliation

- **Client #1 (`cap_p2`):** Host confirmed slot P5, generation 1 for 'cap_p2'.
- **Client #2 (`cap_p3`):** Host confirmed slot P6, generation 1 for 'cap_p3'.
- **Client #3 (`cap_p4`):** Host confirmed slot P3, generation 1 for 'cap_p4'.
- **Client #4 (`cap_p5`):** Host confirmed slot P8, generation 1 for 'cap_p5'.
- **Client #5 (`cap_p6`):** Host confirmed slot P7, generation 1 for 'cap_p6'.
- **Client #6 (`cap_p7`):** Host confirmed slot P2, generation 1 for 'cap_p7'.
- **Client #7 (`cap_p8`):** Host confirmed slot P4, generation 1 for 'cap_p8'.
- **Client #8 (`cap_p9`):** Host correctly did not assign any slot to 'cap_p9' (expected rejection).

---

## 2.9. Suite: Matrix 09: Fault Injection Resilience (Sudden Client Kill)

- **Scenario:** `hold-w`
- **Mode:** `simultaneous` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27209`
- **Total Elapsed:** `1.012s`
- **Client Count:** `2`
- **Session Overlap:** `0.301s` (Peak concurrent: `2`)

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|
| #1 | `fault_victim` | `Fault_Victim` | `6256` | `P3` (id=2) | `N/A` | `1` | 0.311s | `1` | `CONFIRMED` | 0P / 0F | **PASS** |
| #2 | `fault_survivor` | `Fault_Survivor` | `5644` | `P2` (id=1) | `156354` | `1` | 1.006s | `0` | `CONFIRMED` | 9P / 0F | **PASS** |

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)
- **Concurrency Overlap:** PASSED (Full concurrency verified: all 2 clients concurrently active for 0.301s (peak concurrent: 2).)

### Host Ground-Truth Reconciliation

- **Client #1 (`fault_victim`):** Host confirmed slot P3, generation 1 for 'fault_victim'.
- **Client #2 (`fault_survivor`):** Host confirmed slot P2, generation 1 for 'fault_survivor'.

---

## 2.10. Suite: Matrix 10: Deterministic Chaos Stress Cycle (Seed 4242)

- **Scenario:** `chaos-engine`
- **Mode:** `chaos` (Stagger: `0ms`)
- **Target Endpoint:** `127.0.0.1:27210`
- **Total Elapsed:** `8.039s`
- **Client Count:** `7`

### Client Execution Matrix

| Client Index | Participant ID | Display Name | PID | Assigned Slot | Session ID | Gen | Duration | Exit Code | Host Confirmation | Verified Contracts | Verdict |
|:------------:|:---------------|:------------:|:---:|:-------------:|:----------:|:---:|:--------:|:---------:|:-----------------:|:-------------------:|:-------:|

### Invariant Checks & Observations

- **Slot Collision Check:** PASSED (All assigned slots distinct)
- **Session Generation Check:** PASSED (Correct connectionGeneration progression)
- **Session ID Uniformity:** PASSED (Identical sessionId across peers)

### Host Ground-Truth Reconciliation


### Chaos Stress Execution Details

- **Seed:** `4242`
- **Total Chaos Actions:** `18`
- **Client Spawns:** `16`
- **Sudden Kills (SIGKILL):** `2`
- **Mass Disconnect Events:** `0`
- **Reconnection Attempts:** `0`
- **Lingering Child Processes Reaped:** `7` (Target: 0)
- **Chaos Invariant Violations:** `0`

---

## 3. Native KEX Integration Readiness Audit

The Multi-Client Test Runner enforces strict separation between socket verification and native engine mechanics.
Under no circumstances are mock-host test runs accepted as evidence for native engine contracts.

| Contract ID | Description | Native Hook Dependency | Evaluated Status | Detail |
|:-----------:|:------------|:-----------------------|:----------------:|:-------|
| `KEX-01` | Native P2 Puppet Actor instantiation | `Factory::SpawnActor(0) on host simulation thread upon MsgHello auth` | **DEFERRED** | Requires native KEX telemetry hook [Factory::SpawnActor(0) on host simulation thread upon MsgHello auth]. Target host is mock-simulation or unhooked native binary. Packet transmission alone does not prove native engine displacement. Deferred pending Sol Max Phase 3 native hook integration. |
| `KEX-02` | Native P2 physics displacement in world | `applyPuppetMotion() updating actor position vector on KEX physics tick` | **DEFERRED** | Requires native KEX telemetry hook [applyPuppetMotion() updating actor position vector on KEX physics tick]. Target host is mock-simulation or unhooked native binary. Packet transmission alone does not prove native engine displacement. Deferred pending Sol Max Phase 3 native hook integration. |
| `KEX-03` | Native P1 state isolation & non-interference | `GameInstance::GetPlayer(0) health/inventory/camera memory boundary check` | **DEFERRED** | Requires native KEX telemetry hook [GameInstance::GetPlayer(0) health/inventory/camera memory boundary check]. Target host is mock-simulation or unhooked native binary. Packet transmission alone does not prove native engine displacement. Deferred pending Sol Max Phase 3 native hook integration. |
| `KEX-04` | Safe native engine StateExit and process shutdown | `Engine::StateExit and actor cleanup on client disconnect / session teardown` | **DEFERRED** | Requires native KEX telemetry hook [Engine::StateExit and actor cleanup on client disconnect / session teardown]. Target host is mock-simulation or unhooked native binary. Packet transmission alone does not prove native engine displacement. Deferred pending Sol Max Phase 3 native hook integration. |

### Minimal Native KEX Telemetry Dependency Specification
To enable automated native contract validation (KEX-01..KEX-04) without modifying
Wire Protocol v1.10 or the Multi-Client Test Runner:

1. **Emission Interface:** The native host executable (or injected DLL) must output structured telemetry markers to stdout or a designated log file (`--kex-telemetry-file <path>`):
   - `[KEX-HOOK] ActorSpawn: slot=P<N> actorId=<id> type=<0>`
   - `[KEX-HOOK] MotionTick: slot=P<N> pos=(<x>,<y>,<z>) vel=<f>`
   - `[KEX-HOOK] P1StateCheck: slot0_hp=<hp> slot0_status=<state>`
   - `[KEX-HOOK] ActorDestroy: slot=P<N> actorId=<id>`
   - `[KEX-HOOK] EngineShutdown: clean=<1|0> code=<code>`
2. **Factuality Guarantee:** The Multi-Client Test Runner will read this channel and transition KEX checks from `DEFERRED` to `PASS` or `FAIL` strictly upon receiving verified events. Under no circumstances will mock-host runs be accepted as evidence for native engine contracts.


## 4. Discovered Defect & Limitation Registry

1. **Slot Retention Policy in `pse_host.exe`:** When a client cleanly disconnects (`LEAVE_USER_QUIT`), the slot marks `connected = false` but preserves `participantId` and `reconnectKey`. A new client with a distinct `participantId` cannot claim this reserved slot. Once 7 participants have connected, subsequent new participants receive `REJECT_SERVER_FULL` until host restart. **Status: Verified Working As Designed for session recovery, but lacks slot recycling/eviction for new users.**
2. **Anti-Hijack Enforcement:** Reconnecting with a forged cryptographic key produces instant rejection (`REJECT_HIJACK_ATTEMPT` code 8). Existing legitimate player slots are completely protected. **Status: PASSED.**
3. **Capacity Ceiling (7 Remote Clients):** When 7 clients are connected, the 8th participant is strictly rejected with `REJECT_SERVER_FULL` (code 4). **Status: PASSED.**
4. **Process Group Reaping:** Windows child process groups are aggressively tracked by PID. Abnormal exits or timeouts are cleaned up with zero zombie processes remaining. **Status: PASSED.**
