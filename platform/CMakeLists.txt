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
# Platform Path Configuration
# =============================================================================
set(PLATFORM_PATH "")

if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    set(PLATFORM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/STM32")
    set(PLATFORM_MCU_PREFIX "stm32h7")
    set(PLATFORM_MCU_NAME_PREFIX "STM32H7")
    set(PLATFORM_VENDOR "ST")
else()
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported currently.")
endif()

# =============================================================================
# Startup File and Link Script
# =============================================================================
string(TOLOWER "startup_${BOARD_MCU}" STARTUP_FILE_BASE_NAME)
string(TOUPPER "${BOARD_MCU}_FLASH" LINK_SCRIPT_BASE_NAME)

set(STARTUP_FILE "${PLATFORM_PATH}/startup/${STARTUP_FILE_BASE_NAME}.s")
set(LINKSCRIPT_FILE "${PLATFORM_PATH}/link/${LINK_SCRIPT_BASE_NAME}.ld")

if(NOT EXISTS "${STARTUP_FILE}")
    message(FATAL_ERROR "Startup file not found: ${STARTUP_FILE}")
endif()

if(NOT EXISTS "${LINKSCRIPT_FILE}")
    message(FATAL_ERROR "Link script not found: ${LINKSCRIPT_FILE}")
endif()

# =============================================================================
# HAL Driver Paths
# =============================================================================
if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    set(DRIVER_PATH "${CMAKE_SOURCE_DIR}/lib/Drivers/${PLATFORM_MCU_NAME_PREFIX}xx_HAL_Driver")
endif()

set(DRIVER_INC_PATH "${DRIVER_PATH}/Inc")
set(DRIVER_SRC_PATH "${DRIVER_PATH}/Src")
set(CMSIS_PATH "${CMAKE_SOURCE_DIR}/lib/Drivers/CMSIS")

# Vendor-specific middleware path
if(PLATFORM_VENDOR STREQUAL "ST")
    set(MIDDLEWARE_PATH "${CMAKE_SOURCE_DIR}/lib/Middlewares/${PLATFORM_VENDOR}")
else()
    set(MIDDLEWARE_PATH "${CMAKE_SOURCE_DIR}/lib/Middlewares")
endif()

if(NOT EXISTS "${DRIVER_PATH}")
    message(FATAL_ERROR "Missing drivers for ${BOARD_MCU_SERIES}")
endif()

# =============================================================================
# Compile Definitions
# =============================================================================

set(COMPILE_DEFS
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    USE_FULL_LL_DRIVER
    ${BOARD_MCU}
    ${BOARD_MCU_SERIES}
)

# =============================================================================
# Collect Source Files
# =============================================================================

# HAL driver sources (exclude templates)
file(GLOB DRIVER_SOURCES LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS "${DRIVER_SRC_PATH}/*.c")
list(FILTER DRIVER_SOURCES EXCLUDE REGEX ".*template.*")

# Middleware sources (exclude templates)
file(GLOB_RECURSE MIDDLEWARE_SOURCES "${MIDDLEWARE_PATH}/*.c")
list(FILTER MIDDLEWARE_SOURCES EXCLUDE REGEX ".*template.*")

# Platform system sources
set(SYSTEM_PATH "${PLATFORM_PATH}/system")

# Common system files (all MCU)
set(SYSTEM_SOURCES
    "${SYSTEM_PATH}/syscalls.c"
    "${SYSTEM_PATH}/sysmem.c"
)

# MCU series-specific files in system directory (match files containing PLATFORM_MCU_PREFIX)
file(GLOB SYSTEM_MCU_SOURCES "${SYSTEM_PATH}/*${PLATFORM_MCU_PREFIX}*")
list(APPEND SYSTEM_SOURCES ${SYSTEM_MCU_SOURCES})

# Target-specific files (match files containing PLATFORM_MCU_PREFIX)
set(TARGET_PATH "${SYSTEM_PATH}/target")
file(GLOB TARGET_SOURCES "${TARGET_PATH}/*${PLATFORM_MCU_PREFIX}*")
list(APPEND SYSTEM_SOURCES ${TARGET_SOURCES})

# =============================================================================
# Include Directories
# =============================================================================

# Board config header path
set(BOARD_CONFIG_PATH "${CMAKE_SOURCE_DIR}/configs/${TARGET}")

set(INCLUDE_DIRS
    "${DRIVER_INC_PATH}"
    "${CMSIS_PATH}/Include"
    "${CMSIS_PATH}/Device/${PLATFORM_VENDOR}/${BOARD_MCU_SERIES}/Include"
    "${SYSTEM_PATH}"
    "${BOARD_CONFIG_PATH}"
)

# Vendor-specific middleware include directories
if(PLATFORM_VENDOR STREQUAL "ST")
    list(APPEND INCLUDE_DIRS
        "${CMSIS_PATH}/Device/${PLATFORM_VENDOR}/${PLATFORM_MCU_NAME_PREFIX}xx/Include"
    )
endif()

list(APPEND INCLUDE_DIRS "${SYSTEM_PATH}")

# =============================================================================
# Link Script Configuration
# =============================================================================
set(PLATFORM_LINK_SCRIPT "${LINKSCRIPT_FILE}" CACHE INTERNAL "Linker script path")

# =============================================================================
# Add Platform Library
# =============================================================================
add_library(PlatformBase INTERFACE)
target_include_directories(PlatformBase INTERFACE ${INCLUDE_DIRS})
target_compile_definitions(PlatformBase INTERFACE ${COMPILE_DEFS})

add_library(HalDriver OBJECT)
target_sources(HalDriver
    PRIVATE
    ${DRIVER_SOURCES}
    ${SYSTEM_SOURCES}
    ${STARTUP_FILE}
)
target_link_libraries(HalDriver
    PUBLIC
    PlatformBase
)
