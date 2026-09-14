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
#         -DTHETAGP_CFG_TEST=ON
#
# Note: The gamepad/HID injection commands (test.set_mode, inject_*,
# set_override, etc.) were removed in commit 2408e32 (refactor(test):
# remove TestInjector and gamepad/HID report hooks). The current
# firmware test domain exposes flash/memory commands instead:
#   test.flash_info, test.mem_info, test.flash_read (read-only)
#   test.spi_mode, test.erase_sector, test.compaction, test.chip_erase
# Destructive commands (chip_erase / erase_sector / compaction) are
# NOT executed by this suite to avoid wiping the SPI flash.

import os
import time
from cdc_serial import open_serial, TestContext

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

    # test.flash_info — read-only flash geometry
    r = ctx.send("test.flash_info")
    ok("flash_info",
       r and r.get("status") == "ok"
       and r.get("sizeBytes") is not None
       and r.get("sectorSize") is not None)

    # test.mem_info — raw linker-symbol readout of the six memory regions
    r = ctx.send("test.mem_info")
    mem = r if isinstance(r, dict) and r.get("status") == "ok" else None
    ok("mem_info fields",
       mem is not None
       and all(has_fields(mem, [f"{region}_{suffix}"
                                for suffix in ("base", "end", "size", "used")])
               for region in REGIONS)
       and has_fields(mem, ["ram_reserved_bytes", "stack_bytes",
                            "heap_bytes", "ram_live_bytes"]))

    # Region capacities are board constants (build independent)
    ok("mem_info region sizes",
       mem is not None
       and all(field_int(mem, f"{region}_size") == size
               for region, size in REGION_SIZES.items()))

    # used == end - base and used <= size, for every region
    ok("mem_info used arithmetic",
       mem is not None
       and all(region_used_ok(mem, region) for region in REGIONS))

    # reserved = stack + heap; live = sum(RAM used) - reserved
    ok("mem_info reserve arithmetic", reserve_ok(mem))

    # test.flash_read — read 32 bytes from flash start (read-only)
    r = ctx.send("test.flash_read", addr=0, len=32)
    ok("flash_read 32B@0",
       r and r.get("status") == "ok" and r.get("lenRead") == 32)

    # test.flash_read — invalid len rejected
    r = ctx.send("test.flash_read", addr=0, len=99999)
    ok("flash_read invalid len",
       r and r.get("status") == "error")

    # test.spi_mode — set DMA mode (default, non-destructive)
    r = ctx.send("test.spi_mode", mode=1)
    ok("spi_mode DMA",
       r and r.get("status") == "ok" and r.get("mode") == 1)

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

    # Region values must match the raw readout of test.mem_info
    ok("get_usage == mem_info regions",
       mem is not None
       and all(eq_fields(usage, f"ram_{region}_used_bytes", mem, f"{region}_used")
               for region in RAM_REGIONS))

    ok("get_usage flash == mem_info flash",
       mem is not None
       and eq_fields(usage, "mcu_flash_used_bytes", mem, "flash_used")
       and eq_fields(usage, "mcu_flash_total_bytes", mem, "flash_size"))

    ok("get_usage ram totals == mem_info",
       mem is not None and ram_totals_match(usage, mem))

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
