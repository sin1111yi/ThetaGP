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

-- scripts/config_lib/utils/pin_parser.lua

local M = {}

-- Port mapping table
local PORT_MAP = {
    A = "Port::PortA",
    B = "Port::PortB",
    C = "Port::PortC",
    D = "Port::PortD",
    E = "Port::PortE",
    F = "Port::PortF",
    G = "Port::PortG",
    H = "Port::PortH",
    I = "Port::PortI"
}

-- Validate pin format (e.g., "PA0", "PB15")
function M.validate_pin_format(pin_str)
    if type(pin_str) ~= "string" then
        return false, "Pin must be a string"
    end
    
    -- Lua pattern matching: uppercase letter followed by any characters, ending with digit
    if not pin_str:match("^%u.*%d$") then
        return false, "Invalid pin format '" .. pin_str .. "' (expected 'PA0' format)"
    end
    
    -- Validate first character is 'P' (Port prefix)
    local first_char = pin_str:sub(1, 1)
    if first_char ~= 'P' then
        return false, "Invalid prefix '" .. first_char .. "' (expected 'P' for Port)"
    end
    
    -- Validate second character is A-I (port letter)
    local port_char = pin_str:sub(2, 2)
    local valid_ports = "ABCDEFGH"
    if not valid_ports:find(port_char, 1, true) then
        return false, "Invalid port '" .. port_char .. "' (expected A-I)"
    end
    
    return true, nil
end

-- Parse pin string to Port and Pin
function M.parse_pin(pin_str)
    local valid, err = M.validate_pin_format(pin_str)
    if not valid then
        error(err)
    end
    
    local port_char = pin_str:match("^P([A-I])")
    local pin_num = tonumber(pin_str:match("%d+$"))
    
    local port = PORT_MAP[port_char]
    if not port then
        error("Invalid port '" .. port_char .. "'")
    end
    
    return port, string.format("Pin::Pin%d", pin_num)
end

-- Generate pin macro definition
function M.generate_pin_macro(name, pin_str)
    local port, pin = M.parse_pin(pin_str)
    return string.format('#define %-28s {%s, %s}', name, port, pin)
end

-- Generate pin struct initializer string (no #define prefix)
-- Input: "PB13" → Output: "{Port::PortB, Pin::Pin13}"
function M.generate_pin_struct(pin_str)
    local port, pin = M.parse_pin(pin_str)
    return string.format('{%s, %s}', port, pin)
end

-- Generate pin array macro (multi-line format)
function M.generate_pin_array_macro(macro_name, pins)
    local lines = {}
    local count = #pins
    
    table.insert(lines, '#define ' .. macro_name .. ' \\')
    
    for i, pin_entry in ipairs(pins) do
        if pin_entry.pin then
            local port, pin = M.parse_pin(pin_entry.pin)
            local line = '    {' .. port .. ', ' .. pin .. '}'
            if i < count then
                line = line .. ', \\'
            end
            table.insert(lines, line)
        end
    end
    
    return table.concat(lines, '\n')
end

return M
