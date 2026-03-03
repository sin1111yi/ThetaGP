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
# Project Configuration
# =============================================================================

if(NOT DEFINED TARGET)
    message(FATAL_ERROR "TARGET is not defined, please specify the target platform (e.g. -DTARGET=...)")
endif()

# Project version
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)

# =============================================================================
# Board Configuration
# =============================================================================

set(ENV{TARGET} "${TARGET}")
set(ENV{THETAGP_SOURCE_DIR} "${CMAKE_SOURCE_DIR}")

# Find Lua interpreter
find_program(LUA_EXECUTABLE NAMES lua lua5.3 lua5.4 luajit)

if(NOT LUA_EXECUTABLE)
    message(FATAL_ERROR "Lua interpreter not found. Please install lua.")
endif()

message(STATUS "Found Lua: ${LUA_EXECUTABLE}")

execute_process(
    COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/configs/config.lua
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/configs/${TARGET}/board_config.cmake)

# =============================================================================
# Toolchain Configuration (must be before project())
# =============================================================================

# First include toolchain settings to get THETAGP_TOOLCHAIN_PREFIX and THETAGP_MIN_VERSION
if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    include(${CMAKE_SOURCE_DIR}/platform/STM32/cmake/toolchain.cmake)
else()
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported currently.")
endif()

# =============================================================================
# Build Type
# =============================================================================

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (e.g. Debug, Release, RelWithDebInfo)" FORCE)
endif()

# =============================================================================
# Project Definition
# =============================================================================

project(ThetaGP_${TARGET}_${CMAKE_BUILD_TYPE}
    LANGUAGES C CXX ASM
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
)

# =============================================================================
# Compiler Standards
# =============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Subdirectories
# =============================================================================

add_subdirectory(lib)
add_subdirectory(platform)
add_subdirectory(src)
