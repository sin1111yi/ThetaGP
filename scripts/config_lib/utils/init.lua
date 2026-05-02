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

-- scripts/config_lib/utils/init.lua

local M = {}

M.log = require("utils.log")
M.table_utils = require("utils.table_utils")
M.pin_parser = require("utils.pin_parser")

-- Export common functions
M.contains = M.table_utils.contains
M.is_array_table = M.table_utils.is_array_table
M.array_length = M.table_utils.array_length
M.parse_pin = M.pin_parser.parse_pin
M.validate_pin_format = M.pin_parser.validate_pin_format
M.generate_pin_macro = M.pin_parser.generate_pin_macro
M.generate_pin_array_macro = M.pin_parser.generate_pin_array_macro
M.generate_pin_struct = M.pin_parser.generate_pin_macro

return M
