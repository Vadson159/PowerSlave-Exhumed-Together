# PowerSlave Exhumed Together — Manual Respawn Architecture

**Document ID:** `DOC-16-RESPAWN-DESIGN`  
**Classification:** Systems Architecture & Network Protocol Specification  
**Target Binary:** `anubis_Shipping_Steam_x64.exe` (SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)  
**Target Package:** `PowerslaveEX.kpf` (SHA-256 `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`)  
**Scope:** Specification of manual respawn mechanics on key 'E', death screen UI, host-authoritative network validation, native puppet lifecycle teardown, weapon reset rules, TPK handling, error recovery, and verification criteria for 2–8 players.

---

## 1. Executive Summary & Core Rules

In *PowerSlave Exhumed Together*, the cooperative death and respawn loop replaces the passive 2015 spectator system with an active, player-driven mechanic:

1. **Manual Respawn Only:** A fallen player respawns exclusively by pressing the **'E'** key (`BC_USE` / Use Action). Automatic respawn is strictly forbidden.
2. **Current Level Continuity:** The player respawns immediately at the initial start sector of the **current level**. There is zero requirement to wait for the next map or for surviving teammates.
3. **World State Preservation:** The map geometry, dead enemies, opened doors, activated switches, and quest triggers are **not** reloaded or reset.
4. **Total Party Kill (TPK) Resilience:** If all 2–8 players die simultaneously, the level does not restart. Each player independently sees the death screen and can press 'E' to respawn at the start sector.
5. **Weapons Reset on Death:** Acquired weapons are **not** preserved across death. Upon respawn, the player's armory resets to the standard starting loadout (Machete and Pistol with baseline ammunition). Global story artifacts (Keys, Team Dolls, Transmitter pieces) remain intact and shared on the server.
6. **Host Authority:** All respawn transitions are gated by host validation (`RespawnRequest` $\to$ `RespawnConfirm`) with `mapEpoch` checking to prevent stale or duplicate activations.

---

## 2. Player State Machine & Flowchart

Each connected player slot ($P_1 \dots P_8$) operates under the following state machine:

```
    ┌──────────────┐
    │    ALIVE     │◄─────────────────────────────────────────────────┐
    └──────┬───────┘                                                  │
           │ health <= 0                                              │
           ▼                                                          │
    ┌──────────────┐                                                  │
    │    DYING     │ (Plays player/john_death, disables collision)    │
    └──────┬───────┘                                                  │
           │ anim finishes / 1.0s grace                               │
           ▼                                                          │
    ┌──────────────┐                                                  │
    │     DEAD     │ (Input locked; shows "Press E to Respawn")       │
    └──────┬───────┘                                                  │
           │ Player presses 'E' (Single-shot debounce)                │
           ▼                                                          │
    ┌──────────────────────┐                                          │
    │  RESPAWN_REQUESTED   │───[ NET_MSG_RESPAWN_REQUEST ]───► Host   │
    └──────┬───────────────┘                                   │      │
           │                                          Validates req   │
           │                                          Cleans old node │
           │                                          Spawns puppet   │
           │                                          Resets weapons  │
           │                                                   │      │
           │◄──────────────────[ NET_MSG_RESPAWN_CONFIRM ]─────┘      │
           ▼                                                          │
    ┌──────────────┐                                                  │
    │  RESPAWNING  │ (Camera warps to start sector, binds puppet)     │
    └──────┬───────┘                                                  │
           │ State confirmed & controls unlocked                      │
           └──────────────────────────────────────────────────────────┘
```

---

## 3. Death Screen & Client Input Capture

### 3.1 Visual HUD Presentation

When transitioning to `DEAD`:
1. **Camera Position:** The camera remains stationary at the location and pitch of death, looking toward the horizon or ground. The camera does **not** attach to or follow other living players.
2. **Visual Overlay:** A subtle translucent dark-red gradient vignettes the screen edges.
3. **On-Screen Prompt:**
   - Centered horizontally at $Y = 70\%$ of viewport height:
     `[ Нажмите E, чтобы возродиться / Press E to Respawn ]`
   - Rendered using the native KEX bitmap font (`kexFont` / `FUN_1400d41e0`).
   - Pulses with a gentle sine-wave alpha oscillation ($0.7 \dots 1.0$) at 1.5 Hz.

### 3.2 Single-Shot Input Debounce

