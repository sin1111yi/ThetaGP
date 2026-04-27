-- scripts/config_lib/validators/log.lua

local M = {}

local VALID_LOG_LEVELS = { "debug", "info", "warn", "error" }

local function validate_log_level(level)
    if type(level) ~= "string" then
        return false, "log_level must be a string (debug, info, warn, error)"
    end

    local lower_level = level:lower()
    local found = false
    for _, v in ipairs(VALID_LOG_LEVELS) do
        if v == lower_level then
            found = true
            break
        end
    end

    if not found then
        return false, string.format(
            "Invalid log_level '%s'. Valid values: %s",
            level,
            table.concat(VALID_LOG_LEVELS, ", ")
        )
    end

    return true, nil
end

function M.validate(log_config)
    if log_config == nil then
        return true, nil
    end

    if type(log_config) ~= "table" then
        return false, "Log configuration must be a table"
    end

    if log_config.enable_log ~= nil then
        if type(log_config.enable_log) ~= "boolean" then
            return false, "enable_log must be a boolean value"
        end
    end

    if log_config.log_level ~= nil then
        local valid, err = validate_log_level(log_config.log_level)
        if not valid then
            return false, "log_level: " .. err
        end
    end

    return true, nil
end

return M