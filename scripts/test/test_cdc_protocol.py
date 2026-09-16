#!/usr/bin/env python3
# This file is a part of ThetaGP.
#
# ThetaGP is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ThetaGP is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# Test: CDC JSON protocol — sys domain + test domain
# Target: CDC ACM virtual serial port on firmware built with
#         -DBUILD_TEST_API=ON (source sees THETAGP_CFG_BUILD_TEST_API=1)
#
# Note: The gamepad/HID injection commands (test.set_mode, inject_*,
# set_override, etc.) were removed in commit 2408e32 (refactor(test):
# remove TestInjector and gamepad/HID report hooks). The current
# firmware test domain exposes flash/memory commands instead:
#   test.flash_info, test.mem_info, test.flash_read (read-only)
#   test.spi_mode, test.erase_sector, test.compaction, test.chip_erase
# Destructive commands (chip_erase / erase_sector / compaction) are
# NOT executed by this suite to avoid wiping the SPI flash.
#
# Board-aware: not every board is fitted with the external SPI flash chip, and
# test.flash_info / test.flash_read / test.spi_mode sit inside
# #ifdef THETAGP_CFG_HAS_FLASH (testcmds.cpp:265-272), so on a board without it
# they are not in the build and answer as unknown commands. The suite asks the
# board (sys.get_usage.ext_flash_total_sectors) instead of assuming an answer:
# with no chip those checks are SKIP with the reason, and with the chip they are
# exactly as strict as before. Stage 2 as a whole is SKIP when the test domain
# is absent (test.mem_info is the probe), and so are the three Stage 3 checks
# that compare sys.get_usage against the raw test.mem_info readout — a build
# without the domain has nothing to compare, so "this build has no test domain"
# no longer reads as a list of failures.

import json
import os
import time
from cdc_serial import open_serial, TestContext

# The dispatcher answers a command it has no handler for with this reason
# (dispatcher.cpp:95-102). It is the only thing that separates "this command is
# not in this build" from "the command is there and rejected its arguments":
# both carry error_code 1, because the length guard of test.flash_read answers
# with error_code 1 too (testcmds.cpp:221-228). A check that only asks "did an
# error come back" therefore scores those two opposite results the same way.
UNKNOWN_CMD_REASON = "unknown command"

# Memory regions exported by the linker script
RAM_REGIONS = ("dtcm", "axi", "d2", "d3", "itcm")
REGIONS = ("flash",) + RAM_REGIONS

# Region capacities = linker script LENGTH. Board constants: only a board
# change moves them, so asserting them is build independent.
REGION_SIZES = {
    "flash": 2097152,   # 2 MB
    "dtcm": 131072,     # 128 KB
    "axi": 524288,      # 512 KB
    "d2": 294912,       # 288 KB
    "d3": 65536,        # 64 KB
    "itcm": 64512,      # 63 KB
}

# sys.get_usage response fields (protocol 1.1, four-item scope)
USAGE_FIELDS = (
    "cpu_load_percent", "task_count",
    "mcu_flash_used_bytes", "mcu_flash_total_bytes",
    "ram_used_bytes", "ram_total_bytes", "ram_reserved_bytes",
    "ram_dtcm_used_bytes", "ram_axi_used_bytes",
    "ram_d2_used_bytes", "ram_d3_used_bytes", "ram_itcm_used_bytes",
    "ext_flash_total_sectors", "ext_flash_used_sectors",
    "ext_flash_free_sectors", "profile_count",
)


def field_int(resp, key):
    """Response field as int, or None when missing/not an integer."""
    value = (resp or {}).get(key)
    return value if isinstance(value, int) else None


def field(resp, key):
    """Response field, or None when there is no usable response."""
    if not isinstance(resp, dict):
        return None
    return resp.get(key)


def status_of(resp):
    """The status field of a response, or None when there is none."""
    return field(resp, "status")


def cmd_routed(resp):
    """True when an answer came from the command's own handler.

    A command this build does not have is answered by the dispatcher itself,
    with the reason "unknown command" and error_code 1 (dispatcher.cpp:95-102).
    Every other answer — including the error of a handler that rejected its
    arguments — came from a handler, so the command exists. test.flash_info and
    test.flash_read sit inside #ifdef THETAGP_CFG_HAS_FLASH (testcmds.cpp:265),
    so on a board without the SPI flash chip they are exactly this case.
    """
    return isinstance(resp, dict) and resp.get("reason") != UNKNOWN_CMD_REASON


def brief(resp):
    """A response on one line, for a check detail or a skip reason."""
    if resp is None:
        return "(no response: timeout)"
    return json.dumps(resp, separators=(",", ":"))


def has_fields(resp, keys):
    """True when every named field is present and integral."""
    for key in keys:
        if field_int(resp, key) is None:
            return False
    return True


