# PowerSlave Exhumed Together — DOC-58C Adversarial Test Matrix & Risk Analysis

**Document ID:** `PSE-DOC58C-MATRIX-V1`  
**Classification:** Adversarial QA Execution Report & Matrix  
**Author:** Gemini 3.8 High, Independent QA & C++20 / Multiplayer Engineer  
**Target Candidate:** `tools/network-visual-proxy-readiness/remediation/remediated_visual_proxy_state_machine.hpp`  
**Execution Date:** 2026-11-03  
**Status:** COMPLETE (32 Scenarios Executed: 29 PASS, 2 FAIL [Candidate Defects], 1 NATIVE_VERIFICATION_REQUIRED)  

---

## 1. Executive Summary

This matrix documents the execution of 32 adversarial integration scenarios testing the contracts between the remediated DOC-58B `VisualProxyStateMachine` and the future native adapter (DOC-58A).

The suite uncovered **two critical architectural defects in the candidate codebase** and **one unproven native assumption** that would have caused crashes and memory leaks in production:
1. **Reentrancy Leak Defect (`TC-PSE-58C-26`):** The candidate state machine lacks a reentrancy guard. When a native hook triggers a reentrant call during `spawnVisualActor`, the outer call overwrites the internal pointer, permanently leaking the inner actor in KEX.
2. **3–8 Player Snapshot Rejection Defect (`TC-PSE-58C-28`):** The candidate embeds `snapshot55::Guard`, which hardcodes `std::array<Slot, 2> slots`. Any snapshot for Player Slots 2 through 7 is rejected with `IdentityMismatch`, blocking 3–8 player co-op.
3. **Stale `isLevelValid` UAF Trap (`TC-PSE-58C-03`):** Proves that a simple boolean `isLevelValid` flag cannot protect against Use-After-Free during `Level::Unload` unless guarded by an atomic native pre-unload hook barrier.

---

## 2. Complete 32-Scenario Master Matrix

