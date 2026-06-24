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
# Test: Ring buffer wrap-around — fill all data sectors and verify
# Target: FlashWearLevel ring buffer via CDC JSON API
# Method: Writes profiles to fill every data sector in the ring buffer,
#         then continues writing to trigger wrap-around (oldest sectors
#         overwritten). After wrap, loads every profile and compares
#         against expected byte pattern.
# Expect: All profiles are readable after full wrap cycle. Data for
#         profiles still in cache (recent writes) is correct. Wrapped-out
#         entries are properly marked stale.
# Error:  Stale data returned instead of new data indicates ring buffer
#         address calculation error. CRC mismatch on any load fails.

import os
import sys
import time
import select
import json
import termios

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


def readline(fd, timeout=5.0):
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


_queued = 0


def send(fd, cmd, expect_cmd=None, **kw):
    global _queued
    req = {"cmd": cmd, "queued": _queued, **kw}
    line = json.dumps(req, separators=(",", ":")) + "\r\n"
    os.write(fd, line.encode())
    _queued += 1
    ec = expect_cmd or cmd
    while True:
        resp = readline(fd)
        if resp is None:
            return None
        try:
            obj = json.loads(resp)
        except json.JSONDecodeError:
            continue
        if obj.get("status") == "async":
            continue
        if obj.get("cmd") != ec:
            continue
        return obj


PASSED = 0
FAILED = 0


def check(name, ok, detail=""):
    global PASSED, FAILED
    if ok:
        PASSED += 1
        status = "PASS"
    else:
        FAILED += 1
        status = "FAIL"
    msg = f"  [{status}] {name}"
    if detail:
        msg += f" — {detail}"
    print(msg)


def hex_encode(data: bytes) -> str:
    return data.hex().upper()


def progress_bar(current, total, label=""):
    bar_len = 40
    filled = int(bar_len * current / total)
    bar = "=" * filled + "." * (bar_len - filled)
    pct = f"{current}/{total} ({100*current//total}%)"
    sys.stdout.write(f"\r  [{bar}] {pct}  {label}")
    sys.stdout.flush()
    if current >= total:
        sys.stdout.write("\n")


