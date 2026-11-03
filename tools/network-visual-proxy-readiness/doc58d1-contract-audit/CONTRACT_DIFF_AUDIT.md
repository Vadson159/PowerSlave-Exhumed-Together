# PowerSlave Exhumed Together — DOC-58D.1 Contract Diff & Regression Integrity Audit

**Document ID:** `PSE-DOC58D1-CONTRACT-DIFF-AUDIT`  
**Date:** 2026-11-03  
**Auditor:** Gemini 3.8 High — Independent Senior C++20 / Multiplayer QA & Code Auditor  
**Target Candidate:** [`doc58d1_hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/doc58d1_hardened_visual_proxy_state_machine.hpp)  
**Snapshot Guard:** [`multi_slot_snapshot_guard.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/multi_slot_snapshot_guard.hpp)  
**Reconciled Test Suite:** [`test_doc58d1_reconciled.exe`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/build/Release/test_doc58d1_reconciled.exe)  
**Machine-Readable Results:** [`TEST_RESULTS.json`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/TEST_RESULTS.json)

---

## 1. Audit Scope & Executive Summary

This audit reconciles the test scenarios and documentation across DOC-58B, DOC-58C, DOC-58D, and the refined candidate DOC-58D.1.
The primary objectives were:
1. Identify and resolve any semantic discrepancies or documentation errors in the regression suites.
2. Investigate specific anomalies highlighted in `TC-PSE-58C-21`, `TC-PSE-58C-23`, and `DOC58B-SC-16`.
3. Verify that no checks were weakened, renamed, or faked to achieve a PASS.
4. Execute the full 74-scenario regression suite against the updated DOC-58D.1 candidate.

### Summary Verdict
- **Total Scenarios Audited:** 74
- **PASS:** 73 (98.65%)
- **NATIVE_VERIFICATION_REQUIRED:** 1 (1.35%) (`TC-PSE-58C-03` / `GATE-PSE-58C-01`)
- **FAIL / BLOCKED:** 0 (0.00%)
- **Candidate Quality:** `REVIEW_READY`

---

## 2. Deep Dive: Key Discrepancy Investigations

### A. Investigation of `TC-PSE-58C-21` (Map Invalidation vs Map Epoch Change)

#### 1. The Discrepancy
- **DOC-58C Original Test:** `TC-PSE-58C-21` in `test_integration_contracts.cpp` (lines 1244–1279):
  - Scenario Name: `"Map Epoch Change between Snapshot & Spawn"`
  - Initial State: `SPAWN_PENDING`
  - Injected Action: `sm.onMapInvalidated(true /* levelAlreadyFreed */);`
  - Assertion: `sm.state() == VisualProxyState::ABSENT`, `spawnRes == SpawnExecutionResult::NotPending`
  - Recorded Outcome: Expected `ABSENT`, Actual `ABSENT`, Verdict `PASS`
- **DOC-58D Documentation Error:**
  - In DOC-58D's `REGRESSION_MATRIX.md` (line 77), the table entry was incorrectly recorded as:  
    `| TC-PSE-58C-21 | Map Epoch Change between Snapshot & Spawn | SPAWN_PENDING | Map epoch 7365 -> 7366 | SPAWN_PENDING | 0 | PASS | High |`
  - The documentation erroneously showed `Expected: SPAWN_PENDING, Actual: SPAWN_PENDING` and described the action as `Map epoch 7365 -> 7366`, while the actual executable code in `test_doc58d_regression.cpp` (lines 1250–1262) executed `onMapInvalidated(true)` and expected `ABSENT`!
- **Root Cause:** A copy-paste documentation error occurred in DOC-58D's markdown table generation where an unexecuted conceptual draft row was pasted into `REGRESSION_MATRIX.md` instead of the actual test execution record.

#### 2. Reconciled Contract & Code Remediation
1. In DOC-58D.1, the test `TC-PSE-58C-21` has been strictly restored to verify that `onMapInvalidated(true)` while `SPAWN_PENDING` transitions state to `ABSENT` and rejects subsequent spawn calls (`SpawnExecutionResult::ShutdownBlocked` / `NotPending`).
2. Furthermore, a new scenario **`TC-PSE-58D1-01`** was implemented to verify what happens when a true map epoch advance occurs via `configureSession(7366, 1)`:
   - **Pre-condition:** State machine has processed a snapshot for epoch 7365 and is in `SPAWN_PENDING`.
   - **Action:** Host advances map epoch, calling `sm.configureSession(7366, 1)`.
   - **Remediated Candidate Invariant:** `doc58d1_hardened_visual_proxy_state_machine.hpp` explicitly drops any stale pending spawn to `ABSENT`:
     ```cpp
     if (m_state == VisualProxyState::SPAWN_PENDING) {
         m_state = VisualProxyState::ABSENT; // Stale spawn cancelled
     }
     ```
   - **Verification:** Verified by `TC-PSE-58D1-01` $\rightarrow$ **PASS**.

