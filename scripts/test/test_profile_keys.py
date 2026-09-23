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
# Test: the key table and a profile body name the same field the same way
# Target: src/gamepad/config/config_store.cpp + key_table.cpp
#         + src/utils/json/json.cpp + lib/frozen/frozen.c, host build
# Method: compiles scripts/test/test_profile_keys_cases.cpp together with those
#         firmware sources with g++ into a temporary directory and runs it.
# Expect: "N checks, 0 failed" and exit 0.
# Error:  exit 1 = a check did not hold (the case names it); exit 2 = the
#         harness or a source did not compile, or no C++ compiler was found.
#
# Why a host test: the board suites reach a profile body only through the save
# and load replies, and need a flash chip, a matching manifest and a device to
# score against. This links the firmware's own reader and writer unchanged, so
# it pins the two names a key carries — and the fact that a body written under
# the table's paths is read back by them — on a machine with no board.

import os
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

HARNESS = os.path.join(REPO_ROOT, "scripts", "test",
                       "test_profile_keys_cases.cpp")
SOURCES = [
    os.path.join(REPO_ROOT, "src", "gamepad", "config", "config_store.cpp"),
    os.path.join(REPO_ROOT, "src", "gamepad", "config", "key_table.cpp"),
    os.path.join(REPO_ROOT, "src", "utils", "json", "json.cpp"),
    os.path.join(REPO_ROOT, "lib", "frozen", "frozen.c"),
]
INCLUDES = [
    os.path.join(REPO_ROOT, "src"),
    os.path.join(REPO_ROOT, "lib", "frozen"),
    # build_info.h carries the target's platform header and its board config;
    # the board headers are checked in with the board's own sources.
    os.path.join(REPO_ROOT, "platform", "STM32", "system"),
    os.path.join(REPO_ROOT, "configs", "BoringTechH743"),
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
    with tempfile.TemporaryDirectory(prefix="thetagp_profile_keys_") as tmp:
        binary = os.path.join(tmp, "test_profile_keys_cases")

        # The board's config header includes the CMSIS device header, which the
        # firmware build takes from the toolchain rather than this repository.
        # The test reads no device register, so an empty stand-in for that one
        # header keeps the compile on the host: the target macro that would pull
        # the device's own headers is not defined here, and a source that did
        # need them would not compile rather than be tested against nothing.
        with open(os.path.join(tmp, "stm32h7xx.h"), "w") as stub:
            stub.write("/* Stand-in for the CMSIS device header (host test). */\n")

        # A section attribute the target's own build defines and a host build
        # does not. The buffer it marks is declared, never touched here.
        cmd = [compiler, "-std=gnu++20", "-Wall", "-Wextra", "-O1",
               "-DCOMMON_ZERO_INIT=", "-I", tmp]
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
            print("FAIL: the table and a profile body do not agree on the names",
                  file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
