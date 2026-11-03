# Architecture Comparison: Client Viewpoint & Camera Synchronization

> **Target**: PowerSlave Exhumed (2022) — Second Instance Client Experience  
> **Target Executable**: `anubis_Shipping_Steam_x64.exe` (SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)  
> **Evaluation Date**: November 2026  
> **Auditor**: Gemini 3.8 Flash High (Senior KEX Reverse Engineering Researcher)

---

## 1. Executive Summary & Verdict

| Architecture Variant | Feasibility Verdict | Risk Level | Implementation Complexity | Primary Fatal Flaw / Obstacle |
|---|---|---|---|---|
| **Variant A: Camera-Only Authoritative Override** | **REFUTED** | `FATAL` | High | Severe world detachment: Camera at target sector, Puppet & Audio Listener at spawn sector. PVS portal culling fails, audio detached, ghost puppet triggers lava/traps at spawn. |
| **Variant B: Local Player Visual Reconciliation** | **HIGH RISK / DEFERRED** | `VERY HIGH` | Very High | Prediction/snapshot fighting: Local `Puppet::Tick` runs physics and triggers independently without replicated world state. Causes duplicate pickups, desynced door triggers, and snapping artifacts. |
| **Variant C: Non-authoritative Client Player Shell** | **APPROVED (WINNING)** | `LOW-MEDIUM` | Minimal | None. Client B puppet acts as an authoritative proxy shell. Local physics/triggers suppressed, authoritative snapshot updates position and sector relinking. Native camera, weapon, audio, and HUD work transparently. |

---

## 2. Deep Comparative Breakdown Across Engine Subsystems

### 2.1 Native Seams & Entry Points

| Dimension | Variant A (Camera Override) | Variant B (Reconciliation) | Variant C (Client Shell) |
|---|---|---|---|
| **Primary Hook Point** | Detour `GameInstance::RenderState` (`0x1401B7700`) or rewrite stack frame `[rbp - 0x60]` | Detour `kexSlavePlayer::Update` (`0x1401DFE30`) + post-tick correction | Detour `kexSlavePuppet::Tick` (`0x1401DB8A0`) to suppress local physics; snapshot ingestion applies coordinates. |
| **Stack Frame Intrusiveness** | Extremely High: Modifying RBP-relative parameters in a 0x320-byte stack frame. | Medium: Modifies player state before/after native tick. | Low: Early exit in puppet tick or selective bypass of physics/triggers. |
| **Leaf Function Interception** | Requires redirecting or spoofing `GetPlayer` (`0x1401B5520`). | Uses normal `GetPlayer`. | Uses normal `GetPlayer`. |

### 2.2 Spatial Partitioning & Portal Frustum Culling (Sector Engine)

In the KEX Engine (derived from the Build / PowerSlave sector portal model), 3D visibility traversal begins at `kexRenderView::sectorIndex`.

| Subsystem | Variant A (Camera Override) | Variant B (Reconciliation) | Variant C (Client Shell) |
|---|---|---|---|
| **Camera Sector** | Must be recalculated manually via `FindSector` (`0x1401C98F0`) every render frame. | Computed by puppet simulation. | Updated from Authoritative Snapshot + `LinkSector` (`0x1401D3D70`). |
| **Puppet Sector** | Stays at map spawn sector ($0$ or initial spawn point). | Follows local prediction (may drift from Host). | Authoritatively matched to Host P2 sector. |
| **Spatial Mismatch Consequence** | **FATAL**: `kexRenderScene::DrawView` renders world from camera sector, but dynamic actors and lighting look up `Puppet->actor->sectorIndex`. Screen flashes, missing wall portals, black void rendering. | Minor visual mispredictions during rapid sector transitions. | Clean, continuous portal rendering. |
| **Sector Boundary Validation** | None (unless manually implemented). | Native `GroundMove` tests portal walls. | Validated against `[0x140A45970]` (max sectors) via `LinkSector`. |

### 2.3 Audio Listener Synchronization

In `GameInstance::StateTick` (`0x1401B6EC0`), the 3D audio listener is updated at lines `0x1401B7170` - `0x1401B71FA`:
```x86asm
mov rax, qword ptr [player + 18h] ; rax = puppet
lea rdx, [rax + 18h]              ; puppet->origin
lea rcx, [rax + 30h]              ; puppet->rotation
call SetListenerTransform
```