To prevent accidental multiple respawn triggers or packet flooding:
```cpp
void Client_HandleDeadInput(CoopClientContext* ctx, uint32_t buttons)
{
    if (ctx->state != PLAYER_STATE_DEAD) {
        return;
    }

    // Check for 'E' key press (BC_USE)
    bool bUsePressed = (buttons & BC_USE) != 0;

    if (bUsePressed && !ctx->bUseKeyWasDown)
    {
        // Single-shot edge trigger: dispatch request exactly once
        ctx->state = PLAYER_STATE_RESPAWN_REQUESTED;
        ctx->respawnRequestTime = kexPlatform::GetTimeMs();

        NetPacket_RespawnRequest req;
        req.packetId = NET_MSG_RESPAWN_REQUEST;
        req.playerId = ctx->localSlotId;
        req.mapEpoch = ctx->currentMapEpoch;
        req.deathSequence = ctx->deathCounter;
        
        ctx->netClient->SendReliable(&req, sizeof(req));
    }

    ctx->bUseKeyWasDown = bUsePressed;
}
```
Holding 'E' down continuously produces exactly **one** network request. Further inputs are ignored while in `PLAYER_STATE_RESPAWN_REQUESTED`.

---

## 4. Host-Authoritative Network Protocol

### 4.1 Packet Definitions

```cpp
#pragma pack(push, 1)

// Client -> Host (Reliable ENet Channel 1)
struct NetPacket_RespawnRequest
{
    uint8_t  packetId;       // 0x40 = NET_MSG_RESPAWN_REQUEST
    uint8_t  playerId;       // Slot ID (1..7 for clients, 0 for host)
    uint32_t mapEpoch;       // Current map instance generation counter
    uint32_t deathSequence;  // Monotonic death counter for this slot
};

// Host -> All Clients (Reliable ENet Channel 1)
struct NetPacket_RespawnConfirm
{
    uint8_t  packetId;       // 0x41 = NET_MSG_RESPAWN_CONFIRM
    uint8_t  playerId;       // Slot ID
    uint32_t mapEpoch;       // Current map generation counter
    float    spawnOrigin[3]; // World coordinates [X, Y, Z] at start sector
    float    spawnYaw;       // Starting orientation angle
    uint32_t newPuppetId;    // Native Actor ID assigned to new puppet
    uint32_t defaultWeapons; // Bitmask of starter weapons (PW_MACHETE | PW_PISTOL)
    uint16_t defaultAmmo;    // Baseline ammunition count
};

#pragma pack(pop)
```

### 4.2 Host Verification Gates

Upon receipt of `NET_MSG_RESPAWN_REQUEST`, the host evaluates five strict criteria:

1. **Player Connection Check:** Is `playerId` an active, connected session slot?
2. **State Validation:** Is server-side state for `playerId` strictly `PLAYER_STATE_DEAD`?
3. **Map Epoch Check:** Does `req.mapEpoch == host.currentMapEpoch`?  
   *(If a level transition occurred while the request was in flight, the packet is silently dropped.)*
4. **Loading Gate:** Is map loading currently in progress (`bMapLoading == true`)?  
   *(If loading, defer or drop request; respawn will occur automatically upon level start.)*
5. **Cinematic Gate:** Is a non-combat narrative sequence active (e.g. King Ramses)?  
   *(If active, queue confirmation until `EndRamsesEvent` completes.)*

If all gates pass, the host proceeds with puppet teardown, starter loadout reset, and respawn confirmation.

---

## 5. Native Puppet Lifecycle Teardown & Re-Spawn

### 5.1 The Dangling Pointer Hazard (Sol Max C05 Discovery)

In experiment `C05`, runtime analysis established that when an enemy AI is actively targeting a secondary player, the enemy actor structure retains a raw pointer or node reference to that player's puppet.

If the dead puppet is deleted without unlinking:
- Enemy AI attempting to evaluate distance in `FUN_1401a02f0` or `FUN_1401ae110` dereferences freed memory, causing an immediate CTD (Crash To Desktop).

### 5.2 Safe Teardown Protocol

Before allocating the new puppet, the host executes the following unlinking sequence:

```cpp
void Host_SafelyRetireDeadPuppet(CoopPlayerSlot* slot)
{
    kexSlavePuppet* pOldPuppet = slot->pPuppet;
    if (pOldPuppet == nullptr) {
        return;
    }

    // 1. Clear target pointers on all active enemy actors
    for (kexActor* actor = g_ActorListHead; actor != nullptr; actor = actor->next)
    {
        if (actor->targetActor == pOldPuppet) {
            actor->targetActor = nullptr; // Unlink target pointer safely
            actor->stateFlags &= ~ACTOR_HAS_TARGET;
        }
    }

    // 2. Unlink puppet from collision sectors
    pOldPuppet->UnlinkFromSectors();

    // 3. Mark for deferred engine retirement (bounded lifecycle)
    pOldPuppet->flags |= ACTOR_RETIRED;
    slot->pPuppet = nullptr;
}
```

### 5.3 Starter Weapon Reset Contract

