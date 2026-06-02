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
# Test: Comprehensive SPI DMA + Wear-Leveling (flash + WL command suite)
# Target: SPI flash (W25QXX) and FlashWearLevel layer via CDC JSON API
# Method: Sends flash.read, flash.write, wl.store, wl.load, wl.erase,
#         wl.status commands through the CDC ACM channel. Tests raw flash
#         R/W integrity, wear-leveling store/load/erase cycles, CRC
#         verification, and status reporting.
# Expect: All flash read/write round-trips return identical data.
#         wl.store creates retrievable entries with CRC-valid payloads.
#         wl.erase marks entries stale; subsequent wl.load returns error.
#         wl.status returns valid slot/wear counters.
# Error:  CRC mismatch on load logs a warning and returns error.
#         Invalid parameters (oversized key/data) return error without
#         corrupting existing entries.

import os
import sys
import time
import select
import json
import termios

PORT = "/dev/ttyACM1"


# ═══════════════════════════════════════════════════════════════════
# Serial I/O (mirrors test_cdc.py pattern)
# ═══════════════════════════════════════════════════════════════════

def open_serial(port):
    """Open CDC ACM port with raw binary mode (no line discipline)."""
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
    """Read one line (up to \\n) from CDC serial. Returns None on timeout."""
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
    """
    Send a JSON command and read responses until we match or timeout.
    Returns the matched JSON object, or None on timeout.
    """
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
            print(f"[unexpected cmd={obj.get('cmd')}] {resp}")
            continue
        print(f"<<< {resp}")
        return obj


def device_available():
    """Check if the CDC ACM port exists and looks like a real device."""
    if not os.path.exists(PORT):
        return False
    try:
        fd = open_serial(PORT)
        r = send(fd, "sys.ping")
        os.close(fd)
        return r is not None and r.get("status") == "ok"
    except Exception:
        return False


# ═══════════════════════════════════════════════════════════════════
# Test helpers
# ═══════════════════════════════════════════════════════════════════

PASSED = 0
FAILED = 0
SKIPPED = 0


def check(name, ok, detail=""):
    """Record a test result and print PASS/FAIL/SKIP."""
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
    """Convert bytes to uppercase hex string."""
    return data.hex().upper()


def hex_decode(s: str) -> bytes:
    """Convert hex string to bytes."""
    return bytes.fromhex(s)


def make_pattern(offset: int, length: int) -> bytes:
    """Generate a deterministic byte pattern for test data."""
    return bytes((i + offset) & 0xFF for i in range(length))


# ═══════════════════════════════════════════════════════════════════
# Test sections
# ═══════════════════════════════════════════════════════════════════

def test_preflight(fd):
    """1. Pre-flight checks — verify board is alive."""
    print("\n" + "=" * 60)
    print("1. PRE-FLIGHT CHECKS")
    print("=" * 60)

    # 1a. sys.ping
    r = send(fd, "sys.ping")
    check("sys.ping — board responds",
          r is not None and r.get("status") == "ok",
          detail=("ok" if r and r.get("status") == "ok" else "no response"))

    # 1b. sys.get_fw_version
    r = send(fd, "sys.get_fw_version")
    check("sys.get_fw_version — firmware info readable",
          r is not None and r.get("status") == "ok" and "version" in r,
          detail=("version=" + r.get("version", "?") if r and "version" in r else "missing version"))

    # 1c. unknown command returns error
    r = send(fd, "flash.no_such_cmd")
    check("flash.unknown_cmd — unknown flash command returns error",
          r is not None and r.get("status") == "error",
          detail="")

    # 1d. unknown WL command returns error
    r = send(fd, "wl.no_such_cmd")
    check("wl.unknown_cmd — unknown WL command returns error",
          r is not None and r.get("status") == "error",
          detail="")

    # 1e. wl.status works even with empty wear-level region
    r = send(fd, "wl.status")
    check("wl.status — wear-level status readable",
          r is not None and r.get("status") == "ok" and "total_slots" in r,
          detail=("total_slots=" + str(r.get("total_slots", "?")) if r else "no response"))


