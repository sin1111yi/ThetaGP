-- scripts/config_lib/utils/table_utils.lua

local M = {}

-- Check if table contains value
function M.contains(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return true
        end
    end
    return false
end

-- Check if table is an array
function M.is_array_table(tbl)
    for k, _ in pairs(tbl) do
        if type(k) == "number" then
            return true
        end
    end
    return false
end

-- Get array length
function M.array_length(tbl)
    local count = 0
    for _ in pairs(tbl) do
        count = count + 1
    end
    return count
end

-- Merge tables
function M.merge_tables(t1, t2)
    local result = {}
    for k, v in pairs(t1) do
        result[k] = v
    end
    for k, v in pairs(t2) do
        result[k] = v
    end
    return result
end

return M
