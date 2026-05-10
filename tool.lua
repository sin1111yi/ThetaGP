#!/usr/bin/env lua
--[[
This file is a part of ThetaGP.

ThetaGP is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

ThetaGP is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.

If not, see <https://www.gnu.org/licenses/>.
]]

-- =============================================================================
-- ThetaGP Build Script
-- =============================================================================

local HELP_TEXT = [[
Usage: lua tool.lua [command] [options]

Commands:
  all                Config + build + flash (default)
  config             Run CMake configuration
  build              Build the project
  flash              Flash firmware via OpenOCD
  help               Show this help message

Options:
  --build-dir DIR    Build directory (default: build)
  --build-type TYPE  Build type: Debug|Release (default: Debug)
  --jobs N           Parallel build jobs (default: auto)
  --openocd-cfg FILE OpenOCD config (required for flash)
  --target TARGET    Board target (required, or set TARGET env)
]]

local function print_help()
    print(HELP_TEXT)
    os.exit(0)
end

-- =============================================================================
-- Utility Functions
-- =============================================================================

local function print_info(...)
    local args = {}
    for _, v in ipairs({ ... }) do
        table.insert(args, tostring(v))
    end
    print("[INFO] " .. table.concat(args, " "))
end

local function print_warning(...)
    local args = {}
    for _, v in ipairs({ ... }) do
        table.insert(args, tostring(v))
    end
    print("[WARNING] " .. table.concat(args, " "))
end

local function print_error(...)
    local args = {}
    for _, v in ipairs({ ... }) do
        table.insert(args, tostring(v))
    end
    print("[ERROR] " .. table.concat(args, " "))
end

local function execute_command(cmd)
    print_info("Executing:", cmd)
    local ret = os.execute(cmd)
    local success = (ret == true) or (ret == 0)
    if not success then
        print_error("Command failed with exit code:", ret)
        return false
    end
    return true
end

local function file_exists(path)
    local f = io.open(path, "r")
    if f then
        io.close(f)
        return true
    end
    return false
end

-- =============================================================================
-- Argument Parsing
-- =============================================================================

local args = { ... }

local options = {
    target = os.getenv("TARGET") or "",
    build_type = "Debug",
    build_dir = "",
    jobs = nil,
    openocd_cfg = "",
}

local commands = {}

local i = 1
while i <= #args do
    local arg = args[i]

    if arg == "--target" then
        i = i + 1
        options.target = args[i]
    elseif arg == "--build-type" then
        i = i + 1
        options.build_type = args[i]
    elseif arg == "--build-dir" then
        i = i + 1
        options.build_dir = args[i]
    elseif arg == "--jobs" then
        i = i + 1
        options.jobs = tonumber(args[i])
    elseif arg == "--openocd-cfg" then
        i = i + 1
        options.openocd_cfg = args[i]
    elseif arg:sub(1, 1) ~= "-" then
        table.insert(commands, arg)
    else
        print_error("Unknown option: " .. arg)
        io.stderr:write(HELP_TEXT)
        os.exit(1)
    end

    i = i + 1
end

if #commands == 0 then
    commands = { "all" }
end

-- =============================================================================
-- Path Resolution
-- =============================================================================

local script_dir = debug.getinfo(1, "S").source:match("^@?(.*[/\\])")
local project_root = script_dir or "./"

-- Resolve relative paths
local function resolve_path(p)
    if p == "" then
        return project_root .. "build"
    end
    if p:sub(1, 1) == "/" then
        return p
    end
    return project_root .. p
end

options.build_dir = resolve_path(options.build_dir)

if options.openocd_cfg ~= "" then
    options.openocd_cfg = resolve_path(options.openocd_cfg)
end

-- =============================================================================
-- CMake Configuration
-- =============================================================================

local function step_config()
    if options.target == "" then
        print_error("TARGET is not defined. Set --target or export TARGET environment variable.")
        return false
    end

    print_info("===========================================")
    print_info("Step: Config")
    print_info("  Target:    " .. options.target)
    print_info("  BuildType: " .. options.build_type)
    print_info("  BuildDir:  " .. options.build_dir)
    print_info("===========================================")

    os.execute("mkdir -p " .. options.build_dir)

    local cmake_args = {
        "-S", project_root,
        "-B", options.build_dir,
        "-DTARGET=" .. options.target,
        "-DCMAKE_BUILD_TYPE=" .. options.build_type,
    }

    local cmd = "cmake " .. table.concat(cmake_args, " ")
    return execute_command(cmd)
end

-- =============================================================================
-- Build
-- =============================================================================

local function step_build()
    print_info("===========================================")
    print_info("Step: Build")
    print_info("  BuildDir:  " .. options.build_dir)
    if options.jobs then
        print_info("  Jobs:      " .. options.jobs)
    end
    print_info("===========================================")

    local cmake_args = { "--build", options.build_dir }

    if options.jobs then
        table.insert(cmake_args, "--parallel")
        table.insert(cmake_args, tostring(options.jobs))
    end

    local cmd = "cmake " .. table.concat(cmake_args, " ")
    return execute_command(cmd)
end

-- =============================================================================
-- Flash via OpenOCD
-- =============================================================================

local function get_elf_path()
    local handle = io.popen("find '" .. options.build_dir .. "' -maxdepth 1 -name '*.elf' -type f 2>/dev/null")
    if not handle then
        return nil
    end
    local result = handle:read("*l")
    handle:close()
    return result
end

local function step_flash()
    if options.openocd_cfg == "" then
        print_error("OpenOCD config not specified. Use --openocd-cfg.")
        return false
    end

    print_info("===========================================")
    print_info("Step: Flash")
    print_info("  OpenOCD config: " .. options.openocd_cfg)
    print_info("===========================================")

    if not file_exists(options.openocd_cfg) then
        print_error("OpenOCD config not found: " .. options.openocd_cfg)
        return false
    end

    local elf = get_elf_path()
    if not elf then
        print_error("No ELF file found in build directory. Build the project first.")
        return false
    end

    print_info("  ELF: " .. elf)

    -- Check if openocd is available
    local openocd_check = io.popen("which openocd 2>/dev/null")
    if not openocd_check or not openocd_check:read("*l") then
        print_error("openocd not found. Please install OpenOCD.")
        if openocd_check then
            openocd_check:close()
        end
        return false
    end
    if openocd_check then
        openocd_check:close()
    end

    local cmd = string.format(
        "openocd -f '%s' -c 'program \"%s\" verify' -c 'reset run' -c 'sleep 100' -c 'shutdown'",
        options.openocd_cfg, elf
    )
    return execute_command(cmd)
end

-- =============================================================================
-- Main
-- =============================================================================

local function main()
    local steps = {
        config = step_config,
        build = step_build,
        flash = step_flash,
        help = print_help,
    }

    local order = { "config", "build", "flash" }
    local found_all = false

    for _, cmd in ipairs(commands) do
        if cmd == "all" then
            found_all = true
            break
        end
    end

    if found_all then
        for _, name in ipairs(order) do
            if not steps[name]() then
                print_error("'" .. name .. "' failed. Aborting.")
                os.exit(1)
            end
        end
    else
        for _, cmd in ipairs(commands) do
            if steps[cmd] then
                if not steps[cmd]() then
                    print_error("'" .. cmd .. "' failed. Aborting.")
                    os.exit(1)
                end
            else
                print_error("Unknown command: " .. cmd)
                io.stderr:write(HELP_TEXT)
                os.exit(1)
            end
        end
    end

    print_info("===========================================")
    print_info("All steps completed successfully!")
    print_info("===========================================")
    os.exit(0)
end

main()
