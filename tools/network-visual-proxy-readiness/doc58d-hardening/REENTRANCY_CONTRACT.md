# PowerSlave Exhumed Together — Reentrancy Guard & Native Seam Contract

**Document ID:** `PSE-DOC-58D-REENTRANCY-CONTRACT`  
**Date:** 2026-11-03  
**Target:** [`hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/hardened_visual_proxy_state_machine.hpp)  
**Assigned Roles:** Claude Opus 5.5 High (DOC-58A Adapter Integrator), GPT-6.1 Sol Extra (REA / Frida Hooking)

---

## 1. Vulnerability Analysis & Defect Root Cause

### A. Defect Identification in DOC-58C (`TC-PSE-58C-26`)
During DOC-58C integration testing, an adversarial scenario demonstrated that if a native KEX hook (or script engine callback, physics trigger, or audio cue) synchronously invokes any mutating state machine API while a native seam method is in progress, the state machine exhibits a critical **Reentrancy Leak Defect**.

### B. Vulnerability Mechanics (DOC-58B Candidate)
1. Thread invokes `sm.executePendingSpawn(seam, validator, 1007, 259)`.
2. Inside `executePendingSpawn`, the state is temporarily `SPAWN_PENDING`.
3. The method calls native seam `seam->spawnVisualActor(...)`.
4. While execution is inside the native hook, an engine event or callback synchronously calls `sm.executePendingSpawn(seam, validator, 1007, 259)`.
5. The inner call evaluates preconditions against `SPAWN_PENDING`, succeeds, allocates native actor `Actor_B` (`0x61A02000`), transitions state to `ACTIVE`, and sets `m_nativeActorPtr = Actor_B`.
6. Control returns to the outer `executePendingSpawn`.
7. Outer `spawnVisualActor` completes and returns `Actor_A` (`0x61A01000`).
8. Outer method executes unconditionally:
   ```cpp
   m_nativeActorPtr = actor; // Overwrites 0x61A02000 with 0x61A01000!
   m_state = VisualProxyState::ACTIVE;
   ```
9. **Outcome:**
   - Two native actors exist in the KEX engine actor pool.
   - The state machine only retains a reference to `Actor_A`.
   - `Actor_B` is permanently leaked in the KEX world sector, never receives transform updates, and can never be destroyed by proxy cleanup, causing map transition crashes.

---

## 2. DOC-58D Architectural Remediation

To eliminate this vulnerability deterministically without dynamic allocations, locks, or performance overhead, DOC-58D implements an RAII-scoped reentrancy barrier:

```cpp
// Member in VisualProxyStateMachine:
bool m_inSeamCall{false};

// RAII Scope Guard inside hardened_visual_proxy_state_machine.hpp:
struct SeamCallScopeGuard {
    bool& flag;
    explicit SeamCallScopeGuard(bool& f) noexcept : flag(f) {
        flag = true;
    }
    ~SeamCallScopeGuard() noexcept {
        flag = false;
    }
    SeamCallScopeGuard(const SeamCallScopeGuard&) = delete;
    SeamCallScopeGuard& operator=(const SeamCallScopeGuard&) = delete;
};
```

### Protection Sequence
1. Upon entry into any native seam invocation (`spawnVisualActor`, `updateVisualTransform`, `transitionSector`, `removeVisualActorEx`):
   ```cpp
   SeamCallScopeGuard seamGuard(m_inSeamCall);
   ```
2. Any concurrent or synchronous reentrant attempt to mutate proxy state evaluates:
   ```cpp
   if (m_inSeamCall) {
       return SpawnExecutionResult::ReentrancyBlocked;
   }
   ```
3. Reentrant calls immediately **fail closed** with dedicated error codes.
4. When the native call returns (or unwinds due to an exception), the RAII destructor resets `m_inSeamCall = false`.

---

## 3. Method-by-Method Reentrancy Specification

| Method | Reentrancy Precondition Check | Return Value on Reentrancy | State Mutation on Reentrancy |
| :--- | :--- | :--- | :--- |
| `executePendingSpawn(...)` | `if (m_inSeamCall)` | `SpawnExecutionResult::ReentrancyBlocked` | None (`m_state` remains untouched) |
| `executePendingRemove(...)` | `if (m_inSeamCall)` | `RemoveExecutionResult::ReentrancyBlocked` | None (no double-free, no quarantine) |
| `handleSnapshot(...)` | `if (m_inSeamCall)` | `ProxyRejectionReason::ReentrancyBlocked` | None (snapshot discarded, no state change) |
| `requestShutdown()` | `if (m_inSeamCall)` | `false` | None (shutdown request deferred) |
| `executeShutdownCleanup(...)` | `if (m_inSeamCall)` | `RemoveExecutionResult::ReentrancyBlocked` | None (cleanup deferred until seam exits) |
| `retireQuarantinedActor(...)` | `if (m_inSeamCall)` | `false` | None (actor remains in quarantine) |
| `configureSession(...)` | `if (m_inSeamCall)` | `false` | None (session parameters unchanged) |

---

## 4. Verification Evidence & Proof

### A. Reproduction Fixtures
- **BEFORE Remediation:** [`repro_reentrancy_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_before.cpp)
  - Target: DOC-58B `remediated_visual_proxy_state_machine.hpp`.
  - Result: 2 native spawns in KEX; inner actor `0x61A02000` leaked. Exit code: 0 (vulnerability confirmed).
- **AFTER Remediation:** [`repro_reentrancy_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_after.cpp)
  - Target: DOC-58D `hardened_visual_proxy_state_machine.hpp`.
  - Result: Inner spawn returned `SpawnExecutionResult::ReentrancyBlocked`; outer returned `Success`; exactly 1 native spawn (`0x61A01000`); zero leaks. Exit code: 0 (remediation verified).

### B. Automated Regression Scenarios
- `TC-PSE-58C-26`: Reentrant Native Seam Callback Guard $\rightarrow$ **PASS**
- `TC-PSE-58D-01`: Reentrant Spawn Blocked (`m_inSeamCall`) $\rightarrow$ **PASS**
- `TC-PSE-58D-02`: Reentrant Remove Blocked (Double-Free Avoidance) $\rightarrow$ **PASS**
- `TC-PSE-58D-03`: Reentrant Shutdown Cleanup Blocked $\rightarrow$ **PASS**
- `TC-PSE-58D-04`: Reentrant Snapshot during Transform Update $\rightarrow$ **PASS**
- `TC-PSE-58D-05`: Reentrant `requestShutdown` Blocked $\rightarrow$ **PASS**
- `TC-PSE-58D-06`: Scope Guard Unwinding on Seam Exception $\rightarrow$ **PASS**
- `TC-PSE-58D-07`: Sequential Reentrancy Burst Resilience (5 consecutive reentrant calls) $\rightarrow$ **PASS**

---

## 5. Contract Requirements for DOC-58A Native Adapter

1. **Non-Reentrant Seam Implementations:**
   The native adapter implementation of `INativeVisualProxySeamRemediated` must **never** call back into the `VisualProxyStateMachine` during the execution of any seam method.
2. **Defensive Reentrancy Handling:**
   If a native KEX engine hook triggers an asynchronous or synchronous notification during `spawnVisualActor` or `removeVisualActorEx`, the adapter must buffer or queue the event rather than dispatching it immediately to the state machine.
3. **Reentrancy Return Code Awareness:**
   If a caller receives `SpawnExecutionResult::ReentrancyBlocked` or `RemoveExecutionResult::ReentrancyBlocked`, it must treat the call as rejected and retry on the subsequent simulation frame.
