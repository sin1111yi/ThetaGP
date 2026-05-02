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
# along with this program. If not, see <https://www.gnu.org/licenses/>.
]]

-- scripts/config_lib/validators/usb.lua

local utils = require("utils")

local M = {}

-- Valid hardware peripherals
local VALID_HW_PERIPHS = { "USB1", "USB2", "ULPI" }

-- Valid speeds
local VALID_SPEEDS = { "high_speed", "full_speed" }

function M.validate(usb_config)
    if not usb_config then
        return true, nil  -- USB configuration is optional
    end
    
    if usb_config.hw_periph then
        if not utils.contains(VALID_HW_PERIPHS, usb_config.hw_periph) then
            return false, string.format(
                "Invalid hw_periph '%s'. Valid values: %s",
                usb_config.hw_periph,
                table.concat(VALID_HW_PERIPHS, ", ")
            )
        end
    end
    
    if usb_config.speed then
        if not utils.contains(VALID_SPEEDS, usb_config.speed) then
            return false, string.format(
                "Invalid speed '%s'. Valid values: %s",
                usb_config.speed,
                table.concat(VALID_SPEEDS, ", ")
            )
        end
    end
    
    return true, nil
end

return M
