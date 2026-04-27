-- scripts/config_lib/generators/log.lua

local M = {}

local LOG_LEVEL_MAP = {
    debug = "Debug",
    info = "Info",
    warn = "Warn",
    error = "Error",
}

function M.generate(log_config)
    local lines = {}

    local enable_log = false
    local log_level = "debug"

    if log_config then
        if log_config.enable_log ~= nil then
            enable_log = log_config.enable_log
        end
        if log_config.log_level then
            log_level = log_config.log_level:lower()
        end
    end

    table.insert(lines, "")
    table.insert(lines, "// Log Configuration")
    table.insert(lines, string.format("#define THETAGP_CFG_LOG_ENABLE %d", enable_log and 1 or 0))
    table.insert(lines,
        string.format("#define THETAGP_CFG_LOG_LEVEL %s", LOG_LEVEL_MAP[log_level] or LOG_LEVEL_MAP["debug"]))

    return lines
end

return M
