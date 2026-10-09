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

include(${CMAKE_SOURCE_DIR}/configs/${TARGET}/board_config.cmake)

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
    "${CMAKE_SOURCE_DIR}/src/pb"
    "${CMAKE_SOURCE_DIR}/.clangd"
    "${CMAKE_BINARY_DIR}/proto_py"
)

add_custom_target(clean-generated
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${THETAGP_GENERATED_ARTIFACTS}
    COMMAND ${CMAKE_COMMAND} -E echo
            "Removed the generated protocol artifacts, the configuration key table, "
            "the language server configuration and the host bindings"
    COMMENT "Cleaning the generated artifacts under src/pb/ and configs/"
    VERBATIM)
