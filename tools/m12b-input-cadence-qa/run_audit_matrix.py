#!/usr/bin/env python3
"""
M1.2b Input Cadence & Jitter QA Suite - Audit Matrix Runner
Executes multi-seed repeatability matrix across baseline and precision clients,
generating isolated evidence in data/audit_runs/.
"""

import os
import sys
import time
import json
import subprocess
import shutil
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent.parent
BIN_DIR = SCRIPT_DIR / "build" / "Release"
AUDIT_DIR = SCRIPT_DIR / "data" / "audit_runs"

NATIVE_CLIENT_EXE = ROOT_DIR / "experiments" / "m1" / "network_input" / "build" / "Release" / "m12a_native_client.exe"
CADENCE_HOST_EXE = BIN_DIR / "cadence_qa_host.exe"
PRECISION_CLIENT_EXE = BIN_DIR / "precision_client.exe"
PROXY_EXE = BIN_DIR / "net_impairment_proxy.exe"

SEEDS = [1337, 2026, 4242]
RUN_DURATION = 6.0

def run_single_scenario(scen_id, client_type, loss_percent=0.0, latency_ms=0, jitter_ms=0, seed=1337, duration=RUN_DURATION):
    run_dir = AUDIT_DIR / scen_id
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)

    use_proxy = (loss_percent > 0.0 or latency_ms > 0 or jitter_ms > 0)
    host_port = 28746 if use_proxy else 28745
    client_port = 28745

    host_summary_file = run_dir / "host_summary.json"
    host_raw_csv = run_dir / "host_raw.csv"
    proxy_stats_file = run_dir / "proxy_stats.json"
    client_stats_file = run_dir / "client_stats.json"
    client_sent_csv = run_dir / "client_sent.csv"

    # 1. Start Host
    host_cmd = [
        str(CADENCE_HOST_EXE),
        "--port", str(host_port),
        "--duration", str(duration + 4.0),
        "--summary", str(host_summary_file),
        "--raw", str(host_raw_csv)
    ]
    host_proc = subprocess.Popen(host_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.5)

    # 2. Start Proxy if needed
    proxy_proc = None
    if use_proxy:
        proxy_cmd = [
            str(PROXY_EXE),
            "--listen", str(client_port),
            "--target", str(host_port),
            "--drop", str(loss_percent),
            "--latency", str(latency_ms),
            "--jitter", str(jitter_ms),
            "--seed", str(seed),
            "--duration", str(duration + 3.0),
            "--stats-out", str(proxy_stats_file)
        ]
        proxy_proc = subprocess.Popen(proxy_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.5)

    # 3. Start Client
    client_proc = None
    client_commands_generated = 0
    client_active_duration_sec = 0.0

    if client_type == "baseline":
        client_run_dir = run_dir / "client_working"
        client_run_dir.mkdir(exist_ok=True)
        ctrl_file = client_run_dir / "client_control.txt"
        ctrl_file.write_text("hold\n", encoding="ascii")

        t_start = time.perf_counter()
        client_proc = subprocess.Popen([str(NATIVE_CLIENT_EXE), str(client_run_dir)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        time.sleep(duration)
        ctrl_file.write_text("quit\n", encoding="ascii")
        try:
            client_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            client_proc.kill()
        t_end = time.perf_counter()
        client_active_duration_sec = t_end - t_start

        # Read client.jsonl to count sent commands
        jsonl_path = client_run_dir / "client.jsonl"
        if jsonl_path.exists():
            with jsonl_path.open("r", encoding="utf-8") as jf:
                for line in jf:
                    if '"event":"sent"' in line:
                        client_commands_generated += 1

    elif client_type == "precision":
        prec_cmd = [
            str(PRECISION_CLIENT_EXE),
            "--port", str(client_port),
            "--rate", "60.0",
            "--duration", str(duration),
            "--raw-out", str(client_sent_csv),
            "--stats-out", str(client_stats_file)
        ]
        t_start = time.perf_counter()
        client_proc = subprocess.Popen(prec_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        client_proc.wait(timeout=duration + 5)
        t_end = time.perf_counter()
        client_active_duration_sec = t_end - t_start

        if client_stats_file.exists():
            try:
                cdata = json.loads(client_stats_file.read_text(encoding="utf-8"))
                client_commands_generated = cdata.get("commands_generated", 0)
            except:
                pass

    if proxy_proc:
        try:
            proxy_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proxy_proc.kill()

    try:
        host_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        host_proc.kill()

    # Consolidate Run Metadata
    meta = {
        "scenario_id": scen_id,
        "client_type": client_type,
        "loss_percent": loss_percent,
        "latency_ms": latency_ms,
        "jitter_ms": jitter_ms,
        "seed": seed if use_proxy else None,
        "duration_configured": duration,
        "client_active_duration_sec": client_active_duration_sec,
        "client_commands_generated": client_commands_generated
    }

    if host_summary_file.exists():
        try:
            hdata = json.loads(host_summary_file.read_text(encoding="utf-8"))
            meta["host_summary"] = hdata
        except Exception as e:
            meta["host_summary_error"] = str(e)

    if proxy_stats_file.exists():
        try:
            pdata = json.loads(proxy_stats_file.read_text(encoding="utf-8"))
            meta["proxy_stats"] = pdata
        except Exception as e:
            meta["proxy_stats_error"] = str(e)

    if client_stats_file.exists():
        try:
            cdata = json.loads(client_stats_file.read_text(encoding="utf-8"))
            meta["client_stats"] = cdata
        except Exception as e:
            meta["client_stats_error"] = str(e)

    (run_dir / "run_meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    
    # Progress line
    pkts = meta.get("host_summary", {}).get("total_packets", 0)
    hz = meta.get("host_summary", {}).get("effective_hz", 0.0)
    drops = meta.get("host_summary", {}).get("baseline_dropouts", 0)
    print(f"  [{scen_id:<35}] Sent: {client_commands_generated:<4} | Recv: {pkts:<4} | Rate: {hz:<4.1f} Hz | Dropouts(6t/150ms): {drops}")
    return meta

def main():
    AUDIT_DIR.mkdir(parents=True, exist_ok=True)
    print("=" * 95)
    print("  M1.2b INPUT CADENCE & JITTER QA AUDIT MATRIX RUNNER")
    print(f"  Target Dir: {AUDIT_DIR}")
    print(f"  Seeds: {SEEDS}, Duration per run: {RUN_DURATION}s")
    print("=" * 95)

    all_results = {}

    # 1. Clean Loopback Benchmarks
    print("\n--- 1. CLEAN LOOPBACK BENCHMARKS ---")
    all_results["clean_baseline"] = run_single_scenario("clean_baseline", "baseline", duration=RUN_DURATION)
    all_results["clean_precision"] = run_single_scenario("clean_precision", "precision", duration=RUN_DURATION)

    # 2. Packet Loss Matrix (5%, 10%, 20%) x Seeds
    for loss in [5.0, 10.0, 20.0]:
        print(f"\n--- 2. PACKET LOSS {loss:.0f}% MATRIX ---")
        for seed in SEEDS:
            b_id = f"baseline_loss{int(loss)}_s{seed}"
            p_id = f"precision_loss{int(loss)}_s{seed}"
            all_results[b_id] = run_single_scenario(b_id, "baseline", loss_percent=loss, seed=seed, duration=RUN_DURATION)
            all_results[p_id] = run_single_scenario(p_id, "precision", loss_percent=loss, seed=seed, duration=RUN_DURATION)

    # 3. Jitter Matrix (20ms latency, ±15ms jitter) x Seeds
    print("\n--- 3. JITTER (20ms lat, ±15ms jit) MATRIX ---")
    for seed in SEEDS:
        b_id = f"baseline_jitter_s{seed}"
        p_id = f"precision_jitter_s{seed}"
        all_results[b_id] = run_single_scenario(b_id, "baseline", latency_ms=20, jitter_ms=15, seed=seed, duration=RUN_DURATION)
        all_results[p_id] = run_single_scenario(p_id, "precision", latency_ms=20, jitter_ms=15, seed=seed, duration=RUN_DURATION)

    # 4. Stress Combined (10% loss, 30ms latency, ±20ms jitter) x Seeds
    print("\n--- 4. STRESS COMBINED (10% loss, 30ms lat, ±20ms jit) MATRIX ---")
    for seed in SEEDS:
        b_id = f"baseline_combined_s{seed}"
        p_id = f"precision_combined_s{seed}"
        all_results[b_id] = run_single_scenario(b_id, "baseline", loss_percent=10.0, latency_ms=30, jitter_ms=20, seed=seed, duration=RUN_DURATION)
        all_results[p_id] = run_single_scenario(p_id, "precision", loss_percent=10.0, latency_ms=30, jitter_ms=20, seed=seed, duration=RUN_DURATION)

    master_summary_file = AUDIT_DIR / "audit_matrix_summary.json"
    master_summary_file.write_text(json.dumps(all_results, indent=2), encoding="utf-8")
    print("\n" + "=" * 95)
    print(f"  Audit matrix completed. Master summary written to: {master_summary_file}")
    print("=" * 95)

if __name__ == "__main__":
    main()
