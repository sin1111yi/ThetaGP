-- scripts/config_lib/generators/cmake.lua

local M = {}

-- Generate CMake variable format
function M.print_cmake_var(name, value)
    local escaped = tostring(value)
        :gsub("\\", "\\\\")
        :gsub('"', '\\"')
        :gsub("\n", "\\n")
    return string.format('set(%s "%s")', name, escaped)
end

function M.generate_board_info(board_info)
    local lines = {}
    table.insert(lines, M.print_cmake_var("BOARD_IDENTIFIER", board_info.identifier or ""))
    table.insert(lines, M.print_cmake_var("BOARD_NAME", board_info.name or ""))
    table.insert(lines, M.print_cmake_var("BOARD_MCU", board_info.mcu or ""))
    table.insert(lines, M.print_cmake_var("BOARD_MCU_SERIES", board_info.mcu_series or ""))
    return lines
end

function M.generate_content(board_info, target_value)
    local lines = M.generate_board_info(board_info)
    table.insert(lines, M.print_cmake_var("TARGET", target_value))
    return table.concat(lines, "\n")
end

return M
