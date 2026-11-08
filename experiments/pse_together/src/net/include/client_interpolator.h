#pragma once

#include "world_entity.h"
#include "replication_protocol.h"
#include <deque>
#include <unordered_map>
#include <cstdint>

namespace pse {

struct InterpolatedState {
    uint32_t entityId = 0;
    uint32_t generation = 1;
    EntityType type = EntityType::NONE;
    uint16_t subType = 0;
    Vec3 position;
    Vec3 velocity;
    Rot2 rotation;
    int32_t health = 100;
    uint32_t stateFlags = 0;
    uint32_t meta = 0;
    bool valid = false;
};

struct SnapshotSample {
    uint64_t clientTimeMs = 0;
    uint32_t serverTick = 0;
    Vec3 position;
    Vec3 velocity;
    Rot2 rotation;
    int32_t health = 100;
    uint32_t stateFlags = 0;
    uint32_t meta = 0;
};

class EntityInterpolationBuffer {
public:
    EntityInterpolationBuffer(uint32_t id, uint32_t gen, EntityType t, uint16_t st = 0)
        : m_entityId(id), m_generation(gen), m_type(t), m_subType(st) {}

    uint32_t getEntityId() const { return m_entityId; }
    uint32_t getGeneration() const { return m_generation; }
    EntityType getType() const { return m_type; }

    void addSample(const SnapshotSample& sample);
    bool interpolate(uint64_t renderTimeMs, InterpolatedState& out) const;
    void resetGeneration(uint32_t newGen);

private:
    uint32_t m_entityId = 0;
    uint32_t m_generation = 1;
    EntityType m_type = EntityType::NONE;
    uint16_t m_subType = 0;
    std::deque<SnapshotSample> m_samples;
    static constexpr size_t MAX_SAMPLES = 20;
};

class ClientEntityInterpolator {
public:
    explicit ClientEntityInterpolator(uint64_t delayMs = 50)
        : m_interpolationDelayMs(delayMs) {}

    void setInterpolationDelayMs(uint64_t delayMs) { m_interpolationDelayMs = delayMs; }
    uint64_t getInterpolationDelayMs() const { return m_interpolationDelayMs; }

    void onEntitySpawned(uint32_t entityId, uint32_t generation, EntityType type, uint16_t subType,
                         const Vec3& pos, const Rot2& rot, int32_t hp, uint64_t nowMs, uint32_t tick);
    void onSnapshotReceived(const EntitySnapshot& snap, uint64_t nowMs, uint32_t serverTick);
    void onEntityDespawned(uint32_t entityId);

    bool getInterpolatedState(uint32_t entityId, uint64_t nowMs, InterpolatedState& out) const;
    size_t getTrackedEntityCount() const { return m_buffers.size(); }
    void clear();

private:
    uint64_t m_interpolationDelayMs = 50;
    std::unordered_map<uint32_t, EntityInterpolationBuffer> m_buffers;
};

} // namespace pse
