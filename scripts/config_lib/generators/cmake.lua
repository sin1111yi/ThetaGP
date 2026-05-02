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

-- scripts/config_lib/generators/cmake.lua

local M = {}

-- Generate CMake variable format
function M.print_cmake_var(name, value)
    local escaped = tostring(value)
        :gsub("\\", "\\\\")
        :gsub('"', '\\"')
        :gsub("\n", "\\n")
    return string.format('set(%s "%s")', name, escaped)
end

function M.generate_board_info(board_info)
    local lines = {}
    table.insert(lines, M.print_cmake_var("BOARD_IDENTIFIER", board_info.identifier or ""))
    table.insert(lines, M.print_cmake_var("BOARD_NAME", board_info.name or ""))
    table.insert(lines, M.print_cmake_var("BOARD_MCU", board_info.mcu or ""))
    table.insert(lines, M.print_cmake_var("BOARD_MCU_SERIES", board_info.mcu_series or ""))
    return lines
end

function M.generate_content(board_info, target_value)
    local lines = M.generate_board_info(board_info)
    table.insert(lines, M.print_cmake_var("TARGET", target_value))
    return table.concat(lines, "\n")
end

return M
