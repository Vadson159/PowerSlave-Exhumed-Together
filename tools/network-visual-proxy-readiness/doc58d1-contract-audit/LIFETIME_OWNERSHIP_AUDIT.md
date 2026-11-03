# PowerSlave Exhumed Together — Lifetime & Ownership Audit (DOC-58D.1)

**Document ID:** `PSE-DOC58D1-LIFETIME-OWNERSHIP-AUDIT`  
**Date:** 2026-11-03  
**Auditor:** Gemini 3.8 High — Senior C++20 / Multiplayer QA & Code Auditor  
**Candidate Component:** [`doc58d1_hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/doc58d1_hardened_visual_proxy_state_machine.hpp)  
**Assigned Roles:** Claude Opus 5.5 High (DOC-58A Native Adapter), GPT-6.1 Sol Extra (REA / Frida Dynamic Verification)

---

## 1. Audit Invariants & Core Verification Findings

This document formally audits the lifetime, ownership, and synchronization contracts governing visual proxy actors across level transitions, network disconnections, and engine shutdowns.

### Summary Table of Audited Requirements
| Requirement | Status | Verification Evidence |
| :--- | :--- | :--- |
| **1. Post-Unload Barrier Enforces Map Ready** | **VERIFIED** | `m_teardownBarrier` remains active after `onPostUnload()`; native operations prohibited until `onMapReady()` or `configureSession()`. Verified in [`test_doc58d1_reconciled.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/test_doc58d1_reconciled.cpp) (`TC-PSE-58D1-02`). |
| **2. Pre-Unload Pointer Retention (No Silent Loss)** | **VERIFIED** | `m_lastUnloadedActorPtr` captures active pointer before nullification. Tracked for live audit under `GATE-PSE-58C-01`. Verified in `TC-PSE-58D1-03`. |
| **3. KEX Native Destruction Unconfirmed** | **UNPROVEN (GATED)** | Autonomous tests simulate sweep, but closed-source KEX binary destruction is unproven. Maintained as `NATIVE_VERIFICATION_REQUIRED` (`GATE-PSE-58C-01` / `03`). |
| **4. Strict Return of `ConfirmedSuccess`** | **VERIFIED** | Only returned on attested synchronous unlinking. Ambiguous outcomes transition to `QUARANTINED` (`DOC58B-SC-01`, `02`). |
| **5. Non-Reentrant Seam Dispatch** | **VERIFIED** | Guarded by `m_inSeamCall` and `SeamCallScopeGuard`. Reentrant calls fail closed (`repro_reentrancy_after.exe`, `TC-PSE-58C-26`). |
| **6. Zero Stale Pointer Dereferences** | **VERIFIED** | `m_nativeActorPtr` nullified on teardown; generational counters (`localProxyGeneration`) protect against ABA address reuse (`TC-PSE-58C-14`). |

---

## 2. In-Depth Analysis of Critical Lifetime Methods

### A. `onPreUnload()` — Entry Barrier
- **Target Hook:** Entry of `kexSlaveLevelLocal::Unload` (`0x1401c90e0`), prior to sector memory deallocation.
- **Contract Guarantees:**
  ```cpp
  void onPreUnload() noexcept {
      if (!isSimulationThread() || m_inSeamCall) return;
      m_sessionActive = false;
      m_teardownBarrier = true;
      if (m_state == VisualProxyState::SPAWN_PENDING) {
          m_state = VisualProxyState::ABSENT;
      } else if (m_state == VisualProxyState::ACTIVE) {
          m_state = VisualProxyState::REMOVE_PENDING;
      }
  }
  ```
- **Ownership Invariant:** The state machine transitions active proxies to `REMOVE_PENDING`, but **strictly suppresses calling native `removeVisualActorEx()`**. Because KEX engine `Level::Unload` sweeps its own world sectors, calling native Remove while the sector structures are being torn down triggers Use-After-Free / Access Violations.

### B. `onPostUnload()` — Exit Teardown & Pointer Preservation
- **Target Hook:** Exit of `kexSlaveLevelLocal::Unload` (`0x1401c90e0`), after engine sector deallocation finishes.
- **Contract Guarantees in DOC-58D.1:**
  ```cpp
  void onPostUnload() noexcept {
      if (!isSimulationThread() || m_inSeamCall) return;
      m_sessionActive = false;
      m_teardownBarrier = true; // Retain barrier active until confirmed Map Ready!
      if (m_nativeActorPtr) {
          m_lastUnloadedActorPtr = m_nativeActorPtr; // Preserved for audit
      }
      m_nativeActorPtr = nullptr;
      m_localProxyGen++;
      m_state = VisualProxyState::ABSENT;
      m_quarantineReason = QuarantineReason::None;
  }
  ```
- **Auditor Resolution:**
  1. In earlier DOC-58D drafts, `onPostUnload()` cleared `m_teardownBarrier = false`, leaving an unprotected timing window between unload exit and new level load.
  2. In DOC-58D.1, **`m_teardownBarrier` remains `true`** across `onPostUnload()`. It is only lowered when `onMapReady()` is invoked or a new map session is explicitly configured via `configureSession()`.
  3. `m_lastUnloadedActorPtr` preserves the physical address of the unloaded actor. This allows GPT-6.1 Sol / Frida to verify whether KEX reclaimed the node during unload (`GATE-PSE-58C-01`).

### C. `executeShutdownCleanup(seam, isLevelValid)`
- **Thread Safety:** Bound to attested simulation thread (`isSimulationThread()`).
- **Reentrancy Protection:** Returns `RemoveExecutionResult::ReentrancyBlocked` if `m_inSeamCall == true`.
- **Level Safety:** If `!isLevelValid || m_teardownBarrier`, native seam calls are skipped unconditionally, returning `RemoveExecutionResult::LevelInvalidSkipped`, preventing KEX memory access violations.

### D. `retireQuarantinedActor(attestedEngineSwept)`
- **Witness Requirement:** Cannot be cleared by timeouts or blind guesses. Requires `attestedEngineSwept == true` (signalled by native witness callback of KEX pool sweep).
- **Audit Verification:** Verified in `TC-PSE-58C-15`. Calling with `false` is rejected and leaves state in `QUARANTINED`. Calling with `true` cleanly transitions to `ABSENT`.

---

## 3. Contract Rules for Claude Opus 5.5 High (DOC-58A Native Adapter)

1. **Dual Hook Installation on `0x1401c90e0`:**
   The adapter must install a trampoline hook intercepting both the entry and exit of `kexSlaveLevelLocal::Unload`:
   - Entry hook invokes `onPreUnload()` on all slots 0..7.
   - Original `0x1401c90e0` executes native engine teardown.
   - Exit hook invokes `onPostUnload()` on all slots 0..7.
2. **Attested Map Ready Hook:**
   The adapter must invoke `onMapReady()` strictly after KEX has finished loading the new level, all sectors are allocated, and the simulation loop is about to begin its first frame.
3. **Session Epoch Advance on Map Change:**
   When transitioning maps, invoke `configureSession(newEpoch, newConnectionGen)` on each proxy instance. This drops any stale pending spawns and initializes the `MultiSlotGuard8` table.
