#pragma once

#include "net_protocol.h"
#include "replication_protocol.h"
#include "engine_adapter.h"
#include "world_entity.h"
#include <variant>
#include <vector>
#include <string>
#include <cstdint>

namespace pse {

// ============================================================================
// Message Priority Categorization
// ============================================================================
enum class MessagePriority : uint8_t {
    BEST_EFFORT = 0, // Dropped at watermark during queue pressure (movement, snapshots)
    CRITICAL = 1     // Protected with headroom, bounded backpressure & resync signal
};

// ============================================================================
// Data Transfer Objects (DTOs) for Thread Bridge
// Decoupled from native KEX and transport-specific ENet structures.
// ============================================================================

// 1. Input Command DTO (Best-Effort, 60 Hz)
struct BridgeInputCommand {
    uint8_t  slotId = 0;
    uint32_t connectionGeneration = 1;
    uint32_t sequence = 0;
    uint32_t clientTick = 0;
    float    pitch = 0.0f;
    float    yaw = 0.0f;
    float    moveForward = 0.0f;
    float    moveRight = 0.0f;
    uint32_t buttons = 0;
    uint8_t  selectedWeapon = WEAPON_PISTOL;

    // Bridge-internal (never serialized): rising button edges and press counts
    // accumulated by the network worker across every wire command coalesced into
    // this one, so one-shot actions survive latest-state coalescing.
    uint32_t pressedEdges = 0;
    uint8_t  attackPresses = 0;
    uint8_t  usePresses = 0;

    static BridgeInputCommand fromNet(const MsgInputCommand& net) {
        BridgeInputCommand dto;
        dto.slotId = net.slotId;
        dto.connectionGeneration = net.connectionGeneration;
        dto.sequence = net.commandSequence;
        dto.clientTick = net.clientTick;
        dto.pitch = net.pitch;
        dto.yaw = net.yaw;
        dto.moveForward = net.moveForward;
        dto.moveRight = net.moveRight;
        dto.buttons = net.buttons;
        dto.selectedWeapon = net.selectedWeapon;
        return dto;
    }

    MsgInputCommand toNet() const {
        MsgInputCommand net;
        net.slotId = slotId;
        net.connectionGeneration = connectionGeneration;
        net.commandSequence = sequence;
        net.clientTick = clientTick;
        net.pitch = pitch;
        net.yaw = yaw;
        net.moveForward = moveForward;
        net.moveRight = moveRight;
        net.buttons = buttons;
        net.selectedWeapon = selectedWeapon;
        return net;
    }
};

// CLIENT-only passive input snapshot. Internal DTO: never serialized. The KEX
// simulation role samples once per simulation tick; the optional client worker
// cadence turns the latest fresh snapshot into v1.10 INPUT commands at 60 Hz.
// sampledSteadyNs uses std::chrono::steady_clock::time_since_epoch(), in ns.
struct BridgeClientInputSample {
    uint8_t slotId = 0;
    uint32_t connectionGeneration = 0;
    uint64_t sampleOrdinal = 0;
    uint64_t sampledSteadyNs = 0;
    uint64_t simulationTick = 0;
    bool valid = false;
    float moveForward = 0.0f;
};

// 2. Player State Snapshot DTO (Best-Effort, 30 Hz)
struct BridgePlayerSnapshot {
    // Internal routing/session metadata, separate from the v1.12 player record.
    static constexpr uint8_t BROADCAST = 0xff;
    uint8_t targetSlot = BROADCAST;
    uint32_t connectionGeneration = 0; // Recipient connection, not entity lifetime.
    uint32_t mapEpoch = 0; // Explicit source required; zero preserves legacy DTO callers.
    uint8_t  slotId = 0;
    uint32_t entityId = 0;
    uint32_t entityGeneration = 1;
    uint32_t sequence = 0;
    uint32_t serverTick = 0;
    float    posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float    velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    float    yaw = 0.0f, pitch = 0.0f;
    int32_t  health = DEFAULT_HEALTH;
    int32_t  armor = DEFAULT_ARMOR;
    uint8_t  activeWeapon = WEAPON_PISTOL;
    uint32_t weaponMask = DEFAULT_WEAPON_MASK;
    uint16_t ammo = DEFAULT_PISTOL_AMMO;
    uint8_t  lifeState = LIFE_ALIVE;
    uint32_t buttons = 0;
    uint32_t deathSequence = 0;
    std::array<uint16_t,8> ammoByWeapon{0,DEFAULT_PISTOL_AMMO,0,0,0,0,0,0};
    uint16_t keys = 0;
    uint32_t artifacts = 0, abilities = 0;
    uint16_t questItems = 0, ankhCount = 0, ankhMask = 0;
    uint32_t teamDolls = 0;
    int32_t pendingWeapon = -1;
    uint64_t stateTimeNs = 0;
    uint32_t resetToken = 0, discontinuitySequence = 0;

