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
# Test: CDC JSON protocol — sys domain + test mode
# Target: CDC ACM virtual serial port on firmware built with
#         -DTHETAGP_ENABLE_TEST_API=ON

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

    # ── Stage 2: test mode ─────────────────────────────────────────────
    print("\n=== Stage 2: test mode ===")

    r = ctx.send("test.set_mode", mode=1)
    ok("set_mode INJECT",
       r and r.get("status") == "ok" and r.get("mode") == 1)

    r = ctx.send("test.get_mode")
    ok("get_mode",
       r and r.get("mode") == 1 and r.get("mode_name") == "INJECT")

    r = ctx.send("test.inject_gamepad_state",
                 buttons=65536, dpad=5,
                 lx=32767, ly=32767, rx=0, ry=0, lt=255, rt=128)
    ok("inject_gamepad_state", r and r.get("status") == "ok")

    r = ctx.send("test.get_gamepad_state")
    ok("get_gamepad_state", r and r.get("buttons") is not None)

    r = ctx.send("test.inject_hid_report",
                 buttons=255, dpad=2,
                 l_x_axis=128, l_y_axis=128, r_x_axis=64, r_y_axis=192)
    ok("inject_hid_report", r and r.get("status") == "ok")

    r = ctx.send("test.get_hid_report")
    ok("get_hid_report",
       r and r.get("buttons") is not None and "dpad" in r)

    r = ctx.send("test.set_override", point="gamepad_state", enabled=True)
    ok("set_override ON",
       r and r.get("status") == "ok" and r.get("enabled") is True)

    r = ctx.send("test.set_override", point="hid_report", enabled=False)
    ok("set_override OFF",
       r and r.get("status") == "ok" and r.get("enabled") is False)

    r = ctx.send("test.clear_inject", point="all")
    ok("clear_inject", r and r.get("status") == "ok")

    r = ctx.send("test.get_status")
    ok("get_status",
       r and r.get("gamepad_history_count") is not None
       and r.get("gamepad_inject_queued") is not None)

    r = ctx.send("test.get_history", type="gamepad_state", count=3)
    ok("get_history gamepad",
       r and r.get("count") is not None and "entries" in r)

    r = ctx.send("test.get_history", type="hid_report", count=3)
    ok("get_history hid",
       r and r.get("count") is not None and "entries" in r)

    r = ctx.send("test.clear_history", type="all")
    ok("clear_history", r and r.get("status") == "ok")

    r = ctx.send("test.reset")
    ok("reset", r and r.get("status") == "ok")

    r = ctx.send("test.get_mode")
    ok("mode after reset = PASS_THRU",
       r and r.get("mode") == 0 and r.get("mode_name") == "PASS_THRU")

    r = ctx.send("test.set_mode", mode=0)
    ok("set_mode PASS_THRU", r and r.get("status") == "ok")

    # ── Summary ────────────────────────────────────────────────────────
    ok_ = ctx.summary()
    os.close(fd)
    return 0 if ok_ else 1


if __name__ == "__main__":
    exit(main())
