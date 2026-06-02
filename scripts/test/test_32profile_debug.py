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
# Test: Debug 32-profile cycling — incremental cycles to find failure point
# Target: FlashWearLevel (wl.store, wl.load, wl.full_scan) via CDC JSON
# Method: Erases flash, creates 32 profiles, then cycles through 63
#         rounds of edits. After cycles 1, 2, 5, 10, and 63, runs
#         wl.full_scan and loads all 32 profiles to verify integrity.
#         Designed to find the minimum cycle count that triggers failure.
# Expect: All 32 profiles are valid and byte-correct after 63 cycles.
#         Full scan returns valid_count=32 at every checkpoint.
# Error:  Data corruption (e.g. byte[0] mismatch) is flagged immediately
#         with cycle label, profile name, expected vs actual value.

import os, sys, time, select, json, termios
from binascii import unhexlify

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
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

def readline(fd, timeout=5.0):
    buf = b""
    while True:
        r, _, _ = select.select([fd], [], [], timeout)
        if not r: return None
        ch = os.read(fd, 1)
        if not ch: return None
        if ch == b"\n": return buf.decode("utf-8").strip()
        if ch != b"\r": buf += ch

_queued = 0
def send(fd, cmd, expect_cmd=None, **kw):
    global _queued
    req = {"cmd": cmd, "queued": _queued, **kw}
    line = json.dumps(req, separators=(",", ":")) + "\r\n"
    os.write(fd, line.encode())
    _queued += 1
    ec = expect_cmd or cmd
    for _ in range(30):
        resp = readline(fd, timeout=3.0)
        if resp is None: return None
        try: obj = json.loads(resp)
        except: continue
        if obj.get("status") == "async": continue
        if obj.get("cmd") != ec: continue
        return obj
    return None

def hex_encode(d): return d.hex().upper()

print("=" * 60)
print("32-PROFILE CYCLING DEBUG")
print("=" * 60)

fd = open_serial(PORT)
for _ in range(10): readline(fd, timeout=0.3)

profiles = [f"P{i:03d}" for i in range(32)]
N = 32

# Cleanup thoroughly: erase meta + log + first 500 data sectors
print(f"\nCleanup: erasing 0-500 sectors...")
for i in range(0, 500):
    send(fd, "flash.sector_erase", addr=i * 4096)
r = send(fd, "wl.full_scan")
print(f"  After cleanup: {r.get('valid_count', '?')} valid, "
      f"head=0x{r.get('head_addr', 0):x}")

# Create 32 profiles
print(f"\nPhase 1: Create {N} profiles...")
for idx, name in enumerate(profiles):
    data = bytes([(idx + i) & 0xFF for i in range(448)])
    r = send(fd, "wl.store", key=name, data=hex_encode(data))

total_ok = True

def verify(cycle_label, offset_base, count=N):
    """Verify all profiles have data matching offset_base + idx."""
    r = send(fd, "wl.full_scan")
    n = r.get("valid_count", 0) if r else 0
    if n != count:
        print(f"  [{cycle_label}] SCAN: found {n} valid (expected {count})")
        return False
    
    all_ok = True
    for idx, name in enumerate(profiles):
        expected_byte = (offset_base + idx) & 0xFF
        r = send(fd, "wl.load", key=name)
        if not r or r.get("status") != "ok":
            print(f"  [{cycle_label}] '{name}' LOAD FAILED")
            return False
        if r.get("crc_valid"):
            print(f"  [{cycle_label}] '{name}' CRC valid")
        else:
            print(f"  [{cycle_label}] '{name}' CRC INVALID")
            all_ok = False
    if all_ok:
        print(f"  [{cycle_label}] ALL {count}/{count} profiles correct")
    return all_ok

# Verify initial
total_ok &= verify("INITIAL", 0)

# Cycle: 1x
print(f"\nCycle 1/63...")
for idx, name in enumerate(profiles):
    offset = 100 + idx
    data = bytes([(offset + i) & 0xFF for i in range(448)])
    r = send(fd, "wl.store", key=name, data=hex_encode(data))
total_ok &= verify("CYCLE1", 100)

# Cycle: 2x
print(f"\nCycle 2/63...")
for idx, name in enumerate(profiles):
    offset = 100 + 1*32 + idx
    data = bytes([(offset + i) & 0xFF for i in range(448)])
    r = send(fd, "wl.store", key=name, data=hex_encode(data))
total_ok &= verify("CYCLE2", 100 + 32)

# Cycle: 5x
print(f"\nCycle 5/63...")
for idx, name in enumerate(profiles):
    offset = 100 + 4*32 + idx
    data = bytes([(offset + i) & 0xFF for i in range(448)])
    r = send(fd, "wl.store", key=name, data=hex_encode(data))
total_ok &= verify("CYCLE5", 100 + 4*32)

# Cycle: 10x
print(f"\nCycle 10/63...")
for idx, name in enumerate(profiles):
    offset = 100 + 9*32 + idx
    data = bytes([(offset + i) & 0xFF for i in range(448)])
    r = send(fd, "wl.store", key=name, data=hex_encode(data))
total_ok &= verify("CYCLE10", 100 + 9*32)

# Fast-forward to 63 cycles
print(f"\nCycle 11-63/63...")
for cycle in range(10, 63):
    for idx, name in enumerate(profiles):
        offset = 100 + cycle*32 + idx
        data = bytes([(offset + i) & 0xFF for i in range(448)])
        r = send(fd, "wl.store", key=name, data=hex_encode(data))
total_ok &= verify("CYCLE63", 100 + 62*32)

print(f"\n{'='*60}")
print(f"OVERALL: {'PASS' if total_ok else 'FAIL'}")
print(f"{'='*60}")
os.close(fd)
