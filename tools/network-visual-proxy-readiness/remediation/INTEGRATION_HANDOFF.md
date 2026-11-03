# PowerSlave Exhumed Together — DOC-58B Remediation Integration Handoff

**Document ID:** `HANDOFF-DOC58B-REMEDIATION-V1`  
**Classification:** Technical Integration Guide & Architecture Contract  
**Author:** Gemini 3.8 High, Independent QA & Multiplayer Networking Specialist  
**To:** Claude Opus 5.5 High / GPT-6.1 Sol Extra (DOC-58 Integrators)  
**Date:** 2026-11-03  
**Status:** CANDIDATE READY FOR REVIEW (`NATIVE INTEGRATION DEFERRED`)  
**Artifact Directory:** `tools/network-visual-proxy-readiness/remediation/`  
**Candidate Header:** `tools/network-visual-proxy-readiness/remediation/remediated_visual_proxy_state_machine.hpp`  

---

## 1. Executive Summary

This handoff transfers the **hardened, remediated Visual Proxy State Machine** to the DOC-58 integration team. All critical defects and false assumptions discovered during the initial readiness review and adversarial lifecycle QA have been remediated and autonomously verified across 18 comparative test scenarios (`AUTONOMOUS QA PASS`).

> [!IMPORTANT]
> **Production Code Status:**  
> The candidate state machine is fully isolated in `tools/network-visual-proxy-readiness/remediation/`.  
> No production DLLs, ENet code, Wire Protocol v1.10 structures, DOC-58A files, or historical evidence have been modified.  
> Native integration remains **DEFERRED** pending completion of DOC-58A native cross-sector movement and independent native audit.

---

## 2. Inventory of Remediated Defects

| Defect / Risk | Baseline Symptom | Candidate Remediation | Verification |
| :--- | :--- | :--- | :--- |
| **ADV-02: Failed Remove (Orphan Leak)** | `removeVisualActor() == false` nullified pointer and set `ABSENT`, leaking live actor in KEX. | Introduced `QUARANTINED` state and `RemoveOutcome` tri-state. Pointer preserved, subsequent spawns blocked. | `SCENARIO-01`, `SCENARIO-02`, `repro_adv02` |
| **ADV-11: Hardcoded Sector 259 Fallacy** | Spawned actor into Sector 259 when P1 was in Sector 176 (phantom invisible sprite). | Added `ISectorValidator` contract. Requires coordinate-sector match; rejects invalid/unverified sectors. | `SCENARIO-14`, `SCENARIO-15`, `repro_adv11` |
| **Level Unload Shutdown (UAF Crash)** | Synchronous `forceShutdown()` called `Remove` during `Unload`, triggering UAF in `Drain`. | Split into `requestShutdown()` and context-aware `executeShutdownCleanup(seam, isLevelValid)`. Skips seam on invalid level. | `SCENARIO-07` |
| **C++ Exception Leakage** | `assertSimulationThread()` threw `std::runtime_error`, causing `std::terminate()` across C hooks. | Entire public API made strictly `noexcept`. Returns explicit `AffinityViolation` fail-closed enum. | `SCENARIO-08`, `SCENARIO-18` |
| **Blind Repeated Removal** | Baseline caller could call `executePendingRemove()` repeatedly on failed pointer. | `QUARANTINED` state strictly blocks repeated native calls (`QuarantinedBlocked`). | `SCENARIO-04` |
| **Duplicate Actor Spawning** | Failed remove in baseline allowed reconnecting client to spawn duplicate proxy. | `QUARANTINED` state blocks `executePendingSpawn()` and snapshot updates until engine sweep. | `SCENARIO-16` |
| **Rogue Mock / Seam Exceptions** | External seam throw crashed caller process. | Internal `try/catch` guards isolate native exceptions and transition actor to `QUARANTINED`. | `SCENARIO-09` |
| **Recycled Actor Pointer (ABA)** | Stale callbacks could mistake recycled heap address for previous actor. | Strictly monotonic `localProxyGeneration()` increments on every retirement/cleanup step. | `SCENARIO-10` |

---

## 3. What Problems Remain Native-Dependent

The following components cannot be resolved by an autonomous state machine alone and require native integration in KEX:

1. **Native Implementation of `ISectorValidator`:**  
   The candidate state machine defines the interface `ISectorValidator`. The concrete implementation must map to DOC-58A's native `PointInSector` or sector boundary lookup in KEX.  
   *Dependency:* Await Opus's completion of DOC-58A cross-sector movement seam.

2. **Native Implementation of `removeVisualActorEx`:**  
   The native adapter must distinguish `ConfirmedSuccess` (actor unlinked from `sectors[sectorId].actorList` and released) from `DefiniteFailure` (actor locked or rejection) and `Uncertain` (exception in hook).

