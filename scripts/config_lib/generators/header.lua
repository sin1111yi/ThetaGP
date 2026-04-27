-- scripts/config_lib/generators/header.lua

local M = {}

-- MCU header file mapping
local MCU_HEADER_MAP = {
    STM32H7 = '#include "stm32h7xx.h"',
    STM32F4 = '#include "stm32f4xx.h"',
    STM32F1 = '#include "stm32f1xx.h"',
}

function M.generate_mcu_header(mcu_series)
    return MCU_HEADER_MAP[mcu_series] or ""
end

function M.generate_content(mcu_series, board_info, pin_lines, keypad_lines, usb_lines, uart_lines, log_lines)
    local mcu_header = M.generate_mcu_header(mcu_series)

    local content = [[
/*
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

]]

    if mcu_header ~= "" then
        content = content .. mcu_header .. "\n\n"
    end

    -- Board Information
    content = content .. [[// =============================================================================
// Board Information
// =============================================================================
]]
    for _, line in ipairs(board_info_lines) do
        content = content .. line .. "\n"
    end

    -- Pin Definitions
    content = content .. [[

// =============================================================================
// Pin Definitions
// =============================================================================

]]
    for _, line in ipairs(pin_lines) do
        content = content .. line .. "\n"
    end

    -- Keypad Configuration
    if #keypad_lines > 0 then
        content = content .. [[
// =============================================================================
// Keypad Configuration
// =============================================================================

]]
        for _, line in ipairs(keypad_lines) do
            content = content .. line .. "\n"
        end
    end

    -- USB Configuration
    if #usb_lines > 0 then
        content = content .. [[
// =============================================================================
// USB Configuration
// =============================================================================

]]
        for _, line in ipairs(usb_lines) do
            content = content .. line .. "\n"
        end
    end

    -- UART Configuration
    if #uart_lines > 0 then
        content = content .. [[
// =============================================================================
// UART Configuration
// =============================================================================

]]
        for _, line in ipairs(uart_lines) do
            content = content .. line .. "\n"
        end
    end

    -- Log Configuration
    if log_lines and #log_lines > 0 then
        for _, line in ipairs(log_lines) do
            content = content .. line .. "\n"
        end
    end

    return content
end

return M
