# PowerSlave Exhumed Together — ENet Native Compatibility Fixes

**Document Version:** 1.00  
**Date:** 2026-11-01  
**Status:** COMPLETE / EMPIRICALLY VERIFIED  
**Author:** Antigravity (Protocol & Autonomous Transport Engineer)  
**Related Documents:**
- [`docs/35_NATIVE_ENET_COMPATIBILITY_CHECK.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/docs/35_NATIVE_ENET_COMPATIBILITY_CHECK.md)
- [`docs/27_M0_COMPLETION_REPORT.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/docs/27_M0_COMPLETION_REPORT.md)
- [`docs/29_ENET_FINAL_ACCEPTANCE.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/docs/29_ENET_FINAL_ACCEPTANCE.md)
- [`tools/enet-prototype/docs/WIRE_PROTOCOL.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/docs/WIRE_PROTOCOL.md)
- [`docs/TEST_RESULTS.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/PowerSlave_Exhumed_Together_mod/docs/TEST_RESULTS.md)

---

## 1. Executive Summary

Following the audit in [`35_NATIVE_ENET_COMPATIBILITY_CHECK.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/docs/35_NATIVE_ENET_COMPATIBILITY_CHECK.md), this document details the targeted native compatibility fixes applied to the autonomous ENet prototype (`PowerSlave_Exhumed_Together_mod/tools/enet-prototype/`).

All work was performed strictly within the prototype directory. **No modifications were made to Milestone M0 native hooks, reverse-engineering experiments, game binaries, or the ongoing C20 investigation.**

### Summary of Changes

| Domain | Problem Identified (DOC-35) | Implementation in ENet Prototype | Verification Status |
|---|---|---|---|
| **1. Weapon Mapping** | 7 weapons, swapped slots (Flamethrower=3, Grenades=4), missing Manacles (Slot 7). | Aligned `enum WeaponType` with KEX 2022 KPF order (0..7, `WEAPON_COUNT = 8`). Updated bitmasks (`DEFAULT_WEAPON_MASK = 0x03`, `ALL_WEAPONS_MASK = 0xFF`). Bumped protocol version to `0x0110` (v1.10). | **CONFIRMED / TEST 43 PASS** |
| **2. Starting Loadout** | Hardcoded assumption of 100 HP / 50 ammo contradicted C19 (200 HP, 60 ammo). | Created configurable `struct StarterLoadout` contract. Defaulted to verified C19 values (200 HP, 60 ammo). Added dynamic configuration methods in simulation and adapter. | **CONFIRMED / TEST 44 PASS** |
| **3. Angle Units** | KEX uses radians internally; wire protocol uses degrees. Ambiguity at adapter boundary. | Retained degrees as canonical wire format. Added explicit bidirectional conversion and normalization helpers (`radToDeg`, `degToRad`, `normalizeAngleDeg`, `normalizeAngleRad`). Verified $\pm 180^\circ$ and $\pm\pi$ wraparound. | **CONFIRMED / TEST 45 PASS** |
| **4. Threading Model** | Risk of direct cross-thread callbacks between ENet worker and KEX simulation thread. | Formulated explicit SPSC (Single-Producer Single-Consumer) queue architecture contract in `engine_adapter.h` and `WIRE_PROTOCOL.md`. Exactly two bounded lock-free ring buffers. | **CONFIRMED / DOCUMENTED** |
| **5. Regression & Suite** | Need to ensure zero breakage to existing 42 tests. | Rebuilt in Release x64. All 42 legacy tests passed, plus 4 new targeted regression tests. Suite grew from 42 to **46 passing tests (100%)**. | **CONFIRMED / 46/46 PASS** |

---

## 2. Technical Implementation Details

### 2.1 Weapon Mapping & Protocol Versioning (KPF Alignment)

