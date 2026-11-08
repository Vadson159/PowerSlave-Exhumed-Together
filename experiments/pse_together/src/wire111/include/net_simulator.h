#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <random>
#include <chrono>
#include <functional>

namespace pse {

struct SimulatedPacket {
    uint8_t channel = 0;
    std::vector<uint8_t> data;
    uint64_t releaseTimeMs = 0;
    void* targetPeer = nullptr; // Opaque peer pointer
    bool isReliable = false;
};

class NetworkSimulator {
public:
    NetworkSimulator();

    void setDropRatePercent(float dropPercent);
    void setLatencyMs(uint32_t latencyMs, uint32_t jitterMs = 0);
    void setReorderEnabled(bool enable);

    float getDropRatePercent() const { return m_dropPercent; }
    uint32_t getLatencyMs() const { return m_latencyMs; }
    uint32_t getJitterMs() const { return m_jitterMs; }

    // Returns true if packet should be queued (delayed) or delivered, false if dropped
    bool processOutbound(uint8_t channel, const uint8_t* data, size_t size, void* peer, bool reliable);

    // Call regularly to drain delayed packets that are ready to send
    void update(uint64_t currentTimestampMs, const std::function<void(uint8_t, const uint8_t*, size_t, void*, bool)>& sendFunc);

    // Metrics
    uint64_t getTotalProcessed() const { return m_totalProcessed; }
    uint64_t getTotalDropped() const { return m_totalDropped; }
    uint64_t getTotalDelayed() const { return m_totalDelayed; }

    void resetMetrics();

private:
    float m_dropPercent = 0.0f;
    uint32_t m_latencyMs = 0;
    uint32_t m_jitterMs = 0;
    bool m_reorderEnabled = false;

    uint64_t m_totalProcessed = 0;
    uint64_t m_totalDropped = 0;
    uint64_t m_totalDelayed = 0;

    std::mt19937 m_rng;
    std::deque<SimulatedPacket> m_delayQueue;
};

} // namespace pse
