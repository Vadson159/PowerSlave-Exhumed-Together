#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>

#pragma comment(lib, "winmm.lib")

namespace pse::qa {

typedef LONG (NTAPI *NtQueryTimerResolutionFn)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

struct TimerResolutionInfo {
    ULONG minResolution100ns = 0; // In 100-ns units (e.g. 156250 = 15.625 ms)
    ULONG maxResolution100ns = 0; // In 100-ns units (e.g. 5000 = 0.5 ms)
    ULONG currentResolution100ns = 0;
    UINT devCapsMinPeriodMs = 0;
    UINT devCapsMaxPeriodMs = 0;
    double currentResolutionMs = 0.0;
    double minResolutionMs = 0.0;
    double maxResolutionMs = 0.0;
};

inline TimerResolutionInfo queryTimerResolution() {
    TimerResolutionInfo info;
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR) {
        info.devCapsMinPeriodMs = tc.wPeriodMin;
        info.devCapsMaxPeriodMs = tc.wPeriodMax;
    }

    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    if (hNtDll) {
        auto queryFn = reinterpret_cast<NtQueryTimerResolutionFn>(GetProcAddress(hNtDll, "NtQueryTimerResolution"));
        if (queryFn) {
            ULONG minRes = 0, maxRes = 0, curRes = 0;
            if (queryFn(&minRes, &maxRes, &curRes) >= 0) {
                info.minResolution100ns = minRes;
                info.maxResolution100ns = maxRes;
                info.currentResolution100ns = curRes;
                info.minResolutionMs = minRes / 10000.0;
                info.maxResolutionMs = maxRes / 10000.0;
                info.currentResolutionMs = curRes / 10000.0;
            }
        }
    }
    return info;
}

struct SleepStats {
    double minMs = 0.0;
    double maxMs = 0.0;
    double meanMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    std::vector<double> samples;
};

inline SleepStats measureSleepPrecision(int iterations = 100, int sleepTargetMs = 1) {
    std::vector<double> samples;
    samples.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        Sleep(sleepTargetMs);
        auto t1 = std::chrono::steady_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        samples.push_back(elapsedMs);
    }

    std::sort(samples.begin(), samples.end());
    SleepStats stats;
    stats.minMs = samples.front();
    stats.maxMs = samples.back();
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.meanMs = sum / samples.size();
    stats.p50Ms = samples[samples.size() * 50 / 100];
    stats.p95Ms = samples[samples.size() * 95 / 100];
    stats.p99Ms = samples[samples.size() * 99 / 100];
    stats.samples = std::move(samples);
    return stats;
}

inline double measureGetTickCountQuantum(int sampleCount = 30) {
    std::vector<double> deltas;
    deltas.reserve(sampleCount);

    for (int i = 0; i < sampleCount; ++i) {
        ULONGLONG tStart = GetTickCount64();
        auto steadyStart = std::chrono::steady_clock::now();
        ULONGLONG tCurrent = tStart;
        while ((tCurrent = GetTickCount64()) == tStart) {
            YieldProcessor();
        }
        auto steadyEnd = std::chrono::steady_clock::now();
        double measuredDeltaMs = std::chrono::duration<double, std::milli>(steadyEnd - steadyStart).count();
        deltas.push_back(measuredDeltaMs);
    }

    std::sort(deltas.begin(), deltas.end());
    return deltas[deltas.size() / 2];
}

} // namespace pse::qa
