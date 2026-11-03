# PowerSlave Exhumed Together — DOC-58C Integration QA Handoff

**Document ID:** `PSE-DOC58C-HANDOFF-V1`  
**Classification:** Engineering Handoff & Architecture Directive  
**Author:** Gemini 3.8 High, Independent C++20 / KEX Lifecycle / Multiplayer QA Engineer  
**To:** Claude Opus 5.5 High (DOC-58A Native Integrator) & GPT-6.1 Sol Extra (Native REA Auditor)  
**Date:** 2026-11-03  
**Status:** CANDIDATE INTEGRATION CONTRACTS AUDITED & READY  

---

## 1. Executive Summary

This handoff concludes **DOC-58C: Native Integration Contract & Adversarial QA**.

We evaluated candidate `remediated_visual_proxy_state_machine.hpp` against 32 rigorous integration contract scenarios. While the candidate's core quarantine model, tri-state removal taxonomy, and sector validation interfaces successfully solve the historical ADV-02 and ADV-11 vulnerabilities, our adversarial QA uncovered **two critical candidate defects** and **one unproven native assumption**:

1. **Reentrancy Leak Defect (`TC-PSE-58C-26`):** The candidate state machine lacks an internal reentrancy guard. Reentrant native callbacks during `spawnVisualActor` overwrite `m_nativeActorPtr`, permanently leaking actors in KEX. (Verified with `repro_reentrancy_hazard.exe`).
2. **3–8 Player Slot Limit Defect (`TC-PSE-58C-28`):** The candidate embeds `snapshot55::Guard`, which hardcodes `std::array<Slot, 2> slots`. Any snapshot for Player Slots 2 through 7 is rejected with `IdentityMismatch`, blocking 3–8 player co-op. (Verified with `repro_snapshot_slot_limit.exe`).
3. **Stale `isLevelValid` UAF Trap (`TC-PSE-58C-03`):** Proves that a boolean `isLevelValid` flag is an insufficient guard without a synchronized Native Pre-Unload hook barrier (`0x1401c90e0`).

> [!IMPORTANT]
> **Production Code Status:**  
> Neither production DLLs, DOC-58A sources, DOC-58B candidate files, ENet code, Wire Protocol v1.10 structures, `PROGRESS.md`, nor historical evidence indices have been modified.  
> All DOC-58C QA tools, tests, and fixtures are strictly contained in `tools/network-visual-proxy-readiness/doc58c-integration-qa/`.

---

## 2. Summary of Test Execution Results

```
============================================================================
DOC-58C QA Suite Summary:
Total Scenarios Executed: 32
PASS:                        29
FAIL (Repro Defects):         2 (Reentrancy Hazard + snapshot55::Guard Slot Limit)
NATIVE_VERIFICATION_REQUIRED: 1 (Level Unload Hook Barrier)
BLOCKED:                      0
============================================================================
```

- **Domain A (KEX Level Lifetime):** 4 PASS, 1 NATIVE_VERIFICATION_REQUIRED
- **Domain B (RemoveOutcome Contract):** 4 PASS
- **Domain C (QUARANTINED Lifecycle Deep):** 6 PASS
- **Domain D (Sector Validation Contract):** 6 PASS
- **Domain E (Affinity, Exceptions, Reentrancy):** 4 PASS, 1 FAIL (Reentrancy Defect)
- **Domain F (Multi-Client 2–8 Players):** 5 PASS, 1 FAIL (Guard 2-Slot Limit Defect)

---

## 3. Mandatory Directives for Claude Opus (DOC-58A / Adapter Integrator)

When completing DOC-58A and integrating `remediated_visual_proxy_state_machine.hpp`:

1. **Update `snapshot55::Guard` to Support 8 Slots:**  
   Replace `std::array<Slot, 2> slots;` with `std::array<Slot, pse::MAX_PLAYERS> slots;` so that `handleSnapshot` accepts snapshots for remote clients in slots 2 through 7.
2. **Enforce Non-Reentrant Seam Dispatch:**  
   The Native Adapter MUST NOT dispatch synchronous callbacks or notifications that reenter `VisualProxyStateMachine` during an in-flight `spawnVisualActor` or `removeVisualActorEx` call. Queue all events and flush them after the seam call returns.
3. **Implement Atomic Pre-Unload Hook (`GATE-PSE-58C-01`):**  
   Hook `kexSlaveLevelLocal::Unload` (`0x1401c90e0`) entry point to synchronously call `proxy.onMapInvalidated(levelAlreadyFreed = true)` *before* KEX frees the `sectors` array.
4. **Implement Concrete `ISectorValidator` via Native `1cf540`:**  
   Wire `ISectorValidator::validateDestinationSector` to native `PointInSector` (`0x1401cf540`), verifying bounds $0 \le \text{sector} < 270$ and coplanar boundary tolerance.

---

## 4. Mandatory Directives for GPT-6.1 Sol (Native REA Auditor)

Verify the following 7 gates in the live KEX binary (`b8f131037187...`):

- **Gate 1 (`GATE-PSE-58C-01`):** Verify `0x1401c90e0` hook timing occurs before sector array deallocation.
- **Gate 2 (`GATE-PSE-58C-02`):** Verify that KEX `Remove` (`0x1401a02b0`) synchronously invokes `Drain` and unlinks the actor from `sectors[sectorId].actorList` before returning.
- **Gate 3 (`GATE-PSE-58C-03`):** Attest the exact KEX ObjectNode pool sweep timing to provide a verified witness for `retireQuarantinedActor(true)`.
- **Gate 4 (`GATE-PSE-58C-04`):** Test `PointInSector` (`0x1401cf540`) on the Ramses portal boundary ($Y = -6912.0$).
- **Gate 5 (`GATE-PSE-58C-05`):** Confirm 100% Simulation TID affinity across 1,000 game frames.
- **Gate 6 (`GATE-PSE-58C-06`):** Verify sector doubly-linked list stability when 7 remote visual proxies reside simultaneously in Sector 259.
- **Gate 7 (`GATE-PSE-58C-07`):** Audit native hook call graph for zero reentrancy.

---

## 5. Artifact Directory & Inventory

All artifacts are persisted on `S:` in:  
`S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness\doc58c-integration-qa\`

| Artifact | Path | Purpose |
| :--- | :--- | :--- |
| `INTEGRATION_CONTRACTS.md` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/INTEGRATION_CONTRACTS.md` | Required Native Adapter guarantees and architectural invariants. |
| `ADVERSARIAL_MATRIX.md` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/ADVERSARIAL_MATRIX.md` | Complete 32-scenario execution matrix, defect analyses, and risk levels. |
| `NATIVE_VERIFICATION_GATES.md` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/NATIVE_VERIFICATION_GATES.md` | Detailed specification of 7 mandatory native game binary gates. |
| `test_integration_contracts.cpp` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/test_integration_contracts.cpp` | C++20 automated test suite executing all 32 integration scenarios. |
| `TEST_RESULTS.json` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/TEST_RESULTS.json` | Actual machine-readable test execution output. |
| `repro_reentrancy_hazard.cpp` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/repro_reentrancy_hazard.cpp` | Minimal repro fixture for candidate reentrancy leak defect. |
| `repro_snapshot_slot_limit.cpp` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/repro_snapshot_slot_limit.cpp` | Minimal repro fixture for candidate 3–8 player slot rejection defect. |
| `CMakeLists.txt` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/CMakeLists.txt` | Standalone MSVC `/W4 /WX /permissive- /utf-8` build configuration. |
| `workflow.py` | `tools/network-visual-proxy-readiness/doc58c-integration-qa/workflow.py` | Automated build and test execution workflow. |
