#[[
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
# along with this program.
#
# If not, see <https://www.gnu.org/licenses/>.
]]

cmake_minimum_required(VERSION 3.22)

# =============================================================================
# Source Files
# =============================================================================

# Collect all source files in src directory
file(GLOB_RECURSE SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# =============================================================================
# Executable Target
# =============================================================================
add_executable(${CMAKE_PROJECT_NAME})

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${SRC_FILES}
    ${THETAGP_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${THETAGP_INCLUDE_DIRS}
)

target_compile_definitions(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${THETAGP_COMPILE_DEFS}
)

# Set link script
set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
    LINK_DEPENDS ${THETAGP_LINK_SCRIPT}
    LINK_FLAGS "-T${THETAGP_LINK_SCRIPT}"
)