    static BridgePlayerSnapshot fromNet(const PlayerStateSnapshot& s, uint32_t seq, uint32_t tick,
                                       uint32_t epoch = 0, uint32_t connectionGen = 0) {
        BridgePlayerSnapshot dto;
        dto.mapEpoch = epoch;
        dto.connectionGeneration = connectionGen;
        dto.slotId = s.slotId;
        dto.entityId = s.entityId;
        dto.entityGeneration = s.entityGeneration;
        dto.sequence = seq;
        dto.serverTick = tick;
        dto.posX = s.posX; dto.posY = s.posY; dto.posZ = s.posZ;
        dto.velX = s.velX; dto.velY = s.velY; dto.velZ = s.velZ;
        dto.yaw = s.yaw; dto.pitch = s.pitch;
        dto.health = s.health;
        dto.armor = s.armor;
        dto.activeWeapon = s.activeWeapon;
        dto.weaponMask = s.weaponMask;
        dto.ammo = s.ammo;
        dto.lifeState = s.lifeState;
        dto.buttons = s.buttons;
        dto.deathSequence = s.deathSequence;
        dto.ammoByWeapon=s.ammoByWeapon;dto.keys=s.keys;dto.artifacts=s.artifacts;dto.abilities=s.abilities;
        dto.questItems=s.questItems;dto.ankhCount=s.ankhCount;dto.ankhMask=s.ankhMask;dto.teamDolls=s.teamDolls;
        dto.pendingWeapon=s.pendingWeapon;dto.stateTimeNs=s.stateTimeNs;dto.resetToken=s.resetToken;dto.discontinuitySequence=s.discontinuitySequence;
        return dto;
    }

    PlayerStateSnapshot toNet() const {
        PlayerStateSnapshot s;
        s.slotId = slotId;
        s.entityId = entityId;
        s.entityGeneration = entityGeneration;
        s.posX = posX; s.posY = posY; s.posZ = posZ;
        s.velX = velX; s.velY = velY; s.velZ = velZ;
        s.yaw = yaw; s.pitch = pitch;
        s.health = health;
        s.armor = armor;
        s.activeWeapon = activeWeapon;
        s.weaponMask = weaponMask;
        s.ammo = ammo;
        s.lifeState = lifeState;
        s.buttons = buttons;
        s.deathSequence = deathSequence;
        s.ammoByWeapon=ammoByWeapon;s.keys=keys;s.artifacts=artifacts;s.abilities=abilities;
        s.questItems=questItems;s.ankhCount=ankhCount;s.ankhMask=ankhMask;s.teamDolls=teamDolls;
        s.pendingWeapon=pendingWeapon;s.stateTimeNs=stateTimeNs;s.resetToken=resetToken;s.discontinuitySequence=discontinuitySequence;
        return s;
    }
};

// 3. World Snapshot DTO (Best-Effort, 30 Hz)
struct BridgeWorldSnapshot {
    uint32_t snapshotSequence = 0;
    uint32_t serverTick = 0;
    uint32_t mapEpoch = 1;
    uint8_t  batchIndex = 0;
    uint8_t  totalBatches = 1;
    std::vector<EntitySnapshot> entities;

