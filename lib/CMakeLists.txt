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

# =============================================================================
# Fetch Dependencies
# =============================================================================

function(thetagp_fetch_dep name repo branch dest)
    if(NOT EXISTS "${dest}/.git")
        message(STATUS "Cloning ${name} → ${dest}")
        execute_process(
            COMMAND git clone --branch ${branch} ${repo} ${dest}
            RESULT_VARIABLE result
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Failed to clone ${name}: ${repo}")
        endif()
        return()
    endif()

    # Repo exists — check upstream for updates (with 5s timeout per
    # git operation to prevent network stalls from blocking configure)
    message(STATUS "${name}: checking for updates (5s timeout)...")
    execute_process(
        COMMAND timeout 5 git -C ${dest} fetch origin ${branch} --quiet
        RESULT_VARIABLE fetch_result
        TIMEOUT 10
        ERROR_QUIET
    )
    if(NOT fetch_result EQUAL 0)
        message(STATUS "${name}: fetch skipped (timeout or network unavailable)")
        return()
    endif()

    execute_process(
        COMMAND git -C ${dest} rev-list --count HEAD..origin/${branch}
        OUTPUT_VARIABLE behind
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        TIMEOUT 10
    )
    if(NOT behind MATCHES "^[0-9]+$")
        message(STATUS "${name}: rev-list failed, update check skipped")
        return()
    endif()
    if(behind GREATER 0)
        message(STATUS "${name}: ${behind} commit(s) behind — updating")
        execute_process(
            COMMAND git -C ${dest} pull --ff-only origin ${branch}
            RESULT_VARIABLE pull_result
        )
        if(NOT pull_result EQUAL 0)
            message(WARNING "${name}: pull failed — manual intervention needed")
        endif()
    endif()
endfunction()

thetagp_fetch_dep(tinyusb
    https://github.com/sin1111yi/tinyusb.git
    "master"
    "${CMAKE_SOURCE_DIR}/lib/tinyusb"
)
thetagp_fetch_dep(mbedtls
    https://github.com/Mbed-TLS/mbedtls.git
    "main"
    "${CMAKE_SOURCE_DIR}/lib/mbedtls"
)
thetagp_fetch_dep(frozen
    https://github.com/cesanta/frozen
    "master"
    "${CMAKE_SOURCE_DIR}/lib/frozen"
)

add_subdirectory(CRC32)

# frozen: lightweight JSON parser/generator (C)
add_library(frozen STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/frozen/frozen.c
)
target_include_directories(frozen
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/frozen
)
target_compile_definitions(frozen
    PRIVATE
    JSON_MINIMAL=1
)
