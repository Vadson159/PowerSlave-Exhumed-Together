# PowerSlave Exhumed Together — M1.2a Adversarial QA Audit Report

**Target Binary:** `tools/m12a-test-client/build/Release/m12a_test_client.exe`  
**Audit Harness:** `tools/m12a-adversarial-qa/m12a_adversarial_qa.exe`  
**Execution Timestamp:** 2026-11-01 23:27:14 UTC  
**Wire Protocol:** v1.10 (`0x0110`, Magic: `0x50534558`)  

## 1. Executive Summary

This adversarial audit independently evaluated the veracity of `m12a_test_client.exe`'s 15/15 PASS claims by running it as an external child process against a controlled, fault-injecting ENet test host across 9 adversarial scenarios.

| Audit Metric | Count | Status & Significance |
|---|---|---|
| **True Injected Fault Detections** | **5 / 5** | **CONFIRMED:** Client reliably caught malformed slots, gen=0, mid-stream disconnects, stale gen on reconnect, and handshake timeouts. |
| **Robustness / Fuzzing Survival** | **1 / 1** | **CONFIRMED:** Client discarded duplicate and stale snapshot packets without crash or buffer corruption. |
| **Independent Host Receipt Audit** | **1 / 1** | **CONFIRMED:** Host independently verified arrival of 30 forward, 1 release, and 16 neutral packets with monotonic sequences. |
| **Protocol Contract Boundary** | **1** | **CLARIFIED:** Client transmission counter (`NET-06`) proves wire transmission, but does not guarantee host delivery under network loss. |
| **False-Positive PASS Discovered** | **1** | **CRITICAL DEFECT DETECTED:** In `NET-14`, if a buggy host erroneously accepts a forged reconnect key instead of rejecting it, the client logs an optimistic fallback message and reports PASS (exit code 0)! |

## 2. Adversarial Scenarios & Evidence

| ID | Scenario Name | Injected Fault | Expected Client Behavior | Actual Exit | Verdict | Observations |
|---|---|---|---|---|---|---|
| **ADV-01** | Malformed WELCOME: Invalid Slot (0xFF >= 8) | Host returns MSG_WELCOME with assignedSlot = 0xFF | Client detects invalid slot, NET-03 FAILS, exit code 1 | Code 1 | **TRUE_DETECTION** | Host sent slot=255. Client rejected slot and terminated with exit code 1 |
| **ADV-02** | Malformed WELCOME: Invalid Generation (gen = 0) | Host returns MSG_WELCOME with connectionGeneration = 0 | Client detects gen=0 != 1, NET-04 FAILS, exit code 1 | Code 1 | **TRUE_DETECTION** | Host sent gen=0. Client flagged NET-04 FAIL and exited with code 1 |
| **ADV-03** | Premature Disconnect During W-Hold | Host kicks client after 5 ticks of W hold (before 30 ticks and release) | Client detects disconnect, aborts stream, NET-06 FAILS, exit code 1, NO hang | Code 1 | **TRUE_DETECTION** | Host received 5 commands before severing connection. Client cleanly caught socket severance without freezing. |
| **ADV-04** | Incorrect Session Generation After Reconnect | Host accepts reconnect but repeats stale connectionGeneration=1 (no increment) | Client detects gen2 == gen1, NET-12 FAILS, exit code 1 | Code 1 | **TRUE_DETECTION** | Host sent stale gen=1 on reconnect. Client flagged NET-12 FAIL and exited with code 1 |
| **ADV-05** | Handshake Delay & Timeout Exceeded | Host is silent and never responds to MSG_HELLO | Client times out gracefully after ~1000ms, NET-02 FAILS, exit code 1, NO hang | Code 1 | **TRUE_DETECTION** | Host ignored MSG_HELLO. Client cleanly timed out after 1000ms without freezing. |
| **ADV-06** | Stale & Duplicate Snapshot Packets Injection | Host injects duplicate snapshot seq=100 and stale snapshot seq=50 | Client discards stale/duplicate snapshots without crash or memory corruption, exit code 0 | Code 0 | **PASS_ROBUST** | Injected duplicate seq=100 and stale seq=50. Client handled cleanly without error. |
| **ADV-07** | Independent Host-Side Receipt Verification (W / Release / Neutral) | None (Conformant baseline; host independently tallies received packets) | Client reports 30 forward, 1 release, 16 neutral. Host independently verifies receipt. | Code 0 | **TRUE_DETECTION** | Host independently received: 30 forward (>=30), 1 release (>=1), 16 neutral (>=15). Sequence range: 1..46. All matched client claims! |
| **ADV-08** | Unreliable Input Packet Loss Contrast (Send vs Receive) | Host network drops input packet seq=15 on CHANNEL_UNRELIABLE_SEQUENCED | Client NET-06 passes locally (30 sent), but host receives only 29. Protocol distinction highlighted. | Code 0 | **CONTRACT_LIMITATION** | CRITICAL PROTOCOL INSIGHT: Client reported 30 sent (PASS), but host received 29 (dropped seq=15). Proves client check is purely a TRANSMISSION check, not host RECEIPT under network loss. |
| **ADV-09** | Audit of Anti-Hijack Assertion Under Buggy Host Key Acceptance | Host bug: host erroneously ACCEPTS forged reconnectKey 0xDEADBEEFCAFEULL instead of rejecting | Should FAIL (host failed to defend slot). Actual client behavior will be tested. | Code 0 | **FALSE_PASS** | DISCOVERED FALSE-POSITIVE PASS! When host buggily accepted forged key, client logged: 'Participant was not on host; host allocated fresh session' and declared [PASS] NET-14 (exit code 0)! |

