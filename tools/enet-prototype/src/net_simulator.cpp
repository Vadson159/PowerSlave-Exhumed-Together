#include "net_simulator.h"
#include <algorithm>

namespace pse {

NetworkSimulator::NetworkSimulator()
    : m_rng(1337) // Deterministic seed for reproducible tests
{
}

void NetworkSimulator::setDropRatePercent(float dropPercent) {
    m_dropPercent = std::clamp(dropPercent, 0.0f, 100.0f);
}

void NetworkSimulator::setLatencyMs(uint32_t latencyMs, uint32_t jitterMs) {
    m_latencyMs = latencyMs;
    m_jitterMs = jitterMs;
}

void NetworkSimulator::setReorderEnabled(bool enable) {
    m_reorderEnabled = enable;
}

bool NetworkSimulator::processOutbound(uint8_t channel, const uint8_t* data, size_t size, void* peer, bool reliable) {
    m_totalProcessed++;

    // Unreliable packets can be dropped if drop rate > 0
    // Reliable packets should not be permanently lost in the simulator (they would be resent by ENet in real life)
    if (!reliable && m_dropPercent > 0.0f) {
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        if (dist(m_rng) < m_dropPercent) {
            m_totalDropped++;
            return false; // Dropped!
        }
    }

    if (m_latencyMs > 0 || m_jitterMs > 0) {
        m_totalDelayed++;
        int32_t jitter = 0;
        if (m_jitterMs > 0) {
            std::uniform_int_distribution<int32_t> jDist(-static_cast<int32_t>(m_jitterMs), static_cast<int32_t>(m_jitterMs));
            jitter = jDist(m_rng);
        }
        int32_t totalDelay = std::max(0, static_cast<int32_t>(m_latencyMs) + jitter);

        // Get current system time in ms
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        SimulatedPacket pkt;
        pkt.channel = channel;
        pkt.data.assign(data, data + size);
        pkt.releaseTimeMs = static_cast<uint64_t>(now + totalDelay);
        pkt.targetPeer = peer;
        pkt.isReliable = reliable;

        if (m_reorderEnabled && !m_delayQueue.empty()) {
            // Randomly insert earlier to simulate reordering
            std::uniform_int_distribution<size_t> posDist(0, m_delayQueue.size());
            size_t idx = posDist(m_rng);
            m_delayQueue.insert(m_delayQueue.begin() + idx, std::move(pkt));
        } else {
            m_delayQueue.push_back(std::move(pkt));
        }
        return true;
    }

    return true; // No delay, immediate send
}

void NetworkSimulator::update(uint64_t currentTimestampMs, const std::function<void(uint8_t, const uint8_t*, size_t, void*, bool)>& sendFunc) {
    if (m_delayQueue.empty()) return;

    size_t i = 0;
    while (i < m_delayQueue.size()) {
        if (currentTimestampMs >= m_delayQueue[i].releaseTimeMs) {
            auto pkt = std::move(m_delayQueue[i]);
            m_delayQueue.erase(m_delayQueue.begin() + i);
            sendFunc(pkt.channel, pkt.data.data(), pkt.data.size(), pkt.targetPeer, pkt.isReliable);
        } else {
            ++i;
        }
    }
}

void NetworkSimulator::resetMetrics() {
    m_totalProcessed = 0;
    m_totalDropped = 0;
    m_totalDelayed = 0;
}

} // namespace pse
