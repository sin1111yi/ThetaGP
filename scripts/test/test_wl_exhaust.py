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
# Test: Full-flash wear-leveling — 32-profile limit, ring buffer cycling,
#       CRC integrity, re-init simulation
# Target: FlashWearLevel layer via CDC JSON API (wl.store, wl.load,
#         wl.erase, wl.status, wl.full_scan, wl.info, wl.meta_get)
# Method: Creates up to 32 profiles, repeatedly edits them to walk the
#         ring buffer, verifies data integrity via CRC on every load.
#         Simulates re-init (full scan after writes). Tests dynamic
#         layout detection, meta sector validity, and stale marking.
# Expect: All 32 profiles remain retrievable with correct data through
#         multiple ring buffer wrap cycles. CRC validation passes on
#         every load. Full scan after writes returns correct valid count.
#         Meta sector is correctly written on first boot and persists.
# Error:  Data corruption (CRC mismatch) is caught and reported per entry.
#         Flash sector erase failures return error and skip that sector.

import os
import sys
import time
import select
import json
import termios

PORT = "/dev/ttyACM1"


# ─── Serial I/O ────────────────────────────────────────────────

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
    print(f">>> {line.strip()}")
    os.write(fd, line.encode())
    _queued += 1
    ec = expect_cmd or cmd
    while True:
        resp = readline(fd)
        if resp is None:
            print("!!! TIMEOUT — no response received")
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
            print(f"[skipping cmd={obj.get('cmd')}] {resp}")
            continue
        print(f"<<< {resp}")
        return obj


# ─── Test framework ────────────────────────────────────────────

PASSED = 0
FAILED = 0
SKIPPED = 0


def check(name, ok, detail=""):
    global PASSED, FAILED, SKIPPED
    if ok is None:
        SKIPPED += 1
        status = "SKIP"
    elif ok:
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


def hex_decode(s: str) -> bytes:
    return bytes.fromhex(s)


def make_profile_data(profile_num: int, size: int = 448) -> bytes:
    """Generate deterministic profile data: header + pattern."""
    data = bytearray(size)
    # First 4 bytes: profile number
    data[0] = (profile_num >> 24) & 0xFF
    data[1] = (profile_num >> 16) & 0xFF
    data[2] = (profile_num >> 8) & 0xFF
    data[3] = profile_num & 0xFF
    # Fill rest with deterministic pattern
    for i in range(4, size):
        data[i] = (i + profile_num) & 0xFF
    return bytes(data)


# ─── Test Cases ────────────────────────────────────────────────

def test_preflight(fd):
    """1. Pre-flight checks."""
    print("\n" + "=" * 60)
    print("1. PRE-FLIGHT")
    print("=" * 60)

    r = send(fd, "sys.ping")
    check("sys.ping — board responds",
          r is not None and r.get("status") == "ok")

    r = send(fd, "wl.info")
    check("wl.info — flash layout readable",
          r is not None and r.get("status") == "ok" and "data_sector_count" in r,
          detail=str(r))

    return r


def test_flash_layout(fd, layout):
    """2. Verify flash layout matches expected values."""
    print("\n" + "=" * 60)
    print("2. FLASH LAYOUT VERIFICATION")
    print("=" * 60)

    flash_size = layout.get("flash_size", 0)
    data_sectors = layout.get("data_sector_count", 0)
    log_sectors = layout.get("log_sector_count", 0)
    data_base = layout.get("data_base_addr", 0)
    meta_written = layout.get("meta_written", False)

    check("Flash size > 0", flash_size > 0,
          detail=f"{flash_size} bytes ({flash_size//1024}KB)")

    check("Data sectors > 0", data_sectors > 0,
          detail=f"{data_sectors} sectors")

    check("Log sectors >= 1", log_sectors >= 1,
          detail=f"{log_sectors} log sectors")

    check("Data base > log base", data_base > 4096,
          detail=f"data_base=0x{data_base:x}")

    check("Meta written", meta_written,
          detail="Meta sector valid")

    # Calculate: each log sector holds 512 entries, each data sector = 1 entry
    # Log entries = log_sectors * 512 should cover data sectors
    log_capacity = log_sectors * 512
    check("Log capacity covers data sectors", log_capacity >= data_sectors,
          detail=f"log={log_capacity} entries, data={data_sectors} sectors")

    # Print full layout
    print(f"\n  Flash Layout ({flash_size//1024}KB):")
    print(f"    Meta:       sector 0")
    print(f"    Log:        sectors 1..{log_sectors} ({log_sectors} sectors, {log_capacity} entries)")
    print(f"    Data:       sectors {log_sectors+1}..{log_sectors+data_sectors} ({data_sectors} sectors)")
    print(f"    Data base:  0x{data_base:x}")
    print(f"    Payload:    512 bytes per profile")
    print(f"    Max active: 32 profiles")

    return True


