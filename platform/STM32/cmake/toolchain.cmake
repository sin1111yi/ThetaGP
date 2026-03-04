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
# ARM Toolchain Configuration for STM32H7
# =============================================================================

# Toolchain prefix and minimum version (used by toolchain detection script)
set(THETAGP_TOOLCHAIN_PREFIX "arm-none-eabi-")
set(THETAGP_MIN_VERSION "13.3.1")

set(CMAKE_SYSTEM_NAME        Generic)
set(CMAKE_SYSTEM_PROCESSOR   arm)

set(CMAKE_C_COMPILER_ID      GNU)
set(CMAKE_CXX_COMPILER_ID    GNU)

set(TOOLCHAIN_PREFIX         ${THETAGP_TOOLCHAIN_PREFIX})

set(CMAKE_C_COMPILER         ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER       ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER       ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER             ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY            ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE               ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM   ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX   ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# =============================================================================
# MCU-specific Configuration
# =============================================================================

if(BOARD_MCU_SERIES STREQUAL "STM32H7")
    # Cortex-M7 flags for STM32H7
    set(TARGET_CPU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
else()
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}. Only STM32H7 is supported currently.")
endif()

# =============================================================================
# Compiler Flags
# =============================================================================

set(COMMON_FLAGS "${TARGET_CPU_FLAGS}")

# C flags
set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS} -Wall -Wextra -Wpedantic -fdata-sections -ffunction-sections")

# C++ flags
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

# ASM flags
set(CMAKE_ASM_FLAGS_INIT "${COMMON_FLAGS} -x assembler-with-cpp -MMD -MP")

# Debug flags
set(CMAKE_C_FLAGS_DEBUG_INIT   "-O0 -g3")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-O0 -g3")

# Release flags
set(CMAKE_C_FLAGS_RELEASE_INIT   "-Os -g0")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Os -g0")

# =============================================================================
# Linker Flags
# =============================================================================

set(CMAKE_C_LINK_FLAGS_INIT "${COMMON_FLAGS}")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} --specs=nano.specs")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,-Map=${THETAGP_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group -lc -lm -Wl,--end-group")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--print-memory-usage")

set(CMAKE_CXX_LINK_FLAGS_INIT "${COMMON_FLAGS}")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} --specs=nano.specs")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -Wl,-Map=${THETAGP_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -Wl,--start-group -lstdc++ -lsupc++ -Wl,--end-group")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -Wl,--print-memory-usage")
