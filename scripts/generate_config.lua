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

-- =============================================================================
-- Get environment variables
-- =============================================================================

local target = os.getenv("TARGET")
if not target or target == "" then
    error("TARGET environment variable is not set")
end

local thetagp_source_dir = os.getenv("THETAGP_SOURCE_DIR")
if not thetagp_source_dir or thetagp_source_dir == "" then
    error("THETAGP_SOURCE_DIR environment variable is not set")
end

-- =============================================================================
-- Set module paths
-- =============================================================================

package.path = thetagp_source_dir .. "/scripts/config_lib/?.lua;" .. 
               thetagp_source_dir .. "/scripts/config_lib/?/init.lua;" .. 
               package.path

-- =============================================================================
-- Load modules
-- =============================================================================

local log = require("utils.log")
local validators = require("validators")
local generators = require("generators")
local utils = require("utils")

-- =============================================================================
-- Load user configuration
-- =============================================================================

package.path = thetagp_source_dir .. "/configs/?.lua;" .. 
               thetagp_source_dir .. "/configs/?/init.lua;" .. 
               package.path

local ok, board_config = pcall(require, target .. ".BoardConfig")
if not ok then
    log.print_error("Failed to load BoardConfig.lua for target '", target, "'")
    log.print_error("Check that configs/", target, "/BoardConfig.lua exists and is valid Lua")
    os.exit(1)
end

log.print_info("Loaded configuration for target: ", target)

-- =============================================================================
-- Validate configuration
-- =============================================================================

local ok, err = pcall(validators.validate_config, board_config)
if not ok then
    log.print_error("Configuration validation failed:")
    for line in err:gmatch("[^\n]+") do
        log.print_error("  ", line)
    end
    os.exit(1)
end

log.print_info("Configuration validation passed")

-- =============================================================================
-- Extract configuration data
-- =============================================================================

local necessary_config = board_config.BoardConfig and board_config.BoardConfig.necessary or {}
local board_info = board_config.BoardInfo or {}
local mcu_series = board_info.mcu_series or ""

-- =============================================================================
-- Generate macro definitions
-- =============================================================================

log.print_info("Generating pin macros...")

-- Generate other pin configurations (LED, etc.)
local pin_lines = {}
for k, v in pairs(necessary_config) do
    if k ~= "keypad" and k ~= "usb" and type(v) == "table" and v.pin then
        table.insert(pin_lines, utils.generate_pin_macro(k:upper() .. "_PIN", v.pin))
        if v.active_low ~= nil then
            table.insert(pin_lines, string.format(
                '#define %-28s %s',
                k:upper() .. "_ACTIVE_LOW",
                v.active_low and "true" or "false"
            ))
        end
    end
end

-- Generate Keypad macros
log.print_info("Generating keypad macros...")
local keypad_lines = generators.keypad.generate(necessary_config.keypad)
log.print_info("  Keypad: ", #keypad_lines, " macros generated")

-- Generate USB macros
log.print_info("Generating USB macros...")
local usb_lines = generators.usb.generate(necessary_config.usb)
log.print_info("  USB: ", #usb_lines, " macros generated")

-- Generate UART macros
log.print_info("Generating UART macros...")
local uart_lines = generators.uart.generate(necessary_config.bus)
log.print_info("  UART: ", #uart_lines, " macros generated")

-- =============================================================================
-- Generate output files
-- =============================================================================

local cmake_output_file = string.format("%s/configs/%s/board_config.cmake", thetagp_source_dir, target)
local header_output_file = string.format("%s/configs/%s/BoardConfig.h", thetagp_source_dir, target)

-- Generate header file content
local header_content = generators.header.generate_content(
    mcu_series,
    board_info,
    pin_lines,
    keypad_lines,
    usb_lines,
    uart_lines
)

-- Generate CMake content
local cmake_content = generators.cmake.generate_content(board_info, target)

-- =============================================================================
-- Write files
-- =============================================================================

local function write_file_safe(filepath, content)
    local file = io.open(filepath, "w")
    if not file then
        log.print_error("Failed to open file for writing: ", filepath)
        os.exit(1)
    end
    file:write(content)
    file:close()
end

log.print_info("Writing ", header_output_file, " ...")
write_file_safe(header_output_file, header_content)
log.print_info("  Done (", #header_content, " bytes)")

log.print_info("Writing ", cmake_output_file, " ...")
write_file_safe(cmake_output_file, cmake_content)
log.print_info("  Done (", #cmake_content, " bytes)")

log.print_info("Configuration generated successfully for target: ", target)