| Audio Behavior | Variant A (Camera Override) | Variant B (Reconciliation) | Variant C (Client Shell) |
|---|---|---|---|
| **Listener Position** | Stuck at spawn sector! Explosions, monster cries, and ambient sounds play relative to spawn point, NOT camera! | Follows predicted player position. | Follows authoritative P2 position. Sound matches viewpoint perfectly. |
| **Listener Orientation** | Fixed to local puppet yaw. Camera rotation is detached from hearing direction. | Matches predicted yaw. | Matches authoritative yaw. Stereo panning is 100% accurate. |
| **Additional Audio Hooks Required** | Requires separate hook on `SetListenerTransform` (`0x1400F2430`) or state tick detour. | None. | None. |

### 2.4 First-Person Weapon & HUD Integrity

Inside `kexRenderScene::DrawView` (`0x1401EFF40`), weapon drawing is executed at `0x1401F078B`:
```x86asm
call GameInstance::GetPlayer
mov  rcx, qword ptr [rax + 0E0h] ; player->weapon
call Weapon::Draw
```

| Aspect | Variant A (Camera Override) | Variant B (Reconciliation) | Variant C (Client Shell) |
|---|---|---|---|
| **Weapon Billboard Orientation** | Oriented towards camera, but bobbing is driven by local player velocity ($0$ if standing at spawn). Weapon looks static and frozen. | Bobbing and recoil run locally; risk of duplicate recoil kicks on snapshot snaps. | Authoritative recoil applied from snapshot; bobbing synchronized smoothly. |
| **Muzzle Flash & Firing Sync** | Local weapon state is detached from Host firing actions. Client cannot see authoritative shots. | Local prediction fires weapon before Host confirmation. | Host snapshot conveys weapon state / firing trigger. Local weapon draws accurately. |
| **HUD Health & Ammo Status** | Reads local player (`Player + 0x24, +0x28`). Health shows full $100$ even if Host P2 is damaged or dead. | Desynchronizes from Host health on taking damage. | Synchronized directly from Authoritative Snapshot fields. |

### 2.5 Local World Divergence & Trigger Hazards

PowerSlave Exhumed maps contain numerous lethal sector specials: lava, crushing ceilings, acid, dart traps, and progression switches.

| Gameplay Element | Variant A (Camera Override) | Variant B (Reconciliation) | Variant C (Client Shell) |
|---|---|---|---|
| **Ghost Puppet at Spawn** | **EXTREME HAZARD**: The native puppet sits at spawn. If spawn has rising water or nearby enemies, local puppet takes damage and dies while camera is miles away! | Puppet moves locally. If local client hits a trigger first, door opens locally while closed on Host. | **ELIMINATED**: Local puppet physics and `EnterSectorSpecial` are suppressed. Local world cannot diverge. |
| **Authoritative Death** | Host P2 dies; Client camera remains floating unless hooked. Local puppet remains alive. | Local puppet fights ragdoll / death state. | Snapshot transmits `health <= 0`; Client Shell transitions cleanly to death camera. |
| **Map Transitions (WorldExit)** | Ghost puppet fails to enter level exit trigger. Camera teleports, but world never changes. | Double trigger execution on exit portal. | Host initiates level change; Client obeys map sync protocol. |

---

## 3. Detailed Architectural Profiles