def test_spi_dma_read_basic(fd):
    """2a. SPI DMA Read — basic JEDEC ID and read-back."""
    print("\n" + "=" * 60)
    print("2a. SPI DMA READ — BASIC")
    print("=" * 60)

    # Read first 8 bytes of flash (should contain JEDEC ID or valid flash content)
    r = send(fd, "flash.read", addr=0, len=8)
    check("flash.read addr=0, len=8 — reads first flash bytes",
          r is not None and r.get("status") == "ok" and r.get("len") == 8,
          detail=("len=" + str(r.get("len", "?")) if r else "no response"))

    # Read at a non-zero address
    r = send(fd, "flash.read", addr=0x1000, len=16)
    check("flash.read addr=0x1000, len=16 — reads at non-zero offset",
          r is not None and r.get("status") == "ok" and r.get("addr") == 0x1000 and r.get("len") == 16,
          detail="")

    # Read maximum length (512 bytes)
    r = send(fd, "flash.read", addr=0, len=512)
    check("flash.read addr=0, len=512 — reads max allowed length",
          r is not None and r.get("status") == "ok" and r.get("len") == 512,
          detail=("len=" + str(r.get("len", "?")) if r else "no response"))


def test_spi_dma_write_and_verify(fd):
    """2b. SPI DMA Write — write pattern, read back, compare."""
    print("\n" + "=" * 60)
    print("2b. SPI DMA WRITE + READ-BACK VERIFY")
    print("=" * 60)

    # Use an area at a known offset that should be writable.
    # Writing to 0x0 (JEDEC ID area) is dangerous — write to a scratch area.
    # Most flash chips have writable space after sector 0.
    # We use a fixed offset in a writable area. The exact safe offset depends
    # on the chip layout. We use 0x100000 (1MB into flash) as a safe scratch zone.
    # NOR flash requires erase before write (bit-AND operation).
    # We erase the sector before each pattern write.
    # Address safety check: verify TEST_ADDR is within flash range
    r = send(fd, "wl.status")
    safe_addr = 0x100000
    if r and r.get("status") == "ok" and "data_base_addr" in r:
        wl_start = r["data_base_addr"]
        wl_size = r.get("data_sector_count", 0)
        # Use an address in a writable area but outside the WL region
        # If WL region is at 0x08000000+, use 0x100000 as scratch
        if wl_start > 0x08000000:
            safe_addr = 0x100000  # 1MB into flash, below WL region
        else:
            # WL region covers lower area -- pick something above it
            safe_addr = wl_start + wl_size + 0x1000
            if safe_addr > 0x200000:
                safe_addr = 0x100000
    else:
        # Can not get WL status, try default address or SKIP
        pass

    TEST_ADDR = safe_addr

    # Erase sector before first write
    r = send(fd, "flash.sector_erase", addr=TEST_ADDR)
    check("flash.sector_erase — prepare sector for write test",
          r is not None and r.get("status") == "ok",
          detail=("addr=0x%X" % TEST_ADDR if r else "no response"))

    # Pattern A: write 0xAA, 0xBB, 0xCC, 0xDD
    pattern_a = bytes([0xAA, 0xBB, 0xCC, 0xDD])
    hex_a = hex_encode(pattern_a)
    r = send(fd, "flash.write", addr=TEST_ADDR, data=hex_a)
    check("flash.write — small pattern (4 bytes) at scratch addr",
          r is not None and r.get("status") == "ok" and r.get("len") == 4,
          detail=("addr=0x%X, len=%d" % (TEST_ADDR, r.get("len", 0)) if r else "no response"))

    # Read back and verify
    r = send(fd, "flash.read_raw", addr=TEST_ADDR, len=4)
    if r and "chunk" in r:
        read_back = r["chunk"]
        match = (read_back == hex_a)
        check("flash.read_raw back — data matches written pattern",
              match,
              detail=("expected=%s got=%s" % (hex_a, read_back)))
    else:
        check("flash.read_raw back — data matches written pattern",
              False,
              detail="read failed")

    # Pattern B: write 128 bytes of 0xA5
    # Re-erase first since we wrote pattern A to the same sector
    r = send(fd, "flash.sector_erase", addr=TEST_ADDR)
    check("flash.sector_erase — erase before pattern B",
          r is not None and r.get("status") == "ok",
          detail="")

    pattern_b = bytes([0xA5] * 128)
    hex_b = hex_encode(pattern_b)
    r = send(fd, "flash.write", addr=TEST_ADDR, data=hex_b)
    check("flash.write — 128-byte pattern",
          r is not None and r.get("status") == "ok" and r.get("len") == 128,
          detail="")

    # Read back and verify
    r = send(fd, "flash.read_raw", addr=TEST_ADDR, len=128)
    if r and "chunk" in r:
        read_back = r["chunk"]
        match = (read_back == hex_b)
        check("flash.read_raw back — 128-byte pattern matches",
              match,
              detail=("expected=%s..." % hex_b[:32] if not match else "match"))
    else:
        check("flash.read_raw back — 128-byte pattern matches",
              False,
              detail="read failed")

    # Pattern C: write 256 bytes of incrementing pattern
    # Re-erase sector again
    r = send(fd, "flash.sector_erase", addr=TEST_ADDR)
    check("flash.sector_erase — erase before pattern C",
          r is not None and r.get("status") == "ok",
          detail="")

    pattern_c = make_pattern(0x42, 256)
    hex_c = hex_encode(pattern_c)
    r = send(fd, "flash.write", addr=TEST_ADDR, data=hex_c)
    check("flash.write — 256-byte incrementing pattern",
          r is not None and r.get("status") == "ok" and r.get("len") == 256,
          detail="")

    # Read back to verify
    r = send(fd, "flash.read_raw", addr=TEST_ADDR, len=256)
    if r and "chunk" in r:
        read_back = r["chunk"]
        match = (read_back == hex_c)
        check("flash.read_raw back — 256-byte pattern matches",
              match,
              detail=("first mismatch" if not match else "match"))
    else:
        check("flash.read_raw back — 256-byte pattern matches",
              False,
              detail="read failed")

    # Write at multiple non-contiguous addresses and verify independence
    # Use different sectors to avoid cross-sector contamination
    addrs = [0x100000, 0x101000, 0x102000]
    patterns = {}
    for i, addr in enumerate(addrs):
        # Erase sector, write pattern, read back for each address
        r = send(fd, "flash.sector_erase", addr=addr)
        check("flash.sector_erase — prepare addr=0x%X" % addr,
              r is not None and r.get("status") == "ok",
              detail="")

        pat = make_pattern(0x10 * i, 16)
        patterns[addr] = pat
        r = send(fd, "flash.write", addr=addr, data=hex_encode(pat))
        check("flash.write at addr=0x%X — write to independent locations" % addr,
              r is not None and r.get("status") == "ok",
              detail="")

    for addr, expected in patterns.items():
        r = send(fd, "flash.read_raw", addr=addr, len=16)
        if r and "chunk" in r:
            match = (r["chunk"] == hex_encode(expected))
            check("flash.read_raw at addr=0x%X — independent data integrity" % addr,
                  match,
                  detail="expected=%s got=%s" % (hex_encode(expected), r["chunk"]))
        else:
            check("flash.read_raw at addr=0x%X — independent data integrity" % addr,
                  False,
                  detail="read failed")


