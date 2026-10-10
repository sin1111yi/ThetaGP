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

cmake_minimum_required(VERSION 4.0)

# =============================================================================
# Board Selection
# =============================================================================
# TARGET names the board this build is for, and configs/<TARGET>/BoardConfig.toml
# is what the name has to resolve to. A name that resolves to nothing is refused
# here, against the list of the boards that exist, rather than later against a
# file that merely is not there.
file(GLOB THETAGP_BOARDS
     RELATIVE "${CMAKE_SOURCE_DIR}/configs"
     "${CMAKE_SOURCE_DIR}/configs/*/BoardConfig.toml")
list(TRANSFORM THETAGP_BOARDS REPLACE "/BoardConfig\\.toml$" "")
list(SORT THETAGP_BOARDS)
list(JOIN THETAGP_BOARDS " " THETAGP_BOARDS_TEXT)

if(NOT TARGET)
    message(FATAL_ERROR
        "TARGET is not set. Configure with -DTARGET=<board>; the boards "
        "declared under configs/ are: ${THETAGP_BOARDS_TEXT}")
endif()
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/configs/${TARGET}/BoardConfig.toml")
    message(FATAL_ERROR
        "No board named '${TARGET}': configs/${TARGET}/BoardConfig.toml is not "
        "there. The boards declared under configs/ are: ${THETAGP_BOARDS_TEXT}")
endif()

# =============================================================================
# Build Type
# =============================================================================
# A single-config generator leaves CMAKE_BUILD_TYPE empty unless the caller
# states it. Both the compile flags and the artifact's name read it, so an empty
# one is defaulted here rather than carried into either.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug or Release)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
endif()

# The toolchain states flags for Debug and Release only, and the artifact's name
# carries whichever is set: a third value would build with no optimization flags
# at all and name itself after a kind of build that has none, so it is refused.
if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}', and the toolchain states "
        "flags for Debug and Release only. Set one of those two, or leave it "
        "unset to get Debug.")
endif()

# =============================================================================
# Python
# =============================================================================
# The one interpreter every generation step runs in: the board configuration
# below, the language-server file the toolchain writes, and the config-key and
# protocol generators under src/. Finding it once keeps them all on it.
find_program(PYTHON3 NAMES python3)

# =============================================================================
# Generation Helpers
# =============================================================================
# The shape every generation step of this build shares lives beside this file,
# in cmake/Generation.cmake, so a step below carries only what differs between
# it and the others: its inputs, its outputs and its command.
include("${CMAKE_SOURCE_DIR}/cmake/Generation.cmake")

