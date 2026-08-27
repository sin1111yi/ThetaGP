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
# firmware test domain exposes flash/mempool commands instead:
#   test.flash_info, test.mempool_info, test.flash_read (read-only)
#   test.spi_mode, test.erase_sector, test.compaction, test.chip_erase
# Destructive commands (chip_erase / erase_sector / compaction) are
# NOT executed by this suite to avoid wiping the SPI flash.

import os
import time
from cdc_serial import open_serial, TestContext


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

    # ── Stage 2: test domain (flash/mempool, read-only) ─────────────
    print("\n=== Stage 2: test domain (read-only commands) ===")

    # test.flash_info — read-only flash geometry
    r = ctx.send("test.flash_info")
    ok("flash_info",
       r and r.get("status") == "ok"
       and r.get("sizeBytes") is not None
       and r.get("sectorSize") is not None)

    # test.mempool_info — read-only pool stats
    r = ctx.send("test.mempool_info")
    ok("mempool_info",
       r and r.get("status") == "ok"
       and r.get("total") is not None
       and r.get("free") is not None)

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

    # sys.get_usage — aggregate system resource report
    r = ctx.send("sys.get_usage")
    ok("get_usage",
       r and r.get("status") == "ok"
       and r.get("cpu_load_percent") is not None
       and r.get("task_count") is not None
       and r.get("mem_pool_total") is not None
       and r.get("flash_total_sectors") is not None)

    # Destructive commands (chip_erase / erase_sector / compaction)
    # are intentionally NOT executed — they would wipe the SPI flash.
    # They are validated by the profile test suite instead.

    # ── Summary ────────────────────────────────────────────────────────
    ok_ = ctx.summary()
    os.close(fd)
    return 0 if ok_ else 1


if __name__ == "__main__":
    exit(main())
