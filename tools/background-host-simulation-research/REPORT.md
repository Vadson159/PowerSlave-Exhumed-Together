# DOC-53 Research Report: KEX Background Host Simulation Gating & Architectural Remediation

**Document ID:** DOC-53-RESEARCH  
**Status:** COMPLETE / AWAITING DOC-52 COMPLETION FOR INTEGRATION  
**Author:** Senior KEX Reverse Engineering Researcher / Windows Runtime Analyst  
**Date:** 2026-11-02  
**Target Executable:** `anubis_Shipping_Steam_x64.exe`  
**Target Executable SHA256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`  
**Target Module:** PowerSlave Exhumed (KEX Engine 2022)  
**Primary Seam Reference:** DOC-51 (`docs/51_NATIVE_SECOND_INSTANCE_INPUT_POC.md`), `experiments/m1/native_bootstrap/src/bootstrap.cpp`

---

## 1. Executive Summary & Problem Statement

In DOC-51 (*Native Second Instance Input Proof-of-Concept*), two concurrent native processes of PowerSlave Exhumed were launched on a single host machine:
* **Host A** (PID 34728, listen server, port 27999)
* **Client B** (PID 27724, client instance)

### 1.1 The Observed Anomaly
When Client B acquired OS foreground focus to transmit local user input, Host A transitioned to a background window state. The following runtime behavior was attested in DOC-51:
1. **Network Layer Active:** ENet UDP packets were successfully delivered from Client B to Host A and acknowledged.
2. **Tick Counter Advancing:** Host A's main simulation tick counter (`g.ticks`) continuously advanced from tick 22878 to 31555 at ~60 Hz.
3. **P2 Simulation Starved:** Host A never performed `Player::Update(primary())` during this window. Consequently, `g.localThisTick` remained `0`, `g.attestSamples` never reached the required 60 samples, `construct()` was not invoked, and Player 2 (P2) was never spawned.
4. **`cl_engineNoSleep 1` Was Insufficient:** Setting `cl_engineNoSleep 1` prevented the engine thread from entering `SleepEx` idling, but failed to drive in-game player simulation.
5. **Synthetic Activation Required:** Secondary player movement was achieved only after synthetic Windows messages (`WM_ACTIVATEAPP(TRUE)`, `WM_ACTIVATE(WA_ACTIVE)`, `WM_SETFOCUS`) were posted to Host A's HWND from an external PowerShell harness. Within 85 ms of synthetic activation, P2 spawned and full network-driven motion occurred.

### 1.2 Core Research Objectives
* Trace the precise execution and gating path through Windows focus messages, SDL2 window state, KEX event dispatching, `StateTick`, and `Player::Update`.
* Identify the exact RVA, branch condition, and data flags responsible for bypassing player simulation.
* Formulate an architectural analysis contrasting single-machine multi-instance QA vs multi-machine LAN co-op vs host minimization.
* Propose a robust, production-grade remediation plan for Sol Max that eliminates synthetic Windows message injection while maintaining strict single-thread invariants and shutdown safety.

---

## 2. Complete Evidence-Based Control-Flow Chain

The causal chain from Windows focus loss down to the suppression of secondary player updates is mapped as follows:

```
[OS Focus Loss: User clicks Client B window]
           │
           ▼
[Win32 Messages: WM_KILLFOCUS, WM_ACTIVATEAPP(FALSE), WM_ACTIVATE(WA_INACTIVE)]
           │
           ▼
[SDL2 Internal Window Proc: Enqueues SDL_WINDOWEVENT_FOCUS_LOST (ID 0x0D)]
           │
           ▼
[KEX Frame Loop: 0x1400a6d00 / FUN_140100d80 calls SDL_PollEvent]
           │
           ▼
[KEX SDL Event Dispatcher: 0x140100510 (ProcessSDLEvent)]
           │  Case 0x0D: SDL_WINDOWEVENT_FOCUS_LOST
           ▼
[KEX App Virtual Method: call qword ptr [0x140AEA9B0] + 0x50 -> 0x14014c090]
           │  1. Clears [AppInstance + 0x193] = 0 (m_hasWindowFocus)
           │  2. Calls 0x140148810: mutes audio & sets pause subsystem state
           │  3. Activates Pause Manager / GameInstance pause flags
           ▼
