# PowerSlave Exhumed Together — DOC-58D Engineering Handoff

**Document ID:** `PSE-DOC-58D-HANDOFF`  
**Date:** 2026-11-03  
**Author:** Gemini 3.8 High (Adversarial QA / C++20 Remediation Engineer)  
**Recipients:**
1. **Claude Opus 5.5 High** (DOC-58A Native Adapter Integrator)
2. **GPT-6.1 Sol Extra** (REA / Frida Dynamic Verification)

---

## 1. Status Overview & Milestone Context

DOC-58D has successfully remediated the two critical defects identified during DOC-58C adversarial testing and completed full regression QA:

| Area | Status | Evidence |
| :--- | :--- | :--- |
| **DOC-58B Remediation Regression** | **18 / 18 PASS** | Suite 1 in [`test_doc58d_regression.exe`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/build/Release/test_doc58d_regression.exe) |
| **DOC-58C Contract Regression** | **31 / 32 PASS**, **1 NATIVE_REQ**, **0 FAIL** | Suite 2 in `test_doc58d_regression.exe` |
| **DOC-58D New Adversarial Tests** | **20 / 20 PASS** | Suite 3 in `test_doc58d_regression.exe` |
| **Reentrancy Leak Defect (`TC-PSE-58C-26`)** | **REMEDIATED (PASS)** | [`repro_reentrancy_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_before.cpp) vs [`repro_reentrancy_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_after.cpp) |
| **3–8 Player Slot Limit (`TC-PSE-58C-28`)** | **REMEDIATED (PASS)** | [`repro_slot_limit_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_slot_limit_before.cpp) vs [`repro_slot_limit_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_slot_limit_after.cpp) |
| **Stale `isLevelValid` / UAF (`TC-PSE-58C-03`)**| **NATIVE_VERIFICATION_REQUIRED** | Maintained as an explicit native verification gate (`GATE-PSE-58C-01`) |
| **Native Integration Status** | **DEFERRED** | Autonomous QA complete; ready for native integration in DOC-58A |

---

## 2. Deliverables & Source Artifacts

All production-candidate headers, tests, and documentation are located in:  
`S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness\doc58d-hardening\`

| Artifact | Type | Description |
| :--- | :--- | :--- |
| [`hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/hardened_visual_proxy_state_machine.hpp) | Header | Hardened visual proxy state machine with `m_inSeamCall` guard, MultiSlotGuard8 integration, and two-phase teardown barrier. |
| [`multi_slot_snapshot_guard.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/multi_slot_snapshot_guard.hpp) | Header | Standalone 8-slot snapshot guard replacing hardcoded 2-slot DOC-55 guard without touching frozen DOC-55 sources. |
| [`repro_reentrancy_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_before.cpp) | Test Fixture | Reproduces reentrancy actor leak on DOC-58B candidate. |
| [`repro_reentrancy_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_reentrancy_after.cpp) | Test Fixture | Proves reentrancy guard blocks reentrant call with `ReentrancyBlocked` on DOC-58D hardened candidate. |
| [`repro_slot_limit_before.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_slot_limit_before.cpp) | Test Fixture | Reproduces Slot 3 rejection on DOC-58B candidate with `snapshot55::Guard`. |
| [`repro_slot_limit_after.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/repro_slot_limit_after.cpp) | Test Fixture | Proves Slot 3 accepted cleanly on DOC-58D candidate with `MultiSlotGuard8`. |
| [`test_doc58d_regression.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/test_doc58d_regression.cpp) | Test Suite | Comprehensive 70-scenario autonomous test harness. |
| [`workflow.py`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/workflow.py) | Python Script | Automated CMake configuration, build, and test runner. |
| [`REGRESSION_MATRIX.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/REGRESSION_MATRIX.md) | Document | Full results matrix for all 70 test scenarios. |
| [`REENTRANCY_CONTRACT.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/REENTRANCY_CONTRACT.md) | Document | Reentrancy barrier specification and non-reentrant dispatch contract. |
| [`MULTISLOT_GUARD_CONTRACT.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/MULTISLOT_GUARD_CONTRACT.md) | Document | MultiSlotGuard8 specification and 8-player cooperative invariants. |
| [`KEX_LIFETIME_CONTRACT_REVIEW.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d-hardening/KEX_LIFETIME_CONTRACT_REVIEW.md) | Document | Two-phase teardown contract and native verification gate analysis. |