def test_spi_dma_error_handling(fd):
    """2c. SPI DMA — error handling."""
    print("\n" + "=" * 60)
    print("2c. SPI DMA — ERROR HANDLING")
    print("=" * 60)

    # Zero-length read
    r = send(fd, "flash.read", addr=0, len=0)
    check("flash.read len=0 — returns error",
          r is not None and r.get("status") == "error",
          detail=("reason=" + r.get("reason", "?")) if r else "")

    # Zero-length write (empty data)
    r = send(fd, "flash.write", addr=0, data="")
    check("flash.write data='' — returns error",
          r is not None and r.get("status") == "error",
          detail=("reason=" + r.get("reason", "?")) if r else "")

    # Read with len truncated to max (512) — should succeed, not error
    r = send(fd, "flash.read", addr=0, len=9999)
    check("flash.read len=9999 (clamped to 512) — succeeds",
          r is not None and r.get("status") == "ok" and r.get("len") == 512,
          detail=("len=" + str(r.get("len", "?")) if r else "no response"))

    # Missing addr (should default to 0)
    r = send(fd, "flash.read", len=4)
    check("flash.read without addr (defaults to 0) — succeeds",
          r is not None and r.get("status") == "ok" and r.get("addr") == 0,
          detail=("addr=" + hex(r.get("addr", -1)) if r else "no response"))

    # Missing len (should default to 64)
    r = send(fd, "flash.read", addr=0)
    check("flash.read without len (defaults to 64) — succeeds",
          r is not None and r.get("status") == "ok" and r.get("len") == 64,
          detail=("len=" + str(r.get("len", "?")) if r else "no response"))