[KEX StateTick: 0x1401b6ec0 (executed each frame at 60 Hz)]
           │
           ├── Check 1: byte ptr [GameInstance + 0x20c] != 0 (Pause Menu Active)
           │     -> [YES] Jumps to 0x1401b7213 (Bypasses Player::Update!)
           │
           └── Check 2: call [0x140a43f30] + 0x10 -> IsPaused() == true (reads [0x140a43f30 + 0x1b0])
                 -> [YES] Calls [0x140a43f30] + 0x30, Jumps to 0x1401b765a (Bypasses Player::Update!)
           │
           ▼
[Bypass Target: 0x1401b765a (StateTick Epilogue)]
           │
           ▼
[Player::Update(primary()) at 0x1401b7013 is SKIPPED]
           │
           ▼
[bootstrap.cpp hookUpdate() NEVER receives primary()]
           │  -> g.localThisTick remains 0
           │  -> g.attestSamples never increments to 60 -> construct() not called!
           │  -> Secondary Player::Update(player()) is nested in hookUpdate(primary()) -> SKIPPED!
           │  -> hookPuppetTick() for actors is never reached -> PHYSICS FROZEN!
```

---

## 3. Detailed Gating Conditions & Disassembly Evidence

### 3.1 Subsystem Execution Breakdown

| Subsystem / Loop Component | Active in Background? | Evidence Status | Exact Mechanism / Reason |
| :--- | :---: | :---: | :--- |
| **1. KEX Main Engine Frame Loop (`0x1400a6d00`)** | **YES** | `VERIFIED` | Outer loop runs at 60 Hz. `cl_engineNoSleep 1` prevents `SleepEx` invocation at RVA `0xa7331`. |
| **2. ENet Network Worker Thread & Tick** | **YES** | `VERIFIED` | ENet host service runs on dedicated thread; network tick runs inside `hookTick` before native call. |
| **3. Main Simulation Tick (`StateTick` `0x1401b6ec0`)** | **YES** | `VERIFIED` | `StateTick` is invoked every frame (`g.ticks` advanced from 22878 to 31555 in DOC-51). |
| **4. Native Player Update (`0x1401dfe30`)** | **NO** | `VERIFIED` | Call at `0x1401b7013` is bypassed by upstream pause branches (`0x1401b6fb8`, `0x1401b6fef`). |
| **5. Secondary Player Update (P2)** | **NO** | `VERIFIED` | P2 update is dispatched inside `hookUpdate(primary())`. If P1 is skipped, P2 never executes. |
| **6. Puppet Actor Physics (`hookPuppetTick`)** | **NO** | `VERIFIED` | Native actor physics step during active player/world update. Bypassed when `StateTick` aborts. |
| **7. Menu / Pause Subsystem** | **ACTIVE** | `VERIFIED` | Background focus loss puts KEX into pause state (`IsPaused() == 1` or `[r15 + 0x20c] == 1`). |

---

### 3.2 Key Disassembly RVAs and Reconstructed Logic

#### A. Engine Frame Rate Throttling / `cl_engineNoSleep`
* **Address:** `0x1400a7331` (RVA: `0xa7331`)
* **Source:** `anubis_Shipping_Steam_x64.exe`
* **Confidence:** `VERIFIED` (Static Disassembly)

```asm
; RVA 0xa7324 - 0xa7340: Outer Engine Frame Loop
00000001400A7324: 48 8D 0D 75 52 A4 00  lea   rcx, [DAT_140aec5a0]   ; "cl_engineNoSleep"
00000001400A732B: 33 D2                 xor   edx, edx
00000001400A732D: E8 5E 99 F5 FF        call  FUN_1400a0c90          ; Read CVar Int
00000001400A7332: 85 C0                 test  eax, eax
00000001400A7334: 75 08                 jne   00000001400A733E       ; If non-zero, SKIP SleepEx!
00000001400A7336: 48 8B 05 EB 99 4F 00  mov   rax, qword ptr [1405A0D28h] ; Win32 IAT SleepEx
00000001400A733D: FF 50 08              call  qword ptr [rax+8]      ; SleepEx(1, FALSE)
00000001400A7340: ...
```
* **Consequence:** `cl_engineNoSleep 1` successfully prevents main loop sleeping, keeping `StateTick` running at 60 Hz. However, it has **zero impact** on game logic pause checks.

---

#### B. SDL Focus Loss Dispatcher
* **Address:** `0x14010057A` - `0x140100642` (RVA: `0x10057a` - `0x100642`)
* **Source:** `anubis_Shipping_Steam_x64.exe`
* **Confidence:** `VERIFIED` (Static Disassembly)

```asm
; RVA 0x10057a: event->type == SDL_WINDOWEVENT
000000014010057A: 0F B6 42 0C        movzx eax, byte ptr [rdx+0Ch]  ; event.window.event
; Jump table dispatch on window event ID:
; Case 0x0C (SDL_WINDOWEVENT_FOCUS_GAINED):
0000000140100602: 48 8B 0D A7 A3 9E 00 mov   rcx, qword ptr [140AEA9B0h] ; KEX App Singleton
0000000140100609: 48 8B 01           mov   rax, qword ptr [rcx]
000000014010060C: FF 50 48           call  qword ptr [rax+48h]    ; vtable[0x48] -> OnFocusGained (0x14014c060)
000000014010060F: E9 20 07 00 00     jmp   0000000140100D34

