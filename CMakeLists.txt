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

# Project version
set(THETAGP_VERSION_MAJOR 0)
set(THETAGP_VERSION_MINOR 1)
set(THETAGP_VERSION_PATCH 1)
set(THETAGP_VERSION "${THETAGP_VERSION_MAJOR}.${THETAGP_VERSION_MINOR}.${THETAGP_VERSION_PATCH}")

if(NOT DEFINED TARGET)
    message(FATAL_ERROR "TARGET is not defined, please specify the target platform (e.g. -DTARGET=...)")
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (e.g. Debug, Release, RelWithDebInfo)" FORCE)
endif()

# Load user-configurable defaults (supports -D override)
include(${CMAKE_SOURCE_DIR}/cmake/thetagp_config.cmake)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "Logging enabled (Debug build) — each log call adds ~100us delay. "
                    "This may affect real-time performance of the 1kHz gamepad loop. "
                    "Set -DCMAKE_BUILD_TYPE=Release to disable.")
endif()

# Project name with version
set(THETAGP_PROJECT_NAME "ThetaGP_${TARGET}_${THETAGP_VERSION}_${CMAKE_BUILD_TYPE}")

set(ENV{TARGET} "${TARGET}")
set(ENV{THETAGP_SOURCE_DIR} "${CMAKE_SOURCE_DIR}")

# Generate board configuration (Python 3.11+ required for tomllib)
find_program(PYTHON3_EXECUTABLE NAMES python3 python)
if(NOT PYTHON3_EXECUTABLE)
    message(FATAL_ERROR "Python 3 not found. Please install Python 3.11+.")
endif()

execute_process(
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_config.py
        --target ${TARGET}
        --source-dir ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CONFIG_GEN_RESULT
)

if(NOT CONFIG_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate board configuration. Check the error messages above.")
endif()

include(${CMAKE_SOURCE_DIR}/configs/${TARGET}/board_config.cmake)

# First include toolchain settings to get THETAGP_TOOLCHAIN_PREFIX and THETAGP_MIN_VERSION
if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    include(${CMAKE_SOURCE_DIR}/platform/STM32/cmake/toolchain.cmake)
else()
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported currently.")
endif()

project(${THETAGP_PROJECT_NAME}
    LANGUAGES C CXX ASM
    VERSION ${THETAGP_VERSION}
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