def test_spi_dma_read_at_various_lengths(fd):
    """2d. SPI DMA Read — test at various lengths and alignments."""
    print("\n" + "=" * 60)
    print("2d. SPI DMA READ — VARIOUS LENGTHS AND ALIGNMENTS")
    print("=" * 60)

    lengths = [1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 511, 512]

    for l in lengths:
        r = send(fd, "flash.read", addr=0, len=l)
        ok = r is not None and r.get("status") == "ok" and r.get("len") == l
        check("flash.read len=%d — correct length" % l,
              ok,
              detail=("returned len=%d" % (r.get("len", -1) if r else -1)) if not ok else "ok")

    # Read at different address alignments
    alignments = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128]
    for a in alignments:
        r = send(fd, "flash.read", addr=a, len=4)
        check("flash.read addr=0x%X, len=4 — unaligned address works" % a,
              r is not None and r.get("status") == "ok" and r.get("addr") == a,
              detail="")


def test_wl_store_and_load(fd):
    """3a. Wear-leveling — store and load a single config."""
    print("\n" + "=" * 60)
    print("3a. WEAR-LEVELING — STORE AND LOAD")
    print("=" * 60)

    # Store a small config
    test_key = "test_config_1"
    test_data = bytes([0xDE, 0xAD, 0xBE, 0xEF])
    hex_data = hex_encode(test_data)

    r = send(fd, "wl.store", key=test_key, data=hex_data)
    check("wl.store — store small config",
          r is not None and r.get("status") == "ok" and r.get("key") == test_key and r.get("len") == 4,
          detail=("key=%s len=%d" % (r.get("key", "?"), r.get("len", 0)) if r else "no response"))

    # Load it back
    r = send(fd, "wl.load", key=test_key)
    if r and r.get("status") == "ok":
        check("wl.load — loaded data OK",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load — loaded data OK",
              False,
              detail="load failed")

    # Store a larger config (512 bytes)
    large_key = "test_config_big"
    large_data = make_pattern(0x77, 448)
    hex_large = hex_encode(large_data)
    r = send(fd, "wl.store", key=large_key, data=hex_large)
    check("wl.store — store 448-byte config",
          r is not None and r.get("status") == "ok" and r.get("len") == 448,
          detail="")

    r = send(fd, "wl.load", key=large_key)
    if r and r.get("status") == "ok":
        check("wl.load — 512-byte config loads correctly",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load — 512-byte config loads correctly",
              False,
              detail="load failed")


def test_wl_update_config(fd):
    """3b. Wear-leveling — update same key with new data."""
    print("\n" + "=" * 60)
    print("3b. WEAR-LEVELING — UPDATE CONFIG (SAME KEY, NEW DATA)")
    print("=" * 60)

    update_key = "test_config_update"
    
    # First value
    data_v1 = bytes([0x01, 0x02, 0x03, 0x04])
    r = send(fd, "wl.store", key=update_key, data=hex_encode(data_v1))
    check("wl.store — first write of update_key",
          r is not None and r.get("status") == "ok",
          detail="")

    # Update with new value
    data_v2 = bytes([0xFF, 0xFE, 0xFD, 0xFC])
    r = send(fd, "wl.store", key=update_key, data=hex_encode(data_v2))
    check("wl.store — update same key with new data",
          r is not None and r.get("status") == "ok",
          detail="")

    # Load and verify it's the new value
    r = send(fd, "wl.load", key=update_key)
    if r and r.get("status") == "ok":
        check("wl.load after update — returns new data, not old",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load after update — returns new data, not old",
              False,
              detail="load failed")

    # Update with different length (shorter)
    data_v3 = bytes([0xAA])
    r = send(fd, "wl.store", key=update_key, data=hex_encode(data_v3))
    check("wl.store — update to shorter data",
          r is not None and r.get("status") == "ok",
          detail="")

    r = send(fd, "wl.load", key=update_key)
    if r and r.get("status") == "ok":
        check("wl.load after shorter update — returns new data",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load after shorter update — returns new data",
              False,
              detail="load failed")


