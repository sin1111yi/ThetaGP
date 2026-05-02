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
