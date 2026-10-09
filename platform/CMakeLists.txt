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
# Platform Selection
# =============================================================================
# Everything this layer states about the platform it builds for -- the tree its
# own files live in, the vendor drivers it is built against, the macros those
# drivers are compiled with and the include directories that carry their headers
# -- is decided in this one block. The rest of the file reads what it sets and
# does not know which platform it is.
if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    set(PLATFORM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/STM32")
    set(PLATFORM_MCU_PREFIX "stm32h7")
    set(PLATFORM_MCU_NAME_PREFIX "STM32H7")
    set(PLATFORM_VENDOR "ST")

    # The vendor driver tree and the CMSIS tree beside it.
    set(PLATFORM_DRIVER_PATH
        "${CMAKE_SOURCE_DIR}/lib/Drivers/${PLATFORM_MCU_NAME_PREFIX}xx_HAL_Driver")
    set(PLATFORM_DRIVER_INC_PATH "${PLATFORM_DRIVER_PATH}/Inc")
    set(PLATFORM_DRIVER_SRC_PATH "${PLATFORM_DRIVER_PATH}/Src")
    set(PLATFORM_CMSIS_PATH "${CMAKE_SOURCE_DIR}/lib/Drivers/CMSIS")

    # The macros this platform's drivers and headers are compiled with, beside
    # the board's own identifiers in COMPILE_DEFS below.
    set(PLATFORM_COMPILE_DEFS
        USE_PWR_LDO_SUPPLY
        USE_HAL_DRIVER
        USE_FULL_LL_DRIVER
    )

    # The include directories that carry this platform's headers, ahead of the
    # board's and the project's own.
    set(PLATFORM_INCLUDE_DIRS
        "${PLATFORM_DRIVER_INC_PATH}"
        "${PLATFORM_CMSIS_PATH}/Include"
        "${PLATFORM_CMSIS_PATH}/Device/${PLATFORM_VENDOR}/${BOARD_MCU_SERIES}/Include"
        "${PLATFORM_CMSIS_PATH}/Device/${PLATFORM_VENDOR}/${PLATFORM_MCU_NAME_PREFIX}xx/Include"
    )
else()
    message(FATAL_ERROR
        "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported "
        "currently.")
endif()

if(NOT EXISTS "${PLATFORM_DRIVER_PATH}")
    message(FATAL_ERROR
        "The ${BOARD_MCU_SERIES} driver tree is not there: "
        "${PLATFORM_DRIVER_PATH}. lib/CMakeLists.txt fetches it — configure "
        "once where it can be fetched, or restore it by hand.")
endif()

# =============================================================================
# Startup File and Link Script
# =============================================================================
# Both are named by the board's MCU, so a board states which MCU it is and this
# resolves the pair under the platform's own tree.
string(TOLOWER "startup_${BOARD_MCU}" STARTUP_FILE_BASE_NAME)
string(TOUPPER "${BOARD_MCU}_FLASH" LINK_SCRIPT_BASE_NAME)

set(STARTUP_FILE "${PLATFORM_PATH}/startup/${STARTUP_FILE_BASE_NAME}.s")
set(LINKSCRIPT_FILE "${PLATFORM_PATH}/link/${LINK_SCRIPT_BASE_NAME}.ld")

foreach(_platform_file IN ITEMS "${STARTUP_FILE}" "${LINKSCRIPT_FILE}")
    if(NOT EXISTS "${_platform_file}")
        message(FATAL_ERROR
            "${_platform_file} is not there. A board names its MCU and this "
            "resolves the startup file and the link script from that name — "
            "add the missing one, or correct BOARD_MCU.")
    endif()
endforeach()

# =============================================================================
# Compile Definitions
# =============================================================================
# What the platform states above, beside the board's own identifiers. Every
# target built against this layer carries the pair.
set(COMPILE_DEFS
    ${PLATFORM_COMPILE_DEFS}
    ${BOARD_MCU}
    ${BOARD_MCU_SERIES}
)

# =============================================================================
# Collect Source Files
# =============================================================================
set(SYSTEM_PATH "${PLATFORM_PATH}/system")

# The vendor driver sources. The templates are not built.
file(GLOB DRIVER_SOURCES LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
     "${PLATFORM_DRIVER_SRC_PATH}/*.c")
list(FILTER DRIVER_SOURCES EXCLUDE REGEX ".*template.*")

# The system files every platform carries, then the ones under this platform's
# own name and the ones under its target-specific name.
set(SYSTEM_SOURCES
    "${SYSTEM_PATH}/syscalls.c"
    "${SYSTEM_PATH}/sysmem.c"
)
file(GLOB SYSTEM_MCU_SOURCES CONFIGURE_DEPENDS
     "${SYSTEM_PATH}/*${PLATFORM_MCU_PREFIX}*")
list(APPEND SYSTEM_SOURCES ${SYSTEM_MCU_SOURCES})

file(GLOB TARGET_SOURCES CONFIGURE_DEPENDS
     "${SYSTEM_PATH}/target/*${PLATFORM_MCU_PREFIX}*")
list(APPEND SYSTEM_SOURCES ${TARGET_SOURCES})

# The platform layer's own peripheral implementations.
file(GLOB_RECURSE PLATFORM_PERIPHERAL_SOURCES CONFIGURE_DEPENDS
     "${PLATFORM_PATH}/peripherals/*.cpp"
     "${PLATFORM_PATH}/peripherals/*.c")

# =============================================================================
# Include Directories
# =============================================================================
# The platform's own, then the board's configuration directory and the project's
# sources, which the platform layer includes by their repository-relative paths.
set(INCLUDE_DIRS
    ${PLATFORM_INCLUDE_DIRS}
    "${SYSTEM_PATH}"
    "${CMAKE_SOURCE_DIR}/configs/${TARGET}"
    "${CMAKE_SOURCE_DIR}/src"
)

# =============================================================================
# Link Script Configuration
# =============================================================================
set(PLATFORM_LINK_SCRIPT "${LINKSCRIPT_FILE}" CACHE INTERNAL "Linker script path")

# =============================================================================
# Platform Targets
# =============================================================================
# PlatformBase is what everything built here is compiled with: the include
# directories above and the definitions above. PlatformDriver is the platform's
# own compiled C sources -- the vendor drivers, the system files and the startup
# file -- and is linked into the executable.
add_library(PlatformBase INTERFACE)
target_include_directories(PlatformBase INTERFACE ${INCLUDE_DIRS})
target_compile_definitions(PlatformBase INTERFACE ${COMPILE_DEFS})

add_library(PlatformDriver OBJECT)
target_sources(PlatformDriver
    PRIVATE
    ${DRIVER_SOURCES}
    ${SYSTEM_SOURCES}
    ${STARTUP_FILE}
)
target_link_libraries(PlatformDriver PUBLIC PlatformBase)

# The peripheral implementations are C++ that reaches into the project's own
# headers and through them into the USB stack only the executable carries, so
# they are compiled as part of the executable rather than as objects of their
# own: this target carries the list, and linking it puts the sources there.
add_library(PlatformPeripherals INTERFACE)
target_sources(PlatformPeripherals INTERFACE ${PLATFORM_PERIPHERAL_SOURCES})
target_link_libraries(PlatformPeripherals INTERFACE PlatformBase)
