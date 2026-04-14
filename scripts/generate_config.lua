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

local validators = require("validators")
local generators = require("generators")
local utils = require("utils")

-- =============================================================================
-- Load user configuration
-- =============================================================================

package.path = thetagp_source_dir .. "/configs/?.lua;" .. 
               thetagp_source_dir .. "/configs/?/init.lua;" .. 
               package.path

local board_config = require(target .. ".BoardConfig")

-- =============================================================================
-- Validate configuration
-- =============================================================================

validators.validate_config(board_config)

-- =============================================================================
-- Extract configuration data
-- =============================================================================

local necessary_config = board_config.BoardConfig and board_config.BoardConfig.necessary or {}
local board_info = board_config.BoardInfo or {}
local mcu_series = board_info.mcu_series or ""

-- =============================================================================
-- Generate macro definitions
-- =============================================================================

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
local keypad_lines = generators.keypad.generate(necessary_config.keypad)

-- Generate USB macros
local usb_lines = generators.usb.generate(necessary_config.usb)

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
    usb_lines
)

-- Generate CMake content
local cmake_content = generators.cmake.generate_content(board_info, target)

-- =============================================================================
-- Write files
-- =============================================================================

local function write_file_safe(filepath, content)
    local file = io.open(filepath, "w")
    if not file then
        error("Failed to open file for writing: " .. filepath)
    end
    file:write(content)
    file:close()
end

write_file_safe(header_output_file, header_content)
write_file_safe(cmake_output_file, cmake_content)

print("Configuration generated successfully for target: " .. target)
