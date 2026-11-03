#!/usr/bin/env python3
"""
Automated Build & Execution Workflow for DOC-58C Native Integration Contract QA.
Builds the contract QA test suite and reproduction fixtures, executes 32 scenarios,
and verifies results. All build and cache artifacts remain on S:.
"""
import os
import sys
import json
import hashlib
import subprocess
from pathlib import Path

LAB_DIR = Path(__file__).resolve().parent
ROOT_DIR = LAB_DIR.parents[2]
BUILD_DIR = LAB_DIR / "build"
CMAKE_EXE = Path(r"C:\Program Files\CMake\bin\cmake.exe")

CONTRACT_QA_EXE = BUILD_DIR / "Release" / "test_integration_contracts.exe"
REPRO_REENTRANCY_EXE = BUILD_DIR / "Release" / "repro_reentrancy_hazard.exe"
REPRO_SLOT_LIMIT_EXE = BUILD_DIR / "Release" / "repro_snapshot_slot_limit.exe"

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def configure():
    print(f"[WORKFLOW] Configuring CMake in {BUILD_DIR}...")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(CMAKE_EXE),
        "-S", str(LAB_DIR),
        "-B", str(BUILD_DIR),
        "-G", "Visual Studio 17 2022",
        "-A", "x64"
    ]
    res = subprocess.run(cmd, cwd=str(LAB_DIR), capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] CMake configure failed:\n{res.stderr}")
        sys.exit(1)
    print("[WORKFLOW] CMake configuration successful.")

def build():
    configure()
    targets = [
        ("test_integration_contracts", CONTRACT_QA_EXE),
        ("repro_reentrancy_hazard", REPRO_REENTRANCY_EXE),
        ("repro_snapshot_slot_limit", REPRO_SLOT_LIMIT_EXE),
    ]
    for target_name, exe_path in targets:
        print(f"[WORKFLOW] Building target: {target_name}...")
        cmd = [
            str(CMAKE_EXE),
            "--build", str(BUILD_DIR),
            "--config", "Release",
            "--target", target_name
        ]
        res = subprocess.run(cmd, cwd=str(LAB_DIR), capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[ERROR] Build failed for {target_name}:\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}")
            sys.exit(1)
        print(f"[WORKFLOW] Built {exe_path.name} (SHA-256: {sha256_file(exe_path)})")

def run():
    build()

    print("\n--- [RUN] Executing Reentrancy Hazard Reproduction Fixture ---")
    res_repro = subprocess.run([str(REPRO_REENTRANCY_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_repro.stdout)
    if res_repro.returncode != 0:
        print(f"[ERROR] Reentrancy repro fixture failed with code {res_repro.returncode}")
        sys.exit(res_repro.returncode)

    print("\n--- [RUN] Executing 3-8 Player Slot Limit Reproduction Fixture ---")
    res_slot = subprocess.run([str(REPRO_SLOT_LIMIT_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_slot.stdout)
    if res_slot.returncode != 0:
        print(f"[ERROR] Slot limit repro fixture failed with code {res_slot.returncode}")
        sys.exit(res_slot.returncode)

    print("\n--- [RUN] Executing Full 32-Scenario Integration Contract QA Suite ---")
    res_qa = subprocess.run([str(CONTRACT_QA_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_qa.stdout)
    if res_qa.returncode != 0:
        print(f"[ERROR] Contract QA suite failed with code {res_qa.returncode}")
        sys.exit(res_qa.returncode)

    results_json = LAB_DIR / "TEST_RESULTS.json"
    if results_json.exists():
        print(f"[WORKFLOW] Successfully generated {results_json.name} ({results_json.stat().st_size} bytes)")
    else:
        print("[ERROR] TEST_RESULTS.json was not generated!")
        sys.exit(1)

if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else "run"
    if action == "configure":
        configure()
    elif action == "build":
        build()
    elif action == "run":
        run()
    else:
        print(f"Unknown action: {action}")
        sys.exit(1)