| ID | Domain | Initial State | Injected Fault / Stimulus | Expected State | Actual State | Seam Calls | Verdict | Risk |
| :--- | :--- | :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| **TC-PSE-58C-01** | A. Lifetime | `ACTIVE` | `requestShutdown()` + `executeShutdownCleanup(true)` | `ABSENT` | `ABSENT` | 1 | **PASS** | High |
| **TC-PSE-58C-02** | A. Lifetime | `ACTIVE` | Level Unload $\rightarrow$ `executeShutdownCleanup(false)` | `ABSENT` | `ABSENT` | 0 | **PASS** | Critical |
| **TC-PSE-58C-03** | A. Lifetime | `ACTIVE` | Stale `isLevelValid=true` after sector free | `UAF_DETECTED` | `UAF_DETECTED` | 1 | **NATIVE_VERIF_REQ** | Critical |
| **TC-PSE-58C-04** | A. Lifetime | `ACTIVE` | Sequential `onMapInvalidated(true)` $\times 2$ | `ABSENT` | `ABSENT` | 0 | **PASS** | Medium |
| **TC-PSE-58C-05** | A. Lifetime | `ACTIVE` | Disconnect $\rightarrow$ Map Invalidate $\rightarrow$ Reconnect $\rightarrow$ Spawn | `ACTIVE` | `ACTIVE` | 1 | **PASS** | High |
| **TC-PSE-58C-06** | B. Remove | `REMOVE_PENDING` | `removeVisualActorEx` returns `ConfirmedSuccess` | `ABSENT` | `ABSENT` | 1 | **PASS** | High |
| **TC-PSE-58C-07** | B. Remove | `REMOVE_PENDING` | `removeVisualActorEx` returns `DefiniteFailure` | `QUARANTINED` | `QUARANTINED` | 1 | **PASS** | Critical |
| **TC-PSE-58C-08** | B. Remove | `REMOVE_PENDING` | `removeVisualActorEx` returns `Uncertain` | `QUARANTINED` | `QUARANTINED` | 1 | **PASS** | Critical |
| **TC-PSE-58C-09** | B. Remove | `REMOVE_PENDING` | Deferred Pool Return $\rightarrow$ Rapid Re-spawn (gen=3) | `ACTIVE` | `ACTIVE` | 1 | **PASS** | High |
| **TC-PSE-58C-10** | C. Quarantine | `QUARANTINED` | Blind repeat call to `executePendingRemove()` | `QUARANTINED` | `QUARANTINED` | 0 | **PASS** | Critical |
| **TC-PSE-58C-11** | C. Quarantine | `QUARANTINED` | `handleSnapshot()` & `executePendingSpawn()` calls | `QUARANTINED` | `QUARANTINED` | 0 | **PASS** | Critical |
| **TC-PSE-58C-12** | C. Quarantine | `QUARANTINED` | Disconnect $\rightarrow$ Reconnect while quarantined | `QUARANTINED` | `QUARANTINED` | 0 | **PASS** | High |
| **TC-PSE-58C-13** | C. Quarantine | `QUARANTINED` | `executeShutdownCleanup(true)` then `(false)` | `ABSENT` | `ABSENT` | 0 | **PASS** | High |
| **TC-PSE-58C-14** | C. Quarantine | `ABSENT` | Pointer address recycled by heap allocator (ABA) | `ACTIVE` | `ACTIVE` | 1 | **PASS** | High |
| **TC-PSE-58C-15** | C. Quarantine | `QUARANTINED` | `retireQuarantinedActor(false)` then `(true)` | `ABSENT` | `ABSENT` | 0 | **PASS** | Critical |
| **TC-PSE-58C-16** | D. Sector | `SPAWN_PENDING` | Sector index out of bounds ($-1$ and $9999$) | `SPAWN_PENDING` | `SPAWN_PENDING` | 0 | **PASS** | Critical |
| **TC-PSE-58C-17** | D. Sector | `SPAWN_PENDING` | Coords in Sector 176, destination 259 passed | `SPAWN_PENDING` | `SPAWN_PENDING` | 0 | **PASS** | High |
| **TC-PSE-58C-18** | D. Sector | `SPAWN_PENDING` | Validator pointer passed as `nullptr` | `SPAWN_PENDING` | `SPAWN_PENDING` | 0 | **PASS** | Medium |
| **TC-PSE-58C-19** | D. Sector | `SPAWN_PENDING` | Coords across portal (Sector 259 $\rightarrow$ 123) | `ACTIVE` | `ACTIVE` | 1 | **PASS** | High |
| **TC-PSE-58C-20** | D. Sector | `SPAWN_PENDING` | Coords on coplanar portal boundary plane ($Y = -6912$) | `ACTIVE` | `ACTIVE` | 1 | **PASS** | Medium |
| **TC-PSE-58C-21** | D. Sector | `SPAWN_PENDING` | Map invalidation during pending spawn | `ABSENT` | `ABSENT` | 0 | **PASS** | High |
| **TC-PSE-58C-22** | E. Affinity | `ACTIVE` | ENet worker thread calls mutating APIs | `ACTIVE` | `ACTIVE` | 0 | **PASS** | Critical |
| **TC-PSE-58C-23** | E. Exceptions | `SPAWN/REMOVE` | Native seam throws `std::runtime_error` | `QUARANTINED` | `QUARANTINED` | 1 | **PASS** | Critical |
| **TC-PSE-58C-24** | E. ABI | N/A | `static_assert` on `noexcept` for all public methods | `ALL_NOEXCEPT` | `ALL_NOEXCEPT` | 0 | **PASS** | Low |
| **TC-PSE-58C-25** | E. Order | `ABSENT` | Out-of-order calls (spawn & remove while ABSENT) | `ABSENT` | `ABSENT` | 0 | **PASS** | Medium |
| **TC-PSE-58C-26** | E. Reentrancy | `SPAWN_PENDING` | Native hook triggers synchronous reentrant spawn | `REENTRANCY_REPRO` | `REENTRANCY_REPRO` | 2 | **FAIL** | Critical |
| **TC-PSE-58C-27** | F. Multi-Client | `ABSENT` | Cross-slot snapshot dispatch (Slot 0 vs 1) | `SPAWN_PENDING` | `SPAWN_PENDING` | 0 | **PASS** | High |
| **TC-PSE-58C-28** | F. Multi-Client | `ABSENT` | Snapshot for Slot 3 (4-player game) dispatched | `SPAWN_PENDING` | `ABSENT` | 0 | **FAIL** | Critical |
| **TC-PSE-58C-29** | F. Multi-Client | `ACTIVE` | Proxy 1 removed; verify Proxy 0 gen unchanged | `GEN0_UNCHANGED` | `GEN0_UNCHANGED` | 1 | **PASS** | High |
| **TC-PSE-58C-30** | F. Multi-Client | `ACTIVE` | Proxy 1 disconnects; Proxy 0 continues streaming | `P1_ABSENT_P0_ACT` | `P1_ABSENT_P0_ACT` | 1 | **PASS** | High |
| **TC-PSE-58C-31** | F. Multi-Client | `ACTIVE` | Proxy 1 quarantined; Proxy 0 streams normally | `P1_QUAR_P0_ACT` | `P1_QUAR_P0_ACT` | 0 | **PASS** | Critical |
| **TC-PSE-58C-32** | F. Multi-Client | `8_CONFIGURED` | Reverse shutdown of all 8 slots after Level Unload | `ALL_ABSENT` | `ALL_ABSENT` | 0 | **PASS** | High |

