-- scripts/config_lib/generators/init.lua

local M = {}

-- Load all generator modules
M.header = require("generators.header")
M.keypad = require("generators.keypad")
M.usb = require("generators.usb")
M.uart = require("generators.uart")
M.cmake = require("generators.cmake")
M.log = require("generators.log")

return M
