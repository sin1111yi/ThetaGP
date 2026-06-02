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
# Test: CDC JSON protocol — Stage 1 (sys domain) + Stage 2 (test mode)
# Target: CDC ACM virtual serial port (ttyACM1) on firmware built with
#         -DTHETAGP_ENABLE_TEST_API=ON
# Method: Sends JSON commands via raw serial, parses JSON responses.
#         Tests sys.ping, sys.get_fw_version, unknown-cmd error handling,
#         test.set_mode/get_mode, inject_gamepad_state, inject_hid_report,
#         set_override, clear_inject, get_status, get_history, clear_history,
#         reset, and mode persistence after reset.
# Expect: All commands return status:"ok" with valid fields. Reset restores
#         PASS_THRU mode. Unknown commands return status:"error" error_code:1.
# Error:  Timeout returns None and marks that test case as FAIL.
#         Unexpected cmd mismatch or garbage data are logged and retried.

import os, termios, time, select, json

PORT = "/dev/ttyACM1"


def open_serial(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    for a in range(4):
        if a == 0:
            attrs[a] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                          termios.ISTRIP | termios.INLCR | termios.IGNCR |
                          termios.ICRNL | termios.IXON)
        elif a == 1:
            attrs[a] &= ~termios.OPOST
        elif a == 2:
            attrs[a] = (attrs[a] & ~(termios.CSIZE | termios.PARENB |
                                     termios.CSTOPB)) | termios.CS8
        elif a == 3:
            attrs[a] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                          termios.ISIG | termios.IEXTEN)
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def readline(fd, timeout=3):
    buf = b""
    while True:
        r, _, _ = select.select([fd], [], [], timeout)
        if not r:
            return None
        ch = os.read(fd, 1)
        if not ch:
            return None
        if ch == b"\n":
            return buf.decode("utf-8").strip()
        if ch != b"\r":
            buf += ch


queued = 0


def send(fd, cmd, expect_cmd=None, **kw):
    global queued
    req = {"cmd": cmd, "queued": queued, **kw}
    line = json.dumps(req, separators=(",", ":")) + "\r\n"
    print(f">>> {line.strip()}")
    os.write(fd, line.encode())
    queued += 1
    ec = expect_cmd or cmd
    while True:
        resp = readline(fd)
        if resp is None:
            print("!!! TIMEOUT")
            return None
        try:
            obj = json.loads(resp)
        except json.JSONDecodeError:
            print(f"[garbage] {resp}")
            continue
        if obj.get("status") == "async":
            print(f"[async] {resp}")
            continue
        if obj.get("cmd") != ec:
            print(f"[unexpected cmd={obj.get('cmd')}] {resp}")
            continue
        print(f"<<< {resp}")
        return obj


def main():
    global queued
    fd = open_serial(PORT)
    time.sleep(1)
    queued = 0
    passed, failed = 0, 0

    def check(name, ok):
        nonlocal passed, failed
        if ok:
            print(f"  PASS"); passed += 1
        else:
            print(f"  FAIL"); failed += 1

    # === Stage 1 Tests ===
    print("\n=== Stage 1: sys domain ===")
    r = send(fd, "sys.ping")
    check("sys.ping", r and r.get("status") == "ok")

    r = send(fd, "sys.get_fw_version")
    check("sys.get_fw_version", r and r.get("status") == "ok" and "version" in r)

    r = send(fd, "test.no_such_cmd")
    check("unknown cmd", r and r.get("status") == "error" and r.get("error_code") == 1)

    # === Stage 2 Tests ===
    print("\n=== Stage 2: test mode ===")

    # test.set_mode / get_mode
    r = send(fd, "test.set_mode", mode=1)
    check("set_mode INJECT", r and r.get("status") == "ok" and r.get("mode") == 1)

    r = send(fd, "test.get_mode")
    check("get_mode", r and r.get("mode") == 1 and r.get("mode_name") == "INJECT")

    # test.inject_gamepad_state
    r = send(fd, "test.inject_gamepad_state", buttons=65536, dpad=5,
             lx=32767, ly=32767, rx=0, ry=0, lt=255, rt=128)
    check("inject_gamepad_state", r and r.get("status") == "ok")

    # test.get_gamepad_state
    r = send(fd, "test.get_gamepad_state")
    check("get_gamepad_state", r and r.get("buttons") is not None)

    # test.inject_hid_report
    r = send(fd, "test.inject_hid_report", buttons=255, dpad=2,
             l_x_axis=128, l_y_axis=128, r_x_axis=64, r_y_axis=192)
    check("inject_hid_report", r and r.get("status") == "ok")

    # test.get_hid_report
    r = send(fd, "test.get_hid_report")
    check("get_hid_report", r and r.get("buttons") is not None and "dpad" in r)

    # test.set_override
    r = send(fd, "test.set_override", point="gamepad_state", enabled=True)
    check("set_override ON", r and r.get("status") == "ok" and r.get("enabled") == True)

    r = send(fd, "test.set_override", point="hid_report", enabled=False)
    check("set_override OFF", r and r.get("status") == "ok" and r.get("enabled") == False)

    # test.clear_inject
    r = send(fd, "test.clear_inject", point="all")
    check("clear_inject", r and r.get("status") == "ok")

    # test.get_status
    r = send(fd, "test.get_status")
    check("get_status", r and r.get("gamepad_history_count") is not None
          and r.get("gamepad_inject_queued") is not None)

    # test.get_history
    r = send(fd, "test.get_history", type="gamepad_state", count=3)
    check("get_history gamepad", r and r.get("count") is not None
          and "entries" in r)

    r = send(fd, "test.get_history", type="hid_report", count=3)
    check("get_history hid", r and r.get("count") is not None
          and "entries" in r)

    # test.clear_history
    r = send(fd, "test.clear_history", type="all")
    check("clear_history", r and r.get("status") == "ok")

    # test.reset
    r = send(fd, "test.reset")
    check("reset", r and r.get("status") == "ok")

    r = send(fd, "test.get_mode")
    check("mode after reset = PASS_THRU", r and r.get("mode") == 0
          and r.get("mode_name") == "PASS_THRU")

    # set_mode back to PASS_THRU
    r = send(fd, "test.set_mode", mode=0)
    check("set_mode PASS_THRU", r and r.get("status") == "ok")

    # === Summary ===
    print(f"\n{'='*40}")
    print(f"Passed: {passed}/{passed+failed}")
    print(f"Failed: {failed}/{passed+failed}")
    os.close(fd)
    return failed == 0


if __name__ == "__main__":
    exit(0 if main() else 1)
