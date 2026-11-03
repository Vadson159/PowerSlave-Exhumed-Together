# PowerSlave Exhumed Together — M1.2a Adversarial QA Hardening Report

**Target Binary:** `tools/m12a-test-client/build/Release/m12a_test_client.exe` (Hardened)  
**Audit Harness:** `tools/m12a-adversarial-qa/m12a_adversarial_qa.exe`  
**Execution Timestamp:** 2026-11-01 23:38:14 UTC  
**Wire Protocol:** v1.10 (`0x0110`, Magic: `0x50534558`)  

## 1. Executive Summary

This report records the verification of the hardening applied to `m12a_test_client.exe` following the initial adversarial audit (`REPORT.md`). The audit had uncovered a critical false-positive PASS in `NET-14` (Anti-Hijack Gate) under `ADV-09`.

| Hardening Metric | Count | Status & Significance |
|---|---|---|
| **True Injected Fault Detections** | **6 / 6** | **CONFIRMED:** Hardened client reliably caught all 6 negative faults (malformed slot, malformed gen, premature disconnect, stale gen reconnect, handshake timeout, and forged key acceptance). |
| **Robustness / Fuzzing Survival** | **1 / 1** | **CONFIRMED:** Client discarded duplicate and stale snapshot packets without crash or buffer corruption. |
| **Independent Host Receipt Audit** | **1 / 1** | **CONFIRMED:** Host independently verified arrival of 30 forward, 1 release, and 16 neutral packets with monotonic sequences. |
| **Protocol Contract Boundary** | **1** | **CLARIFIED:** Client transmission counter (`NET-06/07/08`) proves local transmission dispatch, but does not guarantee lossy UDP host arrival or native KEX movement. |
| **False-Positive PASS Discovered** | **0** | **RESOLVED:** The previous defect in `NET-14` has been eliminated. Buggy host acceptance of forged keys now strictly causes `[FAIL] NET-14` and process exit code 1. |

## 2. Adversarial Scenarios & Evidence Scorecard

| ID | Scenario Name | Injected Fault | Expected Behavior | Actual Exit | Verdict | Observations |
|---|---|---|---|---|---|---|
| **ADV-01** | Malformed WELCOME: Invalid Slot (0xFF >= 8) | Host returns MSG_WELCOME with assignedSlot = 0xFF | Client detects invalid slot, NET-03 FAILS, exit code 1 | Code 1 | **TRUE_DETECTION** | Host sent slot=255. Client rejected slot and terminated with exit code 1 |
| **ADV-02** | Malformed WELCOME: Invalid Generation (gen = 0) | Host returns MSG_WELCOME with connectionGeneration = 0 | Client detects gen=0 != 1, NET-04 FAILS, exit code 1 | Code 1 | **TRUE_DETECTION** | Host sent gen=0. Client flagged NET-04 FAIL and exited with code 1 |
| **ADV-03** | Premature Disconnect During W-Hold | Host kicks client after 5 ticks of W hold (before 30 ticks and release) | Client detects disconnect, aborts stream, NET-06 FAILS, exit code 1, NO hang | Code 1 | **TRUE_DETECTION** | Host received 5 commands before severing connection. Client cleanly caught socket severance without freezing. |
| **ADV-04** | Incorrect Session Generation After Reconnect | Host accepts reconnect but repeats stale connectionGeneration=1 (no increment) | Client detects gen2 == gen1, NET-12 FAILS, exit code 1 | Code 1 | **TRUE_DETECTION** | Host sent stale gen=1 on reconnect. Client flagged NET-12 FAIL and exited with code 1 |
| **ADV-05** | Handshake Delay & Timeout Exceeded | Host is silent and never responds to MSG_HELLO | Client times out gracefully after ~1000ms, NET-02 FAILS, exit code 1, NO hang | Code 1 | **TRUE_DETECTION** | Host ignored MSG_HELLO. Client cleanly timed out after 1000ms without freezing. |
| **ADV-06** | Stale & Duplicate Snapshot Packets Injection | Host injects duplicate snapshot seq=100 and stale snapshot seq=50 | Client discards stale/duplicate snapshots without crash or memory corruption, exit code 0 | Code 0 | **PASS_ROBUST** | Injected duplicate seq=100 and stale seq=50. Client handled cleanly without error. |
| **ADV-07** | Independent Host-Side Receipt Verification (W / Release / Neutral) | None (Conformant baseline; host independently tallies received packets) | Client reports 30 forward, 1 release, 16 neutral. Host independently verifies receipt. | Code 0 | **CONFORMANT_AUDIT** | Host independently received: 30 forward (>=30), 1 release (>=1), 16 neutral (>=15). Sequence range: 1..46. All matched client claims! |
| **ADV-08** | Unreliable Input Packet Loss Contrast (Send vs Receive) | Host network drops input packet seq=15 on CHANNEL_UNRELIABLE_SEQUENCED | Client NET-06 passes locally (30 sent), but host receives only 29. Protocol distinction highlighted. | Code 0 | **CONTRACT_LIMITATION** | CRITICAL PROTOCOL INSIGHT: Client reported 30 sent (PASS), but host received 29 (dropped seq=15). Proves client check is purely a TRANSMISSION check, not host RECEIPT under network loss. |
| **ADV-09** | Audit of Anti-Hijack Assertion Under Buggy Host Key Acceptance | Host bug: host erroneously ACCEPTS forged reconnectKey 0xDEADBEEFCAFEULL instead of rejecting | Client detects host hijack attempt and reports [FAIL] NET-14 with exit code 1 | Code 1 | **TRUE_DETECTION** | CONFIRMED HARDENED: Client detected buggy host acceptance of forged key, declared [FAIL] NET-14, and terminated with exit code 1! |

