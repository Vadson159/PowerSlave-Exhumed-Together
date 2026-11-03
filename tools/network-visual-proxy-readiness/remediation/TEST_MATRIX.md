# PowerSlave Exhumed Together — Remediation Comparative Test Matrix

**Document ID:** `TEST-MATRIX-DOC58B-REMEDIATION`  
**Classification:** Comparative QA Matrix (Baseline vs Remediated Candidate)  
**Author:** Gemini 3.8 High, Independent QA & Multiplayer Networking Specialist  
**Date:** 2026-11-03  
**Status:** AUTONOMOUS QA PASS (18 / 18 PASS)  
**Baseline Header:** `tools/network-visual-proxy-readiness/include/visual_proxy_state_machine.hpp`  
**Candidate Header:** `tools/network-visual-proxy-readiness/remediation/remediated_visual_proxy_state_machine.hpp`  
**Test Executable:** `tools/network-visual-proxy-readiness/remediation/build/Release/test_remediation_comparative.exe`  
**Result Artifact:** `tools/network-visual-proxy-readiness/remediation/TEST_RESULTS.json`  

---

## 1. Summary of Comparative Results

| Category | Total Scenarios | Baseline Failures Reproduced | Candidate Passes Confirmed | Autonomous QA Verdict |
| :--- | :---: | :---: | :---: | :---: |
| Lifecycle & Quarantine (ADV-02) | 4 | 3 | 4 | PASS |
| Safe Shutdown & Level Unload | 3 | 1 | 3 | PASS |
| Thread Affinity & Exceptions | 2 | 2 | 2 | PASS |
| Identity & Protocol Guards | 4 | 0 | 4 | PASS |
| Sector Validation (ADV-11) | 2 | 2 | 2 | PASS |
| Quarantine Immunity & Terminal | 3 | 2 | 3 | PASS |
| **Total** | **18** | **10 Reproduced** | **18 Confirmed** | **AUTONOMOUS QA PASS** |

---

## 2. Detailed Comparative Test Matrix (18 Scenarios)

### SCENARIO-01: Failed Remove — Actor Confirmed Alive (ADV-02)
- **Initial State:** `ACTIVE` (`m_nativeActorPtr = 0xDEADBEEF`), transitioning to `REMOVE_PENDING` on disconnect.
- **Injected Fault:** Native seam `removeVisualActorEx()` returns `DefiniteFailure` (simulates actor locked in sector list).
- **Baseline Behavior:** Calls `removeVisualActor()`, receives `false`. Nullifies `m_nativeActorPtr = nullptr` and sets `m_state = ABSENT`.
- **Baseline Verdict:** `BASELINE_FAIL (Ownership Leaked)`
- **Candidate Behavior:** Calls `removeVisualActorEx()`, receives `DefiniteFailure`. Preserves `m_nativeActorPtr = 0xDEADBEEF`, sets `m_state = QUARANTINED`, `quarantineReason = RemoveDefiniteFailure`. Returns `NativeDefiniteFailureQuarantined`.
- **Candidate Verdict:** `CANDIDATE_PASS (Safely Quarantined)`
- **Seam Calls:** Baseline: 1 / Candidate: 1
- **Reproduction:** `test_remediation_comparative.exe --scenario 1` (or `repro_adv02_failed_remove.exe`)

---

### SCENARIO-02: Failed Remove — Result Uncertain
- **Initial State:** `ACTIVE`, transitioning to `REMOVE_PENDING`.
- **Injected Fault:** Native removal outcome is ambiguous (`Uncertain`, e.g. partial unlinking or hook error).
- **Baseline Behavior:** Baseline lacks tri-state support. Treats as boolean `false`, blindly transitions to `ABSENT` and discards pointer.
- **Baseline Verdict:** `BASELINE_FAIL (No Tri-State Support)`
- **Candidate Behavior:** Receives `RemoveOutcome::Uncertain`. Preserves actor pointer, sets `m_state = QUARANTINED`, `quarantineReason = RemoveUncertain`. Returns `NativeUncertainQuarantined`.
- **Candidate Verdict:** `CANDIDATE_PASS (Quarantined as Uncertain)`
- **Seam Calls:** Baseline: 1 / Candidate: 1
- **Reproduction:** `test_remediation_comparative.exe --scenario 2`

