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
# along with this program. If not, see <https://www.gnu.org/licenses/>.
]]

-- scripts/config_lib/generators/keypad.lua

local utils = require("utils")

local M = {}

local DRIVE_MODE_MAP = {
    scan_matrix = "ScanMatrix",
    io_direct = "IODirect",
    spi_74hc165 = "SpiDriven74HC165",
}

local ACTIVE_MODE_MAP = {
    none = "None",
    low = "Low",
    high = "High",
}

function M.generate(keypad_config)
    local lines = {}

    if not keypad_config then
        return lines
    end

    if keypad_config.drive_mode then
        local mode_value = DRIVE_MODE_MAP[keypad_config.drive_mode:lower()] or ""
        table.insert(lines, string.format(
            '#define %-28s %s',
            'KEYPAD_DRIVE_MODE',
            "KeypadConfig::Mode::" .. mode_value
        ))
    end

    if keypad_config.active_mode then
        local active_value = ACTIVE_MODE_MAP[keypad_config.active_mode:lower()] or "None"
        table.insert(lines, string.format(
            '#define %-28s %s',
            'KEYPAD_ACTIVE_MODE',
            "KeypadConfig::Active::" .. active_value
        ))
    else
        table.insert(lines, string.format(
            '#define %-28s %s',
            'KEYPAD_ACTIVE_MODE',
            "KeypadConfig::Active::None"
        ))
    end

    local drive_mode = keypad_config.drive_mode:lower()

    if drive_mode == "scan_matrix" then
        if keypad_config.drive_pins and #keypad_config.drive_pins > 0 then
            table.insert(lines, string.format(
                '#define %-28s %d',
                'KEYPAD_DRIVE_PIN_NUM',
                #keypad_config.drive_pins
            ))
            table.insert(lines, utils.generate_pin_array_macro(
                'KEYPAD_DRIVE_IO_LIST',
                keypad_config.drive_pins
            ))
        end

        if keypad_config.sense_pins and #keypad_config.sense_pins > 0 then
            table.insert(lines, string.format(
                '#define %-28s %d',
                'KEYPAD_SENSE_PIN_NUM',
                #keypad_config.sense_pins
            ))
            table.insert(lines, utils.generate_pin_array_macro(
                'KEYPAD_SENSE_IO_LIST',
                keypad_config.sense_pins
            ))
        end

        if keypad_config.key_map then
            local drive_num = #keypad_config.drive_pins
            local sense_num = #keypad_config.sense_pins
            local total_keys = drive_num * sense_num

            table.insert(lines, "")
            table.insert(lines, "#define KEYPAD_KEY_MAP \\")

            local max_index = 0
            local item_count = 0
            local total_items = drive_num * sense_num

            for d = 1, drive_num do
                for s = 1, sense_num do
                    item_count = item_count + 1
                    local value = keypad_config.key_map[d][s]
                    if value == nil then
                        value = 0xFF  -- KEYPAD_NO_KEY
                    end
                    if value ~= 0xFF and value > max_index then
                        max_index = value
                    end

                    if item_count < total_items then
                        table.insert(lines, string.format("    %3d, \\", value))
                    else
                        table.insert(lines, string.format("    %3d", value))
                    end
                end
            end

            table.insert(lines, "")
            table.insert(lines, "")
            table.insert(lines, string.format("#define %-28s %d", 'KEYPAD_MAX_KEY_INDEX', max_index))
            table.insert(lines, string.format("#define %-28s %d", 'KEYPAD_MASK_ARRAY_SIZE', math.floor((max_index + 32) / 32)))
            table.insert(lines, string.format("#define %-28s %d", 'KEYPAD_KEY_MAP_SIZE', total_keys))
        end

    elseif drive_mode == "io_direct" then
        if keypad_config.direct_pins and #keypad_config.direct_pins > 0 then
            table.insert(lines, string.format(
                '#define %-28s %d',
                'KEYPAD_DIRECT_PINS_NUM',
                #keypad_config.direct_pins
            ))
            table.insert(lines, utils.generate_pin_array_macro(
                'KEYPAD_DIRECT_PINS',
                keypad_config.direct_pins
            ))
        end

    elseif drive_mode == "spi_74hc165" then
        if keypad_config.spi_chips then
            table.insert(lines, string.format(
                '#define %-28s %d',
                'KEYPAD_SPI_CHIPS',
                keypad_config.spi_chips
            ))
        end
    end

    if keypad_config.button_map and #keypad_config.button_map > 0 then
        table.insert(lines, "")
        table.insert(lines, "#define KEYPAD_BUTTON_MAP \\")

        local count = #keypad_config.button_map
        for i, v in ipairs(keypad_config.button_map) do
            local mask_name = "GAMEPAD_MASK_" .. v:upper()
            if i < count then
                table.insert(lines, string.format("    %-20s, \\", mask_name))
            else
                table.insert(lines, string.format("    %-20s", mask_name))
            end
        end

        table.insert(lines, "")
    end

    return lines
end

return M