---

## 3. Deep Analysis of Discovered Defects & Failures

### 3.1 Defect 1: Reentrancy Hazard (`TC-PSE-58C-26`)
- **Severity:** Critical
- **Symptom:** Leaked native actor in KEX memory; corrupted `m_nativeActorPtr`.
- **Reproduction Tool:** `repro_reentrancy_hazard.exe`
- **Root Cause:** In `VisualProxyStateMachine::executePendingSpawn()`:
  ```cpp
  actor = seam->spawnVisualActor(...);
  // Reentrant callback here invokes executePendingSpawn()
  // Inner call spawns actor2, sets m_nativeActorPtr = actor2, state = ACTIVE
  // Outer call receives actor1, overwrites m_nativeActorPtr = actor1!
  // RESULT: actor2 is permanently orphaned in KEX!
  ```
- **Remediation:**  
  1. Add an internal boolean `bool m_inSeamCall{false}` reentrancy guard in `VisualProxyStateMachine`.
  2. Mandate in the Native Adapter contract that all KEX event dispatches must be queued and fired outside of seam function calls.

### 3.2 Defect 2: 3–8 Player Slot Limit in `snapshot55::Guard` (`TC-PSE-58C-28`)
- **Severity:** Critical (Blocks 3–8 Player Co-op feature)
- **Symptom:** Remote visual proxies for players 2, 3, 4, 5, 6, 7 are permanently stuck in `ABSENT`.
- **Reproduction Tool:** `repro_snapshot_slot_limit.exe`
- **Root Cause:** `VisualProxyStateMachine` embeds `snapshot55::Guard m_guard`. In `snapshot_contract.hpp`:
  ```cpp
  struct Guard {
      std::array<Slot, 2> slots{};
      Verdict accept(...) {
          if (s.slotId >= slots.size()) return Verdict::Identity; // Rejects slots >= 2!
      }
  };
  ```
- **Remediation:** Expand guard capacity to `pse::MAX_PLAYERS` (8 slots):
  `std::array<Slot, pse::MAX_PLAYERS> slots{};`.

### 3.3 Critical Native Requirement: Pre-Unload Hook Barrier (`TC-PSE-58C-03`)
- **Severity:** Critical Crash Hazard (`STATUS_ACCESS_VIOLATION`)
- **Symptom:** Passing stale `isLevelValid = true` during `executeShutdownCleanup` calls `Remove` $\rightarrow$ `Drain` on freed sector array.
- **Remediation:** Sol & Opus must install an attested hook at the entry of `kexSlaveLevelLocal::Unload` (`0x1401c90e0`), synchronously setting `isLevelValid = false` before `sectors` are freed.