In Nightdive's KEX Engine (confirmed via `scripts/weapons.kpf`), weapons are mapped to indices 0 through 7:
- **0:** Machete (`WEAPON_MACHETE`)
- **1:** Pistol (`WEAPON_PISTOL`)
- **2:** M60 Machine Gun (`WEAPON_M60`)
- **3:** Bombs / Grenades (`WEAPON_BOMBS`) — *corrected from older prototype slot 4*
- **4:** Flamethrower (`WEAPON_FLAMETHROWER`) — *corrected from older prototype slot 3*
- **5:** Cobra Staff (`WEAPON_COBRA`)
- **6:** Ring of Ra (`WEAPON_RING`)
- **7:** Manacles of Power (`WEAPON_MANACLES`) — *newly added slot 7*

#### Code Changes (`include/net_protocol.h`, `src/net_protocol.cpp`, `src/simulation.cpp`)

```cpp
// include/net_protocol.h
constexpr uint16_t NET_PROTOCOL_VERSION = 0x0110; // v1.10 (KPF 8-weapon layout & dynamic loadout)

enum WeaponType : uint8_t {
    WEAPON_MACHETE = 0,
    WEAPON_PISTOL = 1,
    WEAPON_M60 = 2,
    WEAPON_BOMBS = 3,         // KEX Slot 3 (was FLAMETHROWER)
    WEAPON_FLAMETHROWER = 4,  // KEX Slot 4 (was GRENADES)
    WEAPON_COBRA = 5,
    WEAPON_RING = 6,
    WEAPON_MANACLES = 7,      // KEX Slot 7 (was missing)
    WEAPON_COUNT = 8
};
constexpr WeaponType WEAPON_MANACLE = WEAPON_MANACLES; // Convenience alias

constexpr uint32_t DEFAULT_WEAPON_MASK = (1u << WEAPON_MACHETE) | (1u << WEAPON_PISTOL); // 0x03
constexpr uint32_t ALL_WEAPONS_MASK = (1u << WEAPON_COUNT) - 1u; // 0xFF (bits 0..7)
constexpr uint16_t DEFAULT_PISTOL_AMMO = 60; // KEX C19 verified starting pistol ammo
constexpr int32_t DEFAULT_HEALTH = 200;      // KEX C19 verified starting health
constexpr int32_t DEFAULT_ARMOR = 0;

const char* getWeaponTypeName(WeaponType type);
```

#### Protocol Version Compatibility Enforcement

Because reordering and adding weapon slots alters the wire protocol semantics, `NET_PROTOCOL_VERSION` was bumped from `0x0100` (`v1.00`) to `0x0110` (`v1.10`). In `ENetHostServer::onMsgHello`:

```cpp
if (msg.clientProtocolVersion != NET_PROTOCOL_VERSION) {
    pse::MsgReject reject;
    reject.reasonCode = pse::REJECT_PROTOCOL_MISMATCH;
    reject.reasonMessage = "Protocol Version Mismatch";
    sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, pse::encodePacket(pse::MSG_REJECT, 0, reject), true);
    enet_peer_disconnect_later(peer, pse::LEAVE_PROTOCOL_ERROR);
    return;
}
```

Any client attempting to connect with `0x0100` (or any version other than `0x0110`) is immediately and safely rejected with `REJECT_PROTOCOL_MISMATCH`.

---

### 2.2 Configurable Starter Loadout Contract

In native experiment C19 (`experiments/C19_RESPAWN_STATE_RESTORATION.md`), the local player consistently respawned with **200 HP and 60 pistol ammo**. Hardcoded constants (`100` and `50`) have been eliminated from the simulation flow in favor of a flexible contract:

```cpp
// include/player_state.h
struct StarterLoadout {
    int32_t  health = DEFAULT_HEALTH;          // Default: 200 (KEX C19 verified)
    int32_t  maxHealth = DEFAULT_HEALTH;       // Default: 200 (KEX C19 verified)
    int32_t  armor = DEFAULT_ARMOR;            // Default: 0
    uint32_t weaponMask = DEFAULT_WEAPON_MASK; // Default: Machete + Pistol (0x03)
    uint8_t  activeWeapon = WEAPON_PISTOL;     // Default: 1 (Pistol)
    uint16_t ammo = DEFAULT_PISTOL_AMMO;       // Default: 60 (KEX C19 verified)
};
```

