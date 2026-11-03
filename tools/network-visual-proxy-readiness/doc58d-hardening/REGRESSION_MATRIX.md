# PowerSlave Exhumed Together — DOC-58D Full Regression & Adversarial Matrix

**Document ID:** `PSE-DOC-58D-REGRESSION-MATRIX`  
**Date:** 2026-11-03  
**Target Binary:** PowerSlave Exhumed (2022) Windows x64  
**Target EXE SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`  
**Hardened State Machine:** [`hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/hardened_visual_proxy_state_machine.hpp)  
**Multi-Slot Guard:** [`multi_slot_snapshot_guard.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/multi_slot_snapshot_guard.hpp)  
**Test Suite:** [`test_doc58d_regression.exe`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/build/Release/test_doc58d_regression.exe)  
**Machine-Readable Results:** [`TEST_RESULTS.json`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/TEST_RESULTS.json)

---

## 1. Executive Summary

The DOC-58D QA campaign executed **70 total automated test scenarios** spanning three suites:
1. **Suite 1: DOC-58B Remediation Regression** (18 scenarios): **18 / 18 PASS**
2. **Suite 2: DOC-58C Integration Contract Regression** (32 scenarios): **31 / 32 PASS**, **1 NATIVE_VERIFICATION_REQUIRED**, **0 FAIL**
   - `TC-PSE-58C-26` (Reentrancy Leak): Remediated (**FAIL $\rightarrow$ PASS**)
   - `TC-PSE-58C-28` (3–8 Player Slot Limit): Remediated (**FAIL $\rightarrow$ PASS**)
   - `TC-PSE-58C-03` (Stale `isLevelValid` / UAF Trap): Maintained strictly as **NATIVE_VERIFICATION_REQUIRED**
3. **Suite 3: DOC-58D Advanced Adversarial Suite** (20 scenarios): **20 / 20 PASS**

### Overall Statistics
| Status | Count | Percentage |
| :--- | :--- | :--- |
| **PASS** | 69 | 98.57% |
| **NATIVE_VERIFICATION_REQUIRED** | 1 | 1.43% |
| **FAIL** | 0 | 0.00% |
| **BLOCKED** | 0 | 0.00% |
| **Total Scenarios** | **70** | **100.0%** |

---

## 2. Transition Status for DOC-58C Defect Scenarios

| Test ID | Defect Description | DOC-58C Verdict | DOC-58D Verdict | Remediation Proof |
| :--- | :--- | :--- | :--- | :--- |
| **TC-PSE-58C-26** | Synchronous native hook reentrancy overwrites stored actor pointer, leaking native actor in KEX pool. | **FAIL** (2 native spawns, 1 actor leaked) | **PASS** (1 native spawn, inner call returns `ReentrancyBlocked`, 0 leaks) | Dedicated before/after repro fixtures: [`repro_reentrancy_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_before.cpp) vs [`repro_reentrancy_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_after.cpp). |
| **TC-PSE-58C-28** | Frozen DOC-55 `snapshot55::Guard` rejects player slots 2–7 with `IdentityMismatch`, blocking 3–8 co-op. | **FAIL** (Slot 3 rejected, proxy remains `ABSENT`) | **PASS** (Slot 3 accepted cleanly into `SPAWN_PENDING`) | Dedicated before/after repro fixtures: [`repro_slot_limit_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_slot_limit_before.cpp) vs [`repro_slot_limit_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_slot_limit_after.cpp). |
| **TC-PSE-58C-03** | Stale `isLevelValid == true` during `Level::Unload` invokes native `Remove` on deallocated sectors, triggering UAF. | **NATIVE_VERIFICATION_REQUIRED** | **NATIVE_VERIFICATION_REQUIRED** | Maintained as an explicit native verification gate (`GATE-PSE-58C-01`). Pre-unload teardown barrier implemented in DOC-58D (`onPreUnload()`). |

---

## 3. Complete 70-Scenario Execution Matrix

### Suite 1: DOC-58B Regression (18 Scenarios)
| ID | Scenario Name | Initial State | Injected Condition / Action | Final State | Seam Calls | Verdict | Risk |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **DOC58B-SC-01** | Failed Remove (DefiniteFailure) | REMOVE_PENDING | Native seam returns `DefiniteFailure` | QUARANTINED | 1 | **PASS** | Critical |
| **DOC58B-SC-02** | Failed Remove (Uncertain) | REMOVE_PENDING | Native seam returns `Uncertain` | QUARANTINED | 1 | **PASS** | Critical |
| **DOC58B-SC-03** | Successful Native Remove | REMOVE_PENDING | Native seam returns `ConfirmedSuccess` | ABSENT | 1 | **PASS** | High |
| **DOC58B-SC-04** | Quarantine Isolation from Updates | QUARANTINED | Dispatched valid gameplay snapshot | QUARANTINED | 0 | **PASS** | Critical |
| **DOC58B-SC-05** | Quarantine Repeated Remove Blocked | QUARANTINED | `executePendingRemove()` called | QUARANTINED | 0 | **PASS** | Critical |
| **DOC58B-SC-06** | Attested Witness Retirement | QUARANTINED | `retireQuarantinedActor(true)` | ABSENT | 0 | **PASS** | High |
| **DOC58B-SC-07** | Safe Shutdown Separation (Valid Level) | ACTIVE | `requestShutdown()` + `executeShutdownCleanup(true)` | ABSENT | 1 | **PASS** | Critical |
| **DOC58B-SC-08** | Safe Shutdown Separation (Invalid Level)| ACTIVE | `requestShutdown()` + `executeShutdownCleanup(false)` | ABSENT | 0 | **PASS** | Critical |
| **DOC58B-SC-09** | Shutdown while Quarantined (Level Valid)| QUARANTINED | `executeShutdownCleanup(true)` | QUARANTINED | 0 | **PASS** | High |
| **DOC58B-SC-10** | Shutdown while Quarantined (Level Freed)| QUARANTINED | `executeShutdownCleanup(false)` | ABSENT | 0 | **PASS** | High |
| **DOC58B-SC-11** | Sector Validation Success | SPAWN_PENDING | Valid sector 259 in Tomb of Ramses | ACTIVE | 1 | **PASS** | High |
| **DOC58B-SC-12** | Sector Validation Out-of-Bounds | SPAWN_PENDING | Invalid sector 9999 | SPAWN_PENDING | 0 | **PASS** | Critical |
| **DOC58B-SC-13** | Sector Spatial Coordinate Mismatch | SPAWN_PENDING | Sector 259 with coords in sector 120 | SPAWN_PENDING | 0 | **PASS** | High |
| **DOC58B-SC-14** | Sector Cross-Portal Transition | ACTIVE | Valid crossing from sector 259 to 123 | ACTIVE | 1 | **PASS** | High |
| **DOC58B-SC-15** | Sector Missing Validator Fail-Closed | SPAWN_PENDING | `executePendingSpawn(seam, nullptr, ...)` | SPAWN_PENDING | 0 | **PASS** | Critical |
| **DOC58B-SC-16** | Native Seam Exception Isolation | SPAWN_PENDING | Mock seam throws `std::runtime_error` | SPAWN_PENDING | 1 | **PASS** | High |
| **DOC58B-SC-17** | Thread Affinity Violation Check | ACTIVE | Snapshot handled from worker thread | ACTIVE | 0 | **PASS** | High |
| **DOC58B-SC-18** | Repeated Map Invalidation Idempotence | ACTIVE | `onMapInvalidated(true)` called x3 | ABSENT | 0 | **PASS** | High |

### Suite 2: DOC-58C Regression (32 Scenarios)
| ID | Scenario Name | Initial State | Injected Condition / Action | Final State | Seam Calls | Verdict | Risk |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-PSE-58C-01** | Clean Level Transition Shutdown | ACTIVE | Normal map change sequence | ABSENT | 1 | **PASS** | High |
| **TC-PSE-58C-02** | Shutdown after Sector Deallocation | ACTIVE | Level unloaded before cleanup | ABSENT | 0 | **PASS** | Critical |
| **TC-PSE-58C-03** | Stale `isLevelValid` during Unload (UAF) | ACTIVE | Native Remove called after sector free | UAF_DETECTED | 1 | **NATIVE_VERIFICATION_REQUIRED** | Critical |
| **TC-PSE-58C-04** | Repeated Level Unload Idempotence | ABSENT | Double `onMapInvalidated(true)` | ABSENT | 0 | **PASS** | Medium |
| **TC-PSE-58C-05** | Disconnect $\rightarrow$ Map Transition $\rightarrow$ Reconnect | ACTIVE | Reconnect in new map epoch 7366 | ACTIVE | 2 | **PASS** | High |
| **TC-PSE-58C-06** | ConfirmedSuccess Native Free | REMOVE_PENDING| KEX drain + pool return confirmed | ABSENT | 1 | **PASS** | High |
| **TC-PSE-58C-07** | DefiniteFailure Native Isolation | REMOVE_PENDING| Native remove rejected by KEX | QUARANTINED | 1 | **PASS** | Critical |
| **TC-PSE-58C-08** | Uncertain Native Outcome Isolation | REMOVE_PENDING| Native hook returns Uncertain | QUARANTINED | 1 | **PASS** | Critical |
| **TC-PSE-58C-09** | Deferred Destruction / Sweep Race | REMOVE_PENDING| Drain done, sweep pending $\rightarrow$ re-spawn | ACTIVE | 2 | **PASS** | High |
| **TC-PSE-58C-10** | Blind Repeated Remove Blocked | QUARANTINED | `executePendingRemove()` | QUARANTINED | 0 | **PASS** | Critical |
| **TC-PSE-58C-11** | Snapshot & Spawn Blocked during Quarantine| QUARANTINED | Snapshot + spawn dispatch | QUARANTINED | 0 | **PASS** | Critical |
| **TC-PSE-58C-12** | Quarantine Retention across Reconnect | QUARANTINED | Disconnect and reconnect | QUARANTINED | 0 | **PASS** | High |
| **TC-PSE-58C-13** | Shutdown while Quarantined (Valid vs Unloaded)| QUARANTINED | Valid blocks; unloaded clears to ABSENT | ABSENT | 0 | **PASS** | High |
| **TC-PSE-58C-14** | Pointer Address Reuse (ABA) Disambiguation| ABSENT | Recycled pointer across reload | ACTIVE | 2 | **PASS** | High |
| **TC-PSE-58C-15** | `retireQuarantinedActor` Witness Rule | QUARANTINED | Unwitnessed vs witnessed retirement | ABSENT | 0 | **PASS** | Critical |
| **TC-PSE-58C-16** | Out-of-Bounds Sector ID Rejection | SPAWN_PENDING | Sector -1 and 9999 | SPAWN_PENDING | 0 | **PASS** | Critical |
| **TC-PSE-58C-17** | Coordinate / Sector Spatial Mismatch | SPAWN_PENDING | Inverted coordinates | SPAWN_PENDING | 0 | **PASS** | High |
| **TC-PSE-58C-18** | Missing Sector Validator | SPAWN_PENDING | `validator = nullptr` | SPAWN_PENDING | 0 | **PASS** | High |
| **TC-PSE-58C-19** | Portal Crossing Verification (259 $\rightarrow$ 123) | ACTIVE | Crossing portal aperture | ACTIVE | 1 | **PASS** | High |
| **TC-PSE-58C-20** | Aperture Boundary Tolerance (Y = -6912.0) | ACTIVE | Snap on aperture plane | ACTIVE | 1 | **PASS** | Medium |
| **TC-PSE-58C-21** | Map Epoch Change between Snapshot & Spawn | SPAWN_PENDING | Map epoch 7365 $\rightarrow$ 7366 | SPAWN_PENDING | 0 | **PASS** | High |
| **TC-PSE-58C-22** | Worker Thread Cross-Call Rejection | ACTIVE | Dispatch from worker thread | ACTIVE | 0 | **PASS** | High |
| **TC-PSE-58C-23** | Mock Native Seam Exception Isolation | SPAWN_PENDING | Exception during spawn | SPAWN_PENDING | 1 | **PASS** | High |
| **TC-PSE-58C-24** | noexcept Specification Verification | ACTIVE | All lifecycle methods verified noexcept | ACTIVE | 0 | **PASS** | High |
| **TC-PSE-58C-25** | Out-of-Order Lifecycle Sequence Resilience | ABSENT | Spawn before snapshot, remove before spawn | ABSENT | 0 | **PASS** | High |
| **TC-PSE-58C-26** | Reentrant Native Seam Callback Guard | SPAWN_PENDING | Reentrant spawn callback inside seam | ACTIVE | 1 | **PASS** | Critical |
| **TC-PSE-58C-27** | Multi-Slot Filtering & Target Isolation | ABSENT | Slot 1 snapshot to Proxy 0 | SPAWN_PENDING | 0 | **PASS** | High |
| **TC-PSE-58C-28** | 3–8 Player Slot MultiSlotGuard8 Support | ABSENT | Slot 3 snapshot dispatched | SPAWN_PENDING | 0 | **PASS** | Critical |
| **TC-PSE-58C-29** | Independent Proxy Generations Across Slots | ACTIVE | Proxy 1 removed; Proxy 0 gen verified | GEN0_UNCHANGED| 1 | **PASS** | High |
| **TC-PSE-58C-30** | Single Client Disconnect with Others Active| ACTIVE | Proxy 1 drops; Proxy 0 streams | PROXY0_ACTIVE | 1 | **PASS** | High |
| **TC-PSE-58C-31** | Quarantine Blast Radius Isolation | ACTIVE | Proxy 1 quarantined; Proxy 0 unaffected | PROXY0_ACTIVE | 1 | **PASS** | Critical |
| **TC-PSE-58C-32** | Coordinated Reverse Shutdown Order (8 Slots) | 8_PROXIES | Reverse shutdown of slots 7..0 after unload | ALL_ABSENT | 0 | **PASS** | High |

### Suite 3: DOC-58D New Advanced Adversarial Tests (20 Scenarios)
| ID | Scenario Name | Initial State | Injected Condition / Action | Final State | Seam Calls | Verdict | Risk |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-PSE-58D-01** | Reentrant Spawn Blocked (`m_inSeamCall`) | SPAWN_PENDING | Reentrant `executePendingSpawn()` | ACTIVE | 1 | **PASS** | Critical |
| **TC-PSE-58D-02** | Reentrant Remove Blocked (Double-Free Avoid)| REMOVE_PENDING| Reentrant `executePendingRemove()` | ABSENT | 1 | **PASS** | Critical |
| **TC-PSE-58D-03** | Reentrant Shutdown Cleanup Blocked | ACTIVE | Reentrant `executeShutdownCleanup()` | ABSENT | 1 | **PASS** | Critical |
| **TC-PSE-58D-04** | Reentrant Snapshot during Transform Update | ACTIVE | Reentrant `handleSnapshot()` | ACTIVE | 1 | **PASS** | High |
| **TC-PSE-58D-05** | Reentrant `requestShutdown` Blocked | ACTIVE | Reentrant `requestShutdown()` | ACTIVE | 1 | **PASS** | High |
| **TC-PSE-58D-06** | Scope Guard Unwinding on Seam Exception | SPAWN_PENDING | Seam throws; verify `m_inSeamCall` reset | ACTIVE | 2 | **PASS** | High |
| **TC-PSE-58D-07** | Sequential Reentrancy Burst Resilience | SPAWN_PENDING | Burst of 5 reentrant calls during spawn | ACTIVE | 1 | **PASS** | High |
| **TC-PSE-58D-08** | Full 8-Slot Sweep (Slots 0..7 All Accepted) | 8_PROXIES | Valid snapshots for slots 0 through 7 | ALL_SPAWN_PEND| 0 | **PASS** | Critical |
| **TC-PSE-58D-09** | Invalid Slot 8 Boundary Rejection | ABSENT | Snapshot with `slotId = 8` | ABSENT | 0 | **PASS** | High |
| **TC-PSE-58D-10** | Invalid Slot 255 Boundary Rejection | ABSENT | Snapshot with `slotId = 255` | ABSENT | 0 | **PASS** | High |
| **TC-PSE-58D-11** | Cross-Slot Replay Isolation | ABSENT | Slot 2 snapshot replayed to Proxy 3 | ABSENT | 0 | **PASS** | High |
| **TC-PSE-58D-12** | Simultaneous 7 Remote Proxies Streaming | 7_PROXIES_ACT | 10 transform updates to 7 remote proxies | ALL_ACTIVE | 70 | **PASS** | Critical |
| **TC-PSE-58D-13** | Client Disconnect & Reconnect in Slot 4 | ACTIVE | Slot 4 drops and reconnects with gen 2 | ACTIVE | 2 | **PASS** | High |
| **TC-PSE-58D-14** | Quarantine Blast Radius Isolation (Slot 6 vs 7)| ACTIVE | Slot 6 quarantined; Slot 7 unaffected | P6_QUAR_P7_ACT| 1 | **PASS** | Critical |
| **TC-PSE-58D-15** | Reverse-Order Shutdown (8 Slots) | 8_PROXIES | Decommission slots 7 down to 0 after unload | ALL_ABSENT | 0 | **PASS** | High |
| **TC-PSE-58D-16** | Pre-Unload Barrier Activation (`onPreUnload`) | SPAWN_PENDING | Pre-unload barrier activated | ABSENT | 0 | **PASS** | Critical |
| **TC-PSE-58D-17** | Pre-Unload Barrier Blocks Snapshots | ACTIVE | Snapshot dispatched while barrier active | ACTIVE | 0 | **PASS** | High |
| **TC-PSE-58D-18** | Pre-Unload Barrier Blocks Spawn | SPAWN_PENDING | `executePendingSpawn()` while barrier active | SPAWN_PENDING | 0 | **PASS** | High |
| **TC-PSE-58D-19** | Pre-Unload Barrier Skips Native Remove | REMOVE_PENDING| `executePendingRemove()` while barrier active| ABSENT | 0 | **PASS** | Critical |
| **TC-PSE-58D-20** | Post-Unload Clean State Reset (`onPostUnload`) | ACTIVE | Pre-unload $\rightarrow$ KEX unload $\rightarrow$ Post-unload| ABSENT | 0 | **PASS** | Critical |

---

## 4. Verification Gate Index

| Gate ID | Target Function / Symbol | Native Verification Requirement | Status | Assigned Role |
| :--- | :--- | :--- | :--- | :--- |
| **GATE-PSE-58C-01** | `kexSlaveLevelLocal::Unload` (`0x1401c90e0`) | Pre-unload hook barrier must trigger prior to sector array deallocation at offset `+0x140`. | **OPEN** | GPT-6.1 Sol / Frida |
| **GATE-PSE-58C-02** | `kexActor::Destroy` | Verify whether deferred drain / pending sweep allows immediate actor pool reuse. | **OPEN** | GPT-6.1 Sol / REA |
| **GATE-PSE-58C-03** | `kexWldSectorLocal::InsertActor` (`0x1401e58f0`) | Verify assert `assert7b3c0` is impossible when validator rejects invalid sector IDs. | **OPEN** | GPT-6.1 Sol / REA |
| **GATE-PSE-58C-04** | Simulation Thread Dispatch | Verify all state machine mutations occur strictly on the simulation loop thread. | **OPEN** | Claude Opus / DOC-58A |
| **GATE-PSE-58C-05** | Native Hook Non-Reentrancy | Verify native hooks never synchronously call back into state machine APIs. | **OPEN** | Claude Opus / DOC-58A |
| **GATE-PSE-58C-06** | 8-Player Native Allocation | Verify KEX actor pool capacity supports 7 remote visual proxy actors concurrently. | **OPEN** | GPT-6.1 Sol / Frida |
| **GATE-PSE-58C-07** | ENet Packet Dispatch to Slots | Verify ENet packet demux correctly routes snapshot streams to slots 0..7 without crosstalk. | **OPEN** | Claude Opus / DOC-58A |