```
========================================================================================
VARIANT A: CAMERA-ONLY OVERRIDE (REFUTED)
========================================================================================
[ Host A Simulation ]                    [ Client B Engine ]
  P2 Transform: (X, Y, Z, Yaw)             Local Puppet: Origin at Spawn (0, 0, 0)
       |                                          |
   (Network)                                      v
       |                                   [Audio Listener: Spawn]  <-- WRONG!
       v                                          |
  [RenderState Hook]                              v
  Overwrites Camera [rbp - 0x60]           [Ghost Puppet: Hits Traps] <-- DANGEROUS!
       |
       v
  [DrawView Camera: P2 Target]
       |
       +--> Portal Frustum Culling fails (Sector mismatch with Actor linked list)
       +--> Weapon billboard frozen (No velocity on local puppet)
       +--> HUD displays spawn health (Not authoritative P2)

========================================================================================
VARIANT B: LOCAL PLAYER VISUAL RECONCILIATION (DEFERRED)
========================================================================================
[ Host A Simulation ]                    [ Client B Engine ]
  Authoritative P2 Physics                 Local Client Input (W, A, S, D)
       |                                          |
       |                                          v
       |                                   [Local Puppet::Tick]
       |                                     - Moves physics
       |                                     - Opens local doors  <-- DESYNC!
       |                                     - Takes local damage <-- DESYNC!
       |                                          |
   (Network Snapshot)                             |
       |                                          v
       +----------------------------------> [Reconciliation Step]
                                              - Prediction error detected
                                              - Snaps position (causes visual stutter)
                                              - Triggers already activated cannot un-fire!

========================================================================================
VARIANT C: NON-AUTHORITATIVE CLIENT PLAYER SHELL (RECOMMENDED)
========================================================================================
[ Host A Simulation ]                    [ Client B Engine ]
  Authoritative P2 Physics                 Local Client Input (W, A, S, D)
       |                                          |
       | (Snapshot: pos, rot, sector, hud)        v
   (Network Snapshot)                      [SendInput to Host] (Already verified DOC-51)
       |                                          |
       v                                          v
  [Client Ingestion]                       [Puppet::Tick Physics SUPPRESSED]
       |                                     - No local trigger activation
       +--> Update Puppet->origin            - No local damage calculation
       +--> Update Puppet->rotation               |
       +--> Update Interpolant registers          v
       +--> LinkSector(actor, sectorId)    [Native Camera Pipeline Runs Unmodified]
       +--> Update HUD health & ammo         - ViewOrigin = Puppet->interp + ViewZ
                                             - DrawView renders correct sector
                                             - Audio Listener matches P2 position
                                             - First-person weapon bob & recoil active
```

---

## 4. Why Variant C is the Only Sound Choice for PowerSlave Exhumed

1. **Zero Disassembly Hacks in Render Loop**:
   - `GameInstance::RenderState` and `kexRenderScene::DrawView` remain completely untouched.
   - Stack-allocated structures and compiler calling conventions are preserved.
2. **Atomic Spatial Consistency**:
   - Calling `LinkSector(puppet->actor, snapshot->sectorId)` (`0x1401D3D70`) cleanly updates the Build-style actor linked list inside the spatial sector array.
   - Portal traversal, dynamic actor lighting, and sprite sorting function exactly as designed by Nightdive.
3. **Automatic 3D Audio**:
   - Because `puppet->origin` and `puppet->rotation` hold the true P2 world transform, the engine's built-in `SetListenerTransform` call in `StateTick` automatically positions the audio listener at the exact camera position.
4. **Preservation of First-Person Visual Polish**:
   - Eye height (`41.0f`), crouch transitions (`17.0f`), camera bob, and weapon sway work natively without reimplementing complex trigonometry in mod code.
5. **No Local Desynchronization**:
   - By suppressing local movement physics in `kexSlavePuppet::Tick`, Client B cannot accidentally step on floor switches, push walls, collect items, or trigger monster wake-up logic ahead of the Host. The Host remains 100% authoritative.

---

## 5. Verification Checklist Before Implementation

Before proceeding to implement Variant C in code, GPT-6.1 Sol Max must ensure:

- [ ] **Thread Affinity**: Position updates and `LinkSector` calls MUST happen on the Main Simulation Thread (inside or adjacent to `StateTick`), never from the background network thread.
- [ ] **State Guard**: Verify `GameInstance->gameState == 3 && pendingState == 0` before touching player memory.
- [ ] **Finite Value Guard**: Validate that incoming $(X, Y, Z)$ and angles are valid IEEE non-NaN floats within reasonable world boundaries.
- [ ] **Sector Bounds Check**: Ensure `0 <= sectorId < numSectors` (`[0x140A45970]`) before invoking `LinkSector`.
- [ ] **Interpolant Priming**: Whenever snapping position, update both current position (`Puppet + 0x18`), previous origin (`Puppet + 0x50`), and evaluated registers (`Puppet + 0x6C, 0x70, 0x74`) to prevent one-frame backward interpolation glitches.
