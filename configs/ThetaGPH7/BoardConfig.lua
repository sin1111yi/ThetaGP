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
    identifier = "ThetaGPH7",
    name = "ThetaGP_Ver.Ultra",
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
                { pin = "PB0" },
                { pin = "PB1" },
            },
            key_map = {
                { 0, 1 },
                { 2, 3 },
            },
        },
        usb = {
            hw_periph = "ULPI",
            speed = "high_speed",
        }
    },
}

return M