# =============================================================================
# Board Configuration
# =============================================================================
# BoardConfig.toml declares the board; BoardConfig.h and board_config.cmake are
# the pair generated from it and are not tracked. A checkout that carries the
# TOML and neither output fails at the include below, so the pair is generated
# here, before anything reads it.
set(BOARD_CONFIG_TOML "${CMAKE_SOURCE_DIR}/configs/${TARGET}/BoardConfig.toml")
set(BOARD_CONFIG_H "${CMAKE_SOURCE_DIR}/configs/${TARGET}/BoardConfig.h")
set(BOARD_CONFIG_CMAKE "${CMAKE_SOURCE_DIR}/configs/${TARGET}/board_config.cmake")
set(BOARD_CONFIG_GENERATOR "${CMAKE_SOURCE_DIR}/scripts/generate_config.py")
# The generator reads the declaration through the scripts/config package, whose
# modules are inputs too. Its two standalone tools are not.
file(GLOB BOARD_CONFIG_GENERATOR_MODULES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/scripts/config/*.py")
list(FILTER BOARD_CONFIG_GENERATOR_MODULES EXCLUDE
     REGEX "/(gen_board_doc|schema_dump)\\.py$")

thetagp_generation_needed(BOARD_CONFIG_NEEDED
    INPUTS "${BOARD_CONFIG_TOML}" "${BOARD_CONFIG_GENERATOR}"
           ${BOARD_CONFIG_GENERATOR_MODULES}
    OUTPUTS "${BOARD_CONFIG_H}" "${BOARD_CONFIG_CMAKE}")

if(NOT BOARD_CONFIG_NEEDED)
    message(STATUS "Board config: configs/${TARGET}/BoardConfig.h is up to date")
elseif(NOT PYTHON3)
    message(FATAL_ERROR
        "configs/${TARGET}/BoardConfig.h and board_config.cmake are missing or "
        "older than ${BOARD_CONFIG_TOML}, and python3 was not found to generate "
        "them. Install python3, or run "
        "python3 scripts/generate_config.py --target ${TARGET} in the source "
        "root and configure again.")
else()
    message(STATUS "Generating configs/${TARGET}/BoardConfig.h from its TOML ...")
    thetagp_run_generator(BOARD_CONFIG_RESULT
        ${PYTHON3} "${BOARD_CONFIG_GENERATOR}" --target "${TARGET}")
    if(NOT BOARD_CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Generating the board configuration failed: "
            "scripts/generate_config.py exited ${BOARD_CONFIG_RESULT}. Its "
            "output is printed above and names the field it refused — fix "
            "configs/${TARGET}/BoardConfig.toml, then configure again.")
    endif()
    thetagp_generated_outputs_ok(BOARD_CONFIG_OK
        "${BOARD_CONFIG_H}" "${BOARD_CONFIG_CMAKE}")
    if(NOT BOARD_CONFIG_OK)
        message(FATAL_ERROR
            "scripts/generate_config.py exited 0 but ${BOARD_CONFIG_H} or "
            "${BOARD_CONFIG_CMAKE} is not there or is empty. The generator is the "
            "only writer of them, and the configuration below reads them.")
    endif()
    message(STATUS "Generated configs/${TARGET}/BoardConfig.h")
endif()

include("${BOARD_CONFIG_CMAKE}")

# =============================================================================
# Project Configuration
# =============================================================================

# Project version: the tag the worktree is on, as `git describe` writes it --
# the tag itself on a release, and the tag it is past with the commits since and
# the commit otherwise, with `-dirty` when the worktree holds changes no commit
# does. There is no number of its own beside the tag: a worktree no tag names is
# the commit it is, and a build without git says so rather than claiming one.
set(THETAGP_VERSION "unknown")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE THETAGP_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE THETAGP_GIT_DESCRIBE_RESULT)
    if(THETAGP_GIT_DESCRIBE_RESULT EQUAL 0)
        string(REGEX REPLACE "^v" "" THETAGP_VERSION "${THETAGP_GIT_DESCRIBE}")
    endif()
endif()

# The artifact's name: the project, the board the binary is for, the revision
# the version names -- a tag on a release, the commit it is otherwise -- and the
# kind of build. A flashed binary can be told from another by its name alone.
set(THETAGP_PROJECT_NAME "ThetaGP_${TARGET}_${THETAGP_VERSION}_${CMAKE_BUILD_TYPE}")

# First include toolchain settings to get THETAGP_TOOLCHAIN_PREFIX and THETAGP_MIN_VERSION
if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    include(${CMAKE_SOURCE_DIR}/platform/STM32/cmake/toolchain.cmake)
else()
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported currently.")
endif()

project(${THETAGP_PROJECT_NAME}
    LANGUAGES C CXX ASM
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

# =============================================================================
# Generated artifacts
# =============================================================================

# The configure step writes generated files into the source tree; the build
# system's own clean does not remove them, so this target does.
set(THETAGP_GENERATED_ARTIFACTS
    "${CMAKE_SOURCE_DIR}/configs/config_keys.gen.h"
    "${CMAKE_SOURCE_DIR}/configs/${TARGET}/BoardConfig.h"
    "${CMAKE_SOURCE_DIR}/configs/${TARGET}/board_config.cmake"
    "${CMAKE_SOURCE_DIR}/src/pb"
    "${CMAKE_SOURCE_DIR}/.clangd"
)

add_custom_target(clean-generated
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${THETAGP_GENERATED_ARTIFACTS}
    COMMAND ${CMAKE_COMMAND} -E echo
            "Removed the board configuration, the config key table, the protocol "
            "sources and the language server configuration"
    COMMENT "Cleaning the generated artifacts under configs/ and src/pb/"
    VERBATIM)
