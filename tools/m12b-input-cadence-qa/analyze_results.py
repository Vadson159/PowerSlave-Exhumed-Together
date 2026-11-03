#!/usr/bin/env python3
"""
Detailed cross-analysis of client send intervals vs host receive intervals,
timeout margin calculations, and summary table generator for REPORT.md.
"""

import json
import csv
from pathlib import Path
import numpy as np

DATA_DIR = Path(__file__).resolve().parent / "data"

def analyze_csv(csv_path):
    if not csv_path.exists():
        return None
    deltas = []
    with csv_path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            d = float(row.get("delta_ms", 0.0))
            if d > 0:
                deltas.append(d)
    if not deltas:
        return None
    arr = np.array(deltas)
    return {
        "count": len(arr),
        "mean": float(np.mean(arr)),
        "std": float(np.std(arr)),
        "min": float(np.min(arr)),
        "max": float(np.max(arr)),
        "p50": float(np.percentile(arr, 50)),
        "p95": float(np.percentile(arr, 95)),
        "p99": float(np.percentile(arr, 99)),
        "effective_hz": float(1000.0 / np.mean(arr)) if np.mean(arr) > 0 else 0.0
    }

def analyze_client_jsonl(jsonl_path):
    if not jsonl_path.exists():
        return None
    # Parse client.jsonl sent records
    sent_times = []
    with jsonl_path.open("r", encoding="utf-8") as f:
        for line in f:
            if not line.strip(): continue
            try:
                rec = json.loads(line)
                if rec.get("event") == "sent":
                    # UTC timestamp
                    sent_times.append(rec)
            except:
                pass
    return len(sent_times)

def main():
    print("="*80)
    print("  M1.2b INPUT CADENCE & JITTER DETAILED CROSS-ANALYSIS")
    print("="*80)

    # 1. Load aggregated results
    agg_path = DATA_DIR / "aggregated_qa_results.json"
    with agg_path.open("r", encoding="utf-8") as f:
        agg = json.load(f)

    # 2. Load scheduler bench
    bench_path = DATA_DIR / "scheduler_bench.json"
    with bench_path.open("r", encoding="utf-8") as f:
        bench = json.load(f)

    # Print summary table
    print("\n--- SCHEDULER MICRO-BENCHMARK (AUTONOMOUS REPRODUCTION) ---")
    print(f"{'Algorithm':<45} | {'Timer':<8} | {'Hz':<6} | {'Mean(ms)':<8} | {'p50(ms)':<8} | {'p95(ms)':<8} | {'>33ms':<6} | {'>100ms':<6}")
    print("-" * 115)
    for b in bench["benchmarks"]:
        timer_str = "1.0ms" if b["time_begin_period"] else "Default"
        print(f"{b['name']:<45} | {timer_str:<8} | {b['effective_hz']:<6.1f} | {b['mean_ms']:<8.2f} | {b['p50_ms']:<8.2f} | {b['p95_ms']:<8.2f} | {b['count_above_33ms']:<6} | {b['count_above_100ms']:<6}")

    print("\n--- END-TO-END NETWORK TEST MATRIX SUMMARY ---")
    print(f"{'Scenario':<25} | {'Client':<10} | {'Loss':<5} | {'Jitter':<8} | {'Packets':<7} | {'Hz':<6} | {'p50(ms)':<8} | {'p95(ms)':<8} | {'Max(ms)':<8} | {'Dropouts':<8}")
    print("-" * 115)

    scen_details = [
        ("scen1_baseline_clean", "Baseline", "0%", "0ms"),
        ("scen2_precision_clean", "Precision", "0%", "0ms"),
        ("scen3_baseline_loss5", "Baseline", "5%", "0ms"),
        ("scen3_baseline_loss10", "Baseline", "10%", "0ms"),
        ("scen3_baseline_loss20", "Baseline", "20%", "0ms"),
        ("scen4_precision_loss5", "Precision", "5%", "0ms"),
        ("scen4_precision_loss10", "Precision", "10%", "0ms"),
        ("scen4_precision_loss20", "Precision", "20%", "0ms"),
        ("scen5_baseline_jitter", "Baseline", "0%", "±15ms"),
        ("scen6_precision_jitter", "Precision", "0%", "±15ms"),
        ("scen7_baseline_combined", "Baseline", "10%", "±20ms"),
        ("scen7_precision_combined", "Precision", "10%", "±20ms"),
    ]

    for key, ctype, loss, jit in scen_details:
        s = agg.get(key, {})
        pkts = s.get("total_packets", 0)
        hz = s.get("effective_hz", 0.0)
        p50 = s.get("p50_interval_ms", 0.0)
        p95 = s.get("p95_interval_ms", 0.0)
        max_ms = s.get("max_interval_ms", 0.0)
        drops = s.get("baseline_dropouts", 0)
        print(f"{key:<25} | {ctype:<10} | {loss:<5} | {jit:<8} | {pkts:<7} | {hz:<6.1f} | {p50:<8.2f} | {p95:<8.2f} | {max_ms:<8.2f} | {drops:<8}")

if __name__ == "__main__":
    main()
