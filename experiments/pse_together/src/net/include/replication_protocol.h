#pragma once

#include "net_protocol.h"
#include "world_entity.h"
#include <vector>
#include <string>

namespace pse {

// World Event Types
enum WorldEventType : uint16_t {
    EVENT_NONE = 0,
    EVENT_DOOR_STATE_CHANGE = 1,
    EVENT_SWITCH_TRIGGERED = 2,
    EVENT_PICKUP_COLLECTED = 3,
    EVENT_DAMAGE_CONFIRMED = 4,
    EVENT_MAP_EPOCH_SYNC = 5
};

const char* getWorldEventTypeName(WorldEventType evt);

// Single Entity Transform & State for Snapshots
struct EntitySnapshot {
    static constexpr size_t SERIALIZED_SIZE = 55;

    uint32_t entityId = 0;
    uint32_t generation = 1;
    uint8_t  type = 0;
    uint16_t subType = 0;
    float    posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float    velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    float    yaw = 0.0f, pitch = 0.0f;
    int32_t  health = 100;
    uint32_t stateFlags = 0;
    uint32_t meta = 0;

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

// 0x21: MSG_ENTITY_SPAWN (Host -> Broadcast, Reliable Channel 0)
struct MsgEntitySpawn {
    static constexpr size_t SERIALIZED_SIZE = 59;

    uint32_t mapEpoch = 1;
    uint32_t entityId = 0;
    uint32_t entityGeneration = 1;
    uint8_t  type = 0;
    uint16_t subType = 0;
    float    posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float    velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    float    yaw = 0.0f, pitch = 0.0f;
    int32_t  health = 100;
    uint32_t stateFlags = 0;
    uint32_t meta = 0;

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

// 0x22: MSG_ENTITY_DESPAWN (Host -> Broadcast, Reliable Channel 0)
struct MsgEntityDespawn {
    static constexpr size_t SERIALIZED_SIZE = 13;

    uint32_t mapEpoch = 1;
    uint32_t entityId = 0;
    uint32_t entityGeneration = 1;
    uint8_t  reason = 0; // DespawnReason

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

// 0x23: MSG_WORLD_SNAPSHOT (Host -> Broadcast, Unreliable Sequenced Channel 3)
struct MsgWorldSnapshot {
    static constexpr size_t HEADER_SIZE = 16;

    uint32_t snapshotSequence = 0;
    uint32_t serverTick = 0;
    uint32_t mapEpoch = 1;
    uint8_t  batchIndex = 0;
    uint8_t  totalBatches = 1;
    std::vector<EntitySnapshot> entities;

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

// 0x24: MSG_WORLD_BASELINE (Host -> Peer on Join/Reconnect, Reliable Channel 0)
struct MsgWorldBaseline {
    uint32_t mapEpoch = 1;
    uint32_t serverTick = 0;
    uint32_t teamKeyMask = 0;
    std::string mapName;
    uint8_t  batchIndex = 0;
    uint8_t  totalBatches = 1;
    std::vector<MsgEntitySpawn> entities;

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

// 0x31: MSG_INTERACTION_REQUEST (Client -> Host, Reliable Channel 0)
struct MsgInteractionRequest {
    static constexpr size_t SERIALIZED_SIZE = 14;

    uint32_t mapEpoch = 1;
    uint8_t  slotId = 0;
    uint32_t targetEntityId = 0;
    uint32_t targetGeneration = 0;
    uint8_t  interactionType = 0; // 0 = USE ('E' press), 1 = TOUCH

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

// 0x32: MSG_WORLD_EVENT_CONFIRM (Host -> Broadcast, Reliable Channel 0)
struct MsgWorldEventConfirm {
    static constexpr size_t SERIALIZED_SIZE = 30;

    uint32_t mapEpoch = 1;
    uint32_t eventSequence = 0;
    uint16_t eventType = 0;
    uint32_t sourceEntityId = 0;
    uint32_t targetEntityId = 0;
    uint32_t targetGeneration = 0;
    int32_t  eventValue = 0;
    float    floatValue = 0.0f;

    void serialize(ByteWriter& w) const;
    bool deserialize(ByteReader& r);
};

} // namespace pse
