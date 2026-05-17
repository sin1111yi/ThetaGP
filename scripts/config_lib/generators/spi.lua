-- scripts/config_lib/generators/spi.lua
--
-- Generates SPI flash configuration macros from BoardConfig.lua bus.spi config
-- New format: DESCRIPTOR_TABLE arrays instead of individual pin macros.
-- Bind macros use BUS_SPI_1 format.

local utils = require("utils")

local M = {}

local PERIPHERAL_ENUM_MAP = {
    SPI1 = "SpiInstance::Spi1",
    SPI2 = "SpiInstance::Spi2",
    SPI3 = "SpiInstance::Spi3",
    SPI4 = "SpiInstance::Spi4",
    SPI5 = "SpiInstance::Spi5",
    SPI6 = "SpiInstance::Spi6",
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

    -- Count entries with bind for USE_SPI_COUNT
    local bind_count = 0
    for _, config in ipairs(flash_list) do
        if config.bind then
            bind_count = bind_count + 1
        end
    end

    -- USE_SPI_X macros (one per entry, regardless of bind)
    for i, _ in ipairs(flash_list) do
        local prefix = string.format("SPI_%d", i)
        table.insert(lines, string.format('#define %-28s', 'USE_' .. prefix))
    end

    if bind_count > 0 then
        table.insert(lines, '')
        table.insert(lines, string.format('#define USE_SPI_COUNT %d', bind_count))

        -- Bind macros (only for entries with bind)
        table.insert(lines, '')
        for i, config in ipairs(flash_list) do
            if config.bind and config.peripheral then
                table.insert(lines, string.format(
                    '#define %-28s %s',
                    string.upper(config.bind) .. "_SPI",
                    string.format("BUS_SPI_%d", i)
                ))
            end
        end

        -- Descriptor table entries
        local desc_entries = {}
        for i, config in ipairs(flash_list) do
            if config.bind and config.peripheral then
                local enum_val = PERIPHERAL_ENUM_MAP[config.peripheral]

                local sclk_str = utils.generate_pin_struct(config.sclk)
                local mosi_str = utils.generate_pin_struct(config.mosi)
                local miso_str = utils.generate_pin_struct(config.miso)
                local ncs_str = utils.generate_pin_struct(config.ncs)

                local bus_pins = string.format('{%s, %s, %s}', sclk_str, mosi_str, miso_str)
                local entry = string.format('    {%s, %s, %s}', enum_val, bus_pins, ncs_str)
                table.insert(desc_entries, entry)
            end
        end

        if #desc_entries > 0 then
            table.insert(lines, '')
            table.insert(lines, string.format('#define %-28s \\', 'SPI_DESC_DATA'))
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
