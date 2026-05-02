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

-- scripts/config_lib/utils/log.lua

local M = {}

function M.print_info(...)
    local args = {}
    for _, v in ipairs({ ... }) do
        table.insert(args, tostring(v))
    end
    print("[INFO] " .. table.concat(args, " "))
end

function M.print_warning(...)
    local args = {}
    for _, v in ipairs({ ... }) do
        table.insert(args, tostring(v))
    end
    print("[WARNING] " .. table.concat(args, " "))
end

function M.print_error(...)
    local args = {}
    for _, v in ipairs({ ... }) do
        table.insert(args, tostring(v))
    end
    print("[ERROR] " .. table.concat(args, " "))
end

function M.file_exists(path)
    local f = io.open(path, "r")
    if f then
        io.close(f)
        return true
    end
    return false
end

function M.execute_command(cmd, ignore_error)
    M.print_info("Executing:", cmd)
    local ret = os.execute(cmd)
    local success = (ret == true) or (ret == 0)
    if not success and not ignore_error then
        M.print_error("Command failed with exit code:", ret)
        return false
    end
    return true
end

return M
