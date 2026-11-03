#!/usr/bin/env python3
"""
M1.2b Input Cadence & Jitter QA - Audit Cross-Analysis and Accounting Verifier
Performs end-to-end 7-tier packet accounting, multi-seed repeatability analysis,
streak length analysis, and input timeout trigger breakdown.
"""

import os
import sys
import json
import csv
from pathlib import Path
import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent
AUDIT_DIR = SCRIPT_DIR / "data" / "audit_runs"

def load_all_runs():
    master_file = AUDIT_DIR / "audit_matrix_summary.json"
    if not master_file.exists():
        return None
    return json.loads(master_file.read_text(encoding="utf-8"))

def analyze_raw_csv_streaks(raw_csv_path, client_type):
    if not raw_csv_path.exists():
        return []
    
    seqs = []
    with raw_csv_path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            seq = int(r.get("client_tick", 0))
            if seq > 0:
                seqs.append(seq)
    
    if not seqs:
        return []
    
    # Missing sequence streaks
    all_seqs = set(range(seqs[0], seqs[-1] + 1))
    missing = sorted(list(all_seqs - set(seqs)))
    
    streaks = []
    cur = []
    for s in missing:
        if not cur or s == cur[-1] + 1:
            cur.append(s)
        else:
            streaks.append(len(cur))
            cur = [s]
    if cur:
        streaks.append(len(cur))
    return streaks