## 3. Detailed Findings

### 3.1. Truly Detected Faults (Legitimate PASS)
- **`ADV-01` (Malformed Slot 0xFF):** Client immediately flagged `[FAIL] NET-03: Invalid slot id: 255` and exited with code 1.
- **`ADV-02` (Malformed Initial Gen 0):** Client caught `[FAIL] NET-04: Unexpected initial gen=0 (expected 1)` and exited with code 1.
- **`ADV-03` (Premature Disconnect):** When socket severed at tick 5, client cleanly aborted stream, flagged `[FAIL] NET-06: Sent only 5 forward commands`, and exited with code 1 without hanging.
- **`ADV-04` (Stale Reconnect Generation):** When host failed to increment generation on reconnect, client caught `[FAIL] NET-12: connectionGeneration was not incremented! gen1=1, gen2=1` and exited with code 1.
- **`ADV-05` (Handshake Timeout):** When host ignored `MSG_HELLO`, client waited exactly `--timeout-ms 1000`, caught `[FAIL] NET-02`, and exited with code 1 without freezing.
- **`ADV-06` (Packet Fuzzing):** Client cleanly dropped duplicate and out-of-order snapshots, preserving internal state monotonicity.

### 3.2. Discovered False-Positive PASS (`ADV-09`)
> [!WARNING]
> **CRITICAL DEFECT IN TEST CLIENT:** `ADV-09` discovered a false-positive PASS in `NET-14` (Anti-Hijack Gate).
>
> In `src/m12a_scenarios.cpp` (lines 318-323):
> ```cpp
> } else if (client.getState() == ClientState::AUTHENTICATED) {
>     report.addPass("NET-14", "Anti-hijack defense gate",
>                    "Participant was not on host; host allocated fresh session with new key (non-hijackable)", attemptMs);
> }
> ```
> When the adversarial host **erroneously accepted** a forged key (`0xDEADBEEFCAFEULL`) targeting a registered participant, the client took this `else if` branch and declared **`[PASS] NET-14`** with process exit code 0!
> A buggy host that fails to enforce cryptographic authentication would thus pass QA undetected.

### 3.3. Protocol Contract Limitation (`ADV-08`)
> [!NOTE]
> **Send Counter vs Host Delivery:** `ADV-08` proved that `m12a_test_client`'s `NET-06`, `NET-07`, and `NET-08` count packets dispatched locally into ENet.
> When UDP packet 15 was dropped on the wire, the client still reported `[PASS] NET-06: Transmitted 30 commands`, while the host only received 29.
> Sol Max and QA must understand that `NET-06` is a **transmission proof**, not a host delivery receipt.

### 3.4. Conditions Unconfirmable Without Native KEX Host
The following 4 conditions can **never** be verified over ENet alone:
1. `KEX-01`: Native Puppet Actor instantiation (`Factory::SpawnActor`, type 0) in host process memory.
2. `KEX-02`: Actual physics displacement and level collision in KEX BSP sectors.
3. `KEX-03`: P1 non-interference (P1 input, camera, inventory, health).
4. `KEX-04`: Clean StateExit and platform shutdown without crashes or memory leaks.

## 4. Required Fixes Before Sol Max M1.2a

1. **Fix `NET-14` False PASS:** In `m12a_scenarios.cpp`, remove the optimistic `else if (client.getState() == ClientState::AUTHENTICATED) addPass` fallback. If the client targeted an existing participant ID with a forged key, host acceptance MUST be recorded as `addFail("NET-14", "Host erroneously authenticated forged key!")`.
2. **Host-Side Receipt Tap in M1.2a Test Fixture:** Sol Max must verify input reception in KEX adapter memory (`MockKexSimulation` or native adapter input queue), not just rely on the client's local send report.

## 5. Reproduction Instructions

```powershell
# Build and execute the complete adversarial QA audit:
cd tools/m12a-adversarial-qa
.\build.bat
.\build\Release\m12a_adversarial_qa.exe
```
