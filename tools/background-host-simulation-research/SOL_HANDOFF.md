# Handoff Specification for Sol Max: DOC-53 Background Host Simulation Integration

**Document ID:** DOC-53-SOL-HANDOFF  
**Status:** READY FOR IMPLEMENTATION (Post-DOC-52 Integration)  
**Author:** Senior KEX Reverse Engineering Researcher / Windows Runtime Analyst  
**Date:** 2026-11-02  
**Target File for Modification:** `experiments/m1/native_bootstrap/src/bootstrap.cpp`  
**Target Binary:** `anubis_Shipping_Steam_x64.exe` (SHA256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)

---

## 1. Objective & Scope

### 1.1 Objective
Eliminate the requirement for external synthetic Windows activation messages (`WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_SETFOCUS`) discovered in DOC-51. Host A must automatically continue native simulation (`Player::Update(primary())` -> secondary `Player::Update(player())`) at 60 Hz when running in the background while Client B holds foreground focus.

### 1.2 Non-Interference Guarantees
* **DOC-52 Boundaries:** Do not alter the terminal shutdown sequence or signal handling currently being investigated in DOC-52.
* **ENet Network Protocol:** Do not alter packet serialization or ENet worker socket behavior.
* **Singleplayer Fidelity:** The solution must activate **only** when an active multiplayer session is established (`g.network && g.network->active()`). Singleplayer pause behavior must remain completely unmodified.

---

## 2. Reverse-Engineered Gating Architecture

### 2.1 Critical RVAs & Offsets
* **Main Simulation Loop (`StateTick`):** `0x1401b6ec0` (RVA `0x1b6ec0`)
* **Pause Manager Singleton:** Stored at `0x140a43f30` (RVA `0xa43f30`)
* **`IsPaused()` Virtual Method Call:** RVA `0x1b6fd7` (Call `[rax + 0x10]` on Pause Manager)
* **Pause Manager State Byte:** `byte ptr [0x140a43f30 + 0x1b0]` (`1` = paused, `0` = running)
* **GameInstance Pause Menu Overlay Flag:** `byte ptr [GameInstance + 0x20c]` (GameInstance at `0x140af33a0`)
* **Player Update Call:** `0x1401b7013` (RVA `0x1b7013`, calls `0x1401dfe30` with `rcx = &GameInstance->p1`)
* **Engine Sleep Throttle:** RVA `0xa7331` (Governed by `cl_engineNoSleep 1`)

### 2.2 Mechanism of Failure in DOC-51
1. User clicks Client B -> Host A loses OS focus.
2. SDL queues `SDL_WINDOWEVENT_FOCUS_LOST` (ID `0x0d`).
3. KEX event dispatcher calls `0x14014c090`, setting `[0x140a43f30 + 0x1b0] = 1`.
4. In `StateTick`, `IsPaused()` returns `1`, causing execution to branch directly to the epilogue (`0x1401b765a`), completely skipping `Player::Update(primary())`.
5. In `bootstrap.cpp`, `hookUpdate(primary())` is never called -> `g.localThisTick` remains `0` -> P2 never spawns, and incoming ENet commands are never stepped.

---

## 3. Thread Ownership & Invariants

```
┌────────────────────────────────────────────────────────┐
│               THREAD CONCURRENCY CONTRACT              │
├───────────────────────────┬────────────────────────────┤
│ Simulation Thread (TID A) │ ENet Worker Thread (TID B) │
│ - Owns KEX Engine State   │ - Owns UDP Socket I/O      │
│ - Executes hookTick()     │ - Executes enet_host_service│
│ - Executes hookUpdate()   │ - Enqueues Input Packets   │
│ - MUTATES GAME ACTORS     │ - NO KEX CALLS ALLOWED     │
└───────────────────────────┴────────────────────────────┘
```

1. **Simulation TID Sole Ownership:** All native KEX updates, actor manipulation, and pause flag overrides **must occur exclusively on the Simulation Thread** (`GetCurrentThreadId() == g.expectedTid`).
2. **Worker Isolation:** The ENet worker thread must NEVER touch KEX memory, invoke `Player::Update`, or manipulate pause flags. Cross-thread communication remains strictly mediated by atomic buffers / thread-safe queues.

---

## 4. Implementation Guide for `bootstrap.cpp`

### 4.1 Recommended Implementation Approach (Option A1: Pause Gate Override)

Modify `experiments/m1/native_bootstrap/src/bootstrap.cpp`:

#### Step 1: Query Network Activity
In `Session` (or via `g.network`), expose an active check:
```cpp
bool isNetworkSessionActive() {
    return g.network != nullptr && g.network->active() && !g.worldExit && !g.shutdown;
}
```

