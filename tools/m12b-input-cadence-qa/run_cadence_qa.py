#!/usr/bin/env python3
"""
M1.2b Input Cadence & Jitter QA Suite (DOC-45 P2-2 Investigation)
Orchestrates automated end-to-end tests for baseline client, precision 60 Hz generator,
packet loss, jitter, and timeout sensitivity.
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
DATA_DIR = SCRIPT_DIR / "data"

NATIVE_CLIENT_EXE = ROOT_DIR / "experiments" / "m1" / "network_input" / "build" / "Release" / "m12a_native_client.exe"
CADENCE_HOST_EXE = BIN_DIR / "cadence_qa_host.exe"
PRECISION_CLIENT_EXE = BIN_DIR / "precision_client.exe"
PROXY_EXE = BIN_DIR / "net_impairment_proxy.exe"
SCHEDULER_BENCH_EXE = BIN_DIR / "scheduler_bench.exe"

def ensure_dirs():
    DATA_DIR.mkdir(parents=True, exist_ok=True)

def run_scenario(name, client_type, loss_percent=0.0, latency_ms=0, jitter_ms=0, seed=1337, duration=10.0):
    print(f"\n{'='*80}\n  RUNNING SCENARIO: {name}\n  Client: {client_type}, Loss: {loss_percent}%, Latency: {latency_ms}ms, Jitter: {jitter_ms}ms\n{'='*80}")

    use_proxy = (loss_percent > 0.0 or latency_ms > 0 or jitter_ms > 0)
    host_port = 28746 if use_proxy else 28745
    client_port = 28745

    summary_file = DATA_DIR / f"{name}_summary.json"
    raw_csv = DATA_DIR / f"{name}_raw.csv"

    # Start QA Host
    host_cmd = [
        str(CADENCE_HOST_EXE),
        "--port", str(host_port),
        "--duration", str(duration + 4.0),
        "--summary", str(summary_file),
        "--raw", str(raw_csv)
    ]
    print(f"[Host] Starting QA Host on port {host_port}...")
    host_proc = subprocess.Popen(host_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.5)

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
            "--duration", str(duration + 3.0)
        ]
        print(f"[Proxy] Starting Impairment Proxy on {client_port} -> {host_port}...")
        proxy_proc = subprocess.Popen(proxy_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.5)

    client_run_dir = DATA_DIR / f"run_{name}"
    if client_run_dir.exists():
        shutil.rmtree(client_run_dir)
    client_run_dir.mkdir(parents=True, exist_ok=True)

    client_proc = None
    if client_type == "baseline":
        ctrl_file = client_run_dir / "client_control.txt"
        ctrl_file.write_text("hold\n", encoding="ascii")
        print(f"[Client] Launching unmodified native client ({NATIVE_CLIENT_EXE})...")
        client_proc = subprocess.Popen([str(NATIVE_CLIENT_EXE), str(client_run_dir)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        # Run for specified duration
        time.sleep(duration)
        print("[Client] Sending 'quit' command...")
        ctrl_file.write_text("quit\n", encoding="ascii")
        try:
            client_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            client_proc.kill()

    elif client_type == "precision":
        sent_csv = DATA_DIR / f"{name}_sent.csv"
        prec_cmd = [
            str(PRECISION_CLIENT_EXE),
            "--port", str(client_port),
            "--rate", "60.0",
            "--duration", str(duration),
            "--raw-out", str(sent_csv)
        ]
        print(f"[Client] Launching Precision 60 Hz generator client...")
        client_proc = subprocess.Popen(prec_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        client_proc.wait(timeout=duration + 5)

    if proxy_proc:
        try:
            proxy_proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proxy_proc.kill()

    try:
        host_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        host_proc.kill()

    # Read host summary
    if summary_file.exists():
        try:
            data = json.loads(summary_file.read_text(encoding="utf-8"))
            print(f"[Result] Total Packets: {data.get('total_packets')}, Effective Hz: {data.get('effective_hz'):.1f}, Dropouts: {data.get('baseline_dropouts')}")
            return data
        except Exception as e:
            print(f"[Error] Failed to parse summary: {e}")
    else:
        print(f"[Error] Summary file not found: {summary_file}")
    return None

def main():
    ensure_dirs()
    print("================================================================================")
    print("  M1.2b INPUT CADENCE & JITTER QA SUITE ORCHESTRATOR")
    print("================================================================================")

    results = {}

    # Scenario 1: Baseline Native Client on Clean Loopback
    results["scen1_baseline_clean"] = run_scenario("scen1_baseline_clean", "baseline", duration=10.0)

    # Scenario 2: Precision 60 Hz Client on Clean Loopback
    results["scen2_precision_clean"] = run_scenario("scen2_precision_clean", "precision", duration=10.0)

    # Scenario 3: Baseline Client with 5%, 10%, 20% Packet Loss
    results["scen3_baseline_loss5"] = run_scenario("scen3_baseline_loss5", "baseline", loss_percent=5.0, seed=1337, duration=10.0)
    results["scen3_baseline_loss10"] = run_scenario("scen3_baseline_loss10", "baseline", loss_percent=10.0, seed=1337, duration=10.0)
    results["scen3_baseline_loss20"] = run_scenario("scen3_baseline_loss20", "baseline", loss_percent=20.0, seed=1337, duration=10.0)

    # Scenario 4: Precision 60 Hz Client with 5%, 10%, 20% Packet Loss
    results["scen4_precision_loss5"] = run_scenario("scen4_precision_loss5", "precision", loss_percent=5.0, seed=1337, duration=10.0)
    results["scen4_precision_loss10"] = run_scenario("scen4_precision_loss10", "precision", loss_percent=10.0, seed=1337, duration=10.0)
    results["scen4_precision_loss20"] = run_scenario("scen4_precision_loss20", "precision", loss_percent=20.0, seed=1337, duration=10.0)

    # Scenario 5: Baseline Client with Jitter (20ms latency, 15ms jitter)
    results["scen5_baseline_jitter"] = run_scenario("scen5_baseline_jitter", "baseline", latency_ms=20, jitter_ms=15, seed=1337, duration=10.0)

    # Scenario 6: Precision 60 Hz Client with Jitter (20ms latency, 15ms jitter)
    results["scen6_precision_jitter"] = run_scenario("scen6_precision_jitter", "precision", latency_ms=20, jitter_ms=15, seed=1337, duration=10.0)

    # Scenario 7: Stress Combined (10% loss, 30ms latency, 20ms jitter)
    results["scen7_baseline_combined"] = run_scenario("scen7_baseline_combined", "baseline", loss_percent=10.0, latency_ms=30, jitter_ms=20, seed=1337, duration=10.0)
    results["scen7_precision_combined"] = run_scenario("scen7_precision_combined", "precision", loss_percent=10.0, latency_ms=30, jitter_ms=20, seed=1337, duration=10.0)

    # Save aggregated results
    agg_file = DATA_DIR / "aggregated_qa_results.json"
    agg_file.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"\n[Done] Aggregated QA results saved to: {agg_file}")

if __name__ == "__main__":
    main()