def eq_fields(resp_a, key_a, resp_b, key_b):
    """True when both fields are present and equal."""
    value = field_int(resp_a, key_a)
    return value is not None and value == field_int(resp_b, key_b)


def le_fields(resp, low_key, high_key):
    """True when low_key <= high_key, both present and integral."""
    low = field_int(resp, low_key)
    high = field_int(resp, high_key)
    if low is None or high is None:
        return False
    return low <= high


def sum_regions(resp, key_fmt):
    """Sum of the five RAM region fields named by key_fmt, or None."""
    total = 0
    for region in RAM_REGIONS:
        value = field_int(resp, key_fmt.format(region=region))
        if value is None:
            return None
        total += value
    return total


def region_used_ok(resp, region):
    """used == end - base and used <= size for one region."""
    base = field_int(resp, f"{region}_base")
    end = field_int(resp, f"{region}_end")
    size = field_int(resp, f"{region}_size")
    used = field_int(resp, f"{region}_used")
    if base is None or end is None or size is None or used is None:
        return False
    return used == end - base and used <= size


def reserve_ok(mem):
    """reserved == stack + heap and live == sum(RAM used) - reserved."""
    reserved = field_int(mem, "ram_reserved_bytes")
    stack = field_int(mem, "stack_bytes")
    heap = field_int(mem, "heap_bytes")
    live = field_int(mem, "ram_live_bytes")
    ram_used = sum_regions(mem, "{region}_used")
    if (reserved is None or stack is None or heap is None
            or live is None or ram_used is None):
        return False
    return reserved == stack + heap and live == ram_used - reserved


def ext_flash_ok(usage):
    """used + free <= total for the external SPI flash."""
    total = field_int(usage, "ext_flash_total_sectors")
    used = field_int(usage, "ext_flash_used_sectors")
    free = field_int(usage, "ext_flash_free_sectors")
    if total is None or used is None or free is None:
        return False
    return used + free <= total


def ram_totals_match(usage, mem):
    """usage aggregates agree with the per-region raw readout of mem_info."""
    used = sum_regions(mem, "{region}_used")
    total = sum_regions(mem, "{region}_size")
    reserved = field_int(mem, "ram_reserved_bytes")
    if used is None or total is None or reserved is None:
        return False
    return (field_int(usage, "ram_used_bytes") == used
            and field_int(usage, "ram_total_bytes") == total
            and field_int(usage, "ram_reserved_bytes") == reserved)


# sys.get_task_info — per-task accounting fields. avgExecUs / avgDeltaUs are
# the two 8-sample moving averages carried in tenths of a microsecond by the
# scheduler; the handler reports them in whole microseconds.
TASK_INFO_FIELDS = ("desiredUs", "avgCycleUs", "actualHz", "maxExecUs",
                    "avgExecUs", "totalExecUs", "avgDeltaUs")

# Existing TIDs: 0 SYSTEM/LOAD, 1 SYSTEM/UPDATE, 2 GAMEPAD/CORE,
# 3 TEST/CMD_PROC. TIDs beyond the build's task set answer with
# ERR_INVALID_PARAM instead.
TASK_IDS = (0, 1, 2, 3)