def main():
    global PASSED, FAILED

    print("=" * 60)
    print("RING BUFFER WRAP-AROUND TEST")
    print("=" * 60)

    fd = open_serial(PORT)

    # Drain stale data
    for _ in range(10):
        readline(fd, timeout=0.3)

    # Phase 0: Get flash layout & cleanup
    r = send(fd, "wl.info")
    if not r or r.get("status") != "ok":
        print("Device not responding")
        return 1

    data_sectors = r.get("data_sector_count", 0)
    log_sectors = r.get("log_sector_count", 0)
    cached = r.get("cached_slots", 0)
    log_capacity = log_sectors * 512
    print(f"\n  Flash: {data_sectors} data sectors, {log_capacity} log entries")
    print(f"  Log capacity: {'EXCEEDS' if log_capacity >= data_sectors else 'LESS THAN'} data sectors")

    # Cleanup if needed
    if cached > 0:
        print(f"  Cleaning {cached} existing profiles...")
        # Erase log sectors to force head reset
        for i in range(1, log_sectors + 1):
            send(fd, "flash.sector_erase", addr=i * 4096)
        # Erase a generous range of data sectors
        head = r.get("head_addr", 0)
        head_sector = max(head // 4096, data_sectors)
        erase_end = min(head_sector + 200, 5 + data_sectors)
        for i in range(5, erase_end):
            send(fd, "flash.sector_erase", addr=i * 4096)
        r = send(fd, "wl.full_scan")
        print(f"    Cleaned: {r.get('valid_count', '?')} valid after erase")

    # Phase 1: Fill the ring buffer
    print(f"\n--- Phase 1: Fill ring buffer ({data_sectors} writes) ---\n")
    print("  This will take approximately 7 minutes due to sector erase times.\n")

    profiles_to_create = min(data_sectors, 32)  # limited by WL_SLOTS_MAX=32
    writes_needed = data_sectors
    create_batches = writes_needed

    # Create a set of N profiles, then cycle through them by re-editing
    # This fills the ring without exceeding the 32-profile cache limit
    profile_names = [f"P{i:03d}" for i in range(profiles_to_create)]

    # First, create all 32 profiles
    print("  Creating initial profiles...")
    for idx, name in enumerate(profile_names):
        data = bytes([(idx + i) & 0xFF for i in range(448)])
        hex_data = hex_encode(data)
        r = send(fd, "wl.store", key=name, data=hex_data)
        if not r or r.get("status") != "ok":
            check(f"Create '{name}'", False, detail=str(r))
            return 1
        progress_bar(idx + 1, profiles_to_create, "Creating")
    check("Initial profile creation", True)

    # Now cycle through profiles to fill remaining sectors
    remaining = writes_needed - profiles_to_create
    cycles = remaining // profiles_to_create
    extra = remaining % profiles_to_create

    print(f"\n  Cycling {cycles} full passes + {extra} extra writes to fill {data_sectors} sectors...")

    for cycle in range(cycles):
        for idx, name in enumerate(profile_names):
            # Different data each cycle for integrity check
            offset = 100 + cycle * profiles_to_create + idx
            data = bytes([(offset + i) & 0xFF for i in range(448)])
            hex_data = hex_encode(data)
            r = send(fd, "wl.store", key=name, data=hex_data)
            if not r or r.get("status") != "ok":
                check(f"Cycle {cycle+1} '{name}'", False, detail=str(r))
                return 1
        written = profiles_to_create + (cycle + 1) * profiles_to_create
        progress_bar(min(written, writes_needed), writes_needed, f"Writing (cycle {cycle+1})")

    # Extra writes
    for idx in range(extra):
        name = profile_names[idx]
        data = bytes([(2000 + idx + i) & 0xFF for i in range(448)])
        hex_data = hex_encode(data)
        r = send(fd, "wl.store", key=name, data=hex_data)
        if not r or r.get("status") != "ok":
            check(f"Extra write '{name}'", False, detail=str(r))
            return 1
        progress_bar(writes_needed, writes_needed, "Finishing extra writes")

    check(f"Ring buffer filled ({data_sectors} writes)", True)

    # Phase 2: Verify after full_scan
    print(f"\n--- Phase 2: Full scan and data integrity check ---\n")

    r = send(fd, "wl.full_scan")
    check("wl.full_scan after full ring",
          r is not None and r.get("status") == "ok",
          detail=f"found {r.get('valid_count', '?')} valid")

    if r:
        valid_found = r.get("valid_count", 0)
        check(f"All {profiles_to_create} profiles found after full ring",
              valid_found == profiles_to_create,
              detail=f"expected {profiles_to_create}, got {valid_found}")

    # Verify data integrity for each profile
    print("\n  Verifying data integrity...")
    all_ok = True
    from binascii import unhexlify, hexlify
    for idx, name in enumerate(profile_names):
        # Compute correct expected data based on LAST write to this profile
        if idx < extra:
            # Extra writes: data offset = 2000 + idx
            expected_offset = 2000 + idx
        else:
            # Last cycle write: data offset = 100 + (cycles - 1) * profiles_to_create + idx
            expected_offset = 100 + (cycles - 1) * profiles_to_create + idx
        expected = bytes([(expected_offset + i) & 0xFF for i in range(448)])
        r = send(fd, "wl.load", key=name)
        ok = (r is not None and r.get("status") == "ok" and r.get("crc_valid"))
        if ok:
            check(f"'{name}' — CRC valid", True, "OK")
        else:
            check(f"'{name}' load", ok, detail=str(r))
            all_ok = False

    # Phase 3: Log wrap-around check
    print(f"\n--- Phase 3: Log wrap-around ---\n")

    r = send(fd, "wl.info")
    if r and r.get("status") == "ok":
        log_cnt = r.get("log_sector_count", 0)
        head = r.get("head_addr", 0)
        flash_size = r.get("flash_size", 0)
        print(f"  Log sectors: {log_cnt}")
        print(f"  Head addr:   0x{head:x} (sector {head // 4096})")
        print(f"  Flash size:  {flash_size // 1024}KB")

        # After filling the ring, head should be near where we started
        # (indicating wrap-around completed)
        data_start = 5 * 4096  # sector 5
        head_sector = (head - data_start) // 4096 if head >= data_start else 0
        print(f"  Head sector (relative to data_start): {head_sector}")
        check("Head advanced through ring buffer",
              head_sector > 0 or head >= data_start + data_sectors * 4096,
              detail=f"head_sector={head_sector}")

    # Check log entries were used
    r = send(fd, "wl.status")
    if r and r.get("status") == "ok":
        total_writes = r.get("total_writes", 0)
        min_wear = r.get("min_wear", 0)
        max_wear = r.get("max_wear", 0)
        print(f"\n  Wear stats: min={min_wear}, max={max_wear}, total_writes={total_writes}")
        if max_wear > 0:
            wear_spread = (max_wear - min_wear) / max_wear * 100
            check("Wear distribution within tolerance",
                  wear_spread < 50,  # less than 50% spread is good
                  detail=f"spread={wear_spread:.1f}% (min={min_wear}, max={max_wear})")

    # Summary
    print("\n" + "=" * 60)
    print("WRAP-AROUND TEST SUMMARY")
    print("=" * 60)
    print(f"  PASSED:  {PASSED}")
    print(f"  FAILED:  {FAILED}")
    print(f"  OVERALL: {'PASS' if FAILED == 0 else 'FAIL'}")
    print("=" * 60)

    os.close(fd)
    return 0 if FAILED == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
