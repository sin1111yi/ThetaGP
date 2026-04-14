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
