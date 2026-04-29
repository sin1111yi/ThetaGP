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

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (e.g. Debug, Release, RelWithDebInfo)" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(THETAGP_CFG_LOG_ENABLE 3)

    if(NOT DEFINED THETAGP_CFG_LOG_LEVEL)
        message(STATUS "Set ThetaGP Log Level to Debug.")
        set(THETAGP_CFG_LOG_LEVEL "Debug")
    endif()
else()
    set(THETAGP_CFG_LOG_ENABLE 0)
endif()

# Get version from git
find_package(Git)

if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --abbrev=7
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_REPO_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --abbrev=7
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_REPO_BUILD_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "v([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" CMAKE_GIT_REPO_VERSION ${GIT_REPO_VERSION})

    if(CMAKE_GIT_REPO_VERSION STREQUAL GIT_REPO_VERSION)
        set(CMAKE_GIT_REPO_VERSION "0.0.0")
    endif()

    message(STATUS "GIT_REPO_VERSION: ${GIT_REPO_VERSION}")
    message(STATUS "CMAKE_GIT_REPO_VERSION: ${CMAKE_GIT_REPO_VERSION}")
    message(STATUS "GIT_REPO_BUILD_ID: ${GIT_REPO_BUILD_ID}")
else()
    set(CMAKE_GIT_REPO_VERSION "0.0.0")
    message(WARNING "Git not found, using default version 0.0.0")
endif()

# Project name with version
set(THETAGP_PROJECT_NAME "ThetaGP_${TARGET}_${CMAKE_GIT_REPO_VERSION}_${CMAKE_BUILD_TYPE}")

set(ENV{TARGET} "${TARGET}")
set(ENV{THETAGP_SOURCE_DIR} "${CMAKE_SOURCE_DIR}")

# Find Lua interpreter
find_program(LUA_EXECUTABLE NAMES lua lua5.3 lua5.4 luajit)

if(NOT LUA_EXECUTABLE)
    message(FATAL_ERROR "Lua interpreter not found. Please install lua.")
endif()

message(STATUS "Found Lua: ${LUA_EXECUTABLE}")

# Fetch dependencies before configuration
message(STATUS "Fetching dependencies...")
execute_process(
    COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/fetch_deps.lua
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE DEPS_FETCH_RESULT
)

if(NOT DEPS_FETCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to fetch dependencies. Please check the error messages above.")
endif()

# Generate board configuration
execute_process(
    COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_config.lua
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/configs/${TARGET}/board_config.cmake)

# First include toolchain settings to get THETAGP_TOOLCHAIN_PREFIX and THETAGP_MIN_VERSION
if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    include(${CMAKE_SOURCE_DIR}/platform/STM32/cmake/toolchain.cmake)
else()
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported currently.")
endif()

project(${THETAGP_PROJECT_NAME}
    LANGUAGES C CXX ASM
    VERSION ${CMAKE_GIT_REPO_VERSION}
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)
add_subdirectory(platform)
add_subdirectory(src)
