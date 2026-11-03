# PowerSlave Exhumed Together — DOC-58D.1 Engineering Handoff

**Document ID:** `PSE-DOC58D1-HANDOFF`  
**Date:** 2026-11-03  
**Auditor:** Gemini 3.8 High — Senior C++20 / Multiplayer QA & Code Auditor  
**Recipients:**
1. **Claude Opus 5.5 High** (DOC-58A Native Adapter Integrator)
2. **GPT-6.1 Sol Extra** (REA / Frida Dynamic Verification)

---

## 1. Candidate Status & Readiness

DOC-58D.1 has completed a thorough regression integrity and contract reconciliation audit. All documentation errors, gate ID mismatches, and test discrepancies have been resolved.
The candidate header [`doc58d1_hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/doc58d1_hardened_visual_proxy_state_machine.hpp) is officially certified as:

$$\mathbf{REVIEW\_READY}$$

- **Autonomous QA Status:** **73 / 74 PASS (98.65%)**, **0 FAIL**, **0 BLOCKED**
- **Native Verification Gate:** **1 OPEN** (`GATE-PSE-58C-01` / `TC-PSE-58C-03`)
- **Production Integration:** **DEFERRED TO DOC-58A** (Production code, KEX hooks, and ENet protocol unmodified)

---

## 2. Deliverables & Production Candidate Files

All artifacts reside strictly on drive `S:` inside:  
`S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness\doc58d1-contract-audit\`

| File | Description | SHA-256 Hash |
| :--- | :--- | :--- |
| [`doc58d1_hardened_visual_proxy_state_machine.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/doc58d1_hardened_visual_proxy_state_machine.hpp) | Production candidate visual proxy state machine with reentrancy guard, multi-slot guard, and two-phase teardown barrier. | `55A73B3C383E5D79D7ABC34B4B6B1AB16DDAA028330465D2BF2F0EE7DA414CFB` |
| [`multi_slot_snapshot_guard.hpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/multi_slot_snapshot_guard.hpp) | Standalone 8-player snapshot guard supporting slots 0..7 without modifying frozen DOC-55 files. | `9C60CC61E091B2E4664216AD02DB63B206BE83F2EC43FFA797E365C34176EC84` |
| [`test_doc58d1_reconciled.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/test_doc58d1_reconciled.cpp) | Reconciled 74-scenario autonomous test harness. | `CEE2A1AC97EAD0090DB84E4E989AE478AE2445E64120FD435BC633D3705BCB87` |
| [`TEST_RESULTS.json`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/TEST_RESULTS.json) | Machine-readable results from the full 74-scenario test execution. | `215985BD057362E5196C38DE9F6E7B6E45176200BDE2DCE8DCDA3E2972BDC753` |
| [`CONTRACT_DIFF_AUDIT.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/CONTRACT_DIFF_AUDIT.md) | Exhaustive diff audit reconciling test assertions across DOC-58B, DOC-58C, and DOC-58D. | *(Generated Document)* |
| [`GATE_ID_CORRECTIONS.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/GATE_ID_CORRECTIONS.md) | Canonical registry of verification gates G1–G7 and new gates G8–G9. | *(Generated Document)* |
| [`LIFETIME_OWNERSHIP_AUDIT.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/LIFETIME_OWNERSHIP_AUDIT.md) | Detailed audit of unload, ownership, exception safety, and teardown barriers. | *(Generated Document)* |
| [`DOC58D1_FINAL_REPORT.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58d1-contract-audit/DOC58D1_FINAL_REPORT.md) | Comprehensive audit closure report. | *(Generated Document)* |

---

## 3. Integration Blueprint for Claude Opus 5.5 High (DOC-58A)

### A. Namespace & Header Inclusion
```cpp
#include "doc58d1_hardened_visual_proxy_state_machine.hpp"
#include "multi_slot_snapshot_guard.hpp"

// Use either namespace:
using pse::proxy::hardened::VisualProxyStateMachine;
// Or:
// using pse::proxy::doc58d1::VisualProxyStateMachine;
```

### B. Hooking Dual-Phase Level Teardown & Map Ready
```cpp
// Hook on kexSlaveLevelLocal::Unload (0x1401c90e0)
void Hook_kexSlaveLevelLocal_Unload(void* thisPtr) {
    // Phase 1 (Entry): Suppress native Remove calls before sectors are deallocated
    for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
        g_visualProxies[slot]->onPreUnload();
    }

    // Call original KEX Level::Unload
    Original_kexSlaveLevelLocal_Unload(thisPtr);

    // Phase 2 (Exit): Zero active pointers and advance generation
    // Teardown barrier remains active to block premature operations
    for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
        g_visualProxies[slot]->onPostUnload();
    }
}

// Hook on KEX Level::Load completion / Map Ready
void Hook_OnMapReady() {
    // Phase 3: Lower teardown barrier when new level is fully ready
    for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
        g_visualProxies[slot]->onMapReady();
    }
}
```

### C. Non-Reentrant Seam Invariants
Ensure your implementation of `INativeVisualProxySeamRemediated` does NOT invoke state machine methods during seam calls. If reentrancy occurs, the state machine will fail closed returning `SpawnExecutionResult::ReentrancyBlocked` without leaking actors or corrupting state.

---

## 4. Native Dynamic Verification Blueprint for GPT-6.1 Sol Extra

Execute verification in `PowerSlaveExhumed.exe` against the restored canonical gates:
- **`GATE-PSE-58C-01`:** Verify page protection at `rcx + 0x140` at `0x1401c90e0` entry, and prove 0 `kexActor::Destroy` calls occur during unload.
- **`GATE-PSE-58C-02`:** Verify synchronous unlinking from `sectors[sectorId].actorList` on `NativeLifecycle_Remove`.
- **`GATE-PSE-58C-03`:** Verify `ObjectNode` pool sweep zeroes payload before calling `retireQuarantinedActor(true)`.
- **`GATE-PSE-58C-04`:** Verify portal aperture coplanar boundary tolerance at $Y = -6912.0$.
- **`GATE-PSE-58C-05`:** Verify simulation thread affinity across 1,000 frames.
- **`GATE-PSE-58C-06`:** Verify sector 259 list walker stability with 7 remote proxies.
- **`GATE-PSE-58C-07`:** Verify zero reentrant calls into proxy state machine during native hooks.
- **`GATE-PSE-58D-08`:** Verify `ISectorValidator` prevents `assert7b3c0` in `InsertActor`.
- **`GATE-PSE-58D-09`:** Verify ENet packet demux routes snapshot packets cleanly to slots 0..7 without crosstalk.