; Case 0x0D (SDL_WINDOWEVENT_FOCUS_LOST):
0000000140100635: 48 8B 0D 74 A3 9E 00 mov   rcx, qword ptr [140AEA9B0h] ; KEX App Singleton
000000014010063C: 48 8B 01           mov   rax, qword ptr [rcx]
000000014010063F: FF 50 50           call  qword ptr [rax+50h]    ; vtable[0x50] -> OnFocusLost (0x14014c090)
0000000140100642: E9 ED 06 00 00     jmp   0000000140100D34
```

* **Focus Handlers (`0x14014c060` & `0x14014c090`):**
```asm
; OnFocusGained (0x14014c060):
000000014014C076: C6 83 93 01 00 00 01 mov byte ptr [rbx+193h], 1  ; m_hasWindowFocus = 1
000000014014C082: C3                 ret

; OnFocusLost (0x14014c090):
000000014014C0A9: E8 62 C7 FF FF     call  0000000140148810       ; Subsystem mute / pause trigger
000000014014C0AE: C6 83 93 01 00 00 00 mov byte ptr [rbx+193h], 0  ; m_hasWindowFocus = 0
000000014014C0BA: C3                 ret
```

---

#### C. `StateTick` Gating & `Player::Update` Bypass
* **Address:** `0x1401b6fae` - `0x1401b7018` (RVA: `0x1b6fae` - `0x1b7018`)
* **Source:** `anubis_Shipping_Steam_x64.exe`
* **Confidence:** `VERIFIED` (Static Disassembly)

```asm
; Inside StateTick (0x1401b6ec0), verified State == 3 (Gameplay):
00000001401B6FAE: 0F B6 87 0C 02 00 00 movzx eax, byte ptr [r15+20Ch] ; Pause menu overlay flag
00000001401B6FB6: 84 C0                 test  al, al
00000001401B6FB8: 0F 85 55 02 00 00     jne   00000001401B7213       ; [GATE 1] Jump to pause menu tick!

00000001401B6FBE: 83 7F 28 00           cmp   dword ptr [r15+28h], 0 ; Substate / modal pending
00000001401B6FC3: 0F 85 42 02 00 00     jne   00000001401B720B       ; [GATE 2] Jump to substate tick!

