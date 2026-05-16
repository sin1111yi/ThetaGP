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
    identifier = "BoringTechH743",
    name = "BoringTech-H743",
    mcu = "STM32H743xx",
    mcu_series = "STM32H7",
}

M.BoardConfig = {
    necessary = {
        led0 = { pin = "PC0", active_low = false },
        keypad = {
            drive_mode = "scan_matrix",
            active_mode = "low",
            drive_pins = {
                { pin = "PD8" },
                { pin = "PD9" },
            },
            sense_pins = {
                { pin = "PC4" },
                { pin = "PC5" },
            },
            key_map = {
                --         PC4  PC5
                --   PD8   0    1
                --   PD9   2    3
                { 0, 1 },
                { 2, 3 },
            },
            button_map = {
                [0] = "B1",
                [1] = "B2",
                [2] = "B3",
                [3] = "B4",
            },
        },
        usb = {
            hw_periph = "USB2",
            speed = "full_speed",
        },
        bus = {
            uart = {
                {
                    bind = "logger",
                    peripheral = "UART1",
                    tx = "PA9",
                    rx = "PA10",
                }
            },
            spi = {
                {
                    bind = "flash",
                    peripheral = "SPI2",
                    sclk = "PB13",
                    mosi = "PB15",
                    miso = "PB14",
                    ncs = "PB12",
                }
            }
        },
    },
}

return M