    static BridgeWorldSnapshot fromNet(const MsgWorldSnapshot& net) {
        BridgeWorldSnapshot dto;
        dto.snapshotSequence = net.snapshotSequence;
        dto.serverTick = net.serverTick;
        dto.mapEpoch = net.mapEpoch;
        dto.batchIndex = net.batchIndex;
        dto.totalBatches = net.totalBatches;
        dto.entities = net.entities;
        return dto;
    }

    MsgWorldSnapshot toNet() const {
        MsgWorldSnapshot net;
        net.snapshotSequence = snapshotSequence;
        net.serverTick = serverTick;
        net.mapEpoch = mapEpoch;
        net.batchIndex = batchIndex;
        net.totalBatches = totalBatches;
        net.entities = entities;
        return net;
    }
};

// 4. Entity Spawn DTO (Critical)
struct BridgeEntitySpawn {
    uint32_t mapEpoch = 1;
    uint32_t entityId = 0;
    uint32_t entityGeneration = 1;
    uint8_t  type = 0; // EntityType
    uint16_t subType = 0;
    Transform3D transform{};
    Velocity3D velocity{};
    ActorStateInfo state{};

    static BridgeEntitySpawn fromNet(const MsgEntitySpawn& net) {
        BridgeEntitySpawn dto;
        dto.mapEpoch = net.mapEpoch;
        dto.entityId = net.entityId;
        dto.entityGeneration = net.entityGeneration;
        dto.type = net.type;
        dto.subType = net.subType;
        dto.transform = Transform3D(net.posX, net.posY, net.posZ, net.yaw, net.pitch, 0.0f);
        dto.velocity = Velocity3D(net.velX, net.velY, net.velZ);
        dto.state = ActorStateInfo(net.health, DEFAULT_HEALTH, net.stateFlags, net.meta);
        return dto;
    }

    MsgEntitySpawn toNet() const {
        MsgEntitySpawn net;
        net.mapEpoch = mapEpoch;
        net.entityId = entityId;
        net.entityGeneration = entityGeneration;
        net.type = type;
        net.subType = subType;
        net.posX = transform.x; net.posY = transform.y; net.posZ = transform.z;
        net.velX = velocity.vx; net.velY = velocity.vy; net.velZ = velocity.vz;
        net.yaw = transform.yaw; net.pitch = transform.pitch;
        net.health = state.health;
        net.stateFlags = state.stateFlags;
        net.meta = state.meta;
        return net;
    }
};

// 5. Entity Despawn DTO (Critical)
struct BridgeEntityDespawn {
    uint32_t mapEpoch = 1;
    uint32_t entityId = 0;
    uint32_t entityGeneration = 1;
    uint8_t  reason = 0; // DespawnReason

    static BridgeEntityDespawn fromNet(const MsgEntityDespawn& net) {
        BridgeEntityDespawn dto;
        dto.mapEpoch = net.mapEpoch;
        dto.entityId = net.entityId;
        dto.entityGeneration = net.entityGeneration;
        dto.reason = net.reason;
        return dto;
    }

    MsgEntityDespawn toNet() const {
        MsgEntityDespawn net;
        net.mapEpoch = mapEpoch;
        net.entityId = entityId;
        net.entityGeneration = entityGeneration;
        net.reason = reason;
        return net;
    }
};

// 6. Respawn Request DTO (Critical)
struct BridgeRespawnRequest {
    uint32_t connectionGeneration = 0; // Internal peer context for generation-scoped refusal/KICK, not serialized.
    uint8_t  slotId = 0;
    uint32_t deathSequence = 0;
    uint32_t mapEpoch = 1;

    static BridgeRespawnRequest fromNet(const MsgRespawnRequest& net) {
        BridgeRespawnRequest dto;
        dto.slotId = net.slotId;
        dto.deathSequence = net.deathSequence;
        dto.mapEpoch = net.mapEpoch;
        return dto;
    }

    MsgRespawnRequest toNet() const {
        MsgRespawnRequest net;
        net.slotId = slotId;
        net.deathSequence = deathSequence;
        net.mapEpoch = mapEpoch;
        return net;
    }
};

// 7. Respawn Confirm DTO (Critical)
struct BridgeRespawnConfirm {
    uint8_t  slotId = 0;
    uint32_t mapEpoch = 1;
    uint32_t deathSequence = 0;
    uint32_t newEntityId = 0;
    uint32_t newGeneration = 1;
    float    posX = 0.0f, posY = 0.0f, posZ = 0.0f, yaw = 0.0f;
    int32_t  health = DEFAULT_HEALTH;
    uint32_t defaultWeapons = DEFAULT_WEAPON_MASK;
    uint16_t ammo = DEFAULT_PISTOL_AMMO;

