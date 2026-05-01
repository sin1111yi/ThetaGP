--[[
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
# along with this program.
#
# If not, see <https://www.gnu.org/licenses/>.
]]

local M = {}

M.BoardInfo = {
    identifier = "ThetaGPH743",
    name = "ThetaGP-H743_Ver.Ultra",
    mcu = "STM32H743xx",
    mcu_series = "STM32H7",
}

M.BoardConfig = {
    necessary = {
        led0 = { pin = "PD8", active_low = true },
        keypad = {
            drive_mode = "scan_matrix",
            active_mode = "none",
            drive_pins = {
                { pin = "PD8" },
                { pin = "PD9" },
            },
            sense_pins = {
                { pin = "PC4" },
                { pin = "PC5" },
            },
            key_map = {
                { 0, 1 },
                { 2, 3 },
            },
            button_map = { "B1", "B2", "B3", "B4" },
        },
        usb = {
            hw_periph = "ULPI",
            speed = "high_speed",
        },
        bus = {
            uart = {
                {
                    bind = "debug",
                    peripheral = "UART1",
                    tx = "PB14",
                    rx = "PB15",
                    baud = 115200,
                }
            }
        },
    },
}

return M
