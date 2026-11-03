# Technical Handoff: Client Viewpoint & Camera Implementation PoC

> **From**: Gemini 3.8 Flash High (Senior KEX Reverse Engineering Researcher)  
> **To**: GPT-6.1 Sol Max (Senior Multiplayer Engine Architect)  
> **Topic**: Client First-Person Viewpoint & Camera Synchronization Architecture  
> **Scope**: Implementation Plan for Client-Side Authoritative Snapshot Ingestion (Variant C: Non-authoritative Client Player Shell)  
> **Target Executable**: `anubis_Shipping_Steam_x64.exe` (SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)

---

## 1. Executive Briefing & Context

Sol, you are driving DOC-55 to achieve the first wire transmission of `MsgPlayerSnapshot` from Host A to Client B over ENet.

Our independent reverse engineering audit of the KEX Engine render loop, spatial sector pipeline, audio subsystem, and player lifecycle has established the following critical facts:

1. **Variant A (Camera-Only Override) is Refuted**:
   Overriding only the camera matrices in `GameInstance::RenderState` detaches the viewpoint from the physical world. The local puppet remains at the spawn sector, which breaks Build-engine portal culling (`DrawView` sector traversal), locks the 3D audio listener to spawn, leaves the first-person weapon frozen, and creates a "ghost puppet" that can drown or trigger traps at spawn.
2. **Variant B (Prediction & Reconciliation) is High Risk for MVP**:
   Running local movement physics and triggers without full world state replication causes desynchronized door openings, duplicate item pickups, and severe snapping artifacts.
3. **Variant C (Non-authoritative Client Player Shell) is the Approved Architecture**:
   Client B already has a fully initialized `kexSlavePlayer` and `kexSlavePuppet`. By suppressing local physics/trigger execution and feeding the incoming authoritative P2 snapshot directly into this shell on the main game thread, **the entire native KEX first-person camera, eye height calculation, head bob, recoil pitch, 3D audio listener, HUD, and first-person weapon billboard work out-of-the-box without touching render internals**.

---

## 2. Minimal PoC Scope Boundaries

To guarantee deterministic success without scope creep:

- **Topology**: 1 Host (simulating P1 + P2) $\leftrightarrow$ 1 Client (Client B).
- **Map**: Single shared map (`map01.kex` / Ramses 1).
- **Simulation**: No client prediction, no input replay, no passive proxy actor spawning (`SpawnActor(0)` is NOT used), no level transitions.
- **Wire Ingestion**: Direct consumption of `MsgPlayerSnapshot` from the ENet network thread into the Main Game Thread via the lock-free SPSC ThreadBridge.

---

## 3. Step-by-Step Implementation Sequence

```
[ ENet Network Thread ]
       |
       |  MsgPlayerSnapshot received
       v
[ ThreadBridge SPSC Queue ] (Lock-free enqueue)
       |
========================================================================
       |  Main Simulation Thread (StateTick Hook)
       v
[ Step 1: ThreadBridge Dequeue ]
       |
[ Step 2: Safety & State Validation ]
       |-- GameState == 3 && PendingState == 0
       |-- Main Thread ID Check
       |-- Finite / Non-NaN Position & Angles
       |-- Sector Bounds Check (0 <= sectorId < numSectors)
       v
[ Step 3: Local Physics Suppression ]
       |-- Detour/Gate kexSlavePuppet::Tick (suppress GroundMove & EnterSectorSpecial)
       v
[ Step 4: Authoritative Snapshot Ingestion ]
       |-- Puppet->origin = snapshot.origin
       |-- Puppet->yaw    = snapshot.yaw (RADIANS!)
       |-- Puppet->rotation = snapshot.rotation (RADIANS!)
       |-- Puppet->simPrevOrigin = snapshot.origin
       |-- Puppet->interpX/Y/Z = snapshot.origin.x/y/z
       |-- kexSlaveActor::LinkSector(puppet->actor, snapshot.sectorId)
       v
[ Step 5: HUD & Weapon Status Sync ]
       |-- Player->health = snapshot.health
       |-- Player->ammo   = snapshot.ammo
       v
[ Step 6: Native KEX Pipeline Runs Naturally ]
       |-- InterpolantManager::Evaluate runs
       |-- RenderState constructs camera from Puppet->interp + ViewZ
       |-- SoundSystem::UpdateListener reads Puppet->origin
       |-- DrawView renders correct sector portal frustum
       |-- First-person weapon billboard updates
```

---