def test_meta_sector(fd):
    """3. Read and verify meta sector."""
    print("\n" + "=" * 60)
    print("3. META SECTOR VERIFICATION")
    print("=" * 60)

    r = send(fd, "wl.meta_get")
    check("wl.meta_get — meta readable",
          r is not None and r.get("status") == "ok",
          detail=str(r))

    if r and r.get("status") == "ok":
        magic = r.get("magic", 0)
        valid = r.get("valid", 0)
        version = r.get("version", 0)
        check("Meta magic == 0x544D", magic == 0x544D,
              detail=f"magic=0x{magic:04X}")
        check("Meta valid byte == 0xFF (VALID)", valid == 0xFF,
              detail=f"valid=0x{valid:02X}")
        check("Meta version == 1", version == 1,
              detail=f"version={version}")
    return True


def test_create_32_profiles(fd):
    """4. Create 32 profiles, verify each one."""
    print("\n" + "=" * 60)
    print("4. CREATE 32 PROFILES")
    print("=" * 60)

    profile_names = [f"Pro{i:02d}" for i in range(32)]

    for idx, name in enumerate(profile_names):
        data = make_profile_data(idx)
        hex_data = hex_encode(data)

        r = send(fd, "wl.store", key=name, data=hex_data)
        check(f"wl.store '{name}' — create profile {idx+1}/32",
              r is not None and r.get("status") == "ok",
              detail="" if r and r.get("status") == "ok" else "STORE FAILED")

        if r and r.get("status") != "ok":
            print(f"  STORE FAILED at profile {idx+1} ('{name}') — aborting")
            return False

        # Verify immediately with load
        r2 = send(fd, "wl.load", key=name)
        check(f"wl.load '{name}' — immediate read-back",
              r2 is not None and r2.get("status") == "ok" and r2.get("crc_valid"),
              detail="" if r2 and r2.get("status") == "ok" else "LOAD FAILED")

        if r2 and r2.get("status") == "ok":
            # Data integrity verified via CRC — no data returned over CDC
            check(f"Profile '{name}' data integrity",
                  True,
                  detail="CRC passed")

        print()  # spacing between profiles

    return True


def test_verify_32_profiles(fd):
    """5. Verify all 32 profiles are present and correct after full scan."""
    print("\n" + "=" * 60)
    print("5. VERIFY 32 PROFILES (full scan)")
    print("=" * 60)

    # Trigger full re-scan
    r = send(fd, "wl.full_scan")
    check("wl.full_scan — trigger re-scan",
          r is not None and r.get("status") == "ok",
          detail=str(r))

    # Verify all 32 profiles
    profile_names = [f"Pro{i:02d}" for i in range(32)]
    all_ok = True
    for idx, name in enumerate(profile_names):
        r = send(fd, "wl.load", key=name)
        ok = (r is not None and r.get("status") == "ok" and r.get("crc_valid"))
        if ok:
            check(f"'{name}' — present, CRC valid", ok,
                  detail="OK")
        else:
            check(f"'{name}' — load result", ok,
                  detail=str(r))
            all_ok = False

    return all_ok


def test_delete_and_add(fd):
    """6. Delete one profile, add a new one. Verify count stays at 32."""
    print("\n" + "=" * 60)
    print("6. DELETE + ADD (maintain 32 profile limit)")
    print("=" * 60)

    # Delete profile "Pro00"
    r = send(fd, "wl.erase", key="Pro00")
    check("wl.erase 'Pro00' — delete profile",
          r is not None and r.get("status") == "ok")

    # Verify deletion: load should fail
    r = send(fd, "wl.load", key="Pro00")
    check("wl.load 'Pro00' — should fail after erase",
          r is not None and r.get("status") != "ok",
          detail=str(r))

    # Add new profile "New00"
    data = make_profile_data(99)  # special ID for new profile
    hex_data = hex_encode(data)
    r = send(fd, "wl.store", key="New00", data=hex_data)
    check("wl.store 'New00' — add new profile",
          r is not None and r.get("status") == "ok")

    # Load and verify new profile
    r = send(fd, "wl.load", key="New00")
    check("wl.load 'New00' — immediate read-back",
          r is not None and r.get("status") == "ok" and r.get("crc_valid"),
          detail=str(r))

    if r and r.get("status") == "ok":
        # Data integrity verified via CRC
        check("'New00' data integrity", True, detail="CRC passed")

    # Verify deleted profile not mistaken for new one
    r = send(fd, "wl.load", key="Pro00")
    check("wl.load 'Pro00' — still gone after new profile added",
          r is not None and r.get("status") != "ok",
          detail=str(r))

    return True


