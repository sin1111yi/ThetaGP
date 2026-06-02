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
# Test: Focused wrap-around — fill ring, verify via direct load (no full_scan)
# Target: FlashWearLevel (wl.info, wl.store, wl.load) via CDC JSON API
# Method: Queries flash layout (data_sector_count), then creates 32
#         profiles and cycles through all data sectors to fill the ring.
#         Phase 2 verifies by direct wl.load (uses RAM cache, no flash
#         scan). Phase 3 cross-checks via wl.full_scan + load.
# Expect: All 32 profiles return correct data via both direct load and
#         full scan paths. Direct load and scan counts match.
# Error:  Direct load/cache returns stale data, while full scan returns
#         correct data — indicates cache/head-pointer desync. Mismatch
#         between Phase 2 and Phase 3 results indicates scan mismatch.

import os, sys, time, select, json, termios
from binascii import unhexlify

PORT = "/dev/ttyACM1"
fd = None

def ser_open():
    global fd
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    for a in range(4):
        if a == 0: attrs[a] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
        elif a == 1: attrs[a] &= ~termios.OPOST
        elif a == 2: attrs[a] = (attrs[a] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)) | termios.CS8
        elif a == 3: attrs[a] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    attrs[4] = termios.B115200; attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)

def readln(to=3.0):
    buf = b""
    while True:
        r, _, _ = select.select([fd], [], [], to)
        if not r: return None
        ch = os.read(fd, 1)
        if not ch: return None
        if ch == b"\n": return buf.decode().strip()
        if ch != b"\r": buf += ch

_q = 0
def cmd(c, **kw):
    global _q
    os.write(fd, (json.dumps({"cmd": c, "queued": _q, **kw}, separators=(",",":")) + "\r\n").encode())
    _q += 1
    for _ in range(20):
        r = readln()
        if r is None: return None
        try: o = json.loads(r)
        except: continue
        if o.get("cmd") == c or o.get("cmd") == kw.get("expect", c):
            return o

def store(name, data):
    return cmd("wl.store", key=name, data=data.hex().upper())

def load(name):
    r = cmd("wl.load", key=name)
    if r and r.get("status") == "ok":
        return unhexlify(r["data"])
    return None

P, F = 0, 0
def chk(name, ok, d=""):
    global P, F
    P += bool(ok); F += not ok
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" — {d}" if d else ""))

print("=" * 60)
print("RING BUFFER WRAP-AROUND TEST (direct verify)")
print("=" * 60)

ser_open()
for _ in range(10): readln(0.3)

r = cmd("wl.info")
n_sectors = r.get("data_sector_count", 0) if r else 0
chk("Device ready", r and r.get("status") == "ok", f"{n_sectors} data sectors")

N = 32  # profiles
P_NAMES = [f"P{i:03d}" for i in range(N)]
cycles = n_sectors // N
extra = n_sectors % N
print(f"\n  Need {n_sectors} writes: {N} initial + {cycles}x cycles + {extra} extra")
print(f"  Estimated time: ~{(n_sectors * 0.2):.0f}s ({(n_sectors * 0.2)/60:.1f}min)")

# Phase 1: Create + fill
print(f"\n--- Phase 1: Fill ring ({n_sectors} writes) ---\n")

for idx, name in enumerate(P_NAMES):
    store(name, bytes([(idx + i) & 0xFF for i in range(512)]))

print("  Created. Cycling...")

for cycle in range(cycles):
    for idx, name in enumerate(P_NAMES):
        off = 100 + cycle * N + idx
        store(name, bytes([(off + i) & 0xFF for i in range(512)]))
    if cycle % 10 == 9 or cycle == cycles - 1:
        sys.stdout.write(f"\r  Cycle {cycle+1}/{cycles}")
        sys.stdout.flush()

for idx in range(extra):
    name = P_NAMES[idx]
    off = 2000 + idx
    store(name, bytes([(off + i) & 0xFF for i in range(512)]))

print(f"\r  Written {n_sectors}/{n_sectors} — done.          ")

# Phase 2: Verify by direct load (no full scan — uses RAM cache)
print(f"\n--- Phase 2: Direct verify ({N} profiles) ---\n")

all_ok = True
for idx, name in enumerate(P_NAMES):
    if idx < extra:
        exp_off = 2000 + idx
    else:
        exp_off = 100 + (cycles - 1) * N + idx
    expected = bytes([(exp_off + i) & 0xFF for i in range(512)])

    data = load(name)
    if data is None:
        chk(f"'{name}'", False, "load failed")
        all_ok = False
    elif data == expected:
        chk(f"'{name}'", True, "OK")
    else:
        diff = f"byte[0]=0x{data[0]:02X} exp=0x{expected[0]:02X}"
        chk(f"'{name}'", False, diff)
        all_ok = False

# Phase 3: Verify via full_scan (cross-check)
print(f"\n--- Phase 3: Full scan + verify ---\n")

r = cmd("wl.full_scan")
n = r.get("valid_count", 0) if r else 0
chk(f"Full scan found {n} profiles", n == N, f"expected {N}")

scan_ok = True
for idx, name in enumerate(P_NAMES):
    if idx < extra:
        exp_off = 2000 + idx
    else:
        exp_off = 100 + (cycles - 1) * N + idx
    expected = bytes([(exp_off + i) & 0xFF for i in range(512)])

    data = load(name)
    if data is None:
        chk(f"  SCAN '{name}'", False, "load failed")
        scan_ok = False
    elif data == expected:
        chk(f"  SCAN '{name}'", True)
    else:
        chk(f"  SCAN '{name}'", False, f"byte[0]=0x{data[0]:02X} exp=0x{expected[0]:02X}")
        scan_ok = False

# Summary
print(f"\n{'='*60}")
print(f"DIRECT LOAD: {'PASS' if all_ok else 'FAIL'}  (P={P} F={F})")
print(f"FULL SCAN:  {'PASS' if scan_ok else 'FAIL'}")
print(f"{'='*60}")

os.close(fd)
sys.exit(0 if all_ok else 1)