---

### B. Investigation of `TC-PSE-58C-23` and `DOC58B-SC-16` / `SC-09` (Native Exception Safety)

#### 1. The Discrepancy
- The prompt identified an apparent conflict:
  - Original DOC-58C supposedly expected `QUARANTINED` upon native seam exception.
  - DOC-58D documentation indicated `SPAWN_PENDING`.
  - In `REGRESSION_MATRIX.md`, `DOC58B-SC-16` was labeled `"Native Seam Exception Isolation"`.

#### 2. Audit Findings from Source Code
1. **The Scrambled Markdown Table in DOC-58D:**
   In DOC-58B's original `test_remediation_comparative.cpp`:
   - `SCENARIO-09` was: `"Exception from Mock Native Seam"` (tested spawn exception $\rightarrow$ stay in `SPAWN_PENDING`, followed by remove exception $\rightarrow$ transition to `QUARANTINED`).
   - `SCENARIO-16` was: `"Repeated Spawn After Quarantined Removal"` (tested that when an actor is already quarantined, new snapshots and spawns are blocked).
   In DOC-58D's `REGRESSION_MATRIX.md`, the author manually typed an inverted table where `SC-09` and `SC-16` were transposed! Line 44 labeled `DOC58B-SC-16` as "Native Seam Exception Isolation" with `SPAWN_PENDING`.
2. **The 3-Step Execution Structure of `TC-PSE-58C-23`:**
   In both DOC-58C (`test_integration_contracts.cpp` lines 1348–1382) and DOC-58D (`test_doc58d_regression.cpp` lines 1286–1304):
   - **Step 1 (Spawn Exception):** `seam.shouldThrowOnSpawn = true`. `executePendingSpawn` caught the exception and returned `NativeExceptionCaught`. State remained `SPAWN_PENDING`.
   - **Step 2 (Retry Spawn):** `seam.shouldThrowOnSpawn = false`. `executePendingSpawn` succeeded cleanly. State transitioned to `ACTIVE`.
   - **Step 3 (Remove Exception):** `seam.shouldThrowOnRemove = true`. Disconnect triggered `executePendingRemove`. The exception was caught, returned `NativeExceptionCaught`, and transitioned state to `QUARANTINED`.
   - **Final Test Summary Record:** Because Step 3 was the final step of the scenario, the test runner recorded `actualState: "QUARANTINED"`.
   - In DOC-58D's markdown table, the author summarized only Step 1, writing `SPAWN_PENDING`, creating the false appearance that the test was altered!

#### 3. Formal Exception Ownership Contract
The audit establishes the following rigorous ownership contract for native exceptions:
- **Pre-Seam Validation Exceptions (Proven Absence of Side Effects):**
  If an exception occurs or a check fails *before* the native KEX function is invoked (e.g. `validateDestinationSector`, `m_inSeamCall`, `m_teardownBarrier`), KEX has executed zero allocations or mutations. Retaining `SPAWN_PENDING` for a clean retry is 100% safe.
- **Seam-Level Exceptions during `spawnVisualActor` (Uncertain Outcome):**
  If `seam->spawnVisualActor` throws an unhandled exception escaping into C++, the state machine cannot know whether KEX allocated an ObjectNode before throwing. If KEX allocated an actor before failing (e.g. during sector link or audio trigger), retrying spawn will leak the first actor.
  - In mock testing, the mock seam throws before allocation, so retry is safe.
  - In production native integration (DOC-58A), the native adapter MinHook wrapper must catch any KEX-level exceptions and guarantee that if an actor was partially allocated, it is destroyed before returning, or return `nullptr` so the state machine transitions to `ABSENT` rather than leaving an untracked native phantom (`GATE-PSE-58C-07`).
- **Seam-Level Exceptions during `removeVisualActorEx`:**
  The actor is already active in KEX. Any exception means removal status is uncertain $\rightarrow$ **Unconditionally transitions to `QUARANTINED`** with `QuarantineReason::NativeExceptionCaught`. This isolates the actor and prevents double-free or reuse.

---

## 3. Reconciled 74-Scenario Execution Matrix