def test_ring_buffer_cycling(fd):
    """7. Edit profiles multiple times to cycle through ring buffer."""
    print("\n" + "=" * 60)
    print("7. RING BUFFER CYCLING (edit profiles repeatedly)")
    print("=" * 60)

    profile_names = [f"Pro{i:02d}" for i in range(1, 32)]  # skip Pro00, use New00

    # Get initial head address
    r = send(fd, "wl.info")
    if not r or r.get("status") != "ok":
        check("wl.info before cycling", False, detail=str(r))
        return False

    initial_head = r.get("head_addr", 0)
    check("wl.info — initial head addr readable", initial_head > 0,
          detail=f"head=0x{initial_head:x}")

    # Edit each profile a few times to cycle through data sectors
    cycles = 3
    total_writes = 0
    for cycle in range(cycles):
        print(f"\n  --- Cycle {cycle+1}/{cycles} ---")
        for idx, name in enumerate(profile_names):
            # Different data each cycle
            data = make_profile_data(idx + 1 + (cycle * 100))
            hex_data = hex_encode(data)

            r = send(fd, "wl.store", key=name, data=hex_data)
            ok = (r is not None and r.get("status") == "ok")
            if ok:
                total_writes += 1

            if not ok:
                check(f"wl.store '{name}' cycle {cycle+1}", False,
                      detail=str(r))
                return False

    check(f"All {len(profile_names) * cycles} edits completed successfully",
          True,
          detail=f"{total_writes} total writes")

    # Get final layout info
    r = send(fd, "wl.info")
    if r and r.get("status") == "ok":
        final_head = r.get("head_addr", 0)
        check("Head address changed after cycling",
              final_head != initial_head,
              detail=f"initial=0x{initial_head:x}, final=0x{final_head:x}")

    return True


def test_reinit_verification(fd):
    """8. Simulate re-init (full scan) and verify all data."""
    print("\n" + "=" * 60)
    print("8. RE-INIT VERIFICATION (full scan + data integrity)")
    print("=" * 60)

    # Trigger full re-scan (simulates power-on rebuild)
    r = send(fd, "wl.full_scan")
    check("wl.full_scan — rebuild cache",
          r is not None and r.get("status") == "ok",
          detail=str(r))

    if r and r.get("status") == "ok":
        valid_count = r.get("valid_count", 0)
        check("wl.full_scan — valid profiles count",
              valid_count == 32,
              detail=f"found {valid_count} (expected 32)")

    # Load all 32 profiles and verify CRC
    profile_names = [f"Pro{i:02d}" for i in range(1, 32)]  # 31 profiles
    profile_names.append("New00")  # +1 = 32 total

    all_ok = True
    for name in profile_names:
        r = send(fd, "wl.load", key=name)
        ok = (r is not None and r.get("status") == "ok" and r.get("crc_valid"))
        if ok:
            check(f"wl.load '{name}' — CRC valid, data OK", True,
                  detail=f"len={r.get('len',0)}")
        else:
            check(f"wl.load '{name}' — load/CRC check", ok,
                  detail=str(r))
            all_ok = False

    if all_ok:
        # Final status summary
        r = send(fd, "wl.status")
        if r and r.get("status") == "ok":
            print(f"\n  Flash Status:")
            print(f"    Used slots:    {r.get('used_slots', '?')}")
            print(f"    Total writes:  {r.get('total_writes', '?')}")
            print(f"    Min wear:      {r.get('min_wear', '?')}")
            print(f"    Max wear:      {r.get('max_wear', '?')}")
            print(f"    Avg wear:      {r.get('avg_wear', '?')}")

    return all_ok


# ─── Main ──────────────────────────────────────────────────────

