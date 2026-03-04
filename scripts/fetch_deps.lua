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
-- ThetaGP Dependency Fetcher
--
-- This script manages external dependencies for the ThetaGP project.
-- It is called during CMake configuration phase to fetch required libraries.
-- =============================================================================

local script_dir = debug.getinfo(1, "S").source:match [[^@?(.*[\/])]]
local project_root = script_dir .. "../"

-- Load dependencies configuration
dofile(project_root .. "deps_config.lua")

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

local function file_exists(path)
    local f = io.open(path, "r")
    if f then
        io.close(f)
        return true
    end
    return false
end

local function is_git_repo(path)
    return file_exists(path .. "/.git")
end

local function execute_command(cmd, ignore_error)
    print_info("Executing:", cmd)
    local ret = os.execute(cmd)
    -- os.execute returns true (success) or false/exit code (failure)
    local success = (ret == true) or (ret == 0)
    if not success and not ignore_error then
        print_error("Command failed with exit code:", ret)
        return false
    end
    return success
end

local function ensure_directory(path)
    os.execute("mkdir -p " .. path)
end

-- =============================================================================
-- Git Operations
-- =============================================================================

local function clone_or_pull(repo)
    local dest_path = project_root .. repo.dest

    if repo.type == "git_sparse" then
        -- Handle sparse checkout with multiple paths
        local sparse_paths = repo.sparse_paths or { repo.sparse_path }

        -- Check if all sparse paths exist
        local all_paths_exist = true
        local missing_paths = {}

        for _, path in ipairs(sparse_paths) do
            local full_path = dest_path .. "/" .. path
            if not file_exists(full_path) then
                all_paths_exist = false
                table.insert(missing_paths, path)
            end
        end

        -- Only skip if all paths exist AND it's a git repo
        if all_paths_exist and is_git_repo(dest_path) then
            print_info("Dependency already exists:", repo.dest)
            return true
        end

        -- Fetch the repository
        print_info("Fetching directory (sparse clone):", repo.name, "->", repo.dest)
        print_info("Sparse paths:", table.concat(sparse_paths, ", "))

        if not all_paths_exist then
            print_info("Missing paths:", table.concat(missing_paths, ", "), "- re-fetching...")
        end

        -- Remove existing directory if it exists (partial or incomplete)
        if file_exists(dest_path) then
            local rm_cmd = string.format("rm -rf '%s'", dest_path)
            os.execute(rm_cmd)
        end

        -- Step 1: Clone with --sparse flag (Git 2.25+)
        local clone_cmd = string.format("git clone --sparse --filter=blob:none --depth 1 %s %s",
            repo.url, dest_path)

        if not execute_command(clone_cmd, repo.optional) then
            if not repo.optional then
                print_error("Failed to clone repository (sparse):", repo.name)
                return false
            else
                print_warning("Skipping optional dependency:", repo.name)
                return true
            end
        end

        -- Step 2: Set sparse checkout paths using git sparse-checkout set
        local sparse_checkout_args = table.concat(sparse_paths, " ")
        local set_sparse_cmd = string.format("cd %s && git sparse-checkout set %s",
            dest_path, sparse_checkout_args)

        if not execute_command(set_sparse_cmd, repo.optional) then
            if not repo.optional then
                print_error("Failed to set sparse checkout:", repo.name)
                return false
            else
                print_warning("Skipping optional dependency:", repo.name)
                return true
            end
        end

        -- Step 3: Checkout the specified branch
        local checkout_cmd = string.format("cd %s && git checkout %s", dest_path, repo.branch)

        if not execute_command(checkout_cmd, repo.optional) then
            if not repo.optional then
                print_error("Failed to checkout branch:", repo.name)
                return false
            else
                print_warning("Skipping optional dependency:", repo.name)
                return true
            end
        end

        return true
    else
        -- Standard full clone
        if is_git_repo(dest_path) then
            print_info("Updating existing repository:", repo.dest)
            local cmd = string.format("cd %s && git fetch --all && git checkout %s", dest_path, repo.branch)
            if not execute_command(cmd, repo.optional) then
                if not repo.optional then
                    print_error("Failed to update repository:", repo.name)
                    return false
                else
                    print_warning("Skipping optional dependency:", repo.name)
                    return true
                end
            end
        else
            -- Check if directory exists and is not empty
            local dir_exists = file_exists(dest_path)
            if dir_exists then
                -- Check if directory is empty
                local handle = io.popen("ls -A '" .. dest_path .. "' 2>/dev/null")
                local result = ""
                if handle then
                    result = handle:read("*a")
                    handle:close()
                end

                if result and result ~= "" then
                    print_warning("Directory exists but is not a git repo, skipping:", repo.dest)
                    return true
                end
            end

            -- Standard full clone
            print_info("Cloning repository:", repo.name, "to", repo.dest)
            ensure_directory(dest_path)
            local cmd = string.format("git clone --branch %s --depth 1 %s %s",
                repo.branch, repo.url, dest_path)
            if not execute_command(cmd, repo.optional) then
                if not repo.optional then
                    print_error("Failed to clone repository:", repo.name)
                    return false
                else
                    print_warning("Skipping optional dependency:", repo.name)
                    return true
                end
            end
        end
    end

    return true
end

-- =============================================================================
-- Main Entry Point
-- =============================================================================

local function main()
    print_info("===========================================")
    print_info("ThetaGP Dependency Fetcher")
    print_info("===========================================")
    print_info("Project root:", project_root)
    print_info("")

    local success = true
    local fetched_count = 0
    local skipped_count = 0

    for _, dep in ipairs(DEPENDENCIES) do
        print_info("-------------------------------------------")
        print_info("Processing dependency:", dep.name)

        local dest_path = project_root .. dep.dest

        -- For git_sparse type, check if all sparse paths exist
        if dep.type == "git_sparse" and is_git_repo(dest_path) then
            local sparse_paths = dep.sparse_paths or { dep.sparse_path }
            local all_paths_exist = true
            local missing_paths = {}

            for _, path in ipairs(sparse_paths) do
                local full_path = dest_path .. "/" .. path
                if not file_exists(full_path) then
                    all_paths_exist = false
                    table.insert(missing_paths, path)
                end
            end

            if all_paths_exist then
                print_info("Dependency already exists:", dep.dest)
                skipped_count = skipped_count + 1
            else
                print_info("Sparse paths missing:", table.concat(missing_paths, ", "), "- re-fetching...")
                if clone_or_pull(dep) then
                    fetched_count = fetched_count + 1
                else
                    success = false
                end
            end
        elseif is_git_repo(dest_path) then
            print_info("Dependency already exists:", dep.dest)
            skipped_count = skipped_count + 1
        else
            if clone_or_pull(dep) then
                fetched_count = fetched_count + 1
            else
                success = false
            end
        end
    end

    print_info("")
    print_info("===========================================")
    print_info("Dependency fetch completed!")
    print_info("  Fetched:", fetched_count)
    print_info("  Skipped (already exists):", skipped_count)
    print_info("===========================================")

    if not success then
        print_error("Some dependencies failed to fetch!")
        os.exit(1)
    end

    os.exit(0)
end

main()
