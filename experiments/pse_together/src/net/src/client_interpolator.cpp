#include "client_interpolator.h"
#include <cmath>
#include <algorithm>

namespace pse {

static float angleLerp(float a, float b, float alpha) {
    float diff = std::fmod(b - a + 180.0f, 360.0f);
    if (diff < 0.0f) diff += 360.0f;
    diff -= 180.0f;
    return a + diff * std::clamp(alpha, 0.0f, 1.0f);
}

void EntityInterpolationBuffer::resetGeneration(uint32_t newGen) {
    m_generation = newGen;
    m_samples.clear();
}

void EntityInterpolationBuffer::addSample(const SnapshotSample& sample) {
    if (!m_samples.empty()) {
        const auto& latest = m_samples.back();
        // Discard out-of-order or duplicate samples
        if (sample.serverTick <= latest.serverTick && sample.serverTick != 0) {
            return;
        }
        if (sample.clientTimeMs <= latest.clientTimeMs && latest.clientTimeMs != 0) {
            return;
        }
    }

    m_samples.push_back(sample);
    while (m_samples.size() > MAX_SAMPLES) {
        m_samples.pop_front();
    }
}

bool EntityInterpolationBuffer::interpolate(uint64_t renderTimeMs, InterpolatedState& out) const {
    if (m_samples.empty()) {
        out.valid = false;
        return false;
    }

    out.entityId = m_entityId;
    out.generation = m_generation;
    out.type = m_type;
    out.subType = m_subType;
    out.valid = true;

    if (m_samples.size() == 1) {
        const auto& s = m_samples.front();
        out.position = s.position;
        out.velocity = s.velocity;
        out.rotation = s.rotation;
        out.health = s.health;
        out.stateFlags = s.stateFlags;
        out.meta = s.meta;
        return true;
    }

    // If render time is older than our oldest sample, clamp to oldest
    if (renderTimeMs <= m_samples.front().clientTimeMs) {
        const auto& s = m_samples.front();
        out.position = s.position;
        out.velocity = s.velocity;
        out.rotation = s.rotation;
        out.health = s.health;
        out.stateFlags = s.stateFlags;
        out.meta = s.meta;
        return true;
    }

    // If render time is newer than our latest sample (extrapolation / packet loss)
    if (renderTimeMs >= m_samples.back().clientTimeMs) {
        const auto& latest = m_samples.back();
        uint64_t dtMs = renderTimeMs - latest.clientTimeMs;
        float dtSec = static_cast<float>(dtMs) / 1000.0f;

        // Bounded extrapolation up to 150ms
        if (dtSec > 0.15f) dtSec = 0.15f;

        out.position.x = latest.position.x + latest.velocity.x * dtSec;
        out.position.y = latest.position.y + latest.velocity.y * dtSec;
        out.position.z = latest.position.z + latest.velocity.z * dtSec;
        out.velocity = latest.velocity;
        out.rotation = latest.rotation;
        out.health = latest.health;
        out.stateFlags = latest.stateFlags;
        out.meta = latest.meta;
        return true;
    }

    // Find two bounding samples for hermite / linear interpolation
    for (size_t i = 0; i + 1 < m_samples.size(); ++i) {
        const auto& s0 = m_samples[i];
        const auto& s1 = m_samples[i + 1];

        if (renderTimeMs >= s0.clientTimeMs && renderTimeMs <= s1.clientTimeMs) {
            uint64_t span = s1.clientTimeMs - s0.clientTimeMs;
            float alpha = (span > 0) ? static_cast<float>(renderTimeMs - s0.clientTimeMs) / static_cast<float>(span) : 0.0f;
            alpha = std::clamp(alpha, 0.0f, 1.0f);

            out.position.x = s0.position.x + (s1.position.x - s0.position.x) * alpha;
            out.position.y = s0.position.y + (s1.position.y - s0.position.y) * alpha;
            out.position.z = s0.position.z + (s1.position.z - s0.position.z) * alpha;

            out.velocity.x = s0.velocity.x + (s1.velocity.x - s0.velocity.x) * alpha;
            out.velocity.y = s0.velocity.y + (s1.velocity.y - s0.velocity.y) * alpha;
            out.velocity.z = s0.velocity.z + (s1.velocity.z - s0.velocity.z) * alpha;

            out.rotation.yaw = angleLerp(s0.rotation.yaw, s1.rotation.yaw, alpha);
            out.rotation.pitch = s0.rotation.pitch + (s1.rotation.pitch - s0.rotation.pitch) * alpha;

            out.health = (alpha < 0.5f) ? s0.health : s1.health;
            out.stateFlags = (alpha < 0.5f) ? s0.stateFlags : s1.stateFlags;
            out.meta = (alpha < 0.5f) ? s0.meta : s1.meta;
            return true;
        }
    }

    const auto& s = m_samples.back();
    out.position = s.position;
    out.velocity = s.velocity;
    out.rotation = s.rotation;
    out.health = s.health;
    out.stateFlags = s.stateFlags;
    out.meta = s.meta;
    return true;
}

// ----------------------------------------------------------------------------
// ClientEntityInterpolator
// ----------------------------------------------------------------------------
void ClientEntityInterpolator::onEntitySpawned(uint32_t entityId, uint32_t generation, EntityType type, uint16_t subType,
                                               const Vec3& pos, const Rot2& rot, int32_t hp, uint64_t nowMs, uint32_t tick) {
    auto it = m_buffers.find(entityId);
    if (it == m_buffers.end()) {
        EntityInterpolationBuffer buf(entityId, generation, type, subType);
        SnapshotSample s;
        s.clientTimeMs = nowMs;
        s.serverTick = tick;
        s.position = pos;
        s.velocity = Vec3(0, 0, 0);
        s.rotation = rot;
        s.health = hp;
        buf.addSample(s);
        m_buffers.emplace(entityId, std::move(buf));
    } else {
        if (generation > it->second.getGeneration()) {
            it->second.resetGeneration(generation);
        }
        SnapshotSample s;
        s.clientTimeMs = nowMs;
        s.serverTick = tick;
        s.position = pos;
        s.velocity = Vec3(0, 0, 0);
        s.rotation = rot;
        s.health = hp;
        it->second.addSample(s);
    }
}

void ClientEntityInterpolator::onSnapshotReceived(const EntitySnapshot& snap, uint64_t nowMs, uint32_t serverTick) {
    auto it = m_buffers.find(snap.entityId);
    if (it == m_buffers.end()) {
        EntityInterpolationBuffer buf(snap.entityId, snap.generation, static_cast<EntityType>(snap.type), snap.subType);
        SnapshotSample s;
        s.clientTimeMs = nowMs;
        s.serverTick = serverTick;
        s.position = Vec3(snap.posX, snap.posY, snap.posZ);
        s.velocity = Vec3(snap.velX, snap.velY, snap.velZ);
        s.rotation = Rot2(snap.yaw, snap.pitch);
        s.health = snap.health;
        s.stateFlags = snap.stateFlags;
        s.meta = snap.meta;
        buf.addSample(s);
        m_buffers.emplace(snap.entityId, std::move(buf));
    } else {
        if (snap.generation < it->second.getGeneration()) {
            return; // Stale generation
        }
        if (snap.generation > it->second.getGeneration()) {
            it->second.resetGeneration(snap.generation);
        }

        SnapshotSample s;
        s.clientTimeMs = nowMs;
        s.serverTick = serverTick;
        s.position = Vec3(snap.posX, snap.posY, snap.posZ);
        s.velocity = Vec3(snap.velX, snap.velY, snap.velZ);
        s.rotation = Rot2(snap.yaw, snap.pitch);
        s.health = snap.health;
        s.stateFlags = snap.stateFlags;
        s.meta = snap.meta;
        it->second.addSample(s);
    }
}

void ClientEntityInterpolator::onEntityDespawned(uint32_t entityId) {
    m_buffers.erase(entityId);
}

bool ClientEntityInterpolator::getInterpolatedState(uint32_t entityId, uint64_t nowMs, InterpolatedState& out) const {
    auto it = m_buffers.find(entityId);
    if (it == m_buffers.end()) {
        out.valid = false;
        return false;
    }

    uint64_t renderTimeMs = (nowMs >= m_interpolationDelayMs) ? (nowMs - m_interpolationDelayMs) : 0;
    return it->second.interpolate(renderTimeMs, out);
}

void ClientEntityInterpolator::clear() {
    m_buffers.clear();
}

} // namespace pse
