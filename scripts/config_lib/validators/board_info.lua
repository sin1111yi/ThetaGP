-- scripts/config_lib/validators/board_info.lua

local utils = require("utils")

local M = {}

-- Valid MCU series
local VALID_MCU_SERIES = { "STM32H7", "STM32F4", "STM32F1" }

-- Required fields
local REQUIRED_FIELDS = {
    "identifier",
    "name",
    "mcu",
    "mcu_series"
}

function M.validate(board_info)
    if not board_info then
        return false, "BoardInfo is required"
    end
    
    -- Check required fields
    for _, field in ipairs(REQUIRED_FIELDS) do
        if not board_info[field] then
            return false, "BoardInfo." .. field .. " is required"
        end
    end
    
    -- Validate MCU series
    if not utils.contains(VALID_MCU_SERIES, board_info.mcu_series) then
        return false, string.format(
            "Unsupported mcu_series '%s'. Valid values: %s",
            board_info.mcu_series,
            table.concat(VALID_MCU_SERIES, ", ")
        )
    end
    
    -- Validate identifier format (alphanumeric and underscores only)
    if not board_info.identifier:match("^[%w_]+$") then
        return false, "BoardInfo.identifier must contain only alphanumeric characters and underscores"
    end
    
    return true, nil
end

return M
