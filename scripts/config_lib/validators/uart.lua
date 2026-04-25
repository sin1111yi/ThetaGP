-- scripts/config_lib/validators/uart.lua

local utils = require("utils")

local M = {}

local VALID_PERIPHERALS = { "UART1", "UART2", "UART3", "UART4", "UART5", "UART6", "UART7", "UART8", "LPUART1" }

function M.validate(bus_config)
    if not bus_config then
        return true, nil
    end

    local uart_list = bus_config.uart
    if not uart_list then
        return true, nil
    end

    if type(uart_list) ~= "table" then
        return false, "bus.uart must be an array"
    end

    for i, config in ipairs(uart_list) do
        if not config.peripheral then
            return false, string.format("bus.uart[%d].peripheral is required", i)
        end

        if not utils.contains(VALID_PERIPHERALS, config.peripheral) then
            return false, string.format(
                "bus.uart[%d].peripheral '%s' is invalid. Valid values: %s",
                i,
                config.peripheral,
                table.concat(VALID_PERIPHERALS, ", ")
            )
        end

        if not config.tx then
            return false, string.format("bus.uart[%d].tx pin is required", i)
        end

        local ok, err = utils.validate_pin_format(config.tx)
        if not ok then
            return false, string.format("bus.uart[%d].tx: %s", i, err)
        end

        if config.rx then
            local ok, err = utils.validate_pin_format(config.rx)
            if not ok then
                return false, string.format("bus.uart[%d].rx: %s", i, err)
            end
        end

        if config.baud then
            if type(config.baud) ~= "number" or config.baud <= 0 then
                return false, string.format(
                    "bus.uart[%d].baud must be a positive number",
                    i
                )
            end
        end
    end

    return true, nil
end

return M