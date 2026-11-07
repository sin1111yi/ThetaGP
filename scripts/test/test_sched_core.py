#!/usr/bin/env python3
"""Compile the scheduler core and run its cases against a simulated clock."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="thetagp_sched_") as tmp:
    binary = Path(tmp) / "sched_core_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra", "-O1",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(ROOT / "src"),
        str(ROOT / "scripts/test/test_sched_core_cases.cpp"),
        str(ROOT / "src/sched/core.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