3. **Attested Hook Timing for Level Unload (`isLevelValid`):**  
   Client B's game loop must supply an accurate `isLevelValid` boolean to `executeShutdownCleanup`. This requires hooking into KEX's level lifecycle (e.g. checking whether `kexSlaveLevelLocal::Unload` has executed or level actor counts `+0xd8` are > 0).

---

## 4. Minimal Required Production Changes

To integrate the candidate state machine into production (`experiments/m1/native_second_instance/src/client.cpp`):

### Step 1: Replace Include
Replace `#include "visual_proxy_state_machine.hpp"` with the remediated header:
```cpp
#include "remediated_visual_proxy_state_machine.hpp"
using VisualProxy = pse::proxy::remediated::VisualProxyStateMachine;
```

### Step 2: Implement Concrete Native Seam & Sector Validator
Wrap existing DOC-57/DOC-58A native hooks into the remediated interfaces:
```cpp
class NativeVisualProxySeam : public pse::proxy::remediated::INativeVisualProxySeamRemediated {
public:
    void* spawnVisualActor(uint32_t actorType, float x, float y, float z, float yaw, int sector) noexcept override {
        return NativeObjectFactory_Spawn(actorType, x, y, z, yaw, sector);
    }
    bool updateVisualTransform(void* actor, float x, float y, float z, float yaw, float pitch) noexcept override {
        return NativeMovement_UpdateTransform(actor, x, y, z, yaw, pitch);
    }
    bool transitionSector(void* actor, int newSector) noexcept override {
        return NativeMovement_LinkSector(actor, newSector);
    }
    pse::proxy::remediated::RemoveOutcome removeVisualActorEx(void* actor) noexcept override {
        if (!actor) return pse::proxy::remediated::RemoveOutcome::DefiniteFailure;
        bool ok = NativeLifecycle_Remove(actor);
        return ok ? pse::proxy::remediated::RemoveOutcome::ConfirmedSuccess
                  : pse::proxy::remediated::RemoveOutcome::DefiniteFailure;
    }
};

class NativeSectorValidator : public pse::proxy::remediated::ISectorValidator {
public:
    bool validateDestinationSector(int sectorId, float x, float y, float z) const noexcept override {
        // Calls DOC-58A PointInSector or validates sector array bounds
        return NativeSector_PointInSector(sectorId, x, y, z);
    }
};
```

### Step 3: Update Tick Execution in Game Loop
```cpp
// Simulation Tick Seam
if (g_visualProxy.state() == pse::proxy::remediated::VisualProxyState::SPAWN_PENDING) {
    // Resolve destination sector via DOC-58A PointInSector
    int resolvedSector = g_sectorResolver.resolveSectorForCoords(pendingX, pendingY, pendingZ);
    auto res = g_visualProxy.executePendingSpawn(&g_nativeSeam, &g_sectorValidator, 1007, resolvedSector);
    if (res != pse::proxy::remediated::SpawnExecutionResult::Success) {
        LOG_WARN("Visual proxy spawn delayed: %s", toString(res));
    }
}
```

---

## 5. DTO & Protocol Compatibility

> [!NOTE]
> **Zero Wire Changes:**  
> The existing `BridgePlayerSnapshot` DTO and Wire Protocol v1.10 remain **100% untouched**.  
> The wire format does NOT need to transmit `sectorId`. Client B determines the sector locally from Host P1's `(posX, posY, posZ)` using the client-side loaded level geometry.

---

## 6. Safe Handling of Failed / Uncertain Removal

If `executePendingRemove()` fails or returns ambiguous status:

1. **State Machine Action:** State machine transitions to `QUARANTINED` and retains `m_nativeActorPtr`.
2. **Game Loop Action:** Do NOT loop calling remove. Do NOT reset proxy state manually.
3. **Recovery Action:** Allow KEX engine's level transition to reclaim the memory during `kexSlaveLevelLocal::Unload`.
4. **Shutdown Action:** Call `executeShutdownCleanup(seam, isLevelValid)`. If level is already unloaded, it cleans up state silently without touching KEX pointers.

---

## 7. Unknowns & Constraints for Future Audits

The following items are marked **`[UNKNOWN]`** and require native verification on real KEX binary:

- `[UNKNOWN]` Exact execution order between `Session::stop()` and `kexSlaveLevelLocal::Unload` during hard application exit (`Alt+F4` / `ExitProcess`).
- `[UNKNOWN]` Whether KEX internal actor linked list (`sectors[id].actorList`) is thread-safe if KEX sound or render threads are running during `Remove`.
- `[UNKNOWN]` Maximum latency between Host P1 crossing a sector boundary and Client B receiving the snapshot, which may cause a 1-tick delay in `PointInSector` resolution.

---

## 8. Summary for Opus & Sol

1. Use `remediated_visual_proxy_state_machine.hpp`.
2. Never hardcode Sector 259. Connect `executePendingSpawn` to DOC-58A's `PointInSector`.
3. Never call native `Remove` after `Unload` has begun.
4. Keep all hooks strictly `noexcept`.