    static BridgeRespawnConfirm fromNet(const MsgRespawnConfirm& net) {
        BridgeRespawnConfirm dto;
        dto.slotId = net.slotId;
        dto.mapEpoch = net.mapEpoch;
        dto.deathSequence = net.deathSequence;
        dto.newEntityId = net.newEntityId;
        dto.newGeneration = net.entityGeneration;
        dto.posX = net.spawnX;
        dto.posY = net.spawnY;
        dto.posZ = net.spawnZ;
        dto.yaw = net.spawnYaw;
        dto.health = DEFAULT_HEALTH;
        dto.defaultWeapons = net.defaultWeapons;
        dto.ammo = net.defaultAmmo;
        return dto;
    }

    MsgRespawnConfirm toNet() const {
        MsgRespawnConfirm net;
        net.slotId = slotId;
        net.mapEpoch = mapEpoch;
        net.deathSequence = deathSequence;
        net.newEntityId = newEntityId;
        net.entityGeneration = newGeneration;
        net.spawnX = posX;
        net.spawnY = posY;
        net.spawnZ = posZ;
        net.spawnYaw = yaw;
        net.defaultWeapons = defaultWeapons;
        net.defaultAmmo = ammo;
        return net;
    }
};

// 8. Map Change DTO (Critical)
struct BridgeMapChange {
    static constexpr uint8_t BROADCAST = 0xFF;

    uint32_t newMapEpoch = 1;
    std::string mapName;
    NativeMapStructure mapStructure;
    uint8_t targetSlot = BROADCAST; // Host routing only (unicast to a joining slot), not serialized
    uint32_t connectionGeneration = 0; // Unicast routing only; required, never serialized. Ignored for broadcast.

    static BridgeMapChange fromNet(const MsgMapChange& net) {
        BridgeMapChange dto;
        dto.newMapEpoch = net.newMapEpoch;
        dto.mapName = net.mapName;
        dto.mapStructure = net.mapStructure;
        return dto;
    }

    MsgMapChange toNet() const {
        MsgMapChange net;
        net.newMapEpoch = newMapEpoch;
        net.mapName = mapName;
        net.mapStructure = mapStructure;
        return net;
    }
};

// 9. Map Ready DTO (Critical)
struct BridgeMapReady {
    uint8_t  slotId = 0;
    uint32_t mapEpoch = 1;
    uint32_t connectionGeneration = 0; // Internal session identity; host stamps from authenticated peer, not wire.
    std::string mapName;
    NativeMapStructure mapStructure;

    static BridgeMapReady fromNet(const MsgMapReady& net) {
        BridgeMapReady dto;
        dto.slotId = net.slotId;
        dto.mapEpoch = net.mapEpoch;
        dto.mapName=net.mapName;dto.mapStructure=net.mapStructure;
        return dto;
    }

    MsgMapReady toNet() const {
        MsgMapReady net;
        net.slotId = slotId;
        net.mapEpoch = mapEpoch;
        net.mapName=mapName;net.mapStructure=mapStructure;
        return net;
    }
};

// 10. World Event DTO (Critical)
struct BridgeWorldEvent {
    uint32_t mapEpoch = 1;
    uint32_t eventSequence = 0;
    uint16_t eventType = EVENT_NONE;
    uint32_t sourceEntityId = 0;
    uint32_t targetEntityId = 0;
    uint32_t targetGeneration = 0;
    int32_t  eventValue = 0;
    float    floatValue = 0.0f;