---

### SCENARIO-03: Successful Native Remove
- **Initial State:** `ACTIVE`, transitioning to `REMOVE_PENDING`.
- **Injected Fault:** None. Native seam returns `ConfirmedSuccess` (or `true`).
- **Baseline Behavior:** State becomes `ABSENT`, pointer nullified, generation incremented.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** State becomes `ABSENT`, pointer nullified, generation incremented, `removeCount` updated. Returns `Success`.
- **Candidate Verdict:** `CANDIDATE_PASS`
- **Seam Calls:** Baseline: 1 / Candidate: 1
- **Reproduction:** `test_remediation_comparative.exe --scenario 3`

---

### SCENARIO-04: Repeated Remove After Failure
- **Initial State:** Post-failed removal (`QUARANTINED` in Candidate, `ABSENT` in Baseline).
- **Injected Fault:** Caller invokes `executePendingRemove()` a second time.
- **Baseline Behavior:** State is `ABSENT`, `m_nativeActorPtr` is null. Returns `false` blindly; no protection against repeated loop calls.
- **Baseline Verdict:** `BASELINE_FAIL (Blind ABSENT State)`
- **Candidate Behavior:** While in `QUARANTINED`, `executePendingRemove()` immediately returns `QuarantinedBlocked` without invoking the native seam.
- **Candidate Verdict:** `CANDIDATE_PASS (Repeated Call Blocked)`
- **Seam Calls:** Baseline: 1 / Candidate: 1 (zero additional calls)
- **Reproduction:** `test_remediation_comparative.exe --scenario 4`

---

### SCENARIO-05: Shutdown During SPAWN_PENDING
- **Initial State:** `SPAWN_PENDING` (snapshot accepted, actor not yet spawned).
- **Injected Fault:** `requestShutdown()` invoked before tick seam executes spawn.
- **Baseline Behavior:** Sets state to `ABSENT`.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** Sets `m_shutdownRequested = true`, transitions to `ABSENT`. Blocks subsequent spawn with `ShutdownBlocked` and rejects snapshots with `ShutdownRequested`. Native object factory is never called.
- **Candidate Verdict:** `CANDIDATE_PASS (Cleanly Aborted)`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 5`

---

### SCENARIO-06: Shutdown During REMOVE_PENDING (Level Valid)
- **Initial State:** `REMOVE_PENDING` (actor active, level structures intact).
- **Injected Fault:** Structured shutdown cleanup invoked with `isLevelValid = true`.
- **Baseline Behavior:** Calls `removeVisualActor()`.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** Invokes `executeShutdownCleanup(seam, isLevelValid = true)`. Calls `removeVisualActorEx()`, cleanly retires actor to `ABSENT`.
- **Candidate Verdict:** `CANDIDATE_PASS`
- **Seam Calls:** Baseline: 1 / Candidate: 1
- **Reproduction:** `test_remediation_comparative.exe --scenario 6`

---

### SCENARIO-07: Shutdown After Level Unloaded (UAF Hazard)
- **Initial State:** `ACTIVE` / `REMOVE_PENDING` during game shutdown or level exit.
- **Injected Fault:** KEX engine has already called `kexSlaveLevelLocal::Unload`, freeing sector arrays (`isLevelValid = false`).
- **Baseline Behavior:** `forceShutdown()` calls `removeVisualActor()` unconditionally! In KEX, `Remove` calls `Drain`, dereferencing freed sector memory -> `STATUS_ACCESS_VIOLATION`.
- **Baseline Verdict:** `BASELINE_FAIL (UAF Vulnerability)`
- **Candidate Behavior:** `executeShutdownCleanup(seam, isLevelValid = false)` detects level invalidation, SKIPS native remove entirely, nullifies pointer, and transitions to `ABSENT`. Returns `LevelInvalidSkipped`.
- **Candidate Verdict:** `CANDIDATE_PASS (Native Remove Safely Skipped)`
- **Seam Calls:** Baseline: 1 (Crash Hazard) / Candidate: 0 (Safe)
- **Reproduction:** `test_remediation_comparative.exe --scenario 7`

---

### SCENARIO-08: Wrong Simulation Thread Affinity
- **Initial State:** State machine registered on Simulation Thread A.
- **Injected Fault:** Method (`executePendingSpawn` / `handleSnapshot`) invoked from background worker Thread B.
- **Baseline Behavior:** Calls `assertSimulationThread()`, which throws `std::runtime_error`. Across unhandled C-ABI hooks, this calls `std::terminate()` and crashes KEX.
- **Baseline Verdict:** `BASELINE_FAIL (Throws Exception across C boundary)`
- **Candidate Behavior:** Non-throwing check returns `SpawnExecutionResult::AffinityViolation` / `ProxyRejectionReason::AffinityViolation`. Zero exceptions thrown. Fail-closed.
- **Candidate Verdict:** `CANDIDATE_PASS (Fail-Closed, No Exceptions)`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 8`