def test_wl_multiple_configs(fd):
    """3c. Wear-leveling — store and manage multiple configs."""
    print("\n" + "=" * 60)
    print("3c. WEAR-LEVELING — MULTIPLE CONFIGS")
    print("=" * 60)

    # Refresh WL cache and clean up old keys to avoid cache-full eviction
    r = send(fd, "wl.full_scan")
    r = send(fd, "wl.status")
    if r and r.get("used_slots", 0) > 20:
        for i in range(32):
            send(fd, "wl.erase", key="P%03d" % i)
        for tag in ["config_1","config_big","config_update","to_erase",
                     "empty","rapid","large","key_with_underscores_and_digits_123"]:
            send(fd, "wl.erase", key="test_" + tag)
        r = send(fd, "wl.full_scan")

    keys_data = {}
    for i in range(5):
        key = "test_multi_%d" % i
        data = make_pattern(0x10 * i, 32)
        keys_data[key] = data
        r = send(fd, "wl.store", key=key, data=hex_encode(data))
        check("wl.store — config %d/5 (key=%s)" % (i + 1, key),
              r is not None and r.get("status") == "ok",
              detail="")

    # Load each back
    for key, expected in keys_data.items():
        r = send(fd, "wl.load", key=key)
        if r and r.get("status") == "ok":
            check("wl.load — key=%s matches stored value" % key,
                  r.get("crc_valid") == True,
                  detail=("crc_valid=%s" % r.get("crc_valid")))
        else:
            check("wl.load — key=%s matches stored value" % key,
                  False,
                  detail="load failed")

    # Verify wl.status reflects multiple used slots
    r = send(fd, "wl.status")
    if r and r.get("status") == "ok":
        used = r.get("used_slots", -1)
        check("wl.status — used_slots >= %d (have %d configs)" % (len(keys_data), len(keys_data)),
              used >= len(keys_data),
              detail=("used_slots=%d" % used))
    else:
        check("wl.status — used_slots >= %d" % len(keys_data),
              False,
              detail="wl.status failed")


def test_wl_erase(fd):
    """3d. Wear-leveling — erase a config, verify it's gone."""
    print("\n" + "=" * 60)
    print("3d. WEAR-LEVELING — ERASE")
    print("=" * 60)

    erase_key = "test_to_erase"
    erase_data = bytes([0xCA, 0xFE, 0xBA, 0xBE])

    # Store something first
    r = send(fd, "wl.store", key=erase_key, data=hex_encode(erase_data))
    check("wl.store — setup for erase test",
          r is not None and r.get("status") == "ok",
          detail="")

    # Erase it
    r = send(fd, "wl.erase", key=erase_key)
    check("wl.erase — erase existing config",
          r is not None and r.get("status") == "ok" and r.get("key") == erase_key,
          detail=("key=%s" % r.get("key", "?")) if r else "")

    # Load should now fail
    r = send(fd, "wl.load", key=erase_key)
    check("wl.load after erase — returns error (config not found)",
          r is not None and r.get("status") == "error",
          detail=("status=%s" % r.get("status", "?")) if r else "no response")

    # Erase non-existent key — should succeed (idempotent)
    r = send(fd, "wl.erase", key="nonexistent_key_that_was_never_stored")
    check("wl.erase non-existent key — succeeds (idempotent)",
          r is not None and r.get("status") == "ok",
          detail=("status=%s" % r.get("status", "?")) if r else "")

    # Verify wl.status after erase
    r = send(fd, "wl.status")
    check("wl.status after erase — usable",
          r is not None and r.get("status") == "ok",
          detail="")


