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

# thetagp_fetch_dep(<name> <repo> <ref> <dest> [FIXED])
#
# Clones <repo> at <ref> into <dest>. By default <ref> is followed: the first
# configure of a day reaches upstream and pulls what it finds, and later ones that
# day use the checkout as it stands. With FIXED, <ref> is a version the dependency
# is pinned to and no configure reaches upstream after the first clone — a moving
# branch is not a pin.
function(thetagp_fetch_dep name repo ref dest)
    set(fixed FALSE)
    if(ARGN AND "${ARGN}" STREQUAL "FIXED")
        set(fixed TRUE)
    endif()

    if(NOT EXISTS "${dest}/.git")
        message(STATUS "Cloning ${name} → ${dest}")
        execute_process(
            COMMAND git clone --branch ${ref} ${repo} ${dest}
            RESULT_VARIABLE result
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Failed to clone ${name}: ${repo}")
        endif()
        return()
    endif()

    if(fixed)
        execute_process(
            COMMAND git -C ${dest} describe --tags --exact-match HEAD
            OUTPUT_VARIABLE at
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT at STREQUAL ref)
            message(STATUS "${name}: checking out ${ref} (was '${at}')")
            execute_process(COMMAND git -C ${dest} fetch --tags --quiet origin)
            execute_process(
                COMMAND git -C ${dest} checkout --quiet ${ref}
                RESULT_VARIABLE checkout_result
            )
            if(NOT checkout_result EQUAL 0)
                message(WARNING "${name}: could not check out ${ref} — staying "
                                "on '${at}'")
            endif()
        endif()
        return()
    endif()

    string(TIMESTAMP today "%Y-%m-%d")
    set(stamp "${CMAKE_BINARY_DIR}/.thetagp-upstream-${name}")
    # The first configure of a day reaches upstream; later ones read the stamp.
    if(EXISTS "${stamp}")
        file(READ "${stamp}" last_check)
        string(STRIP "${last_check}" last_check)
        if(last_check STREQUAL today)
            message(STATUS "${name}: upstream checked today, using the checkout")
            return()
        endif()
    endif()
    file(WRITE "${stamp}" "${today}")

    message(STATUS "${name}: checking upstream (first configure today)...")
    execute_process(
        COMMAND timeout 5 git -C ${dest} fetch origin ${ref} --quiet
        RESULT_VARIABLE fetch_result
        TIMEOUT 10
        ERROR_QUIET
    )
    if(NOT fetch_result EQUAL 0)
        message(WARNING "${name}: upstream is out of reach (timeout or no "
                        "network) — using the checkout as it stands")
        return()
    endif()

    execute_process(
        COMMAND git -C ${dest} rev-list --count HEAD..origin/${ref}
        OUTPUT_VARIABLE behind
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        TIMEOUT 10
    )
    if(NOT behind MATCHES "^[0-9]+$")
        message(WARNING "${name}: cannot compare with upstream — using the "
                        "checkout as it stands")
        return()
    endif()
    if(behind GREATER 0)
        message(STATUS "${name}: ${behind} commit(s) behind — updating")
        execute_process(
            COMMAND git -C ${dest} pull --ff-only origin ${ref}
            RESULT_VARIABLE pull_result
        )
        if(NOT pull_result EQUAL 0)
            message(WARNING "${name}: the update failed — using the checkout as "
                            "it stands; resolve it by hand")
        endif()
    else()
        message(STATUS "${name}: up to date")
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
# Follows upstream master rather than a tag; a checkout newer than the generated
# codec shows up as a header/runtime version mismatch, not a silent change.
thetagp_fetch_dep(nanopb
    https://github.com/nanopb/nanopb.git
    "master"
    "${CMAKE_SOURCE_DIR}/lib/nanopb"
)

# The protocol schema follows its main line, like the other dependencies.
thetagp_fetch_dep(ThetaGP_PB
    git@github.com:sin1111yi/ThetaGP.PB.git
    "main"
    "${CMAKE_SOURCE_DIR}/lib/ThetaGP.PB"
)

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

# nanopb: protobuf encoder/decoder for the protocol messages (C).
# PB_NO_ERRMSG drops the runtime's error strings: the host decoder reports what
# a malformed message was, so the firmware only needs the decode to fail.
add_library(nanopb STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/pb_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/pb_encode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/nanopb/pb_decode.c
)
target_include_directories(nanopb
    PUBLIC
    ${CMAKE_SOURCE_DIR}/lib/nanopb
)
target_compile_definitions(nanopb
    PRIVATE
    PB_NO_ERRMSG
)
