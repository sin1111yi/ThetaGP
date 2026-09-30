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
# Generators (configure-time)
# =============================================================================
# The interpreter the generation steps below run in.
find_program(PYTHON3 python3)

# =============================================================================
# Config Key Table (configure-time)
# =============================================================================
# configs/config_keys.toml declares the config store's fields; the generator
# turns it into the header the firmware includes. A stale header would compile
# and lie about the store, so a missing or older one is regenerated — a gate,
# not a warning.
set(CONFIG_KEYS_TOML "${CMAKE_SOURCE_DIR}/configs/config_keys.toml")
set(CONFIG_KEYS_GENERATOR "${CMAKE_SOURCE_DIR}/scripts/config/gen_config_keys.py")
set(CONFIG_KEYS_H "${CMAKE_SOURCE_DIR}/configs/config_keys.gen.h")

if(NOT EXISTS "${CONFIG_KEYS_TOML}")
    message(FATAL_ERROR
        "configs/config_keys.toml (the config key declaration) is not there. "
        "It is a tracked file and the only source of configs/config_keys.gen.h, "
        "which config_store.cpp and key_table.cpp include — restore it, then "
        "configure again.")
elseif(NOT EXISTS "${CONFIG_KEYS_GENERATOR}")
    message(FATAL_ERROR
        "scripts/config/gen_config_keys.py is not there. It is the only writer "
        "of configs/config_keys.gen.h, which the firmware includes — restore "
        "it, then configure again.")
else()
    set(CONFIG_KEYS_NEEDED FALSE)
    if(NOT EXISTS "${CONFIG_KEYS_H}")
        set(CONFIG_KEYS_NEEDED TRUE)
    else()
        # An empty header with a fresh timestamp is not current.
        file(SIZE "${CONFIG_KEYS_H}" CONFIG_KEYS_H_SIZE)
        if(CONFIG_KEYS_H_SIZE EQUAL 0)
            set(CONFIG_KEYS_NEEDED TRUE)
        elseif("${CONFIG_KEYS_TOML}" IS_NEWER_THAN "${CONFIG_KEYS_H}"
           OR "${CONFIG_KEYS_GENERATOR}" IS_NEWER_THAN "${CONFIG_KEYS_H}")
            set(CONFIG_KEYS_NEEDED TRUE)
        endif()
    endif()

    if(NOT CONFIG_KEYS_NEEDED)
        message(STATUS "Config key table: configs/config_keys.gen.h is up to date")
    elseif(NOT PYTHON3)
        message(FATAL_ERROR
            "configs/config_keys.gen.h is missing or older than "
            "configs/config_keys.toml, and python3 was not found to generate "
            "it. Install python3, or run "
            "python3 scripts/config/gen_config_keys.py in a worktree that has "
            "it and configure from there.")
    else()
        message(STATUS "Generating configs/config_keys.gen.h from configs/config_keys.toml ...")
        execute_process(
            COMMAND ${PYTHON3} "${CONFIG_KEYS_GENERATOR}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE CONFIG_KEYS_RESULT
            OUTPUT_VARIABLE CONFIG_KEYS_STDOUT
            ERROR_VARIABLE CONFIG_KEYS_STDERR
        )
        if(NOT CONFIG_KEYS_STDOUT STREQUAL "")
            message(STATUS "${CONFIG_KEYS_STDOUT}")
        endif()
        if(NOT CONFIG_KEYS_STDERR STREQUAL "")
            message(STATUS "${CONFIG_KEYS_STDERR}")
        endif()
        if(NOT CONFIG_KEYS_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Generating the config key table failed: "
                "scripts/config/gen_config_keys.py exited ${CONFIG_KEYS_RESULT}. "
                "Its output is printed above, and it names the field it could "
                "not carry. Fix configs/config_keys.toml (the declaration), or "
                "run the generator on its own to iterate: "
                "python3 scripts/config/gen_config_keys.py --dry-run")
        endif()
        if(NOT EXISTS "${CONFIG_KEYS_H}")
            message(FATAL_ERROR
                "scripts/config/gen_config_keys.py exited 0 but "
                "configs/config_keys.gen.h is not there. The generator is the "
                "only writer of that header and the firmware includes it — a "
                "build cannot compile a file that does not exist.")
        endif()
        message(STATUS "Generated configs/config_keys.gen.h")
    endif()
endif()

# =============================================================================
# Protobuf Codec Generation (configure-time)
# =============================================================================
# The schema is its own repository, pinned by tag and fetched by lib/CMakeLists.txt.
# Its generator writes the .pb.c/.pb.h pair the firmware compiles into src/pb/; a
# pair that is missing, empty or older than the schema or its .options file is
# regenerated, and a failure stops the configure rather than compiling a stale
# codec.
set(THETAGP_PB_DIR "${CMAKE_SOURCE_DIR}/lib/ThetaGP.PB")

