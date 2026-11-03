#!/usr/bin/env python3
"""
Automated Build & Execution Workflow for DOC-58D Critical Defect Remediation & Full Regression QA.
Builds the hardened regression test suite and before/after reproduction fixtures,
executes 70 scenarios, and verifies results. All build and cache artifacts remain strictly on S:.
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

REGRESSION_QA_EXE = BUILD_DIR / "Release" / "test_doc58d_regression.exe"
REPRO_REENTRANCY_BEFORE_EXE = BUILD_DIR / "Release" / "repro_reentrancy_before.exe"
REPRO_REENTRANCY_AFTER_EXE = BUILD_DIR / "Release" / "repro_reentrancy_after.exe"
REPRO_SLOT_LIMIT_BEFORE_EXE = BUILD_DIR / "Release" / "repro_slot_limit_before.exe"
REPRO_SLOT_LIMIT_AFTER_EXE = BUILD_DIR / "Release" / "repro_slot_limit_after.exe"

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
        ("test_doc58d_regression", REGRESSION_QA_EXE),
        ("repro_reentrancy_before", REPRO_REENTRANCY_BEFORE_EXE),
        ("repro_reentrancy_after", REPRO_REENTRANCY_AFTER_EXE),
        ("repro_slot_limit_before", REPRO_SLOT_LIMIT_BEFORE_EXE),
        ("repro_slot_limit_after", REPRO_SLOT_LIMIT_AFTER_EXE),
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

    print("\n--- [RUN 1/5] Executing Reentrancy Hazard Reproduction (BEFORE Fix) ---")
    res_rb = subprocess.run([str(REPRO_REENTRANCY_BEFORE_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_rb.stdout)
    if res_rb.returncode != 0:
        print(f"[ERROR] Reentrancy BEFORE repro fixture failed with code {res_rb.returncode}")
        sys.exit(res_rb.returncode)

    print("\n--- [RUN 2/5] Executing Reentrancy Guard Verification (AFTER Fix) ---")
    res_ra = subprocess.run([str(REPRO_REENTRANCY_AFTER_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_ra.stdout)
    if res_ra.returncode != 0:
        print(f"[ERROR] Reentrancy AFTER repro fixture failed with code {res_ra.returncode}")
        sys.exit(res_ra.returncode)

    print("\n--- [RUN 3/5] Executing 3-8 Player Slot Limit Reproduction (BEFORE Fix) ---")
    res_sb = subprocess.run([str(REPRO_SLOT_LIMIT_BEFORE_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_sb.stdout)
    if res_sb.returncode != 0:
        print(f"[ERROR] Slot limit BEFORE repro fixture failed with code {res_sb.returncode}")
        sys.exit(res_sb.returncode)

    print("\n--- [RUN 4/5] Executing MultiSlotGuard8 Verification (AFTER Fix) ---")
    res_sa = subprocess.run([str(REPRO_SLOT_LIMIT_AFTER_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_sa.stdout)
    if res_sa.returncode != 0:
        print(f"[ERROR] Slot limit AFTER repro fixture failed with code {res_sa.returncode}")
        sys.exit(res_sa.returncode)

    print("\n--- [RUN 5/5] Executing Full 70-Scenario Regression & Adversarial QA Suite ---")
    res_qa = subprocess.run([str(REGRESSION_QA_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_qa.stdout)
    if res_qa.returncode != 0:
        print(f"[ERROR] Regression QA suite failed with code {res_qa.returncode}")
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