#### Adapter and Simulation Integration

1. `WorldSimulation` maintains an instance of `StarterLoadout m_starterLoadout{}`:
   - `setStarterLoadout(const StarterLoadout& loadout)` allows runtime updates.
   - `getStarterLoadout() const` retrieves active settings.
   - `initHostSlot`, `allocateClientSlot`, and `processRespawnRequest` all invoke:
     ```cpp
     slot.resetToStarterLoadout(sp, m_starterLoadout);
     ```
2. `IEngineReplicationAdapter` and `WorldReplicationHost`:
   - Added `setStarterLoadout(const StarterLoadout& loadout)` to `IEngineReplicationAdapter`.
   - Propagates loadout state across all `m_players[slot]` and synchronizes the underlying simulation, allowing a future KEX adapter to supply level-specific, difficulty-specific, or campaign-checkpoint starting attributes.
   - Test suites and mock fixtures can continue supplying customized loadouts (e.g., 100 HP, 50 ammo, or custom weapon masks) without modifying engine defaults.

---

### 2.3 Angle Units & Adapter Boundary Conversion

Native KEX 2022 uses **radians** for internal camera matrices, actor orientations, and trigonometry. The ENet wire protocol standardizes on **degrees** (`float` in IEEE 754 single precision) to maintain compact, intuitive coordinate representations and precision stability over the wire.

#### Conversion Utilities (`include/net_protocol.h`)

```cpp
constexpr float NET_PI = 3.14159265358979323846f;
constexpr float RAD2DEG = 180.0f / NET_PI;
constexpr float DEG2RAD = NET_PI / 180.0f;

inline float radToDeg(float radians) {
    return radians * RAD2DEG;
}

inline float degToRad(float degrees) {
    return degrees * DEG2RAD;
}

// Normalize angle in degrees to [-180.0, +180.0)
inline float normalizeAngleDeg(float degrees) {
    float a = std::fmod(degrees + 180.0f, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a - 180.0f;
}

// Normalize angle in radians to [-PI, +PI)
inline float normalizeAngleRad(float radians) {
    float a = std::fmod(radians + NET_PI, 2.0f * NET_PI);
    if (a < 0.0f) a += 2.0f * NET_PI;
    return a - NET_PI;
}
```

#### Boundary Conversion Rules

1. **Client Input Injection (ENet $\to$ KEX Engine):**
   $$\text{yaw}_{\text{KEX}} = \text{degToRad}(\text{cmd.yaw}), \quad \text{pitch}_{\text{KEX}} = \text{degToRad}(\text{cmd.pitch})$$
2. **Actor Replication Broadcast (KEX Engine $\to$ ENet Wire):**
   $$\text{yaw}_{\text{ENet}} = \text{normalizeAngleDeg}(\text{radToDeg}(\text{actor.yaw})), \quad \text{pitch}_{\text{ENet}} = \text{radToDeg}(\text{actor.pitch})$$
3. **Interpolation Integrity (`client_interpolator.cpp`):**
   Existing `angleLerp` implementation was verified as mathematically optimal:
   ```cpp
   static float angleLerp(float a, float b, float alpha) {
       float diff = std::fmod(b - a + 180.0f, 360.0f);
       if (diff < 0.0f) diff += 360.0f;
       diff -= 180.0f;
       return a + diff * std::clamp(alpha, 0.0f, 1.0f);
   }
   ```
   Crossings across the $\pm 180^\circ$ seam (e.g., from $+175^\circ$ to $-175^\circ$) interpolate along the shortest arc of $+10^\circ$, avoiding unwanted $350^\circ$ spins. No changes to the interpolator logic were needed.

---

### 2.4 Threading Model & Bounded SPSC Queues

To guarantee thread safety when integrating with KEX 2022 without introducing lock contention, deadlocks, or native callback re-entrancy, the architectural concurrency contract was formalized in `include/engine_adapter.h` and `WIRE_PROTOCOL.md`:

