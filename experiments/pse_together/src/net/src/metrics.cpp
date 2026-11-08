#include "metrics.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace pse {

MetricsCollector::MetricsCollector() {
    m_rttSamples.reserve(4096);
    m_inputLatencySamples.reserve(4096);
    m_queueSamples.reserve(4096);
}

void MetricsCollector::recordRtt(double rttMs) {
    if (rttMs >= 0.0) {
        m_rttSamples.push_back(rttMs);
    }
}

void MetricsCollector::recordInputLatency(double latencyMs) {
    if (latencyMs >= 0.0) {
        m_inputLatencySamples.push_back(latencyMs);
    }
}

void MetricsCollector::recordQueueSize(size_t size) {
    m_queueSamples.push_back(size);
    if (size > m_peakQueueSize) {
        m_peakQueueSize = size;
    }
}

void MetricsCollector::recordBytesSent(uint64_t bytes) {
    m_totalBytesSent += bytes;
}

void MetricsCollector::recordBytesReceived(uint64_t bytes) {
    m_totalBytesReceived += bytes;
}

void MetricsCollector::recordPacketSent() {
    m_totalPacketsSent++;
}

void MetricsCollector::recordPacketReceived() {
    m_totalPacketsReceived++;
}

void MetricsCollector::recordReconnectDuration(double durationMs) {
    m_lastReconnectDurationMs = durationMs;
}

static MetricsCollector::PercentileStats computePercentiles(std::vector<double> samples) {
    MetricsCollector::PercentileStats stats;
    if (samples.empty()) return stats;

    stats.sampleCount = samples.size();
    std::sort(samples.begin(), samples.end());

    stats.min = samples.front();
    stats.max = samples.back();
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.avg = sum / static_cast<double>(samples.size());

    auto getPercentile = [&](double p) -> double {
        if (samples.empty()) return 0.0;
        double rank = p * (samples.size() - 1);
        size_t low = static_cast<size_t>(rank);
        size_t high = (low + 1 < samples.size()) ? low + 1 : low;
        double weight = rank - low;
        return samples[low] * (1.0 - weight) + samples[high] * weight;
    };

    stats.p50 = getPercentile(0.50);
    stats.p95 = getPercentile(0.95);
    stats.p99 = getPercentile(0.99);

    return stats;
}

MetricsCollector::PercentileStats MetricsCollector::calculateRttStats() const {
    return computePercentiles(m_rttSamples);
}

MetricsCollector::PercentileStats MetricsCollector::calculateInputLatencyStats() const {
    return computePercentiles(m_inputLatencySamples);
}

double MetricsCollector::getAvgQueueSize() const {
    if (m_queueSamples.empty()) return 0.0;
    size_t sum = std::accumulate(m_queueSamples.begin(), m_queueSamples.end(), size_t{0});
    return static_cast<double>(sum) / static_cast<double>(m_queueSamples.size());
}

SystemUsage MetricsCollector::querySystemUsage() {
    SystemUsage usage;
    HANDLE hProcess = GetCurrentProcess();

    // RAM Working Set
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        usage.ramWorkingSetBytes = pmc.WorkingSetSize;
    }

    // CPU Times
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors = -1;
    static HANDLE self = NULL;
    static bool initialized = false;

    if (!initialized) {
        SYSTEM_INFO sysInfo;
        FILETIME ftime, fsys, fuser;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));
        self = GetCurrentProcess();
        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
        initialized = true;
        usage.cpuPercent = 0.0;
        return usage;
    }

    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));
    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));

    ULONGLONG sysDiff = sys.QuadPart - lastSysCPU.QuadPart;
    ULONGLONG userDiff = user.QuadPart - lastUserCPU.QuadPart;
    ULONGLONG timeDiff = now.QuadPart - lastCPU.QuadPart;

    if (timeDiff > 0 && numProcessors > 0) {
        double percent = (double)(sysDiff + userDiff) / (double)timeDiff;
        percent /= numProcessors;
        usage.cpuPercent = percent * 100.0;
    }

    lastCPU = now;
    lastSysCPU = sys;
    lastUserCPU = user;

    return usage;
}

void MetricsCollector::reset() {
    m_rttSamples.clear();
    m_inputLatencySamples.clear();
    m_queueSamples.clear();
    m_peakQueueSize = 0;
    m_totalBytesSent = 0;
    m_totalBytesReceived = 0;
    m_totalPacketsSent = 0;
    m_totalPacketsReceived = 0;
    m_lastReconnectDurationMs = 0.0;
}

} // namespace pse
