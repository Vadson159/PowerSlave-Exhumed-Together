#!/usr/bin/env python3
"""
Adversarial Test Workflow for DOC-58B Preparation: Network Visual Proxy Readiness & QA.
Builds and executes the adversarial test harness, producing TEST_RESULTS.json.
"""
import os
import sys
import json
import hashlib
import datetime
import subprocess
from pathlib import Path

LAB_DIR = Path(__file__).resolve().parent
ROOT_DIR = LAB_DIR.parents[2]
BUILD_DIR = LAB_DIR / "build"
RELEASE_EXE = BUILD_DIR / "Release" / "test_adversarial_lifecycle.exe"
CMAKE_EXE = Path(r"C:\Program Files\CMake\bin\cmake.exe")

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
    print(f"[WORKFLOW] Building {RELEASE_EXE}...")
    cmd = [
        str(CMAKE_EXE),
        "--build", str(BUILD_DIR),
        "--config", "Release",
        "--target", "test_adversarial_lifecycle"
    ]
    res = subprocess.run(cmd, cwd=str(LAB_DIR), capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[ERROR] CMake build failed:\n{res.stderr}")
        sys.exit(1)
    print(f"[WORKFLOW] Build complete: {RELEASE_EXE} (SHA-256: {sha256_file(RELEASE_EXE)})")

def run():
    if not RELEASE_EXE.exists():
        build()
    print(f"[WORKFLOW] Running adversarial test harness: {RELEASE_EXE}...")
    res = subprocess.run([str(RELEASE_EXE)], cwd=str(LAB_DIR), capture_output=True, text=True)
    print(res.stdout)
    if res.stderr:
        print(res.stderr, file=sys.stderr)
    sys.exit(res.returncode)

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
