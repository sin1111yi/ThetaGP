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

-- Get TARGET from environment variable
local target = os.getenv("TARGET")
if not target or target == "" then
    error("TARGET environment variable is not set")
end

-- Get THETAGP_SOURCE_DIR from environment variable
local thetagp_source_dir = os.getenv("THETAGP_SOURCE_DIR")
if not thetagp_source_dir or thetagp_source_dir == "" then
    error("THETAGP_SOURCE_DIR environment variable is not set")
end

-- Add configs directory to package.path for require loading
package.path = thetagp_source_dir .. "/configs/?.lua;" .. thetagp_source_dir .. "/configs/?/init.lua;" .. package.path

-- Load BoardConfig using require
local board_config = require(target .. ".BoardConfig")

-- =============================================================================
-- Helper Functions
-- =============================================================================

-- Convert pin string (e.g., "PC0") to Port and Pin enum
local function parse_pin(pin_str)
    local port_char = pin_str:match("([A-I])")
    local pin_num = tonumber(pin_str:match("(%d+)"))

    local port_map = {
        A = "PortA", B = "PortB", C = "PortC", D = "PortD", E = "PortE",
        F = "PortF", G = "PortG", H = "PortH", I = "PortI"
    }

    return port_map[port_char], string.format("Pin%d", pin_num)
end

-- Generate CMake variable format
local function print_cmake_var(name, value)
    local escaped = value:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n")
    return string.format('set(%s "%s")', name, escaped)
end

-- Generate pin struct for bind method
local function generate_pin_macro(name, pin_str)
    local port, pin = parse_pin(pin_str)
    return string.format('#define %-28s {GpioDefine::Port::%s, GpioDefine::Pin::%s}',
                         name, port, pin)
end

-- Generate active_low define
local function generate_active_low_macro(name, value)
    return string.format('#define %-28s %s', name, value and "true" or "false")
end

-- Format name: convert to uppercase, replace underscores, handle arrays
local function format_name(name)
    return name:upper():gsub("_+", "_")
end

-- Recursively process config entries with smart naming
local function process_config(config, prefix, header_lines, cmake_lines, array_index)
    for key, value in pairs(config) do
        -- Skip non-pin configuration entries
        if type(key) == "string" then
            local key_upper = key:upper()
            
            -- Build name with single underscore separator
            local name
            if prefix then
                if array_index then
                    -- For array items: KEYPAD_ROW1, KEYPAD_COL2, USB_DP, etc.
                    name = prefix .. "_" .. array_index .. "_" .. key_upper
                else
                    name = prefix .. "_" .. key_upper
                end
            else
                if array_index then
                    name = array_index .. "_" .. key_upper
                else
                    name = key_upper
                end
            end

            if type(value) == "table" then
                if value.pin then
                    -- This is a pin definition
                    table.insert(header_lines, generate_pin_macro(name .. "_PIN", value.pin))
                    if value.active_low ~= nil then
                        table.insert(header_lines, generate_active_low_macro(name .. "_ACTIVE_LOW", value.active_low))
                    end
                else
                    -- Check if this is an array (numeric keys)
                    local is_array = false
                    local array_count = 0
                    
                    for k, v in pairs(value) do
                        if type(k) == "number" then
                            is_array = true
                            array_count = array_count + 1
                        end
                    end
                    
                    if is_array then
                        -- Process array items with index
                        for i, item in ipairs(value) do
                            if type(item) == "table" then
                                if item.pin then
                                    -- Direct pin in array: KEYPAD_ROW1_PIN
                                    local item_name = name .. (array_count > 1 and "_" .. i or "")
                                    table.insert(header_lines, generate_pin_macro(item_name .. "_PIN", item.pin))
                                    if item.active_low ~= nil then
                                        table.insert(header_lines, generate_active_low_macro(item_name .. "_ACTIVE_LOW", item.active_low))
                                    end
                                else
                                    -- Nested structure in array
                                    process_config(item, name, header_lines, cmake_lines, i)
                                end
                            end
                        end
                    else
                        -- Process nested config
                        process_config(value, name, header_lines, cmake_lines, nil)
                    end
                end
            end
        end
    end
end

-- =============================================================================
-- Generate Output
-- =============================================================================

-- Collect CMake output lines
local cmake_lines = {}

-- Export BoardInfo
if board_config.BoardInfo then
    table.insert(cmake_lines, print_cmake_var("BOARD_IDENTIFIER", board_config.BoardInfo.identifier or ""))
    table.insert(cmake_lines, print_cmake_var("BOARD_NAME", board_config.BoardInfo.name or ""))
    table.insert(cmake_lines, print_cmake_var("BOARD_MCU", board_config.BoardInfo.mcu or ""))
    table.insert(cmake_lines, print_cmake_var("BOARD_MCU_SERIES", board_config.BoardInfo.mcu_series or ""))
end

-- Export TARGET
table.insert(cmake_lines, print_cmake_var("TARGET", target))

-- Generate BoardConfig.h content
local header_lines = {}

-- License header
table.insert(header_lines, [=[
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

]=])

-- Generate MCU-specific HAL header include
local mcu_series = board_config.BoardInfo.mcu_series or ""
if mcu_series == "STM32H7" then
    table.insert(header_lines, '#include "stm32h7xx.h"')
elseif mcu_series == "STM32F4" then
    table.insert(header_lines, '#include "stm32f4xx.h"')
elseif mcu_series == "STM32F1" then
    table.insert(header_lines, '#include "stm32f1xx.h"')
end

table.insert(header_lines, [=[

// =============================================================================
// Board Information
// =============================================================================
#define BOARD_IDENTIFIER    "]=] .. (board_config.BoardInfo.identifier or "") .. [=["
#define BOARD_NAME          "]=] .. (board_config.BoardInfo.name or "") .. [=["

// =============================================================================
// Pin Definitions
// Usage: gpio.bind(PIN_NAME);
// Example: gpio.bind(LED0_PIN);
// =============================================================================

]=])

-- Process BoardConfig if exists
if board_config.BoardConfig then
    if board_config.BoardConfig.necessary then
        process_config(board_config.BoardConfig.necessary, nil, header_lines, cmake_lines, nil)
    end
end

-- Generate output file paths
local cmake_output_file = string.format("%s/configs/%s/board_config.cmake", thetagp_source_dir, target)
local header_output_file = string.format("%s/configs/%s/BoardConfig.h", thetagp_source_dir, target)

-- Write CMake config file
local cmake_file = io.open(cmake_output_file, "w")
if not cmake_file then
    error("Failed to open CMake output file: " .. cmake_output_file)
end
cmake_file:write(table.concat(cmake_lines, "\n"))
cmake_file:close()

-- Write BoardConfig.h file
local header_file = io.open(header_output_file, "w")
if not header_file then
    error("Failed to open header output file: " .. header_output_file)
end
header_file:write(table.concat(header_lines, "\n"))
header_file:close()
