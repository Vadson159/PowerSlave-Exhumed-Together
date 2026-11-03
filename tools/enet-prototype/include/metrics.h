#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>

namespace pse {

struct SystemUsage {
    double cpuPercent = 0.0;
    uint64_t ramWorkingSetBytes = 0;
};

class MetricsCollector {
public:
    MetricsCollector();

    void recordRtt(double rttMs);
    void recordInputLatency(double latencyMs);
    void recordQueueSize(size_t size);
    void recordBytesSent(uint64_t bytes);
    void recordBytesReceived(uint64_t bytes);
    void recordPacketSent();
    void recordPacketReceived();
    void recordReconnectDuration(double durationMs);

    struct PercentileStats {
        double avg = 0.0;
        double min = 0.0;
        double max = 0.0;
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        size_t sampleCount = 0;
    };

    PercentileStats calculateRttStats() const;
    PercentileStats calculateInputLatencyStats() const;

    size_t getPeakQueueSize() const { return m_peakQueueSize; }
    double getAvgQueueSize() const;

    uint64_t getTotalBytesSent() const { return m_totalBytesSent; }
    uint64_t getTotalBytesReceived() const { return m_totalBytesReceived; }
    uint64_t getTotalPacketsSent() const { return m_totalPacketsSent; }
    uint64_t getTotalPacketsReceived() const { return m_totalPacketsReceived; }

    double getLastReconnectDurationMs() const { return m_lastReconnectDurationMs; }

    static SystemUsage querySystemUsage();

    void reset();

private:
    std::vector<double> m_rttSamples;
    std::vector<double> m_inputLatencySamples;
    std::vector<size_t> m_queueSamples;

    size_t m_peakQueueSize = 0;
    uint64_t m_totalBytesSent = 0;
    uint64_t m_totalBytesReceived = 0;
    uint64_t m_totalPacketsSent = 0;
    uint64_t m_totalPacketsReceived = 0;
    double m_lastReconnectDurationMs = 0.0;
};

} // namespace pse
