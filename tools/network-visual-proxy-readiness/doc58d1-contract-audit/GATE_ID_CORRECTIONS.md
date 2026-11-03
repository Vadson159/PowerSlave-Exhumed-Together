# PowerSlave Exhumed Together — Restored Native Verification Gate Registry

**Document ID:** `PSE-DOC58D1-GATE-REGISTRY`  
**Date:** 2026-11-03  
**Auditor:** Gemini 3.8 High — Senior C++20 / Multiplayer QA & Code Auditor  
**Reference Document:** [`doc58c-integration-qa/NATIVE_VERIFICATION_GATES.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/doc58c-integration-qa/NATIVE_VERIFICATION_GATES.md)  
**Assigned Engineers:** GPT-6.1 Sol Extra (REA / Frida Dynamic Tracing), Claude Opus 5.5 High (DOC-58A Native Adapter)

---

## 1. Discrepancy Reconciliation & Gate Restorations

In DOC-58D (`REGRESSION_MATRIX.md` line 142), verification gate IDs `GATE-PSE-58C-03` through `07` were inadvertently reassigned to new topics (such as `InsertActor`, Thread Dispatch, ENet Demux).
This violated the persistent gate numbering contract established in DOC-58C.

**Reconciliation Action:**
1. The canonical IDs `GATE-PSE-58C-01` through `GATE-PSE-58C-07` are **strictly restored** to their original definitions from DOC-58C.
2. The new requirements introduced during DOC-58D (such as `InsertActor` assert prevention and ENet multi-slot packet routing) are assigned **new, distinct identifiers**: `GATE-PSE-58D-08` and `GATE-PSE-58D-09`.

---

## 2. Master Native Verification Gate Registry

| Gate ID | Target Function / Symbol | Architectural Contract Verified | Status | Responsible Engineer |
| :--- | :--- | :--- | :--- | :--- |
| **GATE-PSE-58C-01** | `kexSlaveLevelLocal::Unload` (`0x1401c90e0`) | Pre-unload hook barrier (`onPreUnload`) must execute at entry of `0x1401c90e0` *before* the sector array at offset `rcx + 0x140` is deallocated. Zero calls to `kexActor::Destroy` or `removeVisualActorEx` may occur after sector deallocation. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | GPT-6.1 Sol Extra / Frida |
| **GATE-PSE-58C-02** | `kexActor::Destroy` (`0x1401a02b0` $\rightarrow$ `0x1401b61d0`) | Verify whether KEX synchronously unlinks the actor from `sectors[sectorId].actorList` during `Remove`, or defers unlinking to frame-end sweep. Adapter may only return `ConfirmedSuccess` if unlinking is synchronous. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | GPT-6.1 Sol Extra / REA |
| **GATE-PSE-58C-03** | KEX ObjectNode Pool & Sweep | Attested witness requirement: verify that engine sweep zeroes `node->payload == nullptr` and `node->refs == 0` before adapter triggers `retireQuarantinedActor(true)`. Blind timers forbidden. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | GPT-6.1 Sol Extra / Frida |
| **GATE-PSE-58C-04** | `kexSlaveSectorLocal::PointInSector` (`0x1401cf540`) | Coplanar portal boundary behavior: verify that boundary tolerance ($\epsilon$) prevents dead-zone coordinate rejections when an actor crosses sector aperture planes ($Y = -6912.0$). | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | GPT-6.1 Sol Extra / REA |
| **GATE-PSE-58C-05** | MinHook Hooks on KEX Engine | Simulation thread affinity: verify `GetCurrentThreadId()` across 1,000 consecutive simulation ticks and level unloads to guarantee 0 thread mismatches between tick and lifecycle hooks. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | Claude Opus 5.5 High / DOC-58A |
| **GATE-PSE-58C-06** | `sectors[sectorId].actorList` | Multi-actor sector list contention: verify doubly-linked list walker stability when 7 remote visual proxy actors reside concurrently in Sector 259 during rapid yaw/teleport updates. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | GPT-6.1 Sol Extra / Frida |
| **GATE-PSE-58C-07** | Native Seam Dispatch Pipeline | Reentrancy isolation: verify that native KEX hooks never invoke state machine mutating APIs synchronously during seam execution. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | Claude Opus 5.5 High / DOC-58A |
| **GATE-PSE-58D-08** | `kexWldSectorLocal::InsertActor` (`0x1401e58f0`) | Sector boundary validation: verify that `ISectorValidator` rejection of invalid sector IDs (-1, 9999) reliably prevents triggering engine assertion `assert7b3c0`. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | GPT-6.1 Sol Extra / REA |
| **GATE-PSE-58D-09** | ENet Network Dispatcher | Multi-slot demultiplexing: verify that incoming ENet snapshot packets are routed strictly to slots 0..7 without crosstalk, buffer reuse corruption, or packet starvation. | **OPEN** (`NATIVE_VERIFICATION_REQUIRED`) | Claude Opus 5.5 High / DOC-58A |

---

## 3. Dynamic Verification Plan for GPT-6.1 Sol Extra

1. **Gate 1 (`GATE-PSE-58C-01`):**
   - Intercept `0x1401c90e0` via Frida.
   - On entry: inspect memory at `[rcx + 0x140]`. Confirm page is valid (`PAGE_READWRITE`).
   - Trigger `onPreUnload()` on all active proxies.
   - Monitor `kexActor::Destroy`: confirm 0 calls occur while `0x1401c90e0` is running.
   - On leave: trigger `onPostUnload()`.
2. **Gate 2 & Gate 3 (`GATE-PSE-58C-02`, `GATE-PSE-58C-03`):**
   - Trace actor list unlink order upon `NativeLifecycle_Remove`.
   - Monitor `ObjectNode` pool slots in live game heap to identify the sweep instruction.
3. **Gate 6 (`GATE-PSE-58C-06`):**
   - In cooperative play, join 8 players in Tomb of Ramses spawn sector 259.
   - Verify sector actor list links `[node+0x0]` and `[node+0x8]` are never corrupted during simultaneous movement.
