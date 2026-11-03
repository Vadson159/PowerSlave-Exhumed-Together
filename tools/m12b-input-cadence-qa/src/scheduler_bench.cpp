// SCHEDULER REPRODUCTION & WINDOWS TIMER RESOLUTION BENCHMARK
// Purpose: Isolate and evaluate command scheduling algorithms and timer resolution impact.
// NOTE: Explicitly marked as SCHEDULER REPRODUCTION, not game client execution.

#include "timer_resolution.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <emmintrin.h>

using namespace pse::qa;

struct BenchResult {
    std::string name;
    bool timeBeginPeriodActive = false;
    double timerResolutionMs = 0.0;
    size_t iterations = 0;
    double totalElapsedSec = 0.0;
    double effectiveHz = 0.0;
    double minIntervalMs = 0.0;
    double maxIntervalMs = 0.0;
    double meanIntervalMs = 0.0;
    double stddevIntervalMs = 0.0;
    double p50IntervalMs = 0.0;
    double p95IntervalMs = 0.0;
    double p99IntervalMs = 0.0;
    size_t countAbove33ms = 0; // > 33.3ms (dropped below 30 Hz)
    size_t countAbove50ms = 0; // > 50ms (dropped below 20 Hz)
    size_t countAbove100ms = 0; // > 100ms (timeout risk threshold)
    std::vector<double> intervalsMs;
};

static BenchResult runBenchmark(const std::string& name, bool withTimeBeginPeriod, int modelIndex, size_t iterations = 500) {
    if (withTimeBeginPeriod) {
        timeBeginPeriod(1);
    }

    TimerResolutionInfo timerInfo = queryTimerResolution();

    std::vector<double> intervals;
    intervals.reserve(iterations);

    auto benchStart = std::chrono::steady_clock::now();
    auto prevTime = benchStart;

    if (modelIndex == 0) {
        // Model 0: Baseline GetTickCount64() with 'next = now + 16' (native_client.cpp reproduction)
        auto next = GetTickCount64();
        for (size_t i = 0; i < iterations; ++i) {
            while (true) {
                auto now = GetTickCount64();
                if (now >= next) {
                    auto steadyNow = std::chrono::steady_clock::now();
                    double deltaMs = std::chrono::duration<double, std::milli>(steadyNow - prevTime).count();
                    intervals.push_back(deltaMs);
                    prevTime = steadyNow;
                    next = now + 16; // The accumulator flaw: advances from 'now', discarding any late wakeup!
                    break;
                }
                Sleep(1);
            }
        }
    } else if (modelIndex == 1) {
        // Model 1: GetTickCount64() with fixed interval accumulation 'next += 16'
        auto next = GetTickCount64();
        for (size_t i = 0; i < iterations; ++i) {
            while (true) {
                auto now = GetTickCount64();
                if (now >= next) {
                    auto steadyNow = std::chrono::steady_clock::now();
                    double deltaMs = std::chrono::duration<double, std::milli>(steadyNow - prevTime).count();
                    intervals.push_back(deltaMs);
                    prevTime = steadyNow;
                    next += 16; // Fixed accumulator
                    break;
                }
                Sleep(1);
            }
        }
    } else if (modelIndex == 2) {
        // Model 2: std::chrono::steady_clock with fixed interval 'next += 16667us' and Sleep(1)
        using namespace std::chrono;
        constexpr auto interval = microseconds(16667); // 60 Hz target
        auto next = steady_clock::now() + interval;
        for (size_t i = 0; i < iterations; ++i) {
            while (true) {
                auto now = steady_clock::now();
                if (now >= next) {
                    double deltaMs = duration<double, std::milli>(now - prevTime).count();
                    intervals.push_back(deltaMs);
                    prevTime = now;
                    next += interval;
                    break;
                }
                Sleep(1);
            }
        }
    } else if (modelIndex == 3) {
        // Model 3: std::chrono::steady_clock with hybrid Sleep(1) + spin-pause
        using namespace std::chrono;
        constexpr auto interval = microseconds(16667); // 60 Hz target
        auto next = steady_clock::now() + interval;
        for (size_t i = 0; i < iterations; ++i) {
            while (true) {
                auto now = steady_clock::now();
                if (now >= next) {
                    double deltaMs = duration<double, std::milli>(now - prevTime).count();
                    intervals.push_back(deltaMs);
                    prevTime = now;
                    next += interval;
                    break;
                }
                auto remaining = duration_cast<milliseconds>(next - now);
                if (remaining.count() > 2) {
                    Sleep(1);
                } else {
                    while (steady_clock::now() < next) {
                        _mm_pause();
                    }
                }
            }
        }
    }

    auto benchEnd = std::chrono::steady_clock::now();
    double totalSec = std::chrono::duration<double>(benchEnd - benchStart).count();

    if (withTimeBeginPeriod) {
        timeEndPeriod(1);
    }

    BenchResult res;
    res.name = name;
    res.timeBeginPeriodActive = withTimeBeginPeriod;
    res.timerResolutionMs = timerInfo.currentResolutionMs;
    res.iterations = intervals.size();
    res.totalElapsedSec = totalSec;
    res.effectiveHz = static_cast<double>(intervals.size()) / totalSec;

    std::vector<double> sorted = intervals;
    std::sort(sorted.begin(), sorted.end());

    res.minIntervalMs = sorted.front();
    res.maxIntervalMs = sorted.back();
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    res.meanIntervalMs = sum / sorted.size();

    double sqSum = 0.0;
    for (double v : sorted) {
        double d = v - res.meanIntervalMs;
        sqSum += d * d;
    }
    res.stddevIntervalMs = std::sqrt(sqSum / sorted.size());

    res.p50IntervalMs = sorted[sorted.size() * 50 / 100];
    res.p95IntervalMs = sorted[sorted.size() * 95 / 100];
    res.p99IntervalMs = sorted[sorted.size() * 99 / 100];

    for (double v : sorted) {
        if (v > 33.3) res.countAbove33ms++;
        if (v > 50.0) res.countAbove50ms++;
        if (v > 100.0) res.countAbove100ms++;
    }

    res.intervalsMs = std::move(intervals);
    return res;
}

