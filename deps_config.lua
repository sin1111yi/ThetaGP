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

-- =============================================================================
-- ThetaGP Dependencies Configuration
--
-- This file defines all external dependencies required by the ThetaGP project.
-- Each dependency entry contains:
--   - name: Display name of the dependency
--   - type: Dependency type (currently "git" or "git_sparse")
--   - url: Git repository URL
--   - branch: Branch to checkout (used if tag is not specified)
--   - tag: Specific tag to checkout (takes precedence over branch)
--   - dest: Destination path relative to project root
--   - optional: If true, failure to fetch will not stop the build
--   - sparse_paths: List of paths for sparse checkout (git_sparse type only)
-- =============================================================================

DEPENDENCIES = {
    {
        name = "tinyusb",
        type = "git",
        url = "https://github.com/sin1111yi/tinyusb.git",
        tag = "master",
        dest = "lib/tinyusb",
        optional = false,
    },
    {
        name = "mbedtls",
        type = "git",
        url = "https://github.com/Mbed-TLS/mbedtls.git",
        tag = "v3.6.2",
        dest = "lib/mbedtls",
        optional = false,
    },
    {
        name = "ArduinoJson",
        type = "git",
        url = "https://github.com/bblanchon/ArduinoJson",
        tag = "v7.4.3",
        dest = "lib/ArduinoJson",
        optional = false,
    },
}