set(PROTO_SCHEMAS
    # The one protocol template, from the pinned schema checkout.
    "${THETAGP_PB_DIR}/ThetaGP.proto"
)
# The generated pair lands under src/pb/ and is ignored by git.
set(PROTO_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pb")

# The .pb.c/.pb.h of each schema, in manifest order.
set(PROTO_PB_SOURCES "")
set(PROTO_PB_OUTPUTS "")
foreach(PROTO_SCHEMA IN LISTS PROTO_SCHEMAS)
    if(NOT EXISTS "${PROTO_SCHEMA}")
        message(FATAL_ERROR
            "${PROTO_SCHEMA} is listed in PROTO_SCHEMAS of "
            "${CMAKE_CURRENT_LIST_FILE} but is not there. A schema of that list "
            "is the only source of the pair generated from it, and both the "
            "generation call and the source list below are built from that same "
            "list — restore the schema, or take the line out of the list.")
    endif()
    get_filename_component(PROTO_SCHEMA_STEM "${PROTO_SCHEMA}" NAME_WE)
    set(PROTO_PB_C "${PROTO_OUT_DIR}/${PROTO_SCHEMA_STEM}.pb.c")
    list(APPEND PROTO_PB_SOURCES "${PROTO_PB_C}")
    list(APPEND PROTO_PB_OUTPUTS "${PROTO_PB_C}"
                                 "${PROTO_OUT_DIR}/${PROTO_SCHEMA_STEM}.pb.h")
endforeach()

# Freshness inputs: each schema and the .options file beside it, when present.
set(PROTO_INPUTS ${PROTO_SCHEMAS})
foreach(PROTO_SCHEMA IN LISTS PROTO_SCHEMAS)
    get_filename_component(PROTO_SCHEMA_DIR "${PROTO_SCHEMA}" DIRECTORY)
    get_filename_component(PROTO_SCHEMA_STEM "${PROTO_SCHEMA}" NAME_WE)
    set(PROTO_OPTIONS "${PROTO_SCHEMA_DIR}/${PROTO_SCHEMA_STEM}.options")
    if(EXISTS "${PROTO_OPTIONS}")
        list(APPEND PROTO_INPUTS "${PROTO_OPTIONS}")
    endif()
endforeach()

# The lists as one line each, for the messages below.
string(REPLACE ";" ", " PROTO_SCHEMAS_TEXT "${PROTO_SCHEMAS}")
string(REPLACE ";" ", " PROTO_PB_OUTPUTS_TEXT "${PROTO_PB_OUTPUTS}")
string(REPLACE ";" ", " PROTO_INPUTS_TEXT "${PROTO_INPUTS}")

if(NOT PROTO_SCHEMAS)
    message(STATUS "No protobuf schema is listed — skipping the protobuf codecs")
else()
    set(PROTO_PB_NEEDED FALSE)
    foreach(PROTO_PB_OUTPUT IN LISTS PROTO_PB_OUTPUTS)
        if(NOT EXISTS "${PROTO_PB_OUTPUT}")
            set(PROTO_PB_NEEDED TRUE)
        else()
            # An empty file with a fresh timestamp is not a generated one.
            file(SIZE "${PROTO_PB_OUTPUT}" PROTO_PB_OUTPUT_SIZE)
            if(PROTO_PB_OUTPUT_SIZE EQUAL 0)
                set(PROTO_PB_NEEDED TRUE)
            endif()
        endif()
    endforeach()
    foreach(PROTO_INPUT IN LISTS PROTO_INPUTS)
        foreach(PROTO_PB_OUTPUT IN LISTS PROTO_PB_OUTPUTS)
            if("${PROTO_INPUT}" IS_NEWER_THAN "${PROTO_PB_OUTPUT}")
                set(PROTO_PB_NEEDED TRUE)
            endif()
        endforeach()
    endforeach()

    if(NOT PROTO_PB_NEEDED)
        message(STATUS "Protobuf codecs: ${PROTO_PB_OUTPUTS_TEXT} are up to date")
    elseif(NOT PYTHON3)
        message(FATAL_ERROR
            "A generated protobuf codec (${PROTO_PB_OUTPUTS_TEXT}) is missing, "
            "empty or older than one of ${PROTO_INPUTS_TEXT}, and python3 was "
            "not found to generate them. Install python3, or run "
            "${THETAGP_PB_DIR}/generate.py in an environment that has protoc and "
            "python-protobuf and configure from that worktree.")
    else()
        message(STATUS "Generating ${PROTO_PB_OUTPUTS_TEXT} from ${PROTO_SCHEMAS_TEXT} ...")
        execute_process(
            COMMAND ${PYTHON3} "${THETAGP_PB_DIR}/generate.py" c
                    --nanopb "${CMAKE_SOURCE_DIR}/lib/nanopb"
                    --out "${PROTO_OUT_DIR}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE PROTO_PB_RESULT
            OUTPUT_VARIABLE PROTO_PB_STDOUT
            ERROR_VARIABLE PROTO_PB_STDERR
        )
        if(NOT PROTO_PB_STDOUT STREQUAL "")
            message(STATUS "${PROTO_PB_STDOUT}")
        endif()
        if(NOT PROTO_PB_STDERR STREQUAL "")
            message(STATUS "${PROTO_PB_STDERR}")
        endif()
        if(NOT PROTO_PB_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Generating the protobuf codecs failed: "
                "${THETAGP_PB_DIR}/generate.py exited ${PROTO_PB_RESULT}. Its output "
                "is printed above. It needs protoc on PATH, the fetched "
                "lib/nanopb checkout, and a python-protobuf runtime — install "
                "one, or point $PROTOBUF_PYTHONPATH (or the script's "
                "--protobuf-path) at a site-packages directory that holds "
                "google/protobuf.")
        endif()
        foreach(PROTO_PB_OUTPUT IN LISTS PROTO_PB_OUTPUTS)
            if(NOT EXISTS "${PROTO_PB_OUTPUT}")
                message(FATAL_ERROR
                    "${THETAGP_PB_DIR}/generate.py exited 0 but "
                    "${PROTO_PB_OUTPUT} is not there. The generator is the only "
                    "writer of that file, and it is either named in the source "
                    "list below or included by the sources that are — a build "
                    "cannot compile a file that does not exist.")
            endif()
            file(SIZE "${PROTO_PB_OUTPUT}" PROTO_PB_OUTPUT_SIZE)
            if(PROTO_PB_OUTPUT_SIZE EQUAL 0)
                message(FATAL_ERROR
                    "${THETAGP_PB_DIR}/generate.py exited 0 but "
                    "${PROTO_PB_OUTPUT} is empty. A file of zero bytes is not a "
                    "generated codec, and the next configure would take it for "
                    "current on its timestamp alone.")
            endif()
        endforeach()
        message(STATUS "Generated ${PROTO_PB_OUTPUTS_TEXT}")
    endif()
endif()

# =============================================================================
# Source Files
# =============================================================================

# All sources under src/.
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# =============================================================================
# TinyUSB Configuration
# =============================================================================
# THETAGP_CFG_USB_DBG (in ThetaGP_Config.h) as CFG_TUSB_DEBUG: 1 -> verbose (3),
# 0 -> silent.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/conf/ThetaGP_Config.h" _thetagp_usb_dbg
     REGEX "^#define[ \t]+THETAGP_CFG_USB_DBG[ \t]+([0-9]+)")