Per explicit user requirement: **weapons are not preserved across death; they reset to the standard campaign starter loadout.**

During puppet reconstruction:
```cpp
void Host_ApplyStandardStarterLoadout(CoopPlayerSlot* slot)
{
    // Clear all previously acquired heavy weapons
    slot->weaponsMask = 0;
    memset(slot->ammo, 0, sizeof(slot->ammo));

    // Standard starting loadout for PowerSlave Exhumed:
    // 1. Machete (Always owned, infinite durability)
    slot->weaponsMask |= (1 << PW_MACHETE);
    
    // 2. Pistol with baseline ammo (50 rounds)
    slot->weaponsMask |= (1 << PW_PISTOL);
    slot->ammo[PW_PISTOL] = 50;

    // Reset health & armor
    slot->health = 100;
    slot->armor = 0;

    // NOTE: Shared story items (Transmitter parts, Team Dolls, Keys)
    // are stored globally in host campaign state and remain untouched.
}
```

### 5.4 Spawn Point Resolution

The host locates the level's designated player start sector:
- Coordinates are extracted from `kexLevel::GetPlayerStartOrigin()` and `kexLevel::GetPlayerStartAngle()`.
- **Congestion Prevention:** If multiple players respawn within a 2-second window, the host applies a minor horizontal displacement:
  $$ec{P}_{	ext{spawn}} = ec{P}_{	ext{base}} + egin{pmatrix} \cos(	heta) \cdot 32 \ \sin(	heta) \cdot 32 \ 0 \end{pmatrix}, \quad 	heta = rac{2\pi \cdot 	ext{slotId}}{8}$$
  This prevents physics collision jamming inside the same 16-unit bounding cylinder.

---

## 6. Edge Cases & Resilience

| Scenario | Risk | Mitigation |
|---|---|---|
| **E-Key Spamming / Controller Bounce** | Duplicate network packets flooding server. | Client transitions to `RESPAWN_REQUESTED` on first edge detection and ignores further inputs until confirm or timeout. |
| **Map Transition While Dead** | Request arrives after host loaded next level. | Request contains `mapEpoch`. If packet epoch does not match current host epoch, host drops request. Client spawns naturally in next level. |
| **Total Party Kill (All 8 Dead)** | World might unload or soft-lock. | World tick continues running; all players see "Press E to Respawn". Any player can press E at any time to re-enter from start sector. |
| **Host Disconnects While Client Dead** | Client freezes on death screen. | ENet connection monitor detects host drop after 3.0s heartbeat loss $\implies$ displays disconnect dialogue and returns to main menu. |
| **Packet Loss (UDP)** | Confirm packet dropped by network. | Client runs a 1.0s timeout in `RESPAWN_REQUESTED`. If no confirm arrives, state resets to `DEAD`, allowing re-pressing 'E'. |
| **Death Inside Cinematic Zone** | Player respawns while cutscene is playing. | Host queues confirmation; puppet spawns only after `EndRamsesEvent` or FMV finishes. |

---

## 7. Dependencies on Milestones M0–M3

- **M0-C Dependency (Crucial):** Relies on Sol Max's validated native actor lifecycle contracts and safe enemy-target node release.
- **M1-C Task:** Implementation of the local death screen overlay, E-key interceptor, and starter weapon reset logic.
- **M2-C Task:** Implementation of `NET_MSG_RESPAWN_REQUEST` / `NET_MSG_RESPAWN_CONFIRM` ENet packets and host validation gates.

---

## 8. Verification Criteria & Test Plan (Design Only)

When moving to M1/M2 implementation, verify through the following test cases:

1. **Test TC-RES-01 (Single-Player Local E-Respawn):**
   - Kill player via console or environmental hazard (`lava`).
   - Confirm screen displays «Нажмите E, чтобы возродиться».
   - Verify movement/firing is locked.
   - Press 'E': player respawns at level start with 100 HP, Machete, Pistol (50 ammo), zero heavy weapons.
2. **Test TC-RES-02 (Rapid E-Spamming):**
   - Press 'E' 20 times in rapid succession.
   - Verify exactly one `RespawnRequest` is logged; zero duplicate puppets spawned.
3. **Test TC-RES-03 (Total Party Kill):**
   - In 2-player session, kill both P1 and P2.
   - Verify level does not reload.
   - P1 presses E $\implies$ P1 respawns at start. P2 presses E 10 seconds later $\implies$ P2 respawns next to P1.
4. **Test TC-RES-04 (Enemy Target Pointer Safety):**
   - Anubis monster attacks P2 until P2 dies.
   - P2 presses E and respawns.
   - Verify Anubis AI does not crash or dereference dangling pointer to P2's dead puppet.
