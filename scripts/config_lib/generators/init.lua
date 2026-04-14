-- scripts/config_lib/generators/init.lua

local M = {}

-- Load all generator modules
M.header = require("generators.header")
M.keypad = require("generators.keypad")
M.usb = require("generators.usb")
M.cmake = require("generators.cmake")

return M
