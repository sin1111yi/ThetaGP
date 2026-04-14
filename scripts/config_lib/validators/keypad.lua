-- scripts/config_lib/validators/keypad.lua

local utils = require("utils")

local M = {}

local VALID_DRIVE_MODES = {
    "scan_matrix",
    "io_direct",
    "spi_74hc165"
}

local VALID_ACTIVE_MODES = {
    "none",
    "low",
    "high"
}

local MODE_REQUIREMENTS = {
    scan_matrix = {
        required = { "drive_pins", "sense_pins" },
        validators = {
            drive_pins = function(v) return M.validate_pin_array(v, "drive_pins") end,
            sense_pins = function(v) return M.validate_pin_array(v, "sense_pins") end,
        }
    },
    io_direct = {
        required = { "direct_pins" },
        validators = {
            direct_pins = function(v) return M.validate_pin_array(v, "direct_pins") end,
        }
    },
    spi_74hc165 = {
        required = { "spi_chips" },
        validators = {
            spi_chips = function(v)
                if type(v) ~= "number" then
                    return false, "must be a number"
                end
                if v < 1 or v > 4 then
                    return false, "must be between 1 and 4"
                end
                return true, nil
            end,
        }
    }
}

function M.validate_pin_array(pins, array_name)
    if type(pins) ~= "table" then
        return false, array_name .. " must be an array"
    end

    if #pins == 0 then
        return false, array_name .. " must have at least 1 pin"
    end

    if #pins > 8 then
        return false, array_name .. " cannot have more than 8 pins"
    end

    for i, pin in ipairs(pins) do
        if type(pin) ~= "table" then
            return false, array_name .. " item [" .. i .. "] must be a table"
        end
        if not pin.pin or type(pin.pin) ~= "string" then
            return false, array_name .. " item [" .. i .. "] must have 'pin' field (e.g., 'PA0')"
        end

        local valid, err = utils.validate_pin_format(pin.pin)
        if not valid then
            return false, array_name .. " item [" .. i .. "]: " .. err
        end
    end

    return true, nil
end

function M.validate_key_map(key_map, drive_num, sense_num)
    if type(key_map) ~= "table" then
        return false, "key_map must be a table"
    end

    if #key_map ~= drive_num then
        return false, string.format(
            "key_map must have %d rows (got %d)",
            drive_num, #key_map
        )
    end

    for d = 1, drive_num do
        if type(key_map[d]) ~= "table" then
            return false, string.format("key_map row %d must be a table", d)
        end

        if #key_map[d] ~= sense_num then
            return false, string.format(
                "key_map row %d must have %d columns (got %d)",
                d, sense_num, #key_map[d]
            )
        end

        for s = 1, sense_num do
            local value = key_map[d][s]
            if value ~= nil and value ~= 0xFF then
                if type(value) ~= "number" or value < 0 or value > 63 then
                    return false, string.format(
                        "key_map[%d][%d] must be 0-63 or 0xFF (got %s)",
                        d, s, tostring(value)
                    )
                end
            end
        end
    end

    return true, nil
end

function M.validate_cross_fields(keypad_config, drive_mode)
    if drive_mode == "scan_matrix" then
        local drive_num = keypad_config.drive_pins and #keypad_config.drive_pins or 0
        local sense_num = keypad_config.sense_pins and #keypad_config.sense_pins or 0

        if drive_num == 0 or sense_num == 0 then
            return false, "drive_pins and sense_pins must have at least 1 pin each"
        end

        local total_keys = drive_num * sense_num
        if total_keys > 64 then
            return false, string.format(
                "Total keys (%d) cannot exceed 64 (drive=%d, sense=%d)",
                total_keys, drive_num, sense_num
            )
        end

        if keypad_config.key_map then
            local valid, err = M.validate_key_map(keypad_config.key_map, drive_num, sense_num)
            if not valid then
                return false, "key_map: " .. err
            end
        end
    end

    return true, nil
end

function M.validate(keypad_config)
    if not keypad_config then
        return false, "keypad configuration is missing"
    end

    if not keypad_config.drive_mode then
        return false, "drive_mode is required"
    end

    local drive_mode = keypad_config.drive_mode:lower()
    if not utils.contains(VALID_DRIVE_MODES, drive_mode) then
        return false, string.format(
            "Invalid drive_mode '%s'. Valid values: %s",
            drive_mode,
            table.concat(VALID_DRIVE_MODES, ", ")
        )
    end

    if keypad_config.active_mode then
        local active_mode = keypad_config.active_mode:lower()
        if not utils.contains(VALID_ACTIVE_MODES, active_mode) then
            return false, string.format(
                "Invalid active_mode '%s'. Valid values: %s",
                active_mode,
                table.concat(VALID_ACTIVE_MODES, ", ")
            )
        end
    end

    local mode_req = MODE_REQUIREMENTS[drive_mode]
    if mode_req then
        for _, field in ipairs(mode_req.required) do
            if not keypad_config[field] then
                return false, "'" .. field .. "' is required for drive_mode '" .. drive_mode .. "'"
            end

            if mode_req.validators and mode_req.validators[field] then
                local valid, err = mode_req.validators[field](keypad_config[field])
                if not valid then
                    return false, field .. ": " .. err
                end
            end
        end
    end

    local valid, err = M.validate_cross_fields(keypad_config, drive_mode)
    if not valid then
        return false, err
    end

    return true, nil
end

return M
