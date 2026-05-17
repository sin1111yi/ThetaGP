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

-- scripts/config_lib/generators/uart.lua
--
-- Generates UART configuration macros from BoardConfig.lua bus.uart config
-- New format: DESCRIPTOR_TABLE arrays instead of individual pin macros.
-- Bind macros use BUS_UART_1 format.

local utils = require("utils")

local M = {}

local PERIPHERAL_ENUM_MAP = {
    UART1 = "UartInstance::Uart1",
    UART2 = "UartInstance::Uart2",
    UART3 = "UartInstance::Uart3",
    UART4 = "UartInstance::Uart4",
    UART5 = "UartInstance::Uart5",
    UART6 = "UartInstance::Uart6",
    UART7 = "UartInstance::Uart7",
    UART8 = "UartInstance::Uart8",
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

    -- Count entries with bind for USE_UART_COUNT
    local bind_count = 0
    for _, config in ipairs(uart_list) do
        if config.bind then
            bind_count = bind_count + 1
        end
    end

    -- USE_UART_X macros (one per entry, regardless of bind)
    for i, _ in ipairs(uart_list) do
        local prefix = string.format("UART_%d", i)
        table.insert(lines, string.format('#define %-28s', 'USE_' .. prefix))
    end

    if bind_count > 0 then
        table.insert(lines, '')
        table.insert(lines, string.format('#define USE_UART_COUNT %d', bind_count))

        -- Bind macros (only for entries with bind)
        table.insert(lines, '')
        for i, config in ipairs(uart_list) do
            if config.bind and config.peripheral then
                table.insert(lines, string.format(
                    '#define %-28s %s',
                    string.upper(config.bind) .. "_UART",
                    string.format("BUS_UART_%d", i)
                ))
            end
        end

        -- Descriptor table entries
        local desc_entries = {}
        for i, config in ipairs(uart_list) do
            if config.bind and config.peripheral then
                local enum_val = PERIPHERAL_ENUM_MAP[config.peripheral]

                local tx_str = utils.generate_pin_struct(config.tx)
                local rx_str = utils.generate_pin_struct(config.rx)
                local baud = config.baud or 115200

                local entry = string.format('    {%s, %s, %s, %d}', enum_val, tx_str, rx_str, baud)
                table.insert(desc_entries, entry)
            end
        end

        if #desc_entries > 0 then
            table.insert(lines, '')
            table.insert(lines, string.format('#define %-28s \\', 'UART_DESC_DATA'))
            for j, entry in ipairs(desc_entries) do
                if j < #desc_entries then
                    table.insert(lines, '    ' .. entry .. ', \\')
                else
                    table.insert(lines, '    ' .. entry)
                end
            end
        end
    end

    return lines
end

return M