    static BridgeWorldEvent fromNet(const MsgWorldEventConfirm& net) {
        BridgeWorldEvent dto;
        dto.mapEpoch = net.mapEpoch;
        dto.eventSequence = net.eventSequence;
        dto.eventType = net.eventType;
        dto.sourceEntityId = net.sourceEntityId;
        dto.targetEntityId = net.targetEntityId;
        dto.targetGeneration = net.targetGeneration;
        dto.eventValue = net.eventValue;
        dto.floatValue = net.floatValue;
        return dto;
    }

    MsgWorldEventConfirm toNet() const {
        MsgWorldEventConfirm net;
        net.mapEpoch = mapEpoch;
        net.eventSequence = eventSequence;
        net.eventType = eventType;
        net.sourceEntityId = sourceEntityId;
        net.targetEntityId = targetEntityId;
        net.targetGeneration = targetGeneration;
        net.eventValue = eventValue;
        net.floatValue = floatValue;
        return net;
    }
};

// 11. Interaction Request DTO (Critical)
struct BridgeInteractionRequest {
    uint32_t connectionGeneration = 0; // Internal peer context for generation-scoped refusal/KICK, not serialized.
    uint32_t mapEpoch = 1;
    uint8_t  slotId = 0;
    uint32_t targetEntityId = 0;
    uint32_t targetGeneration = 0;
    uint8_t  interactionType = 0;

    static BridgeInteractionRequest fromNet(const MsgInteractionRequest& net) {
        BridgeInteractionRequest dto;
        dto.mapEpoch = net.mapEpoch;
        dto.slotId = net.slotId;
        dto.targetEntityId = net.targetEntityId;
        dto.targetGeneration = net.targetGeneration;
        dto.interactionType = net.interactionType;
        return dto;
    }

    MsgInteractionRequest toNet() const {
        MsgInteractionRequest net;
        net.mapEpoch = mapEpoch;
        net.slotId = slotId;
        net.targetEntityId = targetEntityId;
        net.targetGeneration = targetGeneration;
        net.interactionType = interactionType;
        return net;
    }
};

// 12. Session / Lifecycle Event DTO (Critical)
// Host worker: CLIENT_JOINED / RECONNECTED / CLIENT_LEFT for remote slots.
// Client worker: CLIENT_JOINED / RECONNECTED for its own slot (with the map the
// host announced in WELCOME) and LOCAL_SESSION_LOST when the transport is gone.
struct BridgeSessionEvent {
    enum class Type : uint8_t {
        CLIENT_JOINED = 0,
        CLIENT_LEFT = 1,
        RECONNECTED = 2,
        REJECTED = 3,
        LOCAL_SESSION_LOST = 4
    } type = Type::CLIENT_JOINED;

    uint8_t  slotId = 0;
    uint32_t connectionGeneration = 1;
    std::string participantId;
    std::string displayName;
    uint64_t reconnectKey = 0;
    uint8_t  reasonCode = 0;
    uint32_t mapEpoch = 0;
    std::string mapName;
    NativeMapStructure mapStructure;
};

// 13. World Baseline DTO (Critical) -> existing MSG_WORLD_BASELINE (0x24).
// Authoritative full replacement of the replicated world for one map epoch.
struct BridgeWorldBaseline {
    static constexpr uint8_t BROADCAST = 0xFF;

    uint8_t  targetSlot = BROADCAST; // Host routing only, not serialized
    uint32_t connectionGeneration = 0; // Unicast routing only; required, never serialized. Ignored for broadcast.
    uint32_t mapEpoch = 1;
    uint32_t serverTick = 0;
    std::string mapName;
    uint8_t  batchIndex = 0;
    uint8_t  totalBatches = 1;
    std::vector<BridgeEntitySpawn> entities;

    static BridgeWorldBaseline fromNet(const MsgWorldBaseline& net) {
        BridgeWorldBaseline dto;
        dto.mapEpoch = net.mapEpoch;
        dto.serverTick = net.serverTick;
        dto.mapName = net.mapName;
        dto.batchIndex = net.batchIndex;
        dto.totalBatches = net.totalBatches;
        dto.entities.reserve(net.entities.size());
        for (const auto& e : net.entities) {
            dto.entities.push_back(BridgeEntitySpawn::fromNet(e));
        }
        return dto;
    }