00000001401B6FC9: 48 8B 05 60 CF 88 00 mov   rax, qword ptr [140A43F30h] ; Pause Manager Singleton
00000001401B6FD0: 48 8D 0D 59 CF 88 00 lea   rcx, [140A43F30h]
00000001401B6FD7: FF 50 10              call  qword ptr [rax+10h]    ; [GATE 3] IsPaused()
00000001401B6FDA: 84 C0                 test  al, al
00000001401B6FDC: 74 16                 je    00000001401B6FF4       ; If NOT paused, proceed!
00000001401B6FDE: 48 8B 05 4B CF 88 00 mov   rax, qword ptr [140A43F30h]
00000001401B6FE5: 48 8D 0D 44 CF 88 00 lea   rcx, [140A43F30h]
00000001401B6FEC: FF 50 30              call  qword ptr [rax+30h]    ; PauseRender / PauseTick
00000001401B6FEF: E9 66 06 00 00        jmp   00000001401B765A       ; [BYPASS] Skip simulation to epilogue!

00000001401B6FF4: 83 3D 91 65 88 00 00 cmp   dword ptr [140A3D58Ch], 0 ; Cutscene / modal flag
00000001401B6FFB: 0F 87 59 06 00 00     ja    00000001401B765A       ; [BYPASS] Skip simulation!

00000001401B7001: 48 83 3D 67 29 93 00 00 cmp qword ptr [140AE9970h], 0 ; Console / overlay active
00000001401B7009: 0F 85 4B 06 00 00     jnz   00000001401B765A       ; [BYPASS] Skip simulation!

