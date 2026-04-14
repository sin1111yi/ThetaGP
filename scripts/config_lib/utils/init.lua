-- scripts/config_lib/utils/init.lua

local M = {}

-- Load all utility modules
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

return M