if(_thetagp_usb_dbg MATCHES "[ \t]([0-9]+)$" AND NOT CMAKE_MATCH_1 STREQUAL "0")
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

# Silence TinyUSB upstream warnings that reach our C++ translation units.
foreach(TUSB_FILE ${TINYUSB_SOURCES})
    get_filename_component(TUSB_EXT "${TUSB_FILE}" EXT)
    if(TUSB_EXT STREQUAL ".cpp" OR TUSB_EXT STREQUAL ".cc")
        set_source_files_properties(${TUSB_FILE} PROPERTIES
            COMPILE_FLAGS "-Wno-volatile -Wno-pedantic")
    endif()
endforeach()
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-volatile>")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-pedantic>")

set(TINYUSB_CONFIG_FILE "conf/tusb_config.h")

# =============================================================================
# MbedTLS Configuration
# =============================================================================
set(MBEDTLS_PATH "${CMAKE_SOURCE_DIR}/lib/mbedtls")
set(MBEDTLS_SRC_PATH "${MBEDTLS_PATH}/library")
set(MBEDTLS_INC_PATH "${MBEDTLS_PATH}/include")

file(GLOB MBEDTLS_SOURCES
    "${MBEDTLS_SRC_PATH}/*.c"
)

# Files that do not build on bare-metal STM32.
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

    # The generated protobuf codec, written under src/pb/ and ignored.
    ${PROTO_PB_SOURCES}

    # ${MBEDTLS_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${TINYUSB_SRC_PATH}

    # ${TINYUSB_SRC_PATH}/../lib/networking

    # ${MBEDTLS_INC_PATH}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
    HalDriver
    frozen
    nanopb
)

target_compile_definitions(${CMAKE_PROJECT_NAME}
    PRIVATE
    ${PLATFORM_COMPILE_DEFS}

    CFG_TUSB_MCU=OPT_MCU_${BOARD_MCU_SERIES}
    CFG_TUSB_CONFIG_FILE="${TINYUSB_CONFIG_FILE}"
    CFG_TUSB_DEBUG=${TINYUSB_DEBUG_LEVEL}

    # MBEDTLS_CONFIG_FILE="${MBEDTLS_CONFIG_FILE}"
    # ${MBEDTLS_DEFS}

    # Source root for the logger's path shortening.
    THETAGP_SRC_PATH="${CMAKE_SOURCE_DIR}"

    BOARD_IDENTIFIER="${BOARD_IDENTIFIER}"
    BOARD_NAME="${BOARD_NAME}"

    # The version sys.get_fw_version reports, from the top-level CMakeLists.
    THETAGP_FW_VERSION="${THETAGP_VERSION}"
)

set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LINK_DEPENDS ${PLATFORM_LINK_SCRIPT}
    LINK_FLAGS "-T${PLATFORM_LINK_SCRIPT}"
)