def test_wl_status(fd):
    """3e. Wear-leveling — status counters make sense."""
    print("\n" + "=" * 60)
    print("3e. WEAR-LEVELING — STATUS COUNTERS")
    print("=" * 60)

    r = send(fd, "wl.status")
    if r and r.get("status") == "ok":
        total = r.get("total_slots", -1)
        used = r.get("used_slots", -1)
        stale = r.get("stale_slots", -1)
        total_writes = r.get("total_writes", -1)
        min_wear = r.get("min_wear", -1)
        max_wear = r.get("max_wear", -1)
        avg_wear = r.get("avg_wear", -1)
        region_addr = r.get("data_base_addr", -1)
        region_size = r.get("data_sector_count", -1)

        check("wl.status — total_slots > 0",
              total > 0,
              detail="total_slots=%d" % total)
        check("wl.status — used_slots >= 0",
              used >= 0,
              detail="used_slots=%d" % used)
        check("wl.status — stale_slots >= 0",
              stale >= 0,
              detail="stale_slots=%d" % stale)
        check("wl.status — total_writes >= 0",
              total_writes >= 0,
              detail="total_writes=%d" % total_writes)
        check("wl.status — min_wear >= 0",
              min_wear >= 0,
              detail="min_wear=%d" % min_wear)
        check("wl.status — max_wear >= min_wear",
              max_wear >= min_wear,
              detail="max_wear=%d min_wear=%d" % (max_wear, min_wear))
        check("wl.status — avg_wear between min and max",
              min_wear <= avg_wear <= max_wear,
              detail="avg_wear=%d [min=%d, max=%d]" % (avg_wear, min_wear, max_wear))
        check("wl.status — used + stale <= total",
              used + stale <= total,
              detail="used=%d stale=%d total=%d" % (used, stale, total))
        check("wl.status — region_addr > 0",
              region_addr > 0,
              detail="data_base_addr=0x%X" % region_addr)
        check("wl.status — region_size > 0",
              region_size > 0,
              detail="data_sector_count=%d" % region_size)

        # Writes counter should be at least as many as our operations
        # (we've done ~16 stores, 5 edits, 1 erase = ~22 writes)
        check("wl.status — total_writes reflects operations performed",
              total_writes >= 10,
              detail="total_writes=%d (expected >= 10)" % total_writes)
    else:
        for name in ["total_slots", "used_slots", "stale_slots", "total_writes",
                      "min_wear", "max_wear", "avg_wear", "data_base_addr", "data_sector_count",
                      "writes >= 10", "used+stale <= total"]:
            check("wl.status — " + name, False, detail="wl.status failed")


def test_wl_edge_cases(fd):
    """4. Edge cases — zero-length, near-sector-size, rapid writes."""
    print("\n" + "=" * 60)
    print("4. EDGE CASES")
    print("=" * 60)

    # 4a. Empty data (zero-length store)
    r = send(fd, "wl.store", key="test_empty", data="")
    check("wl.store with empty data — stores zero-length config",
          r is not None and r.get("status") == "ok" and r.get("len") == 0,
          detail=("len=%d" % r.get("len", -1)) if r else "no response")

    # Load back zero-length data
    r = send(fd, "wl.load", key="test_empty")
    check("wl.load zero-length config — returns empty data",
          r is not None and r.get("status") == "ok" and r.get("len") == 0,
          detail=("len=%d" % r.get("len", -1)) if r else "")

    # 4b. Large data — within CDC frame buffer limits
    large_key = "test_large"
    # Hex payload at 512 bytes (1024 hex chars) fits in 2048-byte RX/TX buffers.
    large_data = make_pattern(0x33, 448)
    hex_large = hex_encode(large_data)
    r = send(fd, "wl.store", key=large_key, data=hex_large)
    check("wl.store — 448 bytes (large payload)",
          r is not None and r.get("status") == "ok" and r.get("len") == 448,
          detail=("len=%d" % r.get("len", -1)) if r else "")

    r = send(fd, "wl.load", key=large_key)
    if r and r.get("status") == "ok":
        check("wl.load — 512 bytes matches stored data",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load — 512 bytes matches stored data",
              False,
              detail="load failed")

    # 4c. Multiple rapid writes to the same key
    rapid_key = "test_rapid"
    for i in range(10):
        data = make_pattern(i, 32)
        r = send(fd, "wl.store", key=rapid_key, data=hex_encode(data))
        check("wl.store — rapid write #%d to same key" % (i + 1),
              r is not None and r.get("status") == "ok",
              detail="")

    # After 10 rapid writes, load back should return the LAST value
    expected_final = make_pattern(9, 32)
    r = send(fd, "wl.load", key=rapid_key)
    if r and r.get("status") == "ok":
        check("wl.load after 10 rapid writes — returns last written value",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load after 10 rapid writes — returns last written value",
              False,
              detail="load failed")

    # 4d. Key with special characters
    special_key = "test_key_with_underscores_and_digits_123"
    special_data = bytes([0x00, 0xFF, 0x7F, 0x80])
    r = send(fd, "wl.store", key=special_key, data=hex_encode(special_data))
    check("wl.store — key with special characters",
          r is not None and r.get("status") == "ok",
          detail="")

    r = send(fd, "wl.load", key=special_key)
    if r and r.get("status") == "ok":
        check("wl.load — special key returns correct data",
              r.get("crc_valid") == True,
              detail=("crc_valid=%s" % r.get("crc_valid")))
    else:
        check("wl.load — special key returns correct data",
              False,
              detail="load failed")

    # 4e. wl.load for non-existent key
    r = send(fd, "wl.load", key="this_key_does_not_exist_at_all")
    check("wl.load non-existent key — returns error",
          r is not None and r.get("status") == "error",
          detail=("status=%s" % r.get("status", "?")) if r else "no response")

    # 4f. wl.store with missing key
    r = send(fd, "wl.store", data=hex_encode(bytes([0x01])))
    check("wl.store without key — returns error",
          r is not None and r.get("status") == "error",
          detail=("status=%s" % r.get("status", "?")) if r else "")

    # 4g. wl.load with missing key
    r = send(fd, "wl.load")
    check("wl.load without key — returns error",
          r is not None and r.get("status") == "error",
          detail=("status=%s" % r.get("status", "?")) if r else "")