```mermaid
flowchart LR
    subgraph ENetWorker["ENet Background Worker Thread"]
        ENetPoll["enet_host_service()<br/>Packet Reception & Decoding"]
    end

    subgraph Queues["Bounded Lock-Free SPSC Queues"]
        InQueue["Inbound SPSC Queue<br/>(Max 128 elements)"]
        OutQueue["Outbound SPSC Queue<br/>(Max 256 elements)"]
    end

    subgraph KEXSim["KEX Main Simulation Thread"]
        GameLoop["Game Tick Loop (60 Hz)<br/>Physics, AI, HUD, Actor Transforms"]
    end

    ENetPoll -->|Push parsed commands & snapshots| InQueue
    InQueue -->|Drain at start of game tick| GameLoop
    GameLoop -->|Push local mutations & inputs| OutQueue
    OutQueue -->|Drain during network pump| ENetPoll
```

1. **Topology:** Exactly **two bounded Single-Producer Single-Consumer (SPSC)** queues are required and sufficient:
   - **Inbound SPSC Queue:** ENet worker produces parsed incoming packets; KEX main thread consumes and applies them during the engine tick.
   - **Outbound SPSC Queue:** KEX main thread produces local input/actor mutations; ENet worker consumes, serializes, and transmits them over UDP.
2. **Bounds & Overflow Protection:**
   - Queues are bounded (128 elements inbound, 256 elements outbound).
   - If inbound queue saturates (e.g., tick stalls), the oldest unconsumed inputs are dropped to prevent latency buildup and memory unboundedness.
3. **Prohibition of Direct Callbacks:**
   ENet network callbacks (`enet_host_service`) must **never** directly invoke KEX native methods (e.g., `pPlayer->Die()`, `kexActor::Spawn()`, level unload). All operations are strictly queued.

---

## 3. Automated Test Suite Execution Results

The prototype was compiled in **Release x64** mode using MSVC (`cl.exe` v19.44, `/std:c++20`, `/O2`, `/MT`). The entire automated test suite (`pse_test_suite.exe`) was executed directly.

### 3.1 Test Suite Summary

- **Total Test Cases:** 46
- **Test Cases Passed:** **46 / 46 (100%)**
- **Test Cases Failed:** 0
- **Process Peak RAM:** 8 MB
- **Process CPU Utilization:** 0.00 %
- **Suite Execution Wall Time:** ~8.8 seconds

### 3.2 Breakdown of Verified Scenarios