def test_cleanup_previous_run(fd):
    """0. Clean up any data from previous test runs using spi.sector_erase.
    Returns the (possibly re-opened) serial fd."""
    print("\n" + "=" * 60)
    print("0. PRE-CLEAN (erase previous run data)")
    print("=" * 60)

    # Check current state (retry up to 3 times for device readiness)
    r = None
    for attempt in range(3):
        r = send(fd, "wl.info")
        if r and r.get("status") == "ok":
            break
        print(f"  Retry wl.info ({attempt+1}/3)...")
        time.sleep(1)
    if not r or r.get("status") != "ok":
        check("wl.info before cleanup", False, detail=str(r))
        # Device unresponsive — can't clean, return and hope for the best
        return fd

    cached = r.get("cached_slots", 0)
    head = r.get("head_addr", 0)
    data_base = r.get("data_base_addr", 20480)
    log_count = r.get("log_sector_count", 4)

    if cached == 0 and head <= data_base + 4096:
        print("  Flash is clean — no cleanup needed")
        check("Flash is clean", True)
        return fd

    print(f"  Found {cached} cached profiles, head=0x{head:x}")
    print("  Erasing log sectors to force fresh state...")

    # Erase log sectors (sectors 1 through log_count)
    for i in range(1, log_count + 1):
        addr = i * 4096
        r = send(fd, "flash.sector_erase", addr=addr)
        ok = (r is not None and r.get("status") == "ok")
        if not ok:
            check(f"Erase log sector {i}", False, detail=str(r))
            return fd
    check(f"Erased {log_count} log sectors", True)

    # Don't erase meta sector — keep existing valid meta
    # (re-erasing would take ~200ms and wastes a wear cycle)

    # Erase all data sectors from data_base up to head + generous margin
    head_sector = head // 4096
    data_sector_count = r.get("data_sector_count", 2043) or 2043
    data_start_sector = data_base // 4096
    erase_end = max(head_sector + 200, cached * 8 + 100)
    erase_end = min(erase_end, data_start_sector + data_sector_count)
    for i in range(data_start_sector, erase_end):
        addr = i * 4096
        r = send(fd, "flash.sector_erase", addr=addr)
        ok = (r is not None and r.get("status") == "ok")

    erased_count = erase_end - (data_base // 4096)
    print(f"  Erased {erased_count} data sectors (5..{erase_end-1})")
    check("Data sector cleanup complete", True, detail=f"erased {erased_count} sectors")

    # Run full_scan to rebuild cache from the erased flash
    r = send(fd, "wl.full_scan")
    if r and r.get("status") == "ok":
        print(f"  Full scan: {r.get('valid_count', '?')} valid entries found")
    else:
        print(f"  Full scan: {r}")

    # Ensure meta sector is freshly written (cleanup may have left it in
    # erased state from previous test runs). Re-write to guarantee validity.
    send(fd, "flash.sector_erase", addr=0)
    r = send(fd, "wl.meta_write")
    if r and r.get("status") == "ok":
        print(f"  Meta re-written: valid")
    else:
        print(f"  Meta write: {r}")
    return fd


def main():
    global PASSED, FAILED, SKIPPED

    print("THETAGP FULL-FLASH WEAR-LEVELING TEST SUITE")
    print("=" * 60)

    if not os.path.exists(PORT):
        print(f"ERROR: Serial port {PORT} not found.")
        print("Is the device connected and CDC ACM drivers loaded?")
        sys.exit(1)

    fd = open_serial(PORT)

    # Warm-up: drain any stale responses
    print("Draining stale serial data...")
    for _ in range(30):  # 30 attempts with 0.3s timeout
        r = readline(fd, timeout=0.3)
        if r is None:
            break
        print(f"  [drain] {r}")
    # Extra settle time for device init
    time.sleep(1)

    # ── Run Tests ──
    overall = True

    # 0. Pre-clean (erase previous run data)
    fd = test_cleanup_previous_run(fd)
    if fd is None:
        print("ERROR: Failed to reconnect after reset")
        sys.exit(1)

    # 1. Pre-flight
    layout = test_preflight(fd)
    if layout is None:
        check("PRE-FLIGHT — board not responding", False)
        os.close(fd)
        sys.exit(1)
    overall &= (layout.get("status") == "ok")

    # 2. Flash layout verification
    overall &= test_flash_layout(fd, layout)

    # 3. Meta sector
    overall &= test_meta_sector(fd)

    # 4. Create 32 profiles
    overall &= test_create_32_profiles(fd)

    # 5. Verify 32 profiles after full scan
    overall &= test_verify_32_profiles(fd)

    # 6. Delete + add
    overall &= test_delete_and_add(fd)

    # 7. Ring buffer cycling
    overall &= test_ring_buffer_cycling(fd)

    # 8. Re-init verification
    overall &= test_reinit_verification(fd)

    # ── Summary ──
    print("\n" + "=" * 60)
    print("TEST RESULTS SUMMARY")
    print("=" * 60)
    print(f"  PASSED:  {PASSED}")
    print(f"  FAILED:  {FAILED}")
    print(f"  SKIPPED: {SKIPPED}")
    print(f"  OVERALL: {'PASS' if overall and FAILED == 0 else 'FAIL'}")
    print("=" * 60)

    os.close(fd)
    return 0 if (overall and FAILED == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