def main():
    runs = load_all_runs()
    if not runs:
        print("[Error] audit_matrix_summary.json not found!")
        return

    print("=" * 115)
    print("  M1.2b INPUT CADENCE & JITTER QA AUDIT ANALYSIS & 7-TIER PACKET ACCOUNTING")
    print("=" * 115)

    # 1. End-to-End Accounting Table
    print("\n--- TABLE 1: 7-TIER END-TO-END PACKET ACCOUNTING ---")
    hdr = f"{'Scenario':<28} | {'L1:Gen':<6} | {'L2:ClientUDP':<12} | {'L3:ProxyObs':<11} | {'L4:Drop':<7} | {'L5:Fwd':<7} | {'L6:HostEv':<9} | {'L7:Acc':<7} | {'Delivery%':<9}"
    print(hdr)
    print("-" * 115)

    for scen_id, data in runs.items():
        l1 = data.get("client_commands_generated", 0)
        cstats = data.get("client_stats", {})
        l2 = cstats.get("enet_udp_datagrams_sent", "-") if cstats else "-"
        
        pstats = data.get("proxy_stats", {})
        l3 = pstats.get("client_udp_total", "-") if pstats else "-"
        l4 = pstats.get("client_input_dropped", "-") if pstats else "-"
        l5 = pstats.get("client_input_forwarded", "-") if pstats else "-"

        hsum = data.get("host_summary", {})
        hacc = hsum.get("accounting", {})
        l6 = hacc.get("total_enet_events", "-")
        l7 = hacc.get("input_commands_accepted", 0)

        delivery = f"{(l7 * 100.0 / l1):.1f}%" if l1 > 0 else "-"
        print(f"{scen_id:<28} | {str(l1):<6} | {str(l2):<12} | {str(l3):<11} | {str(l4):<7} | {str(l5):<7} | {str(l6):<9} | {str(l7):<7} | {delivery:<9}")

    # 2. Cadence & Arrival Distribution Table
    print("\n--- TABLE 2: CADENCE & INTER-ARRIVAL JITTER (ms) ---")
    hdr2 = f"{'Scenario':<28} | {'Client':<9} | {'Hz':<5} | {'Min':<6} | {'p50':<6} | {'p95':<6} | {'p99':<6} | {'Max':<6} | {'Mean':<6} | {'Std':<6}"
    print(hdr2)
    print("-" * 115)

    for scen_id, data in runs.items():
        ctype = data.get("client_type", "")
        hsum = data.get("host_summary", {})
        hz = hsum.get("effective_hz", 0.0)
        pmin = hsum.get("min_interval_ms", 0.0)
        p50 = hsum.get("p50_interval_ms", 0.0)
        p95 = hsum.get("p95_interval_ms", 0.0)
        p99 = hsum.get("p99_interval_ms", 0.0)
        pmax = hsum.get("max_interval_ms", 0.0)
        mean = hsum.get("mean_interval_ms", 0.0)
        std = hsum.get("stddev_interval_ms", 0.0)
        print(f"{scen_id:<28} | {ctype:<9} | {hz:<5.1f} | {pmin:<6.1f} | {p50:<6.1f} | {p95:<6.1f} | {p99:<6.1f} | {pmax:<6.1f} | {mean:<6.1f} | {std:<6.1f}")

    # 3. Timeout Sensitivity Comparison Across Candidate Thresholds
    print("\n--- TABLE 3: INPUT TIMEOUT EVALUATION ACROSS CANDIDATE THRESHOLDS ---")
    hdr3 = f"{'Scenario':<28} | {'6t/150ms':<10} | {'8t/150ms':<10} | {'10t/150ms':<10} | {'10t/170ms':<10} | {'12t/200ms':<10}"
    print(hdr3)
    print("-" * 115)

    for scen_id, data in runs.items():
        hsum = data.get("host_summary", {})
        tcands = hsum.get("timeout_candidates", [])
        c_drops = []
        for c in tcands:
            cnt = c.get("dropout_count", 0)
            t_tick = c.get("triggered_tick_only", 0)
            t_wall = c.get("triggered_wall_only", 0)
            t_both = c.get("triggered_both", 0)
            trig_str = f"{cnt} (T:{t_tick}/W:{t_wall}/B:{t_both})" if cnt > 0 else "0"
            c_drops.append(trig_str)
        while len(c_drops) < 5:
            c_drops.append("-")
        print(f"{scen_id:<28} | {c_drops[0]:<10} | {c_drops[1]:<10} | {c_drops[2]:<10} | {c_drops[3]:<10} | {c_drops[4]:<10}")

    # 4. Multi-Seed Aggregation & Streak Length Distribution
    print("\n--- TABLE 4: MULTI-SEED REPEATABILITY SUMMARY & LOSS STREAK DISTRIBUTION ---")
    scen_groups = [
        "baseline_loss5", "precision_loss5",
        "baseline_loss10", "precision_loss10",
        "baseline_loss20", "precision_loss20",
        "baseline_jitter", "precision_jitter",
        "baseline_combined", "precision_combined"
    ]
    print(f"{'Group':<20} | {'Hz (min/avg/max)':<18} | {'p95 (min/avg/max)':<18} | {'Dropouts (min/avg/max)':<22} | {'Max Streak (pkts)'}")
    print("-" * 115)

    for grp in scen_groups:
        hz_list = []
        p95_list = []
        drop_list = []
        all_streaks = []

        for seed in [1337, 2026, 4242]:
            sid = f"{grp}_s{seed}"
            d = runs.get(sid, {})
            h = d.get("host_summary", {})
            if h:
                hz_list.append(h.get("effective_hz", 0.0))
                p95_list.append(h.get("p95_interval_ms", 0.0))
                drop_list.append(h.get("baseline_dropouts", 0))
            
            raw_csv = AUDIT_DIR / sid / "host_raw.csv"
            streaks = analyze_raw_csv_streaks(raw_csv, d.get("client_type", ""))
            all_streaks.extend(streaks)

        if hz_list:
            hz_str = f"{min(hz_list):.1f} / {np.mean(hz_list):.1f} / {max(hz_list):.1f}"
            p95_str = f"{min(p95_list):.1f} / {np.mean(p95_list):.1f} / {max(p95_list):.1f}"
            drop_str = f"{min(drop_list)} / {np.mean(drop_list):.1f} / {max(drop_list)}"
            max_stk = max(all_streaks) if all_streaks else 0
            stk_dist = {k: all_streaks.count(k) for k in sorted(set(all_streaks))} if all_streaks else {}
            print(f"{grp:<20} | {hz_str:<18} | {p95_str:<18} | {drop_str:<22} | {max_stk} (Dist: {stk_dist})")

if __name__ == "__main__":
    main()
