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

-- Load shared utilities
package.path = script_dir .. "config_lib/?.lua;" .. package.path
local log = require("utils.log")

-- =============================================================================
-- Utility Functions
-- =============================================================================

local function is_git_repo(path)
    return log.log.file_exists(path .. "/.git")
end

local function clone_or_pull(repo)
    local dest_path = project_root .. repo.dest
    local ref = get_git_ref(repo)

    if repo.type == "git_sparse" then
        -- Handle sparse checkout with multiple paths
        local sparse_paths = repo.sparse_paths or { repo.sparse_path }

        -- Check if all sparse paths exist
        local all_paths_exist = true
        local missing_paths = {}

        for _, path in ipairs(sparse_paths) do
            local full_path = dest_path .. "/" .. path
            if not log.file_exists(full_path) then
                all_paths_exist = false
                table.insert(missing_paths, path)
            end
        end

        -- Only skip if all paths exist AND it's a git repo
        if all_paths_exist and is_git_repo(dest_path) then
            log.print_info("Dependency already exists:", repo.dest)
            return true
        end

        -- Fetch the repository
        log.print_info("Fetching directory (sparse clone):", repo.name, "->", repo.dest)
        log.print_info("Sparse paths:", table.concat(sparse_paths, ", "))

        if not all_paths_exist then
            log.print_info("Missing paths:", table.concat(missing_paths, ", "), "- re-fetching...")
        end

        -- Remove existing directory if it exists (partial or incomplete)
        if log.file_exists(dest_path) then
            local rm_cmd = string.format("rm -rf '%s'", dest_path)
            os.execute(rm_cmd)
        end

        -- Step 1: Clone with --sparse flag (Git 2.25+)
        local clone_cmd = string.format("git clone --sparse --filter=blob:none --depth 1 %s %s",
            repo.url, dest_path)

        if not log.execute_command(clone_cmd, repo.optional) then
            if not repo.optional then
                log.print_error("Failed to clone repository (sparse):", repo.name)
                return false
            else
                log.print_warning("Skipping optional dependency:", repo.name)
                return true
            end
        end

        -- Step 2: Set sparse checkout paths using git sparse-checkout set
        local sparse_checkout_args = table.concat(sparse_paths, " ")
        local set_sparse_cmd = string.format("cd %s && git sparse-checkout set %s",
            dest_path, sparse_checkout_args)

        if not log.execute_command(set_sparse_cmd, repo.optional) then
            if not repo.optional then
                log.print_error("Failed to set sparse checkout:", repo.name)
                return false
            else
                log.print_warning("Skipping optional dependency:", repo.name)
                return true
            end
        end

        -- Step 3: Checkout the specified reference (tag or branch)
        local checkout_cmd = string.format("cd %s && git checkout %s", dest_path, ref)

        if not log.execute_command(checkout_cmd, repo.optional) then
            if not repo.optional then
                log.print_error("Failed to checkout branch:", repo.name)
                return false
            else
                log.print_warning("Skipping optional dependency:", repo.name)
                return true
            end
        end

        return true
    else
        -- Standard full clone
        if is_git_repo(dest_path) then
            log.print_info("Updating existing repository:", repo.dest)
            local cmd = string.format("cd %s && git fetch --all && git checkout %s", dest_path, ref)
            if not log.execute_command(cmd, repo.optional) then
                if not repo.optional then
                    log.print_error("Failed to update repository:", repo.name)
                    return false
                else
                    log.print_warning("Skipping optional dependency:", repo.name)
                    return true
                end
            end
        else
            -- Check if directory exists and is not empty
            local dir_exists = log.file_exists(dest_path)
            if dir_exists then
                -- Check if directory is empty
                local handle = io.popen("ls -A '" .. dest_path .. "' 2>/dev/null")
                local result = ""
                if handle then
                    result = handle:read("*a")
                    handle:close()
                end

                if result and result ~= "" then
                    log.print_warning("Directory exists but is not a git repo, skipping:", repo.dest)
                    return true
                end
            end

            -- Standard full clone
            log.print_info("Cloning repository:", repo.name, "to", repo.dest)
            ensure_directory(dest_path)
            local cmd = string.format("git clone --branch %s --depth 1 %s %s",
                ref, repo.url, dest_path)
            if not log.execute_command(cmd, repo.optional) then
                if not repo.optional then
                    log.print_error("Failed to clone repository:", repo.name)
                    return false
                else
                    log.print_warning("Skipping optional dependency:", repo.name)
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
    log.print_info("===========================================")
    log.print_info("ThetaGP Dependency Fetcher")
    log.print_info("===========================================")
    log.print_info("Project root:", project_root)
    log.print_info("")

    local success = true
    local fetched_count = 0
    local skipped_count = 0

    for _, dep in ipairs(DEPENDENCIES) do
        log.print_info("-------------------------------------------")
        log.print_info("Processing dependency:", dep.name)

        local dest_path = project_root .. dep.dest

        -- For git_sparse type, check if all sparse paths exist
        if dep.type == "git_sparse" and is_git_repo(dest_path) then
            local sparse_paths = dep.sparse_paths or { dep.sparse_path }
            local all_paths_exist = true
            local missing_paths = {}

            for _, path in ipairs(sparse_paths) do
                local full_path = dest_path .. "/" .. path
                if not log.file_exists(full_path) then
                    all_paths_exist = false
                    table.insert(missing_paths, path)
                end
            end

            if all_paths_exist then
                log.print_info("Dependency already exists:", dep.dest)
                skipped_count = skipped_count + 1
            else
                log.print_info("Sparse paths missing:", table.concat(missing_paths, ", "), "- re-fetching...")
                if clone_or_pull(dep) then
                    fetched_count = fetched_count + 1
                else
                    success = false
                end
            end
        elseif is_git_repo(dest_path) then
            log.print_info("Dependency already exists:", dep.dest)
            skipped_count = skipped_count + 1
        else
            if clone_or_pull(dep) then
                fetched_count = fetched_count + 1
            else
                success = false
            end
        end
    end

    log.print_info("")
    log.print_info("===========================================")
    log.print_info("Dependency fetch completed!")
    log.print_info("  Fetched:", fetched_count)
    log.print_info("  Skipped (already exists):", skipped_count)
    log.print_info("===========================================")

    if not success then
        log.print_error("Some dependencies failed to fetch!")
        os.exit(1)
    end

    os.exit(0)
end

main()
