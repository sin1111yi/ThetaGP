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
# You should have received a copy of the GNU General Public
# License along with this program.
#
# If not, see <https://www.gnu.org/licenses/>.
#
# Test: Json::missingKeyCount — the key walk across arrays (host, no board)
# Target: src/utils/json/json.cpp + lib/frozen/frozen.c, host build
# Method: compiles scripts/test/test_json_missing_key_cases.cpp together with
#         the two firmware sources with g++ into a temporary directory and
#         runs it. Each case compares a body against itself (want 0)
#         beside controls that must be non-zero; the harness holds the
#         case table and its rationale.
# Expect: "N cases, 0 failed" and exit 0.
# Error:  exit 1 = a case did not match (the table names it and prints
#         want/got); exit 2 = the harness or a source did not compile, or
#         no C++ compiler was found — neither says anything about the
#         walk.
#
# Why a host test rather than another suite under scripts/test/: the board
# suites (test_cdc_protocol.py, test_profile.py) reach this code only through
# the save reply, need a flash chip, a matching manifest and a device to score
# against — and the count they can read off the wire the board derives from its
# own two bodies. This one links the firmware's own json.cpp unchanged, so it
# pins the walk itself and runs on a machine with no board attached.

import os
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

HARNESS = os.path.join(REPO_ROOT, "scripts", "test",
                       "test_json_missing_key_cases.cpp")
SOURCES = [
    os.path.join(REPO_ROOT, "src", "utils", "json", "json.cpp"),
    os.path.join(REPO_ROOT, "lib", "frozen", "frozen.c"),
]
INCLUDES = [
    os.path.join(REPO_ROOT, "src"),
    os.path.join(REPO_ROOT, "lib", "frozen"),
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

    # The binary goes to a temporary directory: a host test does not belong
    # in a firmware build directory, and nothing here is an artifact worth
    # keeping.
    with tempfile.TemporaryDirectory(prefix="thetagp_json_keywalk_") as tmp:
        binary = os.path.join(tmp, "test_json_missing_key_cases")
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
            print("FAIL: the walk does not answer what the cases ask",
                  file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
