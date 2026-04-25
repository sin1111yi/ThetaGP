-- scripts/config_lib/generators/uart.lua

local utils = require("utils")

local M = {}

local PERIPHERAL_ENUM_MAP = {
    UART1 = "Uart1",
    UART2 = "Uart2",
    UART3 = "Uart3",
    UART4 = "Uart4",
    UART5 = "Uart5",
    UART6 = "Uart6",
    UART7 = "Uart7",
    UART8 = "Uart8",
}

function M.generate(bus_config)
    local lines = {}

    if not bus_config then
        return lines
    end

    local uart_list = bus_config.uart
    if not uart_list then
        return lines
    end

    for i, config in ipairs(uart_list) do
        local prefix = string.format("UART%d", i)
        local enum_val = PERIPHERAL_ENUM_MAP[config.peripheral]

        if config.bind and config.peripheral then
            table.insert(lines, string.format(
                '#define %-28s %s',
                string.upper(config.bind) .. "_UART",
                prefix
            ))
        end

        if config.peripheral then
            if enum_val then
                table.insert(lines, string.format(
                    '#define %-28s %s',
                    prefix .. "_" .. "PERIPHERAL",
                    enum_val
                ))
            end
        end

        if config.tx then
            table.insert(lines, utils.generate_pin_macro(prefix .. "_" .. "TX_PIN", config.tx))
        end

        if config.rx then
            table.insert(lines, utils.generate_pin_macro(prefix .. "_" .. "RX_PIN", config.rx))
        end

        local baud = config.baud or 115200
        table.insert(lines, string.format(
            '#define %-28s %d',
            prefix .. "_" .. "BAUDRATE",
            baud
        ))
    end

    return lines
end

return M