int main(int argc, char** argv) {
    std::string jsonPath = "scheduler_bench_results.json";
    size_t iterations = 400; // ~6-12 seconds per test

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) jsonPath = argv[++i];
        else if (arg == "--iterations" && i + 1 < argc) iterations = std::stoul(argv[++i]);
    }

    std::cout << "================================================================================" << std::endl;
    std::cout << "  SCHEDULER REPRODUCTION & WINDOWS TIMER RESOLUTION BENCHMARK" << std::endl;
    std::cout << "  Target: DOC-45 P2-2 Investigation" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // 1. Query OS Timer Resolution
    auto initTimer = queryTimerResolution();
    std::cout << "[OS Timer] DevCaps: min=" << initTimer.devCapsMinPeriodMs << "ms, max=" << initTimer.devCapsMaxPeriodMs << "ms" << std::endl;
    std::cout << "[OS Timer] Initial Resolution: " << std::fixed << std::setprecision(4) << initTimer.currentResolutionMs << " ms (" << initTimer.currentResolution100ns << " in 100ns units)" << std::endl;
    std::cout << "[OS Timer] Min possible resolution: " << initTimer.maxResolutionMs << " ms (" << initTimer.maxResolution100ns << " in 100ns units)" << std::endl;

    double quantumNoPeriod = measureGetTickCountQuantum(20);
    std::cout << "[OS Timer] Measured GetTickCount64 quantum (default): ~" << quantumNoPeriod << " ms" << std::endl;

    timeBeginPeriod(1);
    auto periodActiveTimer = queryTimerResolution();
    double quantumWithPeriod = measureGetTickCountQuantum(20);
    std::cout << "[OS Timer] Resolution with timeBeginPeriod(1): " << periodActiveTimer.currentResolutionMs << " ms (" << periodActiveTimer.currentResolution100ns << " in 100ns units)" << std::endl;
    std::cout << "[OS Timer] Measured GetTickCount64 quantum (timeBeginPeriod): ~" << quantumWithPeriod << " ms" << std::endl;
    timeEndPeriod(1);

    auto sleepNoPeriod = measureSleepPrecision(50, 1);
    timeBeginPeriod(1);
    auto sleepWithPeriod = measureSleepPrecision(50, 1);
    timeEndPeriod(1);

    std::cout << "[Sleep(1)] Default timer: mean=" << sleepNoPeriod.meanMs << "ms (p50=" << sleepNoPeriod.p50Ms << "ms, max=" << sleepNoPeriod.maxMs << "ms)" << std::endl;
    std::cout << "[Sleep(1)] timeBeginPeriod(1): mean=" << sleepWithPeriod.meanMs << "ms (p50=" << sleepWithPeriod.p50Ms << "ms, max=" << sleepWithPeriod.maxMs << "ms)" << std::endl;

    std::cout << "\nRunning Scheduler Benchmarks (iterations=" << iterations << " per model)...\n" << std::endl;

    std::vector<BenchResult> results;

    // Test 1: Model 0 Baseline (GetTickCount64 + next = now + 16), timeBeginPeriod = true (ENet default)
    std::cout << "[1/6] Running Model 0: Baseline GetTickCount64 (now+16) [timeBeginPeriod(1) ACTIVE]..." << std::endl;
    results.push_back(runBenchmark("M0_Baseline_TickCount_NowPlus16_Period1", true, 0, iterations));

    // Test 2: Model 0 Baseline WITHOUT timeBeginPeriod
    std::cout << "[2/6] Running Model 0: Baseline GetTickCount64 (now+16) [Default OS Timer]..." << std::endl;
    results.push_back(runBenchmark("M0_Baseline_TickCount_NowPlus16_NoPeriod", false, 0, iterations));

    // Test 3: Model 1 Fixed Interval GetTickCount64 (next += 16), timeBeginPeriod = true
    std::cout << "[3/6] Running Model 1: GetTickCount64 (next+=16) [timeBeginPeriod(1) ACTIVE]..." << std::endl;
    results.push_back(runBenchmark("M1_FixedAccum_TickCount_NextPlus16_Period1", true, 1, iterations));

    // Test 4: Model 2 Precision steady_clock (next += 16.67ms) + Sleep(1), timeBeginPeriod = true
    std::cout << "[4/6] Running Model 2: steady_clock (next+=16.67ms) + Sleep(1) [timeBeginPeriod(1) ACTIVE]..." << std::endl;
    results.push_back(runBenchmark("M2_Precision_SteadyClock_Sleep1_Period1", true, 2, iterations));

    // Test 5: Model 2 Precision steady_clock WITHOUT timeBeginPeriod
    std::cout << "[5/6] Running Model 2: steady_clock (next+=16.67ms) + Sleep(1) [Default OS Timer]..." << std::endl;
    results.push_back(runBenchmark("M2_Precision_SteadyClock_Sleep1_NoPeriod", false, 2, iterations));

    // Test 6: Model 3 Precision Hybrid (steady_clock + hybrid spin-wait), timeBeginPeriod = true
    std::cout << "[6/6] Running Model 3: steady_clock + Hybrid SpinWait [timeBeginPeriod(1) ACTIVE]..." << std::endl;
    results.push_back(runBenchmark("M3_Precision_HybridSpin_Period1", true, 3, iterations));

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  SCHEDULER BENCHMARK SUMMARY TABLE" << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << std::left << std::setw(42) << "Algorithm"
              << std::setw(10) << "Timer"
              << std::setw(10) << "Freq(Hz)"
              << std::setw(10) << "Mean(ms)"
              << std::setw(10) << "p50(ms)"
              << std::setw(10) << "p95(ms)"
              << std::setw(10) << ">33ms"
              << std::setw(10) << ">100ms" << std::endl;
    std::cout << std::string(112, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left << std::setw(42) << r.name
                  << std::setw(10) << (r.timeBeginPeriodActive ? "1.0ms" : "Default")
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.effectiveHz
                  << std::setw(10) << std::fixed << std::setprecision(2) << r.meanIntervalMs
                  << std::setw(10) << std::fixed << std::setprecision(2) << r.p50IntervalMs
                  << std::setw(10) << std::fixed << std::setprecision(2) << r.p95IntervalMs
                  << std::setw(10) << r.countAbove33ms
                  << std::setw(10) << r.countAbove100ms << std::endl;
    }
    std::cout << std::string(112, '=') << std::endl;

    // Export JSON
    std::ofstream out(jsonPath);
    if (out.is_open()) {
        out << "{\n";
        out << "  \"os_timer\": {\n";
        out << "    \"initial_resolution_ms\": " << initTimer.currentResolutionMs << ",\n";
        out << "    \"period1_resolution_ms\": " << periodActiveTimer.currentResolutionMs << ",\n";
        out << "    \"tickcount_quantum_default_ms\": " << quantumNoPeriod << ",\n";
        out << "    \"tickcount_quantum_period1_ms\": " << quantumWithPeriod << ",\n";
        out << "    \"sleep1_mean_default_ms\": " << sleepNoPeriod.meanMs << ",\n";
        out << "    \"sleep1_mean_period1_ms\": " << sleepWithPeriod.meanMs << "\n";
        out << "  },\n";
        out << "  \"benchmarks\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            out << "    {\n";
            out << "      \"name\": \"" << r.name << "\",\n";
            out << "      \"time_begin_period\": " << (r.timeBeginPeriodActive ? "true" : "false") << ",\n";
            out << "      \"iterations\": " << r.iterations << ",\n";
            out << "      \"effective_hz\": " << r.effectiveHz << ",\n";
            out << "      \"min_ms\": " << r.minIntervalMs << ",\n";
            out << "      \"max_ms\": " << r.maxIntervalMs << ",\n";
            out << "      \"mean_ms\": " << r.meanIntervalMs << ",\n";
            out << "      \"stddev_ms\": " << r.stddevIntervalMs << ",\n";
            out << "      \"p50_ms\": " << r.p50IntervalMs << ",\n";
            out << "      \"p95_ms\": " << r.p95IntervalMs << ",\n";
            out << "      \"p99_ms\": " << r.p99IntervalMs << ",\n";
            out << "      \"count_above_33ms\": " << r.countAbove33ms << ",\n";
            out << "      \"count_above_50ms\": " << r.countAbove50ms << ",\n";
            out << "      \"count_above_100ms\": " << r.countAbove100ms << "\n";
            out << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        std::cout << "[JSON Export] Saved to: " << jsonPath << std::endl;
    }

    return 0;
}