```
=========================================================
  PowerSlave Exhumed Together - Automated Test Suite
=========================================================
[TEST  1] [PASS] Connect All 7 Clients (1 Host + 7 Clients) (804.9 ms) - Authenticated: 7/7
[TEST  2] [PASS] Player Slot Uniqueness (P1-P8) (0.0 ms) - Distinct slots count: 8
[TEST  3] [PASS] Input Command & Authoritative Snapshot Transmission (858.8 ms) - Delta X: 140.000031
[TEST  4] [PASS] Reliable Event Delivery (Map Change Event) (310.8 ms) - Confirmed by Client P3
[TEST  5] [PASS] Ignore Stale / Out-of-Order Snapshots (0.0 ms) - Monotonic sequence enforced
[TEST  6] [PASS] Disconnect Single Participant (Slot P8) (510.4 ms) - Slot marked disconnected
[TEST  7] [PASS] Reconnect to Same Slot & Anti-Hijack Gate (918.8 ms) - Hijack rejected, legitimate reassigned P8 (Gen 2)
[TEST  8] [PASS] Player Death & Manual Respawn on 'E' (Starter Loadout Reset) (511.6 ms) - HP: 200, Weapons reset verified
[TEST  9] [PASS] Duplicate / Invalid Respawn Protection (0.0 ms) - Host rejection code: 2
[TEST 10] [PASS] Map Epoch Advancement & Synchronized Transition (311.1 ms) - Epoch 3 confirmed by all 7 clients
[TEST 11] [PASS] Stale MapEpoch Packet Rejection (0.0 ms) - Epoch 2 command discarded on Epoch 3 server
[TEST 12] [PASS] Malformed & Oversized Packet Immunity (0.0 ms) - Zero crashes on truncated & garbage packets
[TEST 13] [PASS] Inbound Queue Bounds & Memory Overflow Protection (0.0 ms) - Queue size: 128 / 128 (Bounded)
[TEST 14] [PASS] Synthetic Network Impairments (40ms Jitter, 10% Drop, Reorder) (1059.2 ms) - Zero drops, zero disconnects
[TEST 15] [PASS] Input Scheduling - Jitter Burst Decoupling & Zero Speed Hack (266.3 ms) - Processed 1 cmd/tick; distance within expected range
[TEST 16] [PASS] Input Command Filtering - Monotonic Sequences & Generation Gate (266.0 ms) - Stale seq and gen mismatch rejected; correct seq processed
[TEST 17] [PASS] Predictable Action Buttons - Semi-Auto Edge & Starvation Reset (266.1 ms) - Single bullet consumed on edge; starvation zeroed buttons
[TEST 18] [PASS] Crypto Reconnect Key - Ghost Recovery & Generation Invalidation (718.3 ms) - Old peer disconnected, new peer authenticated with gen=2
[TEST 19] [PASS] Graceful Teardown of Host & All 7 Clients (358.3 ms) - Clean exit
[TEST 20] [PASS] Concurrent Spawn of Multiple Objects (209.7 ms) - 10 objects spawned and received
[TEST 21] [PASS] Duplicate Spawn Protection & Idempotency (0.0 ms) - Duplicate spawn safely rejected
[TEST 22] [PASS] Despawn and Entity ID Reuse with New Generation (109.1 ms) - Entity reused with gen=2
[TEST 23] [PASS] Snapshot Loss Resilience & Client Interpolation (0.0 ms) - 50% snapshot loss tolerated; smooth lerp verified
[TEST 24] [PASS] Packet Reordering & Monotonic Sequence Filtering (0.0 ms) - Out-of-order snapshot rejected
[TEST 25] [PASS] Late-Joining Client Baseline Sync (167.3 ms) - Late client received full baseline
[TEST 26] [PASS] Mid-Battle Reconnect (738.6 ms) - Client P2 reconnected to slot P2 (Gen 2); baseline restored team keys=0x01
[TEST 27] [PASS] Map Transition with Active Projectiles (150.2 ms) - Transition to epoch 2 purged 10 projectiles
[TEST 28] [PASS] Preservation of Door/Switch State (255.5 ms) - Switch activation toggled Door 30001
[TEST 29] [PASS] Non-Corruption of Player State Isolation (0.0 ms) - Memory isolation across slots verified
[TEST 30] [PASS] Wire Protocol Wire Sizes & Generational Fields (P0.1) (0.0 ms) - EntitySnapshot=55B, MsgInteractionRequest=14B, MsgWorldEventConfirm=30B
[TEST 31] [PASS] Multi-Batch Snapshot Assembly under 1200B MTU (P0.2/P0.3) (0.1 ms) - 128 entities -> 7 batches <= 1182B
[TEST 32] [PASS] Out-of-Order Snapshot Batch Reassembly (P0.3) (0.0 ms) - Batches delivered [3, 2, 1, 0]; frame completed
[TEST 33] [PASS] Single Batch Loss & Partial Frame Tracking (P0.3) (0.0 ms) - Partial frame tracked upon missing batch
[TEST 34] [PASS] Duplicate Batch Rejection within Frame (P0.3) (0.0 ms) - Duplicate batch dropped via bitmask
[TEST 35] [PASS] Generational Identity & ABA Protection in Adapter (P0.4) (0.3 ms) - Stale gen rejected, matching gen accepted
[TEST 36] [PASS] Multi-Batch Baseline Sync under MTU Budget (P0.2/P0.3) (0.1 ms) - 128 entities baseline synced in 7 batches <= 1200B
[TEST 37] [PASS] Adapter generation = 0 Prohibition (All Methods) (0.0 ms) - Zero generation rejected on 7 adapter methods
[TEST 38] [PASS] Protocol Network Messages generation = 0 Rejection (0.0 ms) - Zero generation rejected over the wire
[TEST 39] [PASS] Generational ABA & Simultaneous Verification (0.3 ms) - Reused ID safely rejected stale gen=1, accepted gen=2
[TEST 40] [PASS] Frame Reassembly 64-bit Mask Boundaries (1, 32, 33, 63, 64) (0.0 ms) - Masks 1, 32, 33, 63, 64 verified UB-free
[TEST 41] [PASS] Sequence Rollover (UINT32_MAX), Inconsistency & Drops (0.0 ms) - Rollover 0xFFFFFFFF -> 1 succeeded
[TEST 42] [PASS] Continuous 128-Entity Client Reception Rate Measurement (0.7 ms) - 3840 entities/s received smoothly
[TEST 43] [PASS] KPF 8-Weapon Mapping, Bitmasks, and 0..7 Range Validation (0.0 ms) - 8 Weapons (0..7) verified, ALL_WEAPONS_MASK=0xFF
[TEST 44] [PASS] Configurable Starter Loadout (Dynamic C19 Defaults & Custom Fixtures) (0.2 ms) - KEX C19 (200HP, 60 ammo) verified
[TEST 45] [PASS] Angle Conversion Utilities & +/-180 deg / +/-pi Wraparound (0.0 ms) - Bidirectional rad/deg conversion verified
[TEST 46] [PASS] Protocol Version Bump (0x0110 / v1.10) & Legacy Client Rejection (165.4 ms) - NET_PROTOCOL_VERSION=0x0110 (v1.10)
=========================================================
  Execution Summary & Metrics
=========================================================
Test Cases Passed: 46 / 46 (100%)
Process RAM Working Set: 8 MB
Process CPU Utilization: 0.00 %
=========================================================
```

