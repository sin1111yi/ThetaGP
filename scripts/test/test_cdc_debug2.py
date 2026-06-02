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
# Test: Individual sys command debug with longer timeout
# Target: CDC ACM virtual serial port (ttyACM1)
# Method: Sends sys.ping, sys.get_fw_version, and a nonexistent command
#         (test.nope) one at a time with 5-second per-command timeout.
#         Flushes stale input between commands. Prints raw responses.
# Expect: sys.ping returns status:"ok". sys.get_fw_version returns version
#         string. test.nope returns status:"error".
# Error:  Timeout returns None and prints the buffered bytes for diagnosis.
#         Useful for isolating which command hangs.

import os, termios, time, select, json

PORT = "/dev/ttyACM1"

def open_serial(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    for attr in (0, 1, 2, 3):
        if attr == 0:
            attrs[attr] = attrs[attr] & ~(termios.IGNBRK|termios.BRKINT|termios.PARMRK|termios.ISTRIP|termios.INLCR|termios.IGNCR|termios.ICRNL|termios.IXON)
        elif attr == 1:
            attrs[attr] = attrs[attr] & ~termios.OPOST
        elif attr == 2:
            attrs[attr] = attrs[attr] & ~(termios.CSIZE|termios.PARENB|termios.CSTOPB) | termios.CS8
        elif attr == 3:
            attrs[attr] = attrs[attr] & ~(termios.ECHO|termios.ECHONL|termios.ICANON|termios.ISIG|termios.IEXTEN)
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

def send(fd, cmd, queued=0):
    req = {"cmd": cmd, "queued": queued}
    line = json.dumps(req, separators=(",",":")) + "\r\n"
    print(f">>> {line.strip()}")
    os.write(fd, line.encode())
    
    t0 = time.time()
    buf = b""
    while time.time() - t0 < 5:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            ch = os.read(fd, 1)
            if not ch:
                break
            if ch == b"\n":
                line = (buf + b"").decode("utf-8").strip()
                print(f"<<< {line}")
                return line
            if ch != b"\r":
                buf += ch
    print(f"!!! TIMEOUT (buffered: {buf!r})")
    return None

fd = open_serial(PORT)
time.sleep(1)

for i, cmd in enumerate(["sys.ping", "sys.get_fw_version", "test.nope"]):
    print(f"\n--- Test {i+1}: {cmd} (queued=0) ---")
    send(fd, cmd, queued=0)
    time.sleep(0.5)
    # Flush any stale input
    termios.tcflush(fd, termios.TCIOFLUSH)

os.close(fd)
