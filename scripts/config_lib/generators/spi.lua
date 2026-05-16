-- scripts/config_lib/generators/spi.lua
--
-- Generates SPI flash configuration macros from BoardConfig.lua bus.spi config
-- Format matches UART generator pattern:
--   #define FLASH_SPI SPI_2          ← bind_SPI = peripheral name (with underscore)
--   #define SPI_2_PERIPHERAL Spi2    ← peripheral name as prefix for sub-macros
--   #define SPI_2_SCLK {Port::PortB, Pin::Pin13}

local utils = require("utils")

local M = {}

local PERIPHERAL_ENUM_MAP = {
    SPI1 = "Spi1",
    SPI2 = "Spi2",
    SPI3 = "Spi3",
    SPI4 = "Spi4",
    SPI5 = "Spi5",
    SPI6 = "Spi6",
}

function M.generate(bus_config)
    local lines = {}

    if not bus_config then
        return lines
    end

    local flash_list = bus_config.spi
    if not flash_list then
        return lines
    end

    for i, config in ipairs(flash_list) do
        local enum_val = PERIPHERAL_ENUM_MAP[config.peripheral]

        if config.bind and config.peripheral then
            -- Transform peripheral name (SPI2 -> SPI_2) to avoid HAL conflict
            local prefix = config.peripheral:gsub("^(SPI)(%d+)$", "SPI_%2")

            -- Bind macro: #define FLASH_SPI SPI_2
            table.insert(lines, string.format(
                '#define %-28s %s',
                string.upper(config.bind) .. "_SPI",
                prefix
            ))

            -- Sub-macros use peripheral name with underscore as prefix (e.g., SPI_2_*)

            if config.peripheral and enum_val then
                table.insert(lines, string.format(
                    '#define %-28s %s',
                    prefix .. "_PERIPHERAL",
                    enum_val
                ))
            end

            if config.sclk then
                table.insert(lines, utils.generate_pin_macro(prefix .. "_SCLK", config.sclk))
            end

            if config.mosi then
                table.insert(lines, utils.generate_pin_macro(prefix .. "_MOSI", config.mosi))
            end

            if config.miso then
                table.insert(lines, utils.generate_pin_macro(prefix .. "_MISO", config.miso))
            end

            if config.ncs then
                table.insert(lines, utils.generate_pin_macro(prefix .. "_NCS", config.ncs))
            end
        end
    end

    return lines
end

return M