---

## 3. Actionable Instructions for Claude Opus 5.5 High (DOC-58A)

### A. Include Directives
Replace inclusions of `visual_proxy_state_machine.hpp` or `remediated_visual_proxy_state_machine.hpp` with:
```cpp
#include "hardened_visual_proxy_state_machine.hpp"
#include "multi_slot_snapshot_guard.hpp"

// Namespace:
using pse::proxy::hardened::VisualProxyStateMachine;
using pse::proxy::hardened::INativeVisualProxySeamRemediated;
using pse::proxy::hardened::ISectorValidator;
using pse::proxy::hardened::RemoveOutcome;
using pse::proxy::hardened::SpawnExecutionResult;
using pse::proxy::hardened::RemoveExecutionResult;
using pse::proxy::hardened::ProxyRejectionReason;
```

### B. Hooking `kexSlaveLevelLocal::Unload` (`0x1401c90e0`)
You must hook both the **entry** and **exit** of `kexSlaveLevelLocal::Unload`:
```cpp
void Hook_kexSlaveLevelLocal_Unload(void* thisPtr) {
    // Phase 1: Pre-Unload Barrier (Entry)
    // Drops pending spawns and suppresses native Remove calls BEFORE sectors are freed!
    for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
        g_visualProxies[slot]->onPreUnload();
    }

    // Call original KEX Level::Unload
    Original_kexSlaveLevelLocal_Unload(thisPtr);

    // Phase 2: Post-Unload Reset (Exit)
    // Nullifies actor pointers, advances generation, and returns to ABSENT
    for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
        g_visualProxies[slot]->onPostUnload();
    }
}
```

### C. Non-Reentrant Native Seam Dispatch
- Ensure that your `INativeVisualProxySeamRemediated` implementation does NOT call back into `VisualProxyStateMachine` during `spawnVisualActor` or `removeVisualActorEx`.
- If an engine event fires synchronously during a seam call, buffer it for dispatch on the next frame.
- Check return values: if `executePendingSpawn` returns `SpawnExecutionResult::ReentrancyBlocked`, log a warning and retry next tick.

### D. Multi-Player Slot Array
Instantiate state machines for all 8 player slots:
```cpp
std::array<std::unique_ptr<VisualProxyStateMachine>, pse::MAX_PLAYERS> g_visualProxies;

void InitVisualProxies() {
    for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
        g_visualProxies[slot] = std::make_unique<VisualProxyStateMachine>(slot);
        g_visualProxies[slot]->registerSimulationThread();
    }
}
```

---

## 4. Actionable Instructions for GPT-6.1 Sol Extra (REA / Frida Verification)

Execute dynamic verification against running target `PowerSlaveExhumed.exe` (SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`):

| Gate ID | Target Verification Check |
| :--- | :--- |
| **GATE-PSE-58C-01** | Verify `onPreUnload()` triggers at `0x1401c90e0` entry, and that sector array at `rcx + 0x140` is valid readable memory at that exact moment. Verify zero calls to `kexActor::Destroy` occur after sector array deallocation. |
| **GATE-PSE-58C-02** | Trace `kexActor::Destroy` return behavior. Determine whether pool reclamation is immediate or deferred to frame end sweep. |
| **GATE-PSE-58C-03** | Verify `kexWldSectorLocal::InsertActor` (`0x1401e58f0`). Confirm sector boundary checks prevent triggering `assert7b3c0`. |
| **GATE-PSE-58C-04** | Verify simulation thread ID in live process matches the thread calling state machine APIs. |
| **GATE-PSE-58C-05** | Trace native hooks to confirm zero reentrant calls into proxy state machine during seam calls. |
| **GATE-PSE-58C-06** | Spawn 7 concurrent visual proxy actors in Tomb of Ramses (sector 259) and verify KEX actor pool allocation limits and performance. |
| **GATE-PSE-58C-07** | Trace ENet network packet reception and verify snapshot demuxing delivers packets cleanly to slots 0..7 without crosstalk. |
