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
# Test API Option
# =============================================================================
option(THETAGP_ENABLE_TEST_API "Enable test API infrastructure" OFF)

# =============================================================================
# Protocol Code Generation (configure-time)
# =============================================================================
# Run gen_proto.py at configure time to generate protocol/proto.h.
# Only regenerates when protocol.toml changes (CMake tracks input/output).
find_program(PYTHON3 python3)
if(PYTHON3 AND THETAGP_ENABLE_TEST_API)
    set(PROTO_TOML "${CMAKE_SOURCE_DIR}/protocol/protocol.toml")
    set(PROTO_H "${CMAKE_SOURCE_DIR}/protocol/proto.h")
    if(EXISTS "${PROTO_TOML}")
        message(STATUS "Generating protocol/proto.h from protocol.toml ...")
        execute_process(
            COMMAND ${PYTHON3} "${CMAKE_SOURCE_DIR}/scripts/gen_proto.py"
                --target cpp
                --outdir-cpp "${CMAKE_SOURCE_DIR}/protocol"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE PROTO_GEN_RESULT
        )
        if(PROTO_GEN_RESULT EQUAL 0)
            message(STATUS "Generated protocol/proto.h")
            # Mark as generated for dependency tracking
            set_property(SOURCE ${PROTO_H} PROPERTY GENERATED TRUE)
        else()
            message(WARNING "protocol/proto.h generation failed (exit=${PROTO_GEN_RESULT})")
        endif()
    else()
        message(STATUS "protocol/protocol.toml not found — skipping code generation")
    endif()
else()
    message(STATUS "Protocol code generation: skipped (python3=${PYTHON3}, test_api=${THETAGP_ENABLE_TEST_API})")
endif()

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
# TinyUSB Configuration
# =============================================================================
option(THETAGP_TINYUSB_DEBUG "TinyUSB debug level (0=off, 1=errors, 2=info, 3=verbose)" OFF)
if(THETAGP_TINYUSB_DEBUG)
    set(TINYUSB_DEBUG_LEVEL 3)
else()
    set(TINYUSB_DEBUG_LEVEL 0)
endif()

# TinyUSB paths
set(TINYUSB_PATH "${CMAKE_SOURCE_DIR}/lib/tinyusb")
set(TINYUSB_SRC_PATH "${TINYUSB_PATH}/src")

include(${TINYUSB_SRC_PATH}/CMakeLists.txt)
tinyusb_sources_get(TINYUSB_SOURCES)

if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    list(APPEND TINYUSB_SOURCES
        ${TINYUSB_SRC_PATH}/portable/synopsys/dwc2/dcd_dwc2.c
        ${TINYUSB_SRC_PATH}/portable/synopsys/dwc2/hcd_dwc2.c
        ${TINYUSB_SRC_PATH}/portable/synopsys/dwc2/dwc2_common.c
    )
endif()

# Suppress TinyUSB upstream source warnings (flexible array members, volatile ops)
# These come from TinyUSB headers included by our .cpp files, not from .c sources
foreach(TUSB_FILE ${TINYUSB_SOURCES})
    set_source_files_properties(${TUSB_FILE} PROPERTIES
        COMPILE_FLAGS "-Wno-volatile -Wno-pedantic")
endforeach()
# Project-wide: same warnings from TinyUSB headers pulled into C++ translation units
add_compile_options(-Wno-volatile -Wno-pedantic)

set(TINYUSB_CONFIG_FILE "conf/tusb_config.h")

# =============================================================================
# MbedTLS Configuration
# =============================================================================
set(MBEDTLS_PATH "${CMAKE_SOURCE_DIR}/lib/mbedtls")
set(MBEDTLS_SRC_PATH "${MBEDTLS_PATH}/library")
set(MBEDTLS_INC_PATH "${MBEDTLS_PATH}/include")

# MbedTLS core sources
file(GLOB MBEDTLS_SOURCES
    "${MBEDTLS_SRC_PATH}/*.c"
)

# Exclude files that don't work on embedded STM32
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*net_sockets\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*entropy_poll\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*timing\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*padlock\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*mps_trace\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*mps_reader\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*psa_crypto_driver_wrappers_no_static\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*platform_util\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*bignum_core\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*constant_time\\.c")
list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX ".*x509_crt\\.c")

set(MBEDTLS_CONFIG_FILE "build/mbedtls_config.h")
set(MBEDTLS_DEFS
    MBEDTLS_ARCH_IS_ARM32
    MBEDTLS_CT_NO_ASM
    MBEDTLS_NO_MLAE
)

# =============================================================================
# Executable Target
# =============================================================================
add_executable(${CMAKE_PROJECT_NAME})

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${SRC_FILES}
    ${TINYUSB_SOURCES}
    ${PLATFORM_PERIPHERAL_SOURCES}

    # ${MBEDTLS_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TINYUSB_SRC_PATH}

    # ${TINYUSB_SRC_PATH}/../lib/networking

    # ${MBEDTLS_INC_PATH}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
    HalDriver
    CRC32
    ArduinoJson
)

target_compile_definitions(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${PLATFORM_COMPILE_DEFS}

    CFG_TUSB_MCU=OPT_MCU_${BOARD_MCU_SERIES}
    CFG_TUSB_CONFIG_FILE="${TINYUSB_CONFIG_FILE}"
    CFG_TUSB_DEBUG=${TINYUSB_DEBUG_LEVEL}

    # MBEDTLS_CONFIG_FILE="${MBEDTLS_CONFIG_FILE}"
    # ${MBEDTLS_DEFS}
    THETAGP_CFG_LOG_ENABLE=${THETAGP_CFG_LOG_ENABLE}
    THETAGP_CFG_LOG_LEVEL=${THETAGP_CFG_LOG_LEVEL}
    THETAGP_SRC_PATH="${CMAKE_SOURCE_DIR}"

    BOARD_IDENTIFIER="${BOARD_IDENTIFIER}"
    BOARD_NAME="${BOARD_NAME}"
)

if(THETAGP_ENABLE_TEST_API)
    target_compile_definitions(${CMAKE_PROJECT_NAME}
        PRIVATE
        THETAGP_ENABLE_TEST_API
    )
endif()

# Set output directory and link script
set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LINK_DEPENDS ${PLATFORM_LINK_SCRIPT}
    LINK_FLAGS "-T${PLATFORM_LINK_SCRIPT}"
)