#### Step 2: Suppress Focus-Loss Auto-Pause in `hookTick`
Within `hookTick(void* self)`:
```cpp
void __fastcall hookTick(void* self) {
    if (self != instance() || !thread()) {
        ++g.foreignCalls; block("tick_identity_or_thread");
        reinterpret_cast<Unary>(g.original[profile::Tick])(self); return;
    }

    // Suppress background auto-pause during active gameplay session
    if (gameplay() && isNetworkSessionActive()) {
        // Clear Pause Manager internal flag if set by focus loss
        Byte* pauseMgr = *reinterpret_cast<Byte**>(g.base + 0xa43f30);
        if (pauseMgr) {
            *reinterpret_cast<Byte*>(pauseMgr + 0x1b0) = 0;
        }
        // Clear GameInstance pause overlay flag if set by focus loss
        *reinterpret_cast<Byte*>(instance() + 0x20c) = 0;
    }

    if (!g.observedTid) {
        g.observedTid = GetCurrentThreadId();
        log("simulation_attested", "\"expected_tid\":%lu,\"observed_tid\":%lu,\"game_instance\":\"%p\"", 
            g.expectedTid, g.observedTid, self);
    }
    if (g.inTick) { block("nested_tick"); return; }
    g.inTick = true; g.localThisTick = 0; ++g.ticks;
    if (g.shutdown) ++g.shutdownTicks;

#ifdef PSE_M12A
    if (g.network) g.network->tick(g.ticks, gameplay() && !g.failed && !g.worldExit && !g.shutdown);
#endif

    reinterpret_cast<Unary>(g.original[profile::Tick])(self);

    // Existing attestation and P2 construction continues cleanly...
    ...
```

*Note on Esc Menu:* If the Host player intentionally presses `Esc` while co-op is running, KEX sets substate or menu state in `[instance() + 0x24] / [instance() + 0x28]`. In multiplayer co-op, listen servers continue ticking world actors in the background while the menu overlay draws. Clearing `0x1b0` ensures clients are not frozen when host accesses options.

#### Step 3: Verify Double-Update Protection
Ensure the existing guards in `hookUpdate` remain intact:
```cpp
reinterpret_cast<Unary>(g.original[profile::Update])(p);
if (p != primary()) return;
if (!thread() || !g.inTick) { ++g.foreignCalls; block("local_update_outside_attested_tick"); return; }
++g.localUpdates; ++g.localThisTick;
if (g.localThisTick != 1) { block("duplicate_local_update"); return; }
```
Because the native engine call inside `hookTick` is the sole caller of `Player::Update(primary())`, clearing the pause flag ensures exactly **one** update per tick frame.

---

## 5. Pre-Conditions & Post-Conditions

### Pre-Conditions (Before Calling Native Tick)
1. `thread() == true` (Current thread is strictly Engine Simulation Thread).
2. `read<int>(instance(), 0x24) == 3` (State is Gameplay).
3. `!g.shutdown && !g.worldExit` (Process is not in teardown).
4. `cl_engineNoSleep == 1` (Engine loop does not invoke `SleepEx`).

### Post-Conditions (After Native Tick Returns)
1. If in background with active network: `g.localThisTick == 1` (P1 executed exactly once).
2. `g.activeTicks` increments at 60 Hz.
3. Once `g.attestSamples >= 60`: `g.p2.actor != nullptr` (P2 constructed and live).
4. For each incoming client packet applied: `g.motionTick == g.ticks` and P2 position changes according to input.
5. Invariant check: `guards() == true` (Storage buffer guards `0xa5` unmodified).

---

## 6. Pass Criteria & Hard Stop Conditions

### Pass Criteria
* **No Synthetic Messages:** Host A successfully progresses through `Phase::Waiting -> Armed -> Live -> Complete` with **zero** external Win32 activation messages sent to its HWND.
* **Continuous Cadence:** `tick_status` logs show continuous increments of `local_updates` at ~60 Hz while Host A window is completely unfocused.
* **P2 Motion Attested:** Client B sends movement keys (`W`/`S`), and Host A logs `network_native_motion` events showing valid coordinate displacement (`finite(v) && maxP2Distance > 0.0f`).
* **Clean Teardown:** Host A and Client B terminate cleanly on window close without hanging or crashing.

### Hard Stop Conditions (Abort Run Immediately)
* `duplicate_local_update` (`g.localThisTick > 1`): Indicates multiple callers invoking P1 update in a single tick.
* `local_update_outside_attested_tick` (`!g.inTick`): Indicates external thread calling player update.
* `controller_identity_or_guards`: Memory corruption in player storage or broken actor handles.
* `shutdown_hang` / `deadlock`: If process hangs for > 5000 ms during window close (refer to DOC-52 for shutdown invariants).

---

## 7. Open Questions for Sol Max Runtime Verification

Please verify the following behaviors during initial native runs:

1. **Pause Flag Coupling:**  
   *Does setting `*reinterpret_cast<Byte*>(pauseMgr + 0x1b0) = 0` alone allow `StateTick` to reach `Player::Update(primary())`, or does `byte ptr [instance() + 0x20c]` also need to be cleared?*  
   *Hypothesis:* Clearing both in `hookTick` is safe and idempotent.
2. **Audio Subsystem Behavior in Background:**  
   *Does KEX mute master audio when unfocused (`0x140148810`), and does that impact simulation?*  
   *Static Finding:* Audio mute does not block the simulation branch in `StateTick`.
3. **Window Minimization vs Background Unfocused:**  
   *If Host A is explicitly minimized (`ShowWindow(SW_MINIMIZE)`), does the engine tick drop to 0 Hz, or does `cl_engineNoSleep 1` keep `StateTick` executing?*  
   *Note:* Background unfocused (visible behind Client B) is sufficient for all CI and integration testing.

---
*End of Handoff Specification DOC-53-SOL-HANDOFF.*