---

### SCENARIO-09: Exception from Mock Native Seam
- **Initial State:** `ACTIVE`, transitioning to `REMOVE_PENDING`.
- **Injected Fault:** Rogue mock or faulty native wrapper throws `std::runtime_error` inside `removeVisualActorEx()`.
- **Baseline Behavior:** Exception escapes unhandled, terminating process.
- **Baseline Verdict:** `BASELINE_FAIL (Exception Leaked)`
- **Candidate Behavior:** State machine catches exception internally, places actor in `QUARANTINED` (`QuarantineReason::NativeExceptionCaught`), and returns `RemoveExecutionResult::NativeExceptionCaught`. No leak, no crash.
- **Candidate Verdict:** `CANDIDATE_PASS (Exception Isolated)`
- **Seam Calls:** Baseline: 1 / Candidate: 1
- **Reproduction:** `test_remediation_comparative.exe --scenario 9`

---

### SCENARIO-10: Recycled Actor Pointer Tracking (Generational Safety)
- **Initial State:** Actor A created, removed; native allocator returns identical pointer address `0xCAFE0001` for new Actor B.
- **Injected Fault:** Memory address reuse (ABA hazard).
- **Baseline Behavior:** Increments `m_localProxyGen` on removal.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** Strictly increments `m_localProxyGen` on every retirement, ensuring that stale cached references or delayed callbacks from prior actor generation are rejected.
- **Candidate Verdict:** `CANDIDATE_PASS (Generational Separation)`
- **Seam Calls:** Baseline: 2 / Candidate: 2
- **Reproduction:** `test_remediation_comparative.exe --scenario 10`

---

### SCENARIO-11: Stale DTO After Disconnect
- **Initial State:** Client B disconnected from session (`onDisconnect()`).
- **Injected Fault:** Late snapshot packet arrives from network queue.
- **Baseline Behavior:** Rejects with `SessionInactive`.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** Rejects with `ProxyRejectionReason::SessionInactive`.
- **Candidate Verdict:** `CANDIDATE_PASS`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 11`

---

### SCENARIO-12: Map Epoch Invalidation
- **Initial State:** Configured map epoch `7365`.
- **Injected Fault:** Snapshot arrives with mismatched map epoch `9999` (level transition desync).
- **Baseline Behavior:** Snapshot Guard rejects with `Identity`. State machine returns `IdentityMismatch`.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** Rejects with `ProxyRejectionReason::IdentityMismatch`.
- **Candidate Verdict:** `CANDIDATE_PASS`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 12`

---

