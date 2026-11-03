"""
Standalone QA Timing Analyzer: Windows Timer Resolution & pumpNetwork Simulation
Author: Senior C++ QA Automation Engineer / Windows Timing Analyst
Target: PowerSlave Exhumed Together (P1 Investigation)
"""

import ctypes
import time
import sys

winmm = ctypes.windll.winmm
kernel32 = ctypes.windll.kernel32

def measure_sleep_quantum(iterations=20):
    durations = []
    for _ in range(iterations):
        t0 = time.perf_counter()
        kernel32.Sleep(1)
        t1 = time.perf_counter()
        durations.append((t1 - t0) * 1000.0) # in ms
    return durations

def simulate_pump_network(duration_ms=300):
    start = time.perf_counter()
    iterations = 0
    while True:
        # simulate host.poll(1) + 8 * client.poll(1) non-blocking execution (~0.2ms)
        kernel32.Sleep(1)
        iterations += 1
        elapsed = (time.perf_counter() - start) * 1000.0
        if elapsed >= duration_ms:
            break
    return iterations, elapsed

def main():
    print("=" * 60)
    print("QA Timing Analyzer: Windows Timer Quantum & pumpNetwork")
    print("=" * 60)

    # 1. Baseline: Default Windows Timer Resolution
    print("\n[Phase 1] Measuring Default Windows Timer Resolution (Pristine)...")
    samples_default = measure_sleep_quantum()
    avg_default = sum(samples_default) / len(samples_default)
    min_default = min(samples_default)
    max_default = max(samples_default)
    print(f"  Sleep(1ms) Samples (ms): {[round(x, 2) for x in samples_default[:8]]}...")
    print(f"  Default Sleep(1ms) Avg: {avg_default:.2f} ms (Min: {min_default:.2f} ms, Max: {max_default:.2f} ms)")

    iters_default, elapsed_default = simulate_pump_network(300)
    print(f"  In 300ms window: {iters_default} iterations completed (Total elapsed: {elapsed_default:.2f} ms)")
    print(f"  Effective iteration cadence: {elapsed_default / iters_default:.2f} ms/iteration")

    # 2. Timer Fixture: timeBeginPeriod(1)
    print("\n[Phase 2] Measuring With timeBeginPeriod(1) (Timer Fixture)...")
    res = winmm.timeBeginPeriod(1)
    try:
        samples_1ms = measure_sleep_quantum()
        avg_1ms = sum(samples_1ms) / len(samples_1ms)
        min_1ms = min(samples_1ms)
        max_1ms = max(samples_1ms)
        print(f"  Sleep(1ms) Samples (ms): {[round(x, 2) for x in samples_1ms[:8]]}...")
        print(f"  1ms Fixture Sleep(1ms) Avg: {avg_1ms:.2f} ms (Min: {min_1ms:.2f} ms, Max: {max_1ms:.2f} ms)")

        iters_1ms, elapsed_1ms = simulate_pump_network(300)
        print(f"  In 300ms window: {iters_1ms} iterations completed (Total elapsed: {elapsed_1ms:.2f} ms)")
        print(f"  Effective iteration cadence: {elapsed_1ms / iters_1ms:.2f} ms/iteration")
    finally:
        winmm.timeEndPeriod(1)

    print("\n[Comparison Summary]")
    print(f"  Loop iteration throughput boost: {iters_1ms / iters_default:.2f}x")
    print(f"  Packet exchange opportunities in 300ms: {iters_default} vs {iters_1ms}")
    print("=" * 60)

if __name__ == "__main__":
    main()