### Suite 1: DOC-58B Remediation Regression (18 Scenarios)
| ID | Scenario Name | Injected Condition / Action | Expected State | Actual State | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **DOC58B-SC-01** | Failed Remove (DefiniteFailure) | Seam returns `DefiniteFailure` | QUARANTINED | QUARANTINED | **PASS** |
| **DOC58B-SC-02** | Failed Remove (Uncertain) | Seam returns `Uncertain` | QUARANTINED | QUARANTINED | **PASS** |
| **DOC58B-SC-03** | Successful Native Remove | Seam returns `ConfirmedSuccess` | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-04** | Repeated Remove Blocked | `executePendingRemove` on failure | QUARANTINED | QUARANTINED | **PASS** |
| **DOC58B-SC-05** | Shutdown during SPAWN_PENDING | `requestShutdown()` | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-06** | Shutdown during REMOVE_PENDING | `executeShutdownCleanup(true)` | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-07** | Shutdown after Level Unloaded | `executeShutdownCleanup(false)` | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-08** | Wrong Thread Affinity Rejection | Snapshot on worker TID | State unchanged | State unchanged | **PASS** |
| **DOC58B-SC-09** | Seam Exception Isolation | Seam throws on spawn/remove | QUARANTINED | QUARANTINED | **PASS** |
| **DOC58B-SC-10** | Recycled Pointer Safety | ABA pointer recycling across gen | ACTIVE | ACTIVE | **PASS** |
| **DOC58B-SC-11** | Stale DTO after Disconnect | Snapshot after `onDisconnect()` | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-12** | Map Epoch Invalidation | Snapshot with stale map epoch | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-13** | Wrong Slot Protection | Slot 1 snapshot to Proxy 0 | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-14** | Unknown Sector Rejection | Spawn with sector -1 / 9999 | SPAWN_PENDING | SPAWN_PENDING | **PASS** |
| **DOC58B-SC-15** | Mismatched Pos/Sector (ADV-11) | Position outside sector bbox | SPAWN_PENDING | SPAWN_PENDING | **PASS** |
| **DOC58B-SC-16** | Repeated Spawn Blocked in Quarantine | Snapshot while quarantined | QUARANTINED | QUARANTINED | **PASS** |
| **DOC58B-SC-17** | Silent Terminal Shutdown | Operations after shutdown | ABSENT | ABSENT | **PASS** |
| **DOC58B-SC-18** | noexcept ABI Safety Audit | Compile-time noexcept audit | ALL_NOEXCEPT | ALL_NOEXCEPT | **PASS** |

