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

-- scripts/config_lib/generators/usb.lua

local M = {}

-- Peripheral mapping
local PERIPHERAL_MAP = {
    USB1 = "OTG1",
    USB2 = "OTG2",
    ULPI = "ULPI",
}

-- Speed mapping
local SPEED_MAP = {
    high_speed = "HS",
    full_speed = "FS",
}

function M.generate(usb_config)
    local lines = {}
    
    if not usb_config then
        return lines
    end
    
    if usb_config.hw_periph then
        local peripheral_value = PERIPHERAL_MAP[usb_config.hw_periph] or "USB1_OTG"
        table.insert(lines, string.format(
            '#define %s',
            "USBHW_IF_" .. peripheral_value
        ))
    end
    
    if usb_config.speed then
        local speed_value = SPEED_MAP[usb_config.speed] or "FS"
        table.insert(lines, string.format(
            '#define %s',
            "USBHW_SPEED_" .. speed_value
        ))
    end
    
    return lines
end

return M