### Step 1: ThreadBridge Lock-Free Queue Ingestion

- **Network Callback**: When `MsgPlayerSnapshot` arrives on the ENet polling thread, pack it into a compact C++ POD struct:
  ```cpp
  struct AuthoritativeSnapshotPod {
      uint32_t tick;
      float posX, posY, posZ;
      float yaw, pitch, roll; // RADIANS
      int32_t sectorId;
      int32_t health;
      int32_t ammo;
      uint16_t buttons;
  };
  ```
- **Action**: Enqueue into the existing lock-free SPSC queue (`ThreadBridge`). Do NOT touch game memory directly from the network thread!

---

### Step 2: Main Game Thread Dispatch & Pre-Flight Checks

Hook point: At the beginning of `GameInstance::StateTick` (`0x1401B6EC0`) or via an engine hook executed on the main game thread.

Perform mandatory guards before dereferencing pointers:

```cpp
// 1. Thread Affinity Check
if (GetCurrentThreadId() != g_MainThreadId) {
    return; // Hard stop: memory must only be mutated on main thread
}

// 2. Engine State Check
auto* gameInstance = *reinterpret_cast<uint8_t**>(0x140A45920);
if (!gameInstance) return;

int32_t gameState = *reinterpret_cast<int32_t*>(gameInstance + 0x20);
int32_t pendingState = *reinterpret_cast<int32_t*>(gameInstance + 0x24);
if (gameState != 3 || pendingState != 0) {
    return; // Game is loading, paused, or in menu
}

// 3. Pointer Integrity Check
auto* player = gameInstance + 0x40; // Player embedded at +0x40
auto* puppet = *reinterpret_cast<uint8_t**>(player + 0x18);
if (!puppet) return;

auto* actor = *reinterpret_cast<uint8_t**>(puppet + 0x28);
if (!actor) return;
```

---

### Step 3: Suppress Local Physics & Trigger Execution

In Client Shell mode, Client B's local puppet must not run independent physics simulation or trigger checks.

- **Target**: `kexSlavePuppet::Tick` (`0x1401DB8A0`).
- **Hook Method**: MinHook detour on `kexSlavePuppet::Tick`.
- **Logic**:
  ```cpp
  typedef void(__fastcall* PuppetTick_fn)(void* thisPtr);
  PuppetTick_fn fpPuppetTick = nullptr;

  void __fastcall Hooked_PuppetTick(void* thisPtr) {
      if (g_IsClientShellMode) {
          // Suppress GroundMove, physics integration, and EnterSectorSpecial.
          // Native simulation does NOT step local puppet.
          return;
      }
      fpPuppetTick(thisPtr);
  }
  ```

---

### Step 4: Ingest Authoritative Transform & Relink Sector

When a new snapshot is dequeued on the main thread:

```cpp
AuthoritativeSnapshotPod snap;
while (g_SnapshotQueue.pop(snap)) {
    // Keep most recent snapshot
}

// 1. Validate finite coordinates
if (!std::isfinite(snap.posX) || !std::isfinite(snap.posY) || !std::isfinite(snap.posZ)) {
    return; // Discard corrupted packet
}

// 2. Ingest World Position
*reinterpret_cast<float*>(puppet + 0x18) = snap.posX;
*reinterpret_cast<float*>(puppet + 0x1C) = snap.posY;
*reinterpret_cast<float*>(puppet + 0x20) = snap.posZ;

// 3. Ingest Orientation (MUST BE RADIANS!)
// Normalize yaw to [-PI, +PI] if needed
*reinterpret_cast<float*>(puppet + 0x24) = snap.yaw;   // body yaw
*reinterpret_cast<float*>(puppet + 0x30) = snap.pitch; // rotation pitch
*reinterpret_cast<float*>(puppet + 0x34) = snap.roll;  // rotation roll
*reinterpret_cast<float*>(puppet + 0x38) = snap.yaw;   // rotation yaw

// 4. Prime Interpolant Registers (Prevents 1-frame snap stutter)
*reinterpret_cast<float*>(puppet + 0x50) = snap.posX;  // prevOrigin.x
*reinterpret_cast<float*>(puppet + 0x54) = snap.posY;  // prevOrigin.y
*reinterpret_cast<float*>(puppet + 0x58) = snap.posZ;  // prevOrigin.z

*reinterpret_cast<float*>(puppet + 0x6C) = snap.posX;  // interpX
*reinterpret_cast<float*>(puppet + 0x70) = snap.posY;  // interpY
*reinterpret_cast<float*>(puppet + 0x74) = snap.posZ;  // interpZ

// 5. Ingest Sector & Relink Actor
uint64_t maxSectors = *reinterpret_cast<uint64_t*>(0x140A45970);
if (snap.sectorId >= 0 && static_cast<uint64_t>(snap.sectorId) < maxSectors) {
    int32_t currentSector = *reinterpret_cast<int32_t*>(actor + 0x4C);
    if (currentSector != snap.sectorId) {
        typedef void(__fastcall* LinkSector_fn)(void* actorPtr, int32_t sector);
        auto LinkSector = reinterpret_cast<LinkSector_fn>(0x1401D3D70);
        LinkSector(actor, snap.sectorId);
    }
}
```