### Suite 2: DOC-58C Integration Contracts (32 Scenarios)
| ID | Scenario Name | Expected State | Actual State | Verdict | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-PSE-58C-01** | Shutdown before Level::Unload | ABSENT | ABSENT | **PASS** | Normal clean teardown |
| **TC-PSE-58C-02** | Shutdown after Sector Deallocation | ABSENT | ABSENT | **PASS** | Seam call skipped safely |
| **TC-PSE-58C-03** | Stale isLevelValid UAF Trap | UAF_DETECTED | UAF_DETECTED | **NATIVE_VERIFICATION_REQUIRED** | GATE-PSE-58C-01 |
| **TC-PSE-58C-04** | Repeated Level Unload (Idempotence) | ABSENT | ABSENT | **PASS** | Idempotent |
| **TC-PSE-58C-05** | Disconnect $\rightarrow$ Map Transition $\rightarrow$ Reconnect | ACTIVE | ACTIVE | **PASS** | Full reconnect lifecycle |
| **TC-PSE-58C-06** | ConfirmedSuccess Clean Removal | ABSENT | ABSENT | **PASS** | Synchronous unlink |
| **TC-PSE-58C-07** | DefiniteFailure Quarantine Contract | QUARANTINED | QUARANTINED | **PASS** | Isolated |
| **TC-PSE-58C-08** | Uncertain Outcome Quarantine Contract | QUARANTINED | QUARANTINED | **PASS** | Ambiguous status isolated |
| **TC-PSE-58C-09** | Deferred Destruction / Sweep Race | ACTIVE | ACTIVE | **PASS** | ABA-safe re-spawn |
| **TC-PSE-58C-10** | Blind Repeated Remove Blocked | QUARANTINED | QUARANTINED | **PASS** | Double-free prevented |
| **TC-PSE-58C-11** | Snapshot & Spawn Blocked in Quarantine | QUARANTINED | QUARANTINED | **PASS** | Updates rejected |
| **TC-PSE-58C-12** | Quarantine Retention across Reconnect | QUARANTINED | QUARANTINED | **PASS** | Retained across session |
| **TC-PSE-58C-13** | Shutdown while Quarantined | ABSENT | ABSENT | **PASS** | Cleared on unload |
| **TC-PSE-58C-14** | Pointer Address Reuse Disambiguation | ACTIVE | ACTIVE | **PASS** | Generation advanced |
| **TC-PSE-58C-15** | retireQuarantinedActor Witness Rule | ABSENT | ABSENT | **PASS** | Attested witness enforced |
| **TC-PSE-58C-16** | Out-of-Bounds Sector Rejection | SPAWN_PENDING | SPAWN_PENDING | **PASS** | assert7b3c0 prevented |
| **TC-PSE-58C-17** | Coordinate / Sector Spatial Mismatch | SPAWN_PENDING | SPAWN_PENDING | **PASS** | Mismatch rejected |
| **TC-PSE-58C-18** | Missing Sector Validator | SPAWN_PENDING | SPAWN_PENDING | **PASS** | Fail-closed |
| **TC-PSE-58C-19** | Portal Crossing Verification (259 $\rightarrow$ 123) | ACTIVE | ACTIVE | **PASS** | Transition validated |
| **TC-PSE-58C-20** | Aperture Boundary Tolerance ($Y = -6912$) | ACTIVE | ACTIVE | **PASS** | Epsilon tolerance |
| **TC-PSE-58C-21** | Map Invalidation during Pending Spawn | ABSENT | ABSENT | **PASS** | Stale spawn cancelled |
| **TC-PSE-58C-22** | Worker Thread Cross-Call Rejection | ACTIVE | ACTIVE | **PASS** | Affinity violation caught |
| **TC-PSE-58C-23** | Mock Native Seam Exception Isolation | QUARANTINED | QUARANTINED | **PASS** | Spawn retry ok, remove quarantined |
| **TC-PSE-58C-24** | noexcept Specification Verification | ALL_NOEXCEPT | ALL_NOEXCEPT | **PASS** | static_assert verified |
| **TC-PSE-58C-25** | Out-of-Order Lifecycle Sequence | ABSENT | ABSENT | **PASS** | Resilient |
| **TC-PSE-58C-26** | Reentrant Native Seam Guard (Remediated) | ACTIVE | ACTIVE | **PASS** | ReentrancyBlocked, zero leaks |
| **TC-PSE-58C-27** | Multi-Slot Filtering & Target Isolation | SPAWN_PENDING | SPAWN_PENDING | **PASS** | Isolated per slot |
| **TC-PSE-58C-28** | 3–8 Player Slot MultiSlotGuard8 Support | SPAWN_PENDING | SPAWN_PENDING | **PASS** | Slot 3 accepted cleanly |
| **TC-PSE-58C-29** | Independent Proxy Generations | GEN0_UNCHANGED| GEN0_UNCHANGED| **PASS** | Slots independent |
| **TC-PSE-58C-30** | Single Client Disconnect Others Active | PROXY0_ACTIVE | PROXY0_ACTIVE | **PASS** | Slot 1 absent, slot 0 streaming |
| **TC-PSE-58C-31** | Quarantine Blast Radius Isolation | PROXY0_ACTIVE | PROXY0_ACTIVE | **PASS** | Contained to failing slot |
| **TC-PSE-58C-32** | Coordinated Reverse Shutdown (8 Slots) | ALL_ABSENT | ALL_ABSENT | **PASS** | Reverse order clean |

### Suite 3: DOC-58D Adversarial Hardening (20 Scenarios)
All 20 scenarios (`TC-PSE-58D-01` to `TC-PSE-58D-20`) $\rightarrow$ **PASS (20 / 20)**.

### Suite 4: DOC-58D.1 Reconciled Audit Suite (4 Scenarios)
| ID | Scenario Name | Test Action | Expected Outcome | Actual Outcome | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **TC-PSE-58D1-01** | Map Epoch `configureSession` Cancels Pending Spawn | Epoch advances to 7366 while `SPAWN_PENDING` | State becomes `ABSENT`, spawn rejected as `NotPending` | `ABSENT`, zero seam calls | **PASS** |
| **TC-PSE-58D1-02** | Post-Unload Barrier Blocks Snapshots until `onMapReady` | Snapshot dispatched while barrier active after unload | Rejected with `ShutdownRequested`, lowered on `onMapReady()` | `ShutdownRequested`, barrier verified | **PASS** |
| **TC-PSE-58D1-03** | Last Unloaded Actor Pointer Tracking | Unload active proxy | Tracked pointer captured in `lastUnloadedActor()`, active ptr null | Captured `0x61A01000`, active `nullptr` | **PASS** |
| **TC-PSE-58D1-04** | MultiSlotGuard8 Session Identity Invariants | Evaluate epoch mismatch, gen mismatch, slot 7 accept | Wrong epoch $\rightarrow$ `Identity`, wrong gen $\rightarrow$ `Identity`, slot 7 $\rightarrow$ `Accept` | All verdicts verified strictly | **PASS** |
