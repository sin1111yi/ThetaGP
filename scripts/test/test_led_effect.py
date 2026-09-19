#!/usr/bin/env python3
# This file is a part of ThetaGP.
#
# ThetaGP is free software: you can redistribute it
# and/or modify it under the terms of the GNU General
# Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# ThetaGP is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General
# Public License along with this program.
#
# If not, see <https://www.gnu.org/licenses/>.
#
# Test: Led::ledEffectRender and Led::ledEffectAdvance (host, no board)
# Target: src/led/led_effect.cpp, host build
# Method: compiles scripts/test/led_effect_cases.cpp together with the
#         firmware's own led_effect.cpp with g++ into a temporary directory and
#         runs it. The harness holds the cases: the colour frame 0 LED 0 starts
#         at, the hue step between neighbouring LEDs and between neighbouring
#         frames, the hue a full turn comes back to, what keyCount 0 / 1 / 32
#         write and what they leave alone, and the frame the 100 Hz tick's
#         accumulator advances to.
# Expect: "N checks, 0 failed" and exit 0.
# Error:  exit 1 = a check did not match (the harness names it and prints
#         want/got); exit 2 = the harness or the source did not compile, or no
#         C++ compiler was found — neither says anything about the render.
#
# Why a host test: the render is pure by construction — no global state, no
# hardware, no allocation — and led_effect.cpp carries no platform header, so
# the host can link the firmware's own source unchanged. The board suites under
# scripts/test/ reach the strip only through a driver that does not exist yet
# (the push layer), and none of them can say anything about a hue.
#
# Why the source is compiled and not re-implemented here: a case that carries
# its own copy of the render says what that copy does. The colour it reads back
# out of the render is matched against the wheel in the harness, which is
# written a second way, so the two are not the same code.

import os
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

HARNESS = os.path.join(REPO_ROOT, "scripts", "test", "led_effect_cases.cpp")
SOURCES = [
    os.path.join(REPO_ROOT, "src", "led", "led_effect.cpp"),
]
INCLUDES = [
    os.path.join(REPO_ROOT, "src"),
]


def main():
    compiler = shutil.which("g++") or shutil.which("c++")
    if compiler is None:
        print("ERROR: no C++ compiler found (looked for g++, c++)",
              file=sys.stderr)
        return 2

    for path in [HARNESS] + SOURCES:
        if not os.path.isfile(path):
            print(f"ERROR: {path} is not there", file=sys.stderr)
            return 2

    # The binary goes to a temporary directory: a host test does not belong in
    # a firmware build directory, and nothing here is an artifact worth keeping.
    with tempfile.TemporaryDirectory(prefix="thetagp_led_effect_") as tmp:
        binary = os.path.join(tmp, "led_effect_cases")
        cmd = [compiler, "-std=gnu++20", "-Wall", "-Wextra", "-O1"]
        for inc in INCLUDES:
            cmd += ["-I", inc]
        cmd += [HARNESS] + SOURCES + ["-o", binary]

        print("$ " + " ".join(cmd))
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.stdout:
            print(build.stdout, end="")
        if build.stderr:
            print(build.stderr, end="", file=sys.stderr)
        if build.returncode != 0:
            print("ERROR: the harness did not compile "
                  f"(exit {build.returncode})", file=sys.stderr)
            return 2

        run = subprocess.run([binary], capture_output=True, text=True)
        if run.stdout:
            print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="", file=sys.stderr)
        if run.returncode != 0:
            print("FAIL: the render does not answer what the cases ask",
                  file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
