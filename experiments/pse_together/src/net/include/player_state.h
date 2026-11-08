#pragma once

#include "net_protocol.h"
#include <string>
#include <deque>
#include <memory>
#include <chrono>

struct _ENetPeer;
typedef struct _ENetPeer ENetPeer;

namespace pse {

// Single Sector / Start Spawn Coordinates for Model World
struct SpawnPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
};

// Starter Loadout Contract (Configurable per level/difficulty, decouples simulation from hardcoded HP/ammo)
struct StarterLoadout {
    int32_t  health = DEFAULT_HEALTH;          // Default: 200 (KEX C19 verified)
    int32_t  maxHealth = DEFAULT_HEALTH;       // Default: 200 (KEX C19 verified)
    int32_t  armor = DEFAULT_ARMOR;            // Default: 0
    uint32_t weaponMask = DEFAULT_WEAPON_MASK; // Default: Machete + Pistol (0x03)
    uint8_t  activeWeapon = WEAPON_PISTOL;     // Default: 1 (Pistol)
    uint16_t ammo = DEFAULT_PISTOL_AMMO;       // Default: 60 (KEX C19 verified)
};

// Internal Player State managed by Simulation & Host
struct PlayerParticipant {
    // Identity & Session
    std::string participantId;
    std::string displayName;
    uint64_t    reconnectKey = 0;        // Secret key assigned for reconnect verification
    uint8_t     slotId = 0;              // 0..7 (0 = Host / P1)
    uint32_t    connectionGeneration = 1;// Incremented on each reconnect
    ENetPeer*   peer = nullptr;          // Transient transport peer
    bool        connected = false;
    uint64_t    lastSeenTimestampMs = 0;

    // Entity Lifetime Tracking (independent of native pointers)
    uint32_t    entityId = 1000;
    uint32_t    entityGeneration = 1;

    // Spatial & Transform
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    float velX = 0.0f;
    float velY = 0.0f;
    float velZ = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Health, Armor & Armory
    int32_t  health = DEFAULT_HEALTH;
    int32_t  armor = DEFAULT_ARMOR;
    uint8_t  activeWeapon = WEAPON_PISTOL;
    uint32_t weaponMask = DEFAULT_WEAPON_MASK;
    uint16_t ammo = DEFAULT_PISTOL_AMMO;

    // Life State Machine
    LifeState lifeState = LIFE_ALIVE;
    uint32_t  deathCounter = 0;          // Monotonically increases each death
    uint64_t  deathTimestampMs = 0;
    bool      respawnRequested = false;  // Single-shot debounce flag
    uint64_t  respawnRequestMs = 0;
    uint32_t  lastConfirmedDeathSeq = 0;

    // Input & Queue Management
    uint32_t lastProcessedCommandSeq = 0;
    uint32_t lastReceivedCommandSeq = 0;
    std::deque<MsgInputCommand> inboundCommandQueue;
    uint32_t buttons = 0;                // Currently held buttons
    uint32_t lastButtons = 0;            // Buttons on previous simulation tick
    bool     hadInputThisTick = false;   // True if an input command was processed in current tick

    // Telemetry
    uint32_t packetsSent = 0;
    uint32_t packetsReceived = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint32_t lastPingRttMs = 0;

    void resetForNewConnection(ENetPeer* newPeer, uint32_t newGeneration) {
        peer = newPeer;
        connectionGeneration = newGeneration;
        connected = true;
        inboundCommandQueue.clear();
        lastProcessedCommandSeq = 0;
        lastReceivedCommandSeq = 0;
        buttons = 0;
        lastButtons = 0;
        hadInputThisTick = false;
    }

    void resetToStarterLoadout(const SpawnPoint& spawn, const StarterLoadout& loadout = StarterLoadout{}) {
        posX = spawn.x;
        posY = spawn.y;
        posZ = spawn.z;
        velX = 0.0f;
        velY = 0.0f;
        velZ = 0.0f;
        yaw = spawn.yaw;
        pitch = 0.0f;
        health = loadout.health;
        armor = loadout.armor;
        weaponMask = loadout.weaponMask;
        activeWeapon = loadout.activeWeapon;
        ammo = loadout.ammo;
        lifeState = LIFE_ALIVE;
        respawnRequested = false;
        buttons = 0;
        lastButtons = 0;
        hadInputThisTick = false;
        entityGeneration++;
    }

    PlayerStateSnapshot toSnapshot() const {
        PlayerStateSnapshot s;
        s.slotId = slotId;
        s.entityId = entityId;
        s.entityGeneration = entityGeneration;
        s.posX = posX;
        s.posY = posY;
        s.posZ = posZ;
        s.velX = velX;
        s.velY = velY;
        s.velZ = velZ;
        s.yaw = yaw;
        s.pitch = pitch;
        s.health = health;
        s.armor = armor;
        s.activeWeapon = activeWeapon;
        s.weaponMask = weaponMask;
        s.ammo = ammo;
        s.lifeState = static_cast<uint8_t>(lifeState);
        s.buttons = buttons;
        s.deathSequence = deathCounter;
        return s;
    }
};

} // namespace pse