## 3. Detailed Verification of Hardening

### 3.1. Resolution of NET-14 False-Positive PASS (ADV-09)
- **Defect in Prior Version:** The original test client contained a permissive fallback `else if (client.getState() == ClientState::AUTHENTICATED) addPass(...)` in `m12a_scenarios.cpp`. When an adversarial host improperly accepted forged reconnect key `0xDEADBEEFCAFEULL` instead of rejecting it, the client recorded `[PASS] NET-14` with exit code 0.
- **Hardened Implementation:**
  1. **Existing Participant Seeding:** When evaluating an anti-hijack defense, the test client ensures the target participant ID is established on the host first, making it a genuine existing session.
  2. **Strict Cryptographic Gate:** For an existing participant ID, the client requires `MSG_REJECT` with `REJECT_HIJACK_ATTEMPT` (code 8) or `REJECT_ALREADY_CONNECTED` (code 7).
  3. **Slot/Session Identity Inspection:** If `MSG_WELCOME` is received, the client inspects slot and session identity:
     - If `reconnectKey == forgedKey` OR `generation > 1` OR existing slot was granted -> **`[FAIL] NET-14: CRITICAL HIJACK DEFECT`**.
     - Only if the host allocated a new independent session (`gen=1`, fresh server key `!= forgedKey`) is a non-hijack state recognized.
- **Hardening Proof:**
  - On buggy adversarial host (`ACCEPT_FORGED_KEY`): `m12a_test_client` immediately declares **`[FAIL] NET-14: CRITICAL HIJACK DEFECT`** and exits with code 1. `ADV-09` confirms **`TRUE_DETECTION`**.
  - On conformant `pse_host.exe`: `m12a_test_client` receives `MSG_REJECT (code 8)`, logs **`[PASS] NET-14`**, and exits with code 0.
  - In-process `--self-test`: Passes 15/15 with exit code 0.

### 3.2. Clarification of Local Dispatch (NET-06, NET-07, NET-08)
> [!IMPORTANT]
> **Local Wire Dispatch vs Lossy Delivery & KEX Movement:**
> As proven by `ADV-08`, checks `NET-06`, `NET-07`, and `NET-08` measure packets formatted and dispatched into the local ENet socket queue (`enet_peer_send`).
> Over unreliable UDP channels (`CHANNEL_UNRELIABLE_SEQUENCED`), packets can be dropped in transit (e.g., client dispatches 30 forward commands, host receives 29). Local send assertions do NOT constitute delivery receipts on the host, nor do they prove native KEX physics displacement.

### 3.3. Conditions Unverifiable Without Native KEX Host
The following 4 conditions remain deferred until GPT-6.1 Sol Max connects the native KEX DLL adapter to ENet:
1. `KEX-01`: Native Puppet Actor instantiation (`Factory::SpawnActor`, type 0) in host process memory.
2. `KEX-02`: Actual physics displacement and level collision in KEX BSP sectors.
3. `KEX-03`: P1 non-interference (P1 input, camera, inventory, health).
4. `KEX-04`: Clean StateExit and platform shutdown without crashes or memory leaks.

## 4. Reproduction Instructions

```powershell
# 1. Build hardened test client and adversarial QA harness:
cd tools/m12a-test-client
.\build.bat
cd ../m12a-adversarial-qa
.\build.bat

# 2. Run adversarial QA suite (confirms 6/6 fault detections, 0 false passes):
.\run_adversarial_qa.bat

# 3. Run client against in-process mock host (confirms 15/15 PASS):
cd ../m12a-test-client
.\build\Release\m12a_test_client.exe --self-test
```
