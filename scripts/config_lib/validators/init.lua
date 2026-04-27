-- scripts/config_lib/validators/init.lua

local M = {}

-- Load all validator modules
M.board_info = require("validators.board_info")
M.keypad = require("validators.keypad")
M.usb = require("validators.usb")
M.uart = require("validators.uart")
M.log = require("validators.log")

-- Export validation functions
M.validate_board_info = M.board_info.validate
M.validate_keypad = M.keypad.validate
M.validate_usb = M.usb.validate
M.validate_uart = M.uart.validate
M.validate_log = M.log.validate

-- Main validation entry point
function M.validate_config(board_config)
    local errors = {}
    
    -- Validate BoardInfo
    local valid, err = M.validate_board_info(board_config.BoardInfo)
    if not valid then
        table.insert(errors, "BoardInfo: " .. err)
    end
    
    -- Validate necessary configuration
    if board_config.BoardConfig and board_config.BoardConfig.necessary then
        local necessary = board_config.BoardConfig.necessary
        
        -- Keypad is required
        if not necessary.keypad then
            table.insert(errors, "BoardConfig.necessary.keypad is required")
        else
            local valid, err = M.validate_keypad(necessary.keypad)
            if not valid then
                table.insert(errors, "Keypad: " .. err)
            end
        end
        
        -- USB configuration is optional
        if necessary.usb then
            local valid, err = M.validate_usb(necessary.usb)
            if not valid then
                table.insert(errors, "USB: " .. err)
            end
        end
        
        -- UART configuration is optional
        if necessary.bus then
            local valid, err = M.validate_uart(necessary.bus)
            if not valid then
                table.insert(errors, "Bus: " .. err)
            end
        end
    else
        table.insert(errors, "BoardConfig.necessary is required")
    end
    
    -- Validate log configuration
    if board_config.BoardConfig and board_config.BoardConfig.enable_log ~= nil then
        local valid, err = M.validate_log(board_config.BoardConfig)
        if not valid then
            table.insert(errors, "Log: " .. err)
        end
    elseif board_config.BoardConfig and board_config.BoardConfig.log_level ~= nil then
        local valid, err = M.validate_log(board_config.BoardConfig)
        if not valid then
            table.insert(errors, "Log: " .. err)
        end
    end

    -- Throw error if validation failed
    if #errors > 0 then
        local error_msg = "Configuration validation failed:\n"
        for _, err in ipairs(errors) do
            error_msg = error_msg .. "  - " .. err .. "\n"
        end
        error(error_msg)
    end
    
    return true, nil
end

return M