def task_info_ok(info):
    """Per-task invariants for one sys.get_task_info response."""
    if not has_fields(info, TASK_INFO_FIELDS):
        return False
    if info["avgExecUs"] > info["maxExecUs"]:
        return False
    if info["totalExecUs"] < info["avgExecUs"]:
        return False
    cycle = info["avgCycleUs"]
    hz = info["actualHz"]
    return hz == (1000000 // cycle if cycle > 0 else 0)


def task_info_sigma_us_per_s(tasks):
    """Σ(avgExecUs × actualHz) over the tasks, in microseconds per second.

    The sum is where the execution time of every taskFunc call would land if
    the scheduler did nothing else, so it is comparable against the CPU load
    the SYSTEM/LOAD task derives from the scheduler's own execution timer.
    """
    total = 0
    for info in tasks:
        exec_us = field_int(info, "avgExecUs")
        hz = field_int(info, "actualHz")
        if exec_us is None or hz is None:
            return None
        total += exec_us * hz
    return total


def main():
    fd = open_serial()
    time.sleep(1)
    ctx = TestContext(fd)
    ok = ctx.check

    # ── Stage 1: sys domain ────────────────────────────────────────────
    print("\n=== Stage 1: sys domain ===")
    r = ctx.send("sys.ping")
    ok("sys.ping", r and r.get("status") == "ok")

    r = ctx.send("sys.get_fw_version")
    ok("sys.get_fw_version",
       r and r.get("status") == "ok" and "version" in r)

    r = ctx.send("test.no_such_cmd")
    ok("unknown cmd",
       r and r.get("status") == "error" and r.get("error_code") == 1)

    # ── Stage 2: test domain (flash/memory, read-only) ─────────────────
    print("\n=== Stage 2: test domain (read-only commands) ===")

    # Precondition: does this build carry the test domain at all? It is compiled
    # in by the test-API switch (THETAGP_CFG_BUILD_TEST_API), and test.mem_info is
    # the one command in it that no board-specific #ifdef removes, so it is the probe.
    # Without this precondition a build without the domain scores every check below as
    # FAIL, which reads like a regression instead of "this board is not the one this
    # stage is about".
    mem_probe = ctx.send("test.mem_info")
    mem = mem_probe if status_of(mem_probe) == "ok" else None
    if mem is not None:
        ok("test domain present", True, detail="test.mem_info answers")
    else:
        ctx.skip("test domain present",
                 "test.mem_info -> %s: this build has no test domain "
                 "(the test-API switch is off), so the whole of Stage 2 is out of "
                 "scope for it" % brief(mem_probe))

    # Board fact, read from the board itself rather than assumed: not every
    # board is fitted with the external SPI flash chip. When it is absent the
    # firmware reports ext_flash_total_sectors = 0 and compiles test.flash_info,
    # test.flash_read and test.spi_mode out of the build entirely
    # (#ifdef THETAGP_CFG_HAS_FLASH, testcmds.cpp:265-272), so they answer as
    # unknown commands. Those checks are then "this board does not have the
    # thing under test" — SKIP, with the reason — and stay exactly as strict as
    # before on a board that does have the chip.
    usage_probe = ctx.send("sys.get_usage")
    ext_sectors = field_int(usage_probe, "ext_flash_total_sectors")
    has_ext_flash = (mem is not None and ext_sectors is not None
                     and ext_sectors > 0)
    if mem is None:
        flash_reason = "test domain absent on this board (see above)"
    else:
        flash_reason = (
            "no external SPI flash on this board: sys.get_usage reports "
            "ext_flash_total_sectors=%s, and test.flash_* / test.spi_mode are "
            "compiled out (#ifdef THETAGP_CFG_HAS_FLASH), so they answer as "
            "unknown commands" % ext_sectors
        )

    def check_test(name, passed, detail=""):
        """A check of the test domain: SKIP when the domain is absent."""
        if mem is not None:
            ok(name, passed, detail=detail)
        else:
            ctx.skip(name, "test domain absent on this board")

    def check_flash(name, passed, detail=""):
        """A flash-dependent check: SKIP (with the board reason) when this
        board has no external SPI flash, otherwise scored as before."""
        if has_ext_flash:
            ok(name, passed, detail=detail)
        else:
            ctx.skip(name, flash_reason)

    # test.flash_info — read-only flash geometry
    r = ctx.send("test.flash_info")
    check_flash("flash_info",
                status_of(r) == "ok"
                and field(r, "sizeBytes") is not None
                and field(r, "sectorSize") is not None)

    # test.mem_info — raw linker-symbol readout of the six memory regions
    # (the response of the precondition above, read again as a value)
    check_test("mem_info fields",
               mem is not None
               and all(has_fields(mem, [f"{region}_{suffix}"
                                        for suffix in ("base", "end", "size", "used")])
                       for region in REGIONS)
               and has_fields(mem, ["ram_reserved_bytes", "stack_bytes",
                                    "heap_bytes", "ram_live_bytes"]))

    # Region capacities are board constants (build independent)
    check_test("mem_info region sizes",
               mem is not None
               and all(field_int(mem, f"{region}_size") == size
                       for region, size in REGION_SIZES.items()))

    # used == end - base and used <= size, for every region
    check_test("mem_info used arithmetic",
               mem is not None
               and all(region_used_ok(mem, region) for region in REGIONS))

    # reserved = stack + heap; live = sum(RAM used) - reserved
    check_test("mem_info reserve arithmetic", reserve_ok(mem))

    # test.flash_read — read 32 bytes from flash start (read-only). Two
    # separate facts, scored separately: the command is there at all, and it
    # honours the length it was asked for.
    read_ok = ctx.send("test.flash_read", addr=0, len=32)
    check_flash("flash_read domain present", cmd_routed(read_ok),
                detail="test.flash_read answers as a routed command, not as "
                       "the dispatcher's unknown-command error")
    check_flash("flash_read 32B@0",
                status_of(read_ok) == "ok" and field(read_ok, "lenRead") == 32)

    # test.flash_read — an over-long length has to be rejected. This is the
    # check that used to pass for the wrong reason: "length rejected" and
    # "command does not exist" are opposite results and both arrive as
    # {status:error,error_code:1} (testcmds.cpp:221-228 vs
    # dispatcher.cpp:95-102), so the domain assertion above is what makes this
    # one mean what it says — if the command is missing, the length guard has
    # not been demonstrated and this reports FAIL instead of a free PASS.
    read_bad = ctx.send("test.flash_read", addr=0, len=99999)
    check_flash("flash_read invalid len rejected",
                cmd_routed(read_bad)
                and status_of(read_bad) == "error"
                and field(read_bad, "lenRead") is None,
                detail="len=99999 -> %s" % brief(read_bad))

    # test.spi_mode — set DMA mode (default, non-destructive)
    r = ctx.send("test.spi_mode", mode=1)
    check_flash("spi_mode DMA",
                status_of(r) == "ok" and field(r, "mode") == 1)

    # ── Stage 3: sys.get_usage — four-item resource report ─────────────
    print("\n=== Stage 3: sys.get_usage (four-item scope) ===")

    usage = ctx.send("sys.get_usage")
    ok("get_usage fields",
       usage is not None and usage.get("status") == "ok"
       and has_fields(usage, USAGE_FIELDS))

    # Aggregate consistency — build independent regression checks
    usage_ram_used = sum_regions(usage, "ram_{region}_used_bytes")
    ok("get_usage ram sum",
       usage_ram_used is not None
       and field_int(usage, "ram_used_bytes") == usage_ram_used)

    ok("get_usage ram bounds",
       le_fields(usage, "ram_used_bytes", "ram_total_bytes")
       and le_fields(usage, "ram_reserved_bytes", "ram_used_bytes"))

    ok("get_usage flash bounds",
       le_fields(usage, "mcu_flash_used_bytes", "mcu_flash_total_bytes"))

    ok("get_usage ext_flash bounds", ext_flash_ok(usage))

    # Region values must match the raw readout of test.mem_info. These three
    # checks need both domains at once — sys.get_usage on one side, the raw
    # test.mem_info on the other — so a build without the test domain has
    # nothing to compare against and they are SKIP with the reason, exactly as
    # in Stage 2 (check_test), instead of three failures that read like a
    # regression of a comparison the build was never able to make. With the
    # domain present they stay as strict as before.
    check_test("get_usage == mem_info regions",
               all(eq_fields(usage, f"ram_{region}_used_bytes", mem, f"{region}_used")
                   for region in RAM_REGIONS))

    check_test("get_usage flash == mem_info flash",
               eq_fields(usage, "mcu_flash_used_bytes", mem, "flash_used")
               and eq_fields(usage, "mcu_flash_total_bytes", mem, "flash_size"))

    check_test("get_usage ram totals == mem_info", ram_totals_match(usage, mem))

    # ── Stage 4: sys.get_task_info — per-task time accounting ──────────
    print("\n=== Stage 4: sys.get_task_info (per-task accounting) ===")

    bad_tid = ctx.send("sys.get_task_info", tid=99)
    ok("task_info invalid TID rejected",
       bad_tid is not None and bad_tid.get("status") == "error"
       and bad_tid.get("error_code") == 2)

    tasks = []
    for tid in TASK_IDS:
        info = ctx.send("sys.get_task_info", tid=tid)
        if info is not None and info.get("status") == "ok":
            tasks.append(info)
        ok(f"task_info tid {tid}", task_info_ok(info))

    ok("task_info task count", len(tasks) >= 4,
       detail=f"{len(tasks)} of {len(TASK_IDS)} TIDs answered")

    # Time decomposition: Σ(avgExecUs × actualHz) is what the task functions
    # themselves consume per second, while cpu_load_percent is measured on the
    # very same taskFunc interval, so the two describe the same quantity. The
    # 1.3 ceiling is a sanity bound — a missing µs/10th-µs scaling factor
    # would overshoot it by 10×.
    load = field_int(ctx.send("sys.get_usage"), "cpu_load_percent")
    sigma = task_info_sigma_us_per_s(tasks)
    sigma_percent = None if sigma is None else sigma / 10000.0
    if sigma_percent is None:
        print("  Σ(avgExecUs×actualHz) unavailable")
    else:
        print(f"  Σ(avgExecUs×actualHz) = {sigma} µs/s = "
              f"{sigma_percent:.2f}% ; cpu_load_percent = {load}%")
    ok("task_info exec accounting bounds",
       sigma_percent is not None and sigma_percent <= 130.0,
       detail=f"Σ={sigma_percent}% vs load={load}%")

    # Destructive commands (chip_erase / erase_sector / compaction)
    # are intentionally NOT executed — they would wipe the SPI flash.
    # They are validated by the profile test suite instead.

    # ── Summary ────────────────────────────────────────────────────────
    ok_ = ctx.summary()
    os.close(fd)
    return 0 if ok_ else 1


if __name__ == "__main__":
    exit(main())
