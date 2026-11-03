# PowerSlave Exhumed Together — Sol Max Integration Interface Specification

**Document ID:** `PSE-ENET-INTEGRATION-01`  
**Target:** Sol Max (Native Engine Reverse-Engineering & Hooking Lead)  
**Binary:** `anubis_Shipping_Steam_x64.exe` (SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)  
**Scope:** Architectural contract for connecting the standalone ENet transport layer to the native KEX Engine simulation tick after completion of Milestone M0.

---

## 1. Threading Boundary & Re-entrancy Rules

> [!CRITICAL]
> **Zero Direct Native Calls from Network Threads:**
> The ENet network polling thread MUST NEVER call native KEX functions directly.
> Runtime experiments in M0 demonstrated that `kexGameInstance`, global scratch buffers, sector traversal caches, and AngelScript execution contexts are strictly single-threaded and not re-entrant.

```
       Network Worker Thread                     Native KEX Engine Thread
┌─────────────────────────────────┐        ┌──────────────────────────────────┐
│   ENet UDP Socket Pump (1-2ms)  │        │   kexGameInstance::Tick() (60Hz) │
│                                 │        │                                  │
│ 1. enet_host_service()          │        │ 1. Drain Inbound Command Queues  │
│ 2. Validate Packet & Header     │───────►│    - Apply P1..P8 inputs         │
│ 3. Enqueue to Bounded FIFO      │ (Queue)│ 2. Run Native Physics & AI Step  │
│                                 │        │ 3. Run Weapon / Collision Step   │
│ 4. Drain Delayed Outbound Queue │◄───────│ 4. Harvest Authoritative State   │
│ 5. enet_peer_send()             │(Snap)  │ 5. Emit MsgPlayerSnapshot        │
└─────────────────────────────────┘        └──────────────────────────────────┘
```

---

## 2. Shared Data Interface

### 2.1 Bounded Inbound Command FIFO
For each participant slot $P_1 \dots P_8$:

```cpp
struct NativePlayerCommandQueue {
    std::mutex queueMutex;
    uint32_t lastProcessedSequence = 0;
    uint32_t activeConnectionGeneration = 1;
    std::deque<pse::MsgInputCommand> queue; // Max 128 elements
};

extern NativePlayerCommandQueue g_ClientCommandQueues[8];
```

**Thread Safety Contract:**
- **Network Thread:** Pushes parsed and validated `MsgInputCommand` into `g_ClientCommandQueues[slotId].queue`. If size $\ge 128$, drops oldest command to prevent memory bloat.
- **Engine Tick Thread:** In `HOOK_PLAYER_INPUT` (pre-physics), locks `queueMutex`, pops all pending commands, applies the newest command sequence to `kexPlayer::cmd`, and advances orientation and buttons.

### 2.2 Authoritative Snapshot Harvest
At the conclusion of the native frame tick (post-physics, pre-render):

```cpp
void Native_HarvestAuthoritativeSnapshot(uint32_t currentTick, uint32_t mapEpoch)
{
    pse::MsgPlayerSnapshot snapshot;
    snapshot.snapshotSequence = ++g_SnapshotCounter;
    snapshot.serverTick = currentTick;
    snapshot.mapEpoch = mapEpoch;

    for (uint8_t i = 0; i < 8; ++i)
    {
        kexPlayer* pPlayer = g_PlayerContexts[i];
        if (pPlayer == nullptr || !pPlayer->IsActive()) continue;

        kexSlavePuppet* pPuppet = pPlayer->GetPuppet();
        if (pPuppet == nullptr) continue;

        pse::PlayerStateSnapshot ps;
        ps.slotId = i;
        ps.entityId = pPuppet->actorId;
        ps.entityGeneration = pPlayer->generation;
        ps.posX = pPuppet->origin.x;
        ps.posY = pPuppet->origin.y;
        ps.posZ = pPuppet->origin.z;
        ps.velX = pPuppet->velocity.x;
        ps.velY = pPuppet->velocity.y;
        ps.velZ = pPuppet->velocity.z;
        ps.yaw = pPlayer->yaw;
        ps.pitch = pPlayer->pitch;
        ps.health = pPlayer->health;
        ps.armor = pPlayer->armor;
        ps.activeWeapon = pPlayer->selectedWeapon;
        ps.weaponMask = pPlayer->weaponsBitmask;
        ps.ammo = pPlayer->ammo[pPlayer->selectedWeapon];
        ps.lifeState = pPlayer->IsDead() ? pse::LIFE_DEAD : pse::LIFE_ALIVE;
        ps.deathSequence = pPlayer->deathCount;

        snapshot.players.push_back(ps);
    }

    g_NetHost->BroadcastSnapshot(snapshot);
}
```

---

## 3. Manual Respawn & C05 Dangling Pointer Mitigation

> [!CAUTION]
> **DO NOT MANUALLY ZERO `actor->targetActor`:**
> Setting `actor->targetActor = nullptr` directly in a raw loop is forbidden. In KEX Engine, AI actors hold managed references to player puppet nodes. Direct raw pointer mutation bypasses engine refcounting and corrupts internal node lists.
> 
> **Required Protocol for Sol Max:**
> 1. Call the native target release routine (`SetTarget(nullptr)` at RVA `0x1d35d0`).
> 2. Ensure dead puppet `ObjectNode` refcounts decrement to zero cleanly.
> 3. Verify via runtime inspection that no dangling references remain before retiring the puppet.
> 4. Unlink the puppet from collision sectors using native contracts.
> 5. Spawn a fresh puppet using the proven factory, apply radial spacing at start sector, and reset loadout to Machete + Pistol (`DOC-16`).
> 6. Emit `MsgRespawnConfirm` to all peers.

> [!WARNING]
> **Illustrative Pseudo-Interfaces — NOT Confirmed Native ABI:**
> All class structures, member fields (e.g. `pPlayer->cmd`, `pPlayer->yaw`), and hook function signatures presented in this document are **conceptual architectural pseudo-code** intended to show data flow boundaries. They do **NOT** represent confirmed native C++ ABI layouts of `anubis_Shipping_Steam_x64.exe`. Sol Max must discover and verify exact runtime calling conventions, offsets, and class contracts before connecting hooks.

---

## 4. Components Ready for Direct Reuse

| Component | File Path | Reuse Status | Description |
|---|---|---|---|
| **Wire Protocol Headers** | `include/net_protocol.h` | **100% Ready** | Binary packet definitions, serialization/deserialization, bounds checks. |
| **Stream Serialization** | `include/net_protocol.h` (`ByteWriter`/`ByteReader`) | **100% Ready** | Endian-safe, bounds-checked binary streaming without heap allocation. |
| **Host Session Manager** | `include/enet_host.h`, `src/enet_host.cpp` | **100% Ready** | Slot allocation, reconnection generation, join token verification. |
| **Client Transport Node** | `include/enet_client.h`, `src/enet_client.cpp` | **100% Ready** | Handshake, sequence tracking, snapshot filtering. |
| **Network Simulator** | `include/net_simulator.h`, `src/net_simulator.cpp` | **100% Ready** | In-app latency and packet loss testing harness. |
| **Metrics Collector** | `include/metrics.h`, `src/metrics.cpp` | **100% Ready** | RTT percentiles, bandwidth tracking, Windows process CPU/RAM queries. |
| **Model World Simulation** | `include/simulation.h`, `src/simulation.cpp` | **Reference / Prototype** | Used in standalone testing; to be bridged to native KEX state in M2. |