    MsgWorldBaseline toNet() const {
        MsgWorldBaseline net;
        net.mapEpoch = mapEpoch;
        net.serverTick = serverTick;
        net.mapName = mapName;
        net.batchIndex = batchIndex;
        net.totalBatches = totalBatches;
        net.entities.reserve(entities.size());
        for (const auto& e : entities) {
            net.entities.push_back(e.toNet());
        }
        return net;
    }
};

// 14. Transport Control DTO (Critical, Simulation -> Worker only, never serialized)
struct BridgeControl {
    enum class Type : uint8_t {
        KICK_SLOT = 0,      // Host: disconnect a slot that cannot be recovered in-session
        RECONNECT = 1       // Client: drop the session and reconnect (WELCOME + baseline)
    } type = Type::KICK_SLOT;

    uint8_t slotId = 0;
    uint8_t reasonCode = 0;
    uint32_t connectionGeneration = 0; // Required for KICK_SLOT; captured by its owner before enqueue. RECONNECT unchanged.
};

// ============================================================================
// Unified Bridge Message Variant
// ============================================================================
using BridgeMessage = std::variant<
    BridgeInputCommand,
    BridgeClientInputSample,
    BridgePlayerSnapshot,
    BridgeWorldSnapshot,
    BridgeEntitySpawn,
    BridgeEntityDespawn,
    BridgeRespawnRequest,
    BridgeRespawnConfirm,
    BridgeMapChange,
    BridgeMapReady,
    BridgeWorldEvent,
    BridgeInteractionRequest,
    BridgeSessionEvent,
    BridgeWorldBaseline,
    BridgeControl
>;

inline bool isCriticalMessage(const BridgeMessage& msg) {
    return !(std::holds_alternative<BridgeInputCommand>(msg) ||
             std::holds_alternative<BridgeClientInputSample>(msg) ||
             std::holds_alternative<BridgePlayerSnapshot>(msg) ||
             std::holds_alternative<BridgeWorldSnapshot>(msg));
}

// Message Priority Resolver
inline MessagePriority getMessagePriority(const BridgeMessage& msg) {
    return std::visit([](const auto& val) -> MessagePriority {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, BridgeInputCommand> ||
                      std::is_same_v<T, BridgeClientInputSample> ||
                      std::is_same_v<T, BridgePlayerSnapshot> ||
                      std::is_same_v<T, BridgeWorldSnapshot>) {
            return MessagePriority::BEST_EFFORT;
        } else {
            return MessagePriority::CRITICAL;
        }
    }, msg);
}

inline const char* getBridgeMessageTypeName(const BridgeMessage& msg) {
    return std::visit([](const auto& val) -> const char* {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, BridgeInputCommand>) return "InputCommand";
        else if constexpr (std::is_same_v<T, BridgeClientInputSample>) return "ClientInputSample";
        else if constexpr (std::is_same_v<T, BridgePlayerSnapshot>) return "PlayerSnapshot";
        else if constexpr (std::is_same_v<T, BridgeWorldSnapshot>) return "WorldSnapshot";
        else if constexpr (std::is_same_v<T, BridgeEntitySpawn>) return "EntitySpawn";
        else if constexpr (std::is_same_v<T, BridgeEntityDespawn>) return "EntityDespawn";
        else if constexpr (std::is_same_v<T, BridgeRespawnRequest>) return "RespawnRequest";
        else if constexpr (std::is_same_v<T, BridgeRespawnConfirm>) return "RespawnConfirm";
        else if constexpr (std::is_same_v<T, BridgeMapChange>) return "MapChange";
        else if constexpr (std::is_same_v<T, BridgeMapReady>) return "MapReady";
        else if constexpr (std::is_same_v<T, BridgeWorldEvent>) return "WorldEvent";
        else if constexpr (std::is_same_v<T, BridgeInteractionRequest>) return "InteractionRequest";
        else if constexpr (std::is_same_v<T, BridgeSessionEvent>) return "SessionEvent";
        else if constexpr (std::is_same_v<T, BridgeWorldBaseline>) return "WorldBaseline";
        else if constexpr (std::is_same_v<T, BridgeControl>) return "Control";
        return "Unknown";
    }, msg);
}

} // namespace pse