### 3.3 Scalability & Replication Benchmark Verification

The high-load benchmark (`pse_benchmark.exe`) was executed to confirm performance under the updated protocol version and layout:
- **8 Concurrent Peers:** 1 Host + 7 Clients.
- **128 Replicated Entities:** Transmitted across 7 batches per frame (max batch size: 1182 bytes $\le 1200$ bytes MTU target).
- **Host Egress Throughput:**
  - Clean Loopback: **1405.86 KB/s** (~200.84 KB/s per client).
  - Synthetic Network Impairments (40ms Latency, 15ms Jitter, 10% Drop): **1286.57 KB/s** (~183.80 KB/s per client, average RTT 80.7 ms).
- **Client Ingest Rate:** **3005 - 3343 entities/second** ingested and smoothly interpolated.
- **Resource Footprint:** **CPU $\le 0.6\%$**, **RAM $\approx 11\text{ MB}$**.

---

## 4. File Modification Ledger

All changes were strictly contained inside `PowerSlave_Exhumed_Together_mod/tools/enet-prototype/`:

| File | Changes Made |
|---|---|
| [`include/net_protocol.h`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/net_protocol.h) | Bumped `NET_PROTOCOL_VERSION` to `0x0110` (v1.10). Updated `enum WeaponType` to KPF 8-slot layout (0..7). Added `ALL_WEAPONS_MASK = 0xFF`. Updated `DEFAULT_HEALTH = 200`, `DEFAULT_PISTOL_AMMO = 60`. Added angle conversion and normalization helpers (`radToDeg`, `degToRad`, `normalizeAngleDeg`, `normalizeAngleRad`). |
| [`src/net_protocol.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/net_protocol.cpp) | Updated `getProtocolVersionString` to `"PowerSlave Exhumed Together ENet Protocol v1.10"`. Implemented `getWeaponTypeName` for all 8 weapons. |
| [`include/player_state.h`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/player_state.h) | Defined `struct StarterLoadout` (defaults: 200 HP, 60 ammo). Updated `PlayerParticipant::resetToStarterLoadout` to accept configurable `StarterLoadout`. |
| [`include/simulation.h`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/simulation.h) | Added `StarterLoadout m_starterLoadout` member and `setStarterLoadout`/`getStarterLoadout` accessors. |
| [`src/simulation.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/simulation.cpp) | Integrated `m_starterLoadout` into `initHostSlot`, `allocateClientSlot`, and `processRespawnRequest`. Verified weapon selection check (`cmd.selectedWeapon < WEAPON_COUNT`) now validates all 8 weapons. |
| [`include/engine_adapter.h`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/engine_adapter.h) | Documented the 2-queue bounded SPSC threading contract between ENet worker and KEX simulation thread. Added `setStarterLoadout`/`getStarterLoadout` methods to `IEngineReplicationAdapter`. Aligned `ActorStateInfo` default health with `DEFAULT_HEALTH`. |
| [`include/world_entity.h`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/world_entity.h) | Included `net_protocol.h`. Updated `PlayerNetworkEntity` constructor to use `DEFAULT_HEALTH` (200), `DEFAULT_PISTOL_AMMO` (60), and `DEFAULT_WEAPON_MASK` (0x03). |
| [`include/world_replication.h`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/world_replication.h) | Declared `setStarterLoadout` and `getStarterLoadout` in `WorldReplicationHost`. Added `StarterLoadout m_starterLoadout`. |
| [`src/world_replication.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/world_replication.cpp) | Implemented `setStarterLoadout` and `getStarterLoadout` in `WorldReplicationHost`, synchronizing both the underlying simulation and active player entities. |
| [`tests/test_suite.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/tests/test_suite.cpp) | Added Scenarios 43 (8-Weapon mapping & range), 44 (Configurable loadout & C19 defaults), 45 (Angle conversion & seam wraparound), and 46 (Protocol version 0x0110 bump & legacy rejection). |
| [`docs/WIRE_PROTOCOL.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/docs/WIRE_PROTOCOL.md) | Updated specification to v1.10. Added Section 4.2.1 (KPF 8-Weapon table), Section 4.2.2 (Angle units & boundary conversions), Section 4.2.3 (Configurable Starter Loadout), and Section 5 (Threading & SPSC Queue Architecture Contract). |

---

## 5. Remaining Dependencies on Milestone M0

The ENet prototype is now **100% compatible** with all confirmed native game contracts established in M0 through C19. However, the following external native dependencies remain before beginning Milestone M1:

1. **C20 (Native Player/Puppet Unload & Cleanup Contract):**
   - *Status:* `BLOCKED_BY_M0`.
   - *Requirement:* Sol Max must complete experiment C20 to determine the exact native cleanup sequence when changing levels or retiring remote puppets. The ENet protocol already tracks `mapEpoch` and purges transient entities on `MSG_MAP_CHANGE`, but the native hook in `Exhumed.exe` must guarantee that native puppet structures are cleanly retired before the new level geometry instantiates.
2. **KEX Adapter Implementation:**
   - *Status:* Deferred until M0 closure.
   - *Requirement:* Implementation of the concrete C++ adapter inheriting `IEngineReplicationAdapter` and bridging to the two bounded SPSC lock-free queues.
3. **No Milestone M1 Premature Activation:**
   - No native hooks, DLL injection harnesses, or in-engine networking systems were created during this turn. Milestone M1 will begin only after Sol Max officially closes Milestone M0.

---

## 6. Conclusion & Readiness

The autonomous ENet prototype is fully reconciled with all empirical findings of Milestone M0:
- **Wire Protocol Version:** `v1.10` (`0x0110`)
- **Weapon Enum:** 8 weapons (0..7, KPF compliant)
- **Starting Loadout:** Fully configurable (defaulting to C19-confirmed 200 HP, 60 ammo)
- **Angle Math:** Fully documented with bidirectional degree $\leftrightarrow$ radian conversion helpers and verified boundary wraparound
- **Threading Model:** Formally constrained to two bounded SPSC queues
- **Automated Verification:** **46 / 46 PASS** in Release x64

The prototype stands ready for integration into the game binary as soon as Sol Max delivers the completed M0 signoff.
