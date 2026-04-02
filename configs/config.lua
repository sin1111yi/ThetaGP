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
-- Helper Functions (Pure Functions - No Side Effects)
-- =============================================================================

-- Convert pin string (e.g., "PC0") to Port and Pin enum
local function parse_pin(pin_str)
    local port_char = pin_str:match("([A-I])")
    local pin_num = tonumber(pin_str:match("(%d+)"))

    local port_map = {
        A = "PortA",
        B = "PortB",
        C = "PortC",
        D = "PortD",
        E = "PortE",
        F = "PortF",
        G = "PortG",
        H = "PortH",
        I = "PortI"
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
    return string.format('#define %-28s {Port::%s, Pin::%s}',
        name, port, pin)
end

-- Generate active_low define
local function generate_active_low_macro(name, value)
    return string.format('#define %-28s %s', name, value and "true" or "false")
end

-- Generate USB hardware peripheral define
local function generate_usb_hw_peripheral_macro(mode)
    local peripheral_map = {
        USB1 = "USB1_OTG",           -- full speed USB differential line
        USB2 = "USB2_OTG",           -- high speed USB differential line
        ULPI = "USB_HW_PERIPH_ULPI", -- ULPI peripheral for external USB PHY
    }
    local peripheral_value = peripheral_map[mode] or "USB1_OTG"
    return string.format('#define %-28s %s', "CONF_USB_HW_PERIPHERAL", peripheral_value)
end

-- Generate USB speed define
local function generate_usb_speed_macro(speed)
    local speed_map = {
        high_speed = "USB_OTG_HIGH_SPEED",
        full_speed = "USB_OTG_FULL_SPEED",
    }
    local speed_value = speed_map[speed] or "USB_OTG_FS_SPEED"
    return string.format('#define %-28s %s', "CONF_USB_SPEED", speed_value)
end

-- Build name with single underscore separator
local function build_name(prefix, key, array_index)
    local key_upper = key:upper()
    if prefix then
        if array_index then
            return prefix .. "_" .. array_index .. "_" .. key_upper
        else
            return prefix .. "_" .. key_upper
        end
    else
        if array_index then
            return array_index .. "_" .. key_upper
        else
            return key_upper
        end
    end
end

-- Check if a table is an array (has numeric keys)
local function is_array_table(t)
    for k, _ in pairs(t) do
        if type(k) == "number" then
            return true
        end
    end
    return false
end

-- Process a single pin configuration entry, returns generated lines
local function process_pin_entry(name, value)
    local lines = {}
    table.insert(lines, generate_pin_macro(name .. "_PIN", value.pin))
    if value.active_low ~= nil then
        table.insert(lines, generate_active_low_macro(name .. "_ACTIVE_LOW", value.active_low))
    end
    return lines
end

-- Process a single config entry, returns generated header lines and cmake lines
local function process_config_entry(key, value, prefix, array_index)
    local header_lines = {}
    local cmake_lines = {}

    if type(key) ~= "string" then
        return header_lines, cmake_lines
    end

    local name = build_name(prefix, key, array_index)

    if type(value) ~= "table" then
        return header_lines, cmake_lines
    end

    -- Pin definition
    if value.pin then
        local pin_lines = process_pin_entry(name, value)
        for _, line in ipairs(pin_lines) do
            table.insert(header_lines, line)
        end
        return header_lines, cmake_lines
    end

    -- Array handling
    if is_array_table(value) then
        local array_count = 0
        for _ in pairs(value) do
            array_count = array_count + 1
        end

        for i, item in ipairs(value) do
            if type(item) == "table" then
                if item.pin then
                    local item_name = name .. (array_count > 1 and "_" .. i or "")
                    local pin_lines = process_pin_entry(item_name, item)
                    for _, line in ipairs(pin_lines) do
                        table.insert(header_lines, line)
                    end
                else
                    -- Nested structure in array
                    local nested_header, nested_cmake = process_config_entry(key, item, name, i)
                    for _, line in ipairs(nested_header) do
                        table.insert(header_lines, line)
                    end
                    for _, line in ipairs(nested_cmake) do
                        table.insert(cmake_lines, line)
                    end
                end
            end
        end
    else
        -- Nested config - recursively process
        for nested_key, nested_value in pairs(value) do
            local nested_header, nested_cmake = process_config_entry(nested_key, nested_value, name, nil)
            for _, line in ipairs(nested_header) do
                table.insert(header_lines, line)
            end
            for _, line in ipairs(nested_cmake) do
                table.insert(cmake_lines, line)
            end
        end
    end

    return header_lines, cmake_lines
end

-- Process all config entries, returns collected lines
local function process_config(config)
    local all_header_lines = {}
    local all_cmake_lines = {}

    for key, value in pairs(config) do
        local header_lines, cmake_lines = process_config_entry(key, value, nil, nil)
        for _, line in ipairs(header_lines) do
            table.insert(all_header_lines, line)
        end
        for _, line in ipairs(cmake_lines) do
            table.insert(all_cmake_lines, line)
        end
    end

    return all_header_lines, all_cmake_lines
end

-- Process USB special configurations, returns generated header lines
local function process_usb_config(usb_config)
    local lines = {}
    if not usb_config then
        return lines
    end

    if usb_config.hw_periph then
        table.insert(lines, generate_usb_hw_peripheral_macro(usb_config.hw_periph))
    end

    if usb_config.speed then
        table.insert(lines, generate_usb_speed_macro(usb_config.speed))
    end

    return lines
end

-- Generate MCU-specific HAL header include
local function generate_mcu_header(mcu_series)
    local header_map = {
        STM32H7 = '#include "stm32h7xx.h"',
        STM32F4 = '#include "stm32f4xx.h"',
        STM32F1 = '#include "stm32f1xx.h"',
    }
    return header_map[mcu_series] or ""
end

-- Generate BoardInfo defines
local function generate_board_info(board_info)
    local lines = {}
    table.insert(lines, string.format('#define BOARD_IDENTIFIER    "%s"', board_info.identifier or ""))
    table.insert(lines, string.format('#define BOARD_NAME          "%s"', board_info.name or ""))
    return lines
end

-- Generate CMake BoardInfo variables
local function generate_cmake_board_info(board_info)
    local lines = {}
    table.insert(lines, print_cmake_var("BOARD_IDENTIFIER", board_info.identifier or ""))
    table.insert(lines, print_cmake_var("BOARD_NAME", board_info.name or ""))
    table.insert(lines, print_cmake_var("BOARD_MCU", board_info.mcu or ""))
    table.insert(lines, print_cmake_var("BOARD_MCU_SERIES", board_info.mcu_series or ""))
    return lines
end

-- Generate header file content
local function generate_header_content(mcu_series, board_info, pin_lines, usb_lines)
    local mcu_header = generate_mcu_header(mcu_series)
    local board_info_lines = generate_board_info(board_info)

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

    content = content .. [[// =============================================================================
// Board Information
// =============================================================================
]]
    for _, line in ipairs(board_info_lines) do
        content = content .. line .. "\n"
    end

    content = content .. [[

// =============================================================================
// Pin Definitions
// =============================================================================

]]

    for _, line in ipairs(pin_lines) do
        content = content .. line .. "\n"
    end

    if #usb_lines > 0 then
        content = content .. "\n// =============================================================================\n"
        content = content .. "// USB Configuration\n"
        content = content .. "// =============================================================================\n\n"
        for _, line in ipairs(usb_lines) do
            content = content .. line .. "\n"
        end
    end

    return content
end

-- Generate CMake content
local function generate_cmake_content(board_info, target_value, cmake_lines)
    local content_lines = generate_cmake_board_info(board_info)
    table.insert(content_lines, print_cmake_var("TARGET", target_value))

    for _, line in ipairs(cmake_lines) do
        table.insert(content_lines, line)
    end

    return table.concat(content_lines, "\n")
end

-- Write content to file
local function write_file_safe(filepath, content)
    local file = io.open(filepath, "w")
    if not file then
        error("Failed to open file for writing: " .. filepath)
    end
    file:write(content)
    file:close()
end

-- =============================================================================
-- Main Execution
-- =============================================================================

-- Extract board configuration
local necessary_config = board_config.BoardConfig and board_config.BoardConfig.necessary or {}
local board_info = board_config.BoardInfo or {}
local mcu_series = board_info.mcu_series or ""

-- Process pin configurations
local pin_lines, cmake_lines = process_config(necessary_config)

-- Process USB configurations
local usb_lines = process_usb_config(necessary_config.usb)

-- Generate output file paths
local cmake_output_file = string.format("%s/configs/%s/board_config.cmake", thetagp_source_dir, target)
local header_output_file = string.format("%s/configs/%s/BoardConfig.h", thetagp_source_dir, target)

-- Generate file contents
local header_content = generate_header_content(mcu_series, board_info, pin_lines, usb_lines)
local cmake_content = generate_cmake_content(board_info, target, cmake_lines)

-- Write output files
write_file_safe(header_output_file, header_content)
write_file_safe(cmake_output_file, cmake_content)