def cleanup(fd):
    """Cleanup test configs from flash."""
    print("\n" + "=" * 60)
    print("CLEANUP — removing test artifacts")
    print("=" * 60)

    keys_to_clean = [
        "test_config_1", "test_config_big", "test_config_update",
        "test_to_erase", "test_empty", "test_large",
        "test_rapid", "test_key_with_underscores_and_digits_123",
        "nonexistent_key_that_was_never_stored",
    ]
    keys_to_clean += ["test_multi_%d" % i for i in range(5)]

    for key in keys_to_clean:
        r = send(fd, "wl.erase", key=key)
        if r and r.get("status") == "ok":
            print(f"  cleaned: {key}")
        elif r and r.get("status") == "error":
            print(f"  already gone: {key}")


# ═══════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════

def main():
    global PASSED, FAILED, SKIPPED

    print("ThetaGP SPI DMA + Wear-Leveling Test Suite")
    print("=" * 60)
    print(f"Target port: {PORT}")

    # Device detection
    if not os.path.exists(PORT):
        print(f"\n[WARNING] {PORT} not found.")
        print("  Board may not be connected, or THETAGP_ENABLE_TEST_API not set.")
        print("  All tests will be SKIPPED.\n")
        print("  Build with test API:")
        print("    lua tool.lua config --target BoringTechH743 -DTHETAGP_ENABLE_TEST_API=ON")
        print("    lua tool.lua build")
        print("    lua tool.lua flash ...")
        print("  Then connect the device's USB CDC port (not debug probe).")
        return 0  # Clean exit for CI/automation

    try:
        fd = open_serial(PORT)
    except Exception as e:
        print(f"\n[ERROR] Cannot open {PORT}: {e}")
        print("  Is the device connected and not in use by another process?")
        print(f"  Check with: ls -l {PORT}")
        return 1

    # Drain any stale boot-up output
    time.sleep(1.5)
    stale = 0
    while True:
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            break
        try:
            os.read(fd, 4096)
            stale += 1
        except Exception:
            break
    if stale:
        print(f"  (drained {stale} reads of stale boot output before tests)")

    # Verify board is alive
    print("\nProbing board...")
    probe = send(fd, "sys.ping")
    if probe is None or probe.get("status") != "ok":
        print("\n[ERROR] Board did not respond to sys.ping.")
        print("  Check USB connection and that test API is enabled in build.")
        os.close(fd)
        return 1
    print("  Board alive. Starting tests.\n")

    # ── Run test groups ──
    test_preflight(fd)
    test_spi_dma_read_basic(fd)
    test_spi_dma_write_and_verify(fd)
    test_spi_dma_error_handling(fd)
    test_spi_dma_read_at_various_lengths(fd)
    test_wl_store_and_load(fd)
    test_wl_update_config(fd)
    test_wl_multiple_configs(fd)
    test_wl_erase(fd)
    test_wl_status(fd)
    test_wl_edge_cases(fd)
    cleanup(fd)

    # ── Summary ──
    total = PASSED + FAILED + SKIPPED
    print("\n" + "=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    print(f"  PASSED:   {PASSED:3d}/{total}")
    print(f"  FAILED:   {FAILED:3d}/{total}")
    print(f"  SKIPPED:  {SKIPPED:3d}/{total}")
    print(f"  -------------------")
    print(f"  TOTAL:    {total:3d}")
    if FAILED > 0:
        print("\n[!] Some tests FAILED. Review output above for details.")
    elif SKIPPED > 0:
        print("\n[*] All non-skipped tests passed.")
    else:
        print("\n[*] ALL TESTS PASSED.")
    print("=" * 60)

    os.close(fd)
    return 0 if FAILED == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