---

### Step 5: Synchronize HUD & Weapon State

Update HUD and weapon state directly inside `kexSlavePlayer`:

```cpp
// Update Player Health and Ammo
*reinterpret_cast<int32_t*>(player + 0x24) = snap.health;
*reinterpret_cast<int32_t*>(player + 0x28) = snap.ammo;

// If shooting button pressed on Host, trigger local weapon firing animation
if (snap.buttons & BUTTON_ATTACK) {
    auto* weapon = *reinterpret_cast<uint8_t**>(player + 0xE0);
    if (weapon) {
        // Trigger local muzzle flash / animation state
    }
}
```

---

## 4. Strict Pass Criteria (10 Mandatory Gates)

Before reporting success on the Camera PoC, the following 10 gates must pass:

| Gate | Criterion | Verification Method |
|---|---|---|
| **Gate 1** | **Viewpoint Motion** | When Host moves P2 forward (W), Client B's screen moves forward smoothly. |
| **Gate 2** | **Angular Precision** | Turning P2 on Host rotates Client B's viewpoint without 57.3x radian/degree scaling distortion. |
| **Gate 3** | **Frustum & Portal Culling** | Walking through doorways and sector portals renders geometry cleanly without black voids or missing walls. |
| **Gate 4** | **Eye Height & Bob** | Viewpoint rests at eye level ($41.0f$ units above floor) and exhibits walking head bob. |
| **Gate 5** | **Weapon Model** | First-person weapon billboard is rendered at the bottom of the screen with proper sway. |
| **Gate 6** | **3D Audio Alignment** | Sound sources (enemies, ambient torches) pan correctly in Client B's headphones relative to current viewpoint. |
| **Gate 7** | **HUD Integrity** | Health and ammo counters on Client B match Host P2 authoritative values. |
| **Gate 8** | **No Local Divergence** | Client B walking into doors or switches does not open them locally unless Host P2 opens them. |
| **Gate 9** | **Concurrency Safety** | Zero thread contention or race conditions between ENet worker thread and engine main thread. |
| **Gate 10** | **Crash Freedom** | Sustained 5-minute gameplay session without crash, access violation, or heap corruption. |

---

## 5. Hard Stop Conditions & Fail-Safes

If any of the following occur during development or testing, **halt execution immediately**:

1. **`snap.sectorId >= maxSectors`**: Passing an out-of-bounds sector to `LinkSector` causes an immediate assertion abort (`call 0x14007B3C0`).
2. **`!std::isfinite(val)`**: Writing NaN or $\pm\infty$ into puppet origin or angles causes `RenderState` matrix inversion failure and DirectX crash.
3. **Writing from Background Thread**: Writing to `puppet` or calling `LinkSector` from the ENet thread will cause heap corruption and intermittent AV crashes.
4. **Altering `GameInstance::GetPlayer`**: Replacing or detouring `GetPlayer` (`0x1401B5520`) breaks engine inlining and induces pointer misalignment.

---

## 6. Pre-Implementation Checklist for Sol

- [ ] Confirm `anubis_Shipping_Steam_x64.exe` SHA-256 is `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
- [ ] Confirm Host transmits `snap.yaw` and `snap.pitch` in **radians**, normalized to $[-\pi, +\pi]$ and $[-\pi/2, +\pi/2]$.
- [ ] Verify SPSC queue ring buffer size (recommend 64 entries) and ensure thread-safe memory ordering (`std::memory_order_release` / `std::memory_order_acquire`).
- [ ] Ensure `g_IsClientShellMode` flag is set ONLY on Client B, never on Host A.
- [ ] Read `tools/client-viewpoint-readiness/NATIVE_CAMERA_SEAMS.md` for exact register and stack signatures before attaching hooks.
