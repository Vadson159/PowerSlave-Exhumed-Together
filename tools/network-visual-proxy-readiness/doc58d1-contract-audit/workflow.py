#!/usr/bin/env python3
"""
Automated Build & Execution Workflow for DOC-58D.1 Regression Integrity & Contract Reconciliation.
Builds the reconciled QA test suite, executes 74 scenarios, and verifies results.
All build and cache artifacts remain strictly on drive S:.
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

RECONCILED_QA_EXE = BUILD_DIR / "Release" / "test_doc58d1_reconciled.exe"

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
    target_name = "test_doc58d1_reconciled"
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
    print(f"[WORKFLOW] Built {RECONCILED_QA_EXE.name} (SHA-256: {sha256_file(RECONCILED_QA_EXE)})")

def run():
    build()

    print("\n--- [RUN] Executing DOC-58D.1 Reconciled Regression & Contract Audit Suite ---")
    res_qa = subprocess.run([str(RECONCILED_QA_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res_qa.stdout)
    if res_qa.returncode != 0:
        print(f"[ERROR] Reconciled QA suite failed with code {res_qa.returncode}")
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
