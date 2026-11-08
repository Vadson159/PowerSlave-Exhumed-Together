#include "replication_protocol.h"

namespace pse {

const char* getWorldEventTypeName(WorldEventType evt) {
    switch (evt) {
        case EVENT_DOOR_STATE_CHANGE: return "DoorStateChange";
        case EVENT_SWITCH_TRIGGERED:  return "SwitchTriggered";
        case EVENT_PICKUP_COLLECTED:  return "PickupCollected";
        case EVENT_DAMAGE_CONFIRMED:  return "DamageConfirmed";
        case EVENT_MAP_EPOCH_SYNC:    return "MapEpochSync";
        default:                      return "None";
    }
}

// ----------------------------------------------------------------------------
// EntitySnapshot
// ----------------------------------------------------------------------------
void EntitySnapshot::serialize(ByteWriter& w) const {
    w.writeUint32(entityId);
    w.writeUint32(generation);
    w.writeUint8(type);
    w.writeUint16(subType);
    w.writeFloat(posX);
    w.writeFloat(posY);
    w.writeFloat(posZ);
    w.writeFloat(velX);
    w.writeFloat(velY);
    w.writeFloat(velZ);
    w.writeFloat(yaw);
    w.writeFloat(pitch);
    w.writeInt32(health);
    w.writeUint32(stateFlags);
    w.writeUint32(meta);
}

bool EntitySnapshot::deserialize(ByteReader& r) {
    if (!r.readUint32(entityId)) return false;
    if (!r.readUint32(generation)) return false;
    if (!r.readUint8(type)) return false;
    if (!r.readUint16(subType)) return false;
    if (!r.readFloat(posX)) return false;
    if (!r.readFloat(posY)) return false;
    if (!r.readFloat(posZ)) return false;
    if (!r.readFloat(velX)) return false;
    if (!r.readFloat(velY)) return false;
    if (!r.readFloat(velZ)) return false;
    if (!r.readFloat(yaw)) return false;
    if (!r.readFloat(pitch)) return false;
    if (!r.readInt32(health)) return false;
    if (!r.readUint32(stateFlags)) return false;
    if (!r.readUint32(meta)) return false;
    return true;
}

// ----------------------------------------------------------------------------
// MsgEntitySpawn
// ----------------------------------------------------------------------------
void MsgEntitySpawn::serialize(ByteWriter& w) const {
    w.writeUint32(mapEpoch);
    w.writeUint32(entityId);
    w.writeUint32(entityGeneration);
    w.writeUint8(type);
    w.writeUint16(subType);
    w.writeFloat(posX);
    w.writeFloat(posY);
    w.writeFloat(posZ);
    w.writeFloat(velX);
    w.writeFloat(velY);
    w.writeFloat(velZ);
    w.writeFloat(yaw);
    w.writeFloat(pitch);
    w.writeInt32(health);
    w.writeUint32(stateFlags);
    w.writeUint32(meta);
}

bool MsgEntitySpawn::deserialize(ByteReader& r) {
    if (!r.readUint32(mapEpoch)) return false;
    if (!r.readUint32(entityId)) return false;
    if (!r.readUint32(entityGeneration)) return false;
    if (!r.readUint8(type)) return false;
    if (!r.readUint16(subType)) return false;
    if (!r.readFloat(posX)) return false;
    if (!r.readFloat(posY)) return false;
    if (!r.readFloat(posZ)) return false;
    if (!r.readFloat(velX)) return false;
    if (!r.readFloat(velY)) return false;
    if (!r.readFloat(velZ)) return false;
    if (!r.readFloat(yaw)) return false;
    if (!r.readFloat(pitch)) return false;
    if (!r.readInt32(health)) return false;
    if (!r.readUint32(stateFlags)) return false;
    if (!r.readUint32(meta)) return false;
    return true;
}

// ----------------------------------------------------------------------------
// MsgEntityDespawn
// ----------------------------------------------------------------------------
void MsgEntityDespawn::serialize(ByteWriter& w) const {
    w.writeUint32(mapEpoch);
    w.writeUint32(entityId);
    w.writeUint32(entityGeneration);
    w.writeUint8(reason);
}

bool MsgEntityDespawn::deserialize(ByteReader& r) {
    if (!r.readUint32(mapEpoch)) return false;
    if (!r.readUint32(entityId)) return false;
    if (!r.readUint32(entityGeneration)) return false;
    if (!r.readUint8(reason)) return false;
    return true;
}

// ----------------------------------------------------------------------------
// MsgWorldSnapshot
// ----------------------------------------------------------------------------
void MsgWorldSnapshot::serialize(ByteWriter& w) const {
    w.writeUint32(snapshotSequence);
    w.writeUint32(serverTick);
    w.writeUint32(mapEpoch);
    w.writeUint8(batchIndex);
    w.writeUint8(totalBatches);
    w.writeUint16(static_cast<uint16_t>(entities.size()));
    for (const auto& e : entities) {
        e.serialize(w);
    }
}

bool MsgWorldSnapshot::deserialize(ByteReader& r) {
    if (!r.readUint32(snapshotSequence)) return false;
    if (!r.readUint32(serverTick)) return false;
    if (!r.readUint32(mapEpoch)) return false;
    if (!r.readUint8(batchIndex)) return false;
    if (!r.readUint8(totalBatches)) return false;
    uint16_t count = 0;
    if (!r.readUint16(count)) return false;
    entities.clear();
    entities.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (!entities[i].deserialize(r)) return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// MsgWorldBaseline
// ----------------------------------------------------------------------------
void MsgWorldBaseline::serialize(ByteWriter& w) const {
    w.writeUint32(mapEpoch);
    w.writeUint32(serverTick);
    w.writeUint32(teamKeyMask);
    w.writeString(mapName, MAX_STRING_LEN);
    w.writeUint8(batchIndex);
    w.writeUint8(totalBatches);
    w.writeUint16(static_cast<uint16_t>(entities.size()));
    for (const auto& e : entities) {
        e.serialize(w);
    }
}

bool MsgWorldBaseline::deserialize(ByteReader& r) {
    if (!r.readUint32(mapEpoch)) return false;
    if (!r.readUint32(serverTick)) return false;
    if (!r.readUint32(teamKeyMask)) return false;
    if (!r.readString(mapName, MAX_STRING_LEN)) return false;
    if (!r.readUint8(batchIndex)) return false;
    if (!r.readUint8(totalBatches)) return false;
    uint16_t count = 0;
    if (!r.readUint16(count)) return false;
    entities.clear();
    entities.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (!entities[i].deserialize(r)) return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// MsgInteractionRequest
// ----------------------------------------------------------------------------
void MsgInteractionRequest::serialize(ByteWriter& w) const {
    w.writeUint32(mapEpoch);
    w.writeUint8(slotId);
    w.writeUint32(targetEntityId);
    w.writeUint32(targetGeneration);
    w.writeUint8(interactionType);
}

bool MsgInteractionRequest::deserialize(ByteReader& r) {
    if (!r.readUint32(mapEpoch)) return false;
    if (!r.readUint8(slotId)) return false;
    if (!r.readUint32(targetEntityId)) return false;
    if (!r.readUint32(targetGeneration)) return false;
    if (!r.readUint8(interactionType)) return false;
    return true;
}

// ----------------------------------------------------------------------------
// MsgWorldEventConfirm
// ----------------------------------------------------------------------------
void MsgWorldEventConfirm::serialize(ByteWriter& w) const {
    w.writeUint32(mapEpoch);
    w.writeUint32(eventSequence);
    w.writeUint16(eventType);
    w.writeUint32(sourceEntityId);
    w.writeUint32(targetEntityId);
    w.writeUint32(targetGeneration);
    w.writeInt32(eventValue);
    w.writeFloat(floatValue);
}

bool MsgWorldEventConfirm::deserialize(ByteReader& r) {
    if (!r.readUint32(mapEpoch)) return false;
    if (!r.readUint32(eventSequence)) return false;
    if (!r.readUint16(eventType)) return false;
    if (!r.readUint32(sourceEntityId)) return false;
    if (!r.readUint32(targetEntityId)) return false;
    if (!r.readUint32(targetGeneration)) return false;
    if (!r.readInt32(eventValue)) return false;
    if (!r.readFloat(floatValue)) return false;
    return true;
}

} // namespace pse
