#!/usr/bin/env python3
"""
Automated Build & Execution Workflow for DOC-58B Lifecycle State Machine Remediation.
Builds the candidate state machine, runs 18 comparative scenarios and reproduction fixtures,
and verifies results.
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

COMPARATIVE_EXE = BUILD_DIR / "Release" / "test_remediation_comparative.exe"
REPRO_ADV02_EXE = BUILD_DIR / "Release" / "repro_adv02_failed_remove.exe"
REPRO_ADV11_EXE = BUILD_DIR / "Release" / "repro_adv11_sector_fallacy.exe"

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
        ("test_remediation_comparative", COMPARATIVE_EXE),
        ("repro_adv02_failed_remove", REPRO_ADV02_EXE),
        ("repro_adv11_sector_fallacy", REPRO_ADV11_EXE),
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
    if not COMPARATIVE_EXE.exists() or not REPRO_ADV02_EXE.exists() or not REPRO_ADV11_EXE.exists():
        build()

    print("\n--- [RUN] Executing ADV-02 Reproduction Fixture ---")
    res_adv02 = subprocess.run([str(REPRO_ADV02_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_adv02.stdout)
    if res_adv02.returncode != 0:
        print(f"[ERROR] ADV-02 repro fixture failed with code {res_adv02.returncode}")
        sys.exit(res_adv02.returncode)

    print("\n--- [RUN] Executing ADV-11 Reproduction Fixture ---")
    res_adv11 = subprocess.run([str(REPRO_ADV11_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_adv11.stdout)
    if res_adv11.returncode != 0:
        print(f"[ERROR] ADV-11 repro fixture failed with code {res_adv11.returncode}")
        sys.exit(res_adv11.returncode)

    print("\n--- [RUN] Executing 18-Scenario Comparative Test Suite ---")
    res_comp = subprocess.run([str(COMPARATIVE_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_comp.stdout)
    if res_comp.returncode != 0:
        print(f"[ERROR] Comparative suite failed with code {res_comp.returncode}")
        sys.exit(res_comp.returncode)

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
        print(f"Unknown action: {action}. Use 'configure', 'build', or 'run'.")
        sys.exit(1)
