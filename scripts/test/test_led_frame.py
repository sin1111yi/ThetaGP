#!/usr/bin/env python3
"""Check the per-frame LED renderer against the animation formula."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="thetagp_led_") as tmp:
    binary = Path(tmp) / "led_frame_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra",
        "-fsanitize=address,undefined", "-I", str(ROOT / "src"),
        str(ROOT / "scripts/test/test_led_frame_cases.cpp"),
        str(ROOT / "src/drivers/device/rgb_strip/led_effect.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
