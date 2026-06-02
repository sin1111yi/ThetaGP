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

-- scripts/config_lib/generators/flash.lua
--
-- Generates flash chip selection macro from BoardConfig.lua flash.chip field.
--
-- BoardConfig.lua:
--   flash = {
--       chip = "w25qxx"   -- selects FlashW25qxx driver
--   }
--
-- Generated macro:
--   #define FLASH_CHIP_W25QXX

local M = {}

--- Map chip name string to C macro suffix
local CHIP_MACRO_MAP = {
    w25qxx = "W25QXX",
}

function M.generate(flash_config)
    local lines = {}

    if not flash_config or not flash_config.chip then
        return lines
    end

    local chip = flash_config.chip
    local macro_suffix = CHIP_MACRO_MAP[chip]

    if not macro_suffix then
        error("Unknown flash chip: " .. tostring(chip)
              .. ". Supported chips: "
              .. table.concat(
                  (function()
                      local keys = {}
                      for k, _ in pairs(CHIP_MACRO_MAP) do
                          table.insert(keys, k)
                      end
                      table.sort(keys)
                      return keys
                  end)(),
                  ", ")
             )
    end

    table.insert(lines, string.format('#define FLASH_CHIP_%s', macro_suffix))

    return lines
end

return M