### SCENARIO-13: Wrong Slot Protection (Client Puppet Slot 1)
- **Initial State:** `ABSENT`.
- **Injected Fault:** Snapshot arrives with `slotId = 1` (Client B's own puppet slot).
- **Baseline Behavior:** Rejects with `WrongSlot`.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** Rejects with `ProxyRejectionReason::WrongSlot`. Client puppet state remains strictly isolated.
- **Candidate Verdict:** `CANDIDATE_PASS`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 13`

---

### SCENARIO-14: Unknown Sector Spawn Rejection
- **Initial State:** `SPAWN_PENDING`.
- **Injected Fault:** Destination sector is invalid (`-1` or unverified `999`).
- **Baseline Behavior:** Baseline has no sector validator contract. Blindly passes `-1` to native factory.
- **Baseline Verdict:** `BASELINE_FAIL (No Sector Check)`
- **Candidate Behavior:** Sector validator rejects invalid sector. `executePendingSpawn()` returns `SectorValidationFailed`, leaves state in `SPAWN_PENDING`, and calls factory 0 times.
- **Candidate Verdict:** `CANDIDATE_PASS (Spawn Rejected)`
- **Seam Calls:** Baseline: 1 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 14`

---

### SCENARIO-15: Mismatched Position/Sector (ADV-11 Fallacy)
- **Initial State:** `SPAWN_PENDING`. Host P1 coordinates are in Sector 176 (`x=-7488, y=-7100, z=1240`).
- **Injected Fault:** Caller naively passes hardcoded Sector 259 to spawn.
- **Baseline Behavior:** Baseline spawns actor into Sector 259 actorList while coordinates are in Sector 176. Causes invisible sprite and portal rendering asserts.
- **Baseline Verdict:** `BASELINE_FAIL (ADV-11 Hardcoded 259 Bug)`
- **Candidate Behavior:** Sector validator verifies coordinate spatial bounds. Detects mismatch, rejects spawn with `SectorValidationFailed`. When valid sector 176 is supplied, spawn succeeds.
- **Candidate Verdict:** `CANDIDATE_PASS (ADV-11 Eliminated)`
- **Seam Calls:** Baseline: 1 (Corrupt Linkage) / Candidate: 0 on mismatch, 1 on verified sector
- **Reproduction:** `test_remediation_comparative.exe --scenario 15` (or `repro_adv11_sector_fallacy.exe`)

---

### SCENARIO-16: Repeated Spawn After Quarantined Removal
- **Initial State:** Previous removal failed; proxy is in `QUARANTINED` state.
- **Injected Fault:** New snapshot arrives and caller attempts `executePendingSpawn()`.
- **Baseline Behavior:** Baseline transitioned to `ABSENT` despite failure, so it accepts the new snapshot and spawns a second actor! Two live actors now exist for Host P1 (duplicate leak).
- **Baseline Verdict:** `BASELINE_FAIL (Duplicate Actor Created in KEX!)`
- **Candidate Behavior:** `handleSnapshot()` returns `ProxyRejectionReason::Quarantined`. `executePendingSpawn()` returns `SpawnExecutionResult::Quarantined`. Factory call count is strictly 0.
- **Candidate Verdict:** `CANDIDATE_PASS (Duplicate Spawn Blocked)`
- **Seam Calls:** Baseline: 2 / Candidate: 1 (zero new spawns)
- **Reproduction:** `test_remediation_comparative.exe --scenario 16`

---

### SCENARIO-17: No Native Calls After Terminal Shutdown
- **Initial State:** Fully shut down (`requestShutdown()` executed, level invalid).
- **Injected Fault:** Repeated calls to `handleSnapshot`, `executePendingSpawn`, `executePendingRemove`.
- **Baseline Behavior:** Silent state.
- **Baseline Verdict:** `BASELINE_PASS`
- **Candidate Behavior:** All subsequent calls rejected without invoking any seam method. Seam call count strictly 0.
- **Candidate Verdict:** `CANDIDATE_PASS (Silent Terminal State)`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 17`

---

### SCENARIO-18: No C++ Exceptions Across API Boundary (noexcept Audit)
- **Initial State:** Static compile-time type verification & adverse runtime execution.
- **Injected Fault:** Adverse inputs across all public member functions.
- **Baseline Behavior:** Contains `throw std::runtime_error` in `assertSimulationThread()`. Not `noexcept`.
- **Baseline Verdict:** `BASELINE_FAIL (Throws C++ Exceptions)`
- **Candidate Behavior:** All 12 public methods compile-time proven `noexcept` via `static_assert(noexcept(...))`. Zero runtime exceptions under all adverse conditions.
- **Candidate Verdict:** `CANDIDATE_PASS (Verified noexcept ABI Safety)`
- **Seam Calls:** Baseline: 0 / Candidate: 0
- **Reproduction:** `test_remediation_comparative.exe --scenario 18`
