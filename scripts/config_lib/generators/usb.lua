-- scripts/config_lib/generators/usb.lua

local M = {}

-- Peripheral mapping
local PERIPHERAL_MAP = {
    USB1 = "USB1_OTG",
    USB2 = "USB2_OTG",
    ULPI = "USB_ULPI",
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
            "THETAGP_CFG_" .. peripheral_value
        ))
    end
    
    if usb_config.speed then
        local speed_value = SPEED_MAP[usb_config.speed] or "FS"
        table.insert(lines, string.format(
            '#define %s',
            "THETAGP_CFG_USB_" .. speed_value
        ))
    end
    
    return lines
end

return M
