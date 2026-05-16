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

function M.generate_content(mcu_series, board_info, pin_lines, keypad_lines, usb_lines, uart_lines, spi_lines)
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

    for _, line in ipairs(pin_lines) do
        content = content .. line .. "\n"
    end

    if #keypad_lines > 0 then
        content = content .. "\n"
        for _, line in ipairs(keypad_lines) do
            content = content .. line .. "\n"
        end
    end

    if #usb_lines > 0 then
        content = content .. "\n"
        for _, line in ipairs(usb_lines) do
            content = content .. line .. "\n"
        end
    end

    if #uart_lines > 0 then
        content = content .. "\n"
        for _, line in ipairs(uart_lines) do
            content = content .. line .. "\n"
        end
    end

    if spi_lines and #spi_lines > 0 then
        content = content .. "\n"
        for _, line in ipairs(spi_lines) do
            content = content .. line .. "\n"
        end
    end

    return content
end

return M