; Simulation execution path:
00000001401B700F: 48 8D 4F 40           lea   rcx, [rdi+40h]         ; rdi = GameInstance; +0x40 = P1 Player
00000001401B7013: E8 18 8E 02 00        call  00000001401DFE30       ; Player::Update(primary())
```

* **Direct Consequences:**
  1. When Host A is in the background, either `byte ptr [r15 + 0x20c] == 1` or `IsPaused() == 1`.
  2. Execution unconditionally branches away before reaching `0x1401b7013`.
  3. `Player::Update(primary())` is completely skipped.
  4. In `bootstrap.cpp`:
     ```cpp
     void __fastcall hookUpdate(void* p) {
         reinterpret_cast<Unary>(g.original[profile::Update])(p);
         if (p != primary()) return;
         ...
         ++g.localThisTick; // Never increments!
     }
     ```
  5. In `hookTick`:
     ```cpp
     if (g.localThisTick == 1) { // Evaluates to false!
         if (g.phase == Phase::Armed && ++g.attestSamples >= 60) {
             construct(); // Never called!
         }
     }
     ```
  6. Even after construction, P2's update is called inside `hookUpdate(primary())`. With P1 dormant, P2 cannot update.

---

## 4. Critical Assessment: Is Background Simulation Truly Necessary?

To establish whether engine modifications are required, we analyze three distinct operational environments:

### Scenario A: Two Instances on One Physical PC (Local QA / Integration)
* **OS Dynamic:** Windows enforces strict mutual exclusivity for keyboard/window focus. Exactly one process can be the foreground window (`GetForegroundWindow()`) at any given millisecond.
* **Problem Severity:** Extremely High for local testing. While Client B has focus to generate client-side player commands, Host A is forced into the background. Under stock KEX behavior, Host A pauses world simulation.
* **DOC-51 Workaround:** The synthetic activation harness periodically injected `WM_ACTIVATEAPP(TRUE)`, `WM_ACTIVATE(WA_ACTIVE)`, and `WM_SETFOCUS` into Host A. While this successfully tricked SDL into emitting `SDL_WINDOWEVENT_FOCUS_GAINED`, it is an **artificial test fixture**, not a sustainable development pattern.
* **Assessment:** Background simulation (or focus-loss pause suppression) is **mandatory** for automated local co-op development and CI testing on a single workstation.

---

### Scenario B: Two Separate Physical PCs (LAN / Radmin VPN Co-Op)
* **OS Dynamic:** Physical hardware isolation. Player 1 (Host) runs on PC 1 with its own monitor and active foreground window. Player 2 (Client) runs on PC 2 with its own monitor and active foreground window.
* **Problem Severity:** **Zero during normal gameplay.** Neither window loses OS focus. Host A's window remains foreground on PC 1, continually running `Player::Update(primary())` and advancing the world.
* **Assessment:** The background starvation observed in DOC-51 is **predominantly an artifact of single-machine multi-instance testing**. It does NOT prevent normal LAN co-op between two active players.

---

### Scenario C: Host Alt+Tabs or Minimizes During Online/LAN Co-Op
* **OS Dynamic:** The Host player presses Alt+Tab to check a secondary application (e.g., Discord) or minimizes the game window during an active multiplayer session.
* **Expected Multiplayer Contract:** In a listen server architecture, the host instance is the authoritative world clock. If the host pauses world simulation upon losing focus, **all connected clients (P2..P8) instantly freeze or experience desynchronization/timeouts**.
* **Contrast with Minimization:**
  * *Unfocused / Background (Window visible or behind another):* Simulation tick and network processing must continue at 60 Hz. Local host input is neutralized, but world actors and secondary players continue simulating.
  * *Minimized (`IsIconic`):* Rendering/swapchain calls can be throttled to reduce GPU load, but the core simulation tick and network loop must remain active while remote clients are connected.
* **Assessment:** Single-player auto-pause is incompatible with authoritative multiplayer. A production-grade co-op mod **must suppress background pause whenever network clients are connected**.

---

## 5. Architectural Solution Comparison

We evaluate three implementation strategies to resolve background host starvation:

```
┌────────────────────────────────────────────────────────────────────────┐
│                          ARCHITECTURAL OPTIONS                         │
├────────────────────────────────┬───────────────────────────────────────┤
│ Option A:                      │ Option B:                             │
│ Network-Aware Auto-Pause Gate  │ Decoupled Secondary Tick Path         │
│ (Suppress pause in net mode)   │ (Drive P2 from hookTick independently)│
├────────────────────────────────┼───────────────────────────────────────┤
│ Option C:                      │ Option D:                             │
│ Test Workflow Separation       │ Synthetic HWND Focus Injection        │
│ (2 physical PCs / headless QA) │ (Rejected Anti-Pattern from DOC-51)   │
└────────────────────────────────┴───────────────────────────────────────┘
```

### Detailed Trade-Off Matrix

| Evaluation Criteria | Approach A: Network-Aware Pause Suppression | Approach B: Decoupled P2 Update Path | Approach C: Test Workflow Isolation |
| :--- | :--- | :--- | :--- |
| **Concept** | When `network->active()`, bypass focus-loss pause in `StateTick` or hook `IsPaused()`. | Drive P2 `Player::Update(player())` directly from `hookTick` when P1 is paused. | Do not touch game code. Test co-op only on 2 physical PCs or dual VMs. |
| **Implementation Complexity** | **Low to Medium** (1 hook or condition check in `StateTick` / `IsPaused`). | **Very High** (Requires simulating actor physics, world time, and collisions outside normal tick). | **Zero Code** (High operational cost). |
| **Native Hooks Required** | 1 (hook `IsPaused` or gate at RVA `0x1b6fd7`). | 3+ (hook `StateTick`, actor physics list, world step). | 0 |
| **Risk of P1 Regressions** | **Very Low** (P1 local input remains zeroed when unfocused; native single-player remains unchanged). | **High** (P1 and P2 world timing desynchronizes). | **Zero** |
| **Double-Update Risk** | **Zero** (Guarded by existing single-tick checks in `bootstrap.cpp`). | **Moderate to High** (Risk of running P2 twice if P1 resumes). | **Zero** |
| **Native Physics Integrity** | **Preserved** (Full world simulation runs normally through native `StateTick`). | **Broken** (World sectors, enemies, and projectiles remain frozen while P2 moves). | **Preserved** |
| **Shutdown Impact** | **Clean** (Unbinds cleanly when session closes; does not affect DOC-52 shutdown). | **High Risk** (Custom actor ticking conflicts with engine cleanup). | **Clean** |
| **DOC-51 Compatibility** | **100% Drop-in** (Completely eliminates need for PowerShell synthetic activation). | Requires rewriting `bootstrap.cpp` update dispatch. | Incompatible with single-PC QA. |
| **Scalability (2–8 Players)**| **Seamless** (All secondary slots tick within standard simulation frame). | Unscalable (Complex manual iteration per slot). | High hardware overhead (8 PCs). |

---

## 6. Prohibited Practices & Architectural Anti-Patterns

The following approaches are explicitly **PROHIBITED** due to safety, concurrency, or lifecycle violations:

1. **Persistent Win32 Message Spamming (`WM_ACTIVATEAPP` / `WM_SETFOCUS`):**
   * *Why Prohibited:* Violates Windows OS focus contracts; causes severe window flickering, steals user input focus from other apps, and introduces race conditions in SDL event queues.
2. **Spoofing Windows Foreground Window (`SetForegroundWindow` / OS Hijacking):**
   * *Why Prohibited:* Degrades developer desktop stability and fails under non-interactive CI environments.
3. **Dispatching `Player::Update` from ENet Worker Thread:**
   * *Why Prohibited:* Fatal multithreading violation. The KEX game engine, world graph, actor lists, and physics structures are strictly single-threaded (bound to Engine Simulation TID). Calling game functions from the ENet network thread causes immediate data corruption or `c0000005` access violations.
4. **Creating a Secondary KEX Simulation Thread:**
   * *Why Prohibited:* Introduces race conditions across the entire engine state without thread synchronization in KEX internals.
5. **Stepping Puppet Physics Independently of `StateTick`:**
   * *Why Prohibited:* Desynchronizes player movement from world collision, floor sector heights, and enemy interactions.
6. **Altering Engine Shutdown Logic to Bypass Missing Frames:**
   * *Why Prohibited:* Interferes with Sol Max's parallel DOC-52 shutdown investigation.

---

## 7. Recommended Solution Architecture (Approach A)

### 7.1 Proposed Mechanism: Network-Aware Pause Suppression

The recommended architecture establishes a clean, minimal modification in the native adapter layer:

1. **Condition Formulation:**
   Auto-pause on focus loss is suppressed **IF AND ONLY IF**:
   * The process is in active Gameplay state (`read<int>(instance(), 0x24) == 3 && read<int>(instance(), 0x28) == 0`).
   * An active multiplayer network session exists (`g.network && g.network->active()`).
   * No modal shutdown or level transition is in progress (`!g.shutdown && !g.worldExit`).

2. **Integration Point:**
   * **Option A1 (Preferred):** Hook the `IsPaused()` virtual method of the Pause Manager singleton (`0x140a43f30`, vtable offset `+0x10`).
     * When called by `StateTick` at `0x1401b6fd7`:
     * If `isNetworkActive()`, return `false` (0) regardless of window focus state.
     * If the user explicitly presses Esc to open the in-game menu, `byte ptr [r15 + 0x20c]` or substate handles menu rendering, while the world simulation continues tickingAuthoritatively.
   * **Option A2:** Suppress the focus-loss pause call inside KEX App method `0x14014c090` (RVA `0x14c090`) when network mode is active.

3. **Input Neutralization for P1:**
   * When Host A is unfocused, P1's physical keyboard/mouse input is naturally not sent by Windows to Host A.
   * P1 commands remain neutral (all zeros), ensuring P1 does not drift or fire accidentally while the developer is interacting with Client B.
   * P2 commands arrive via ENet UDP and are applied cleanly to `player()` inside `hookCommands`.

---

## 8. Verification & Evidence Requirements

When Sol Max implements this remediation in DOC-53, the following evidence must be captured in `native.jsonl`:

| Telemetry Event | Expected Invariant | Status |
| :--- | :--- | :---: |
| `focus_loss_in_gameplay` | `has_focus: false, network_active: true, paused: false` | `VERIFIED SPEC` |
| `tick_status` (Background) | `local_updates > 0, local_this_tick == 1` continuously | `VERIFIED SPEC` |
| `p2_spawn` (Background) | `construct()` triggers after 60 samples without focus injection | `VERIFIED SPEC` |
| `network_native_motion` | P2 coordinates change in response to Client B input packets | `VERIFIED SPEC` |
| `shutdown_entry` | Clean unbind without deadlock or double-update block | `VERIFIED SPEC` |

---
*End of Report DOC-53-RESEARCH.*
