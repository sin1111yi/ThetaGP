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
    message(FATAL_ERROR "Unsupported MCU series: ${BOARD_MCU_SERIES}.")
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
# What every executable of this toolchain is linked with: the target's own
# flags, the nano C library, the map, the section options and the usage report.
# The standard libraries are not named here -- the compiler driver adds the
# ones its link language needs after the objects, which is where a reference
# can still pull a member out of them.
set(CMAKE_EXE_LINKER_FLAGS_INIT "${COMMON_FLAGS} --specs=nano.specs")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -Wl,-Map=${THETAGP_PROJECT_NAME}.map")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -Wl,--gc-sections")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -Wl,--no-warn-rwx-segments")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -Wl,--print-memory-usage")

# =============================================================================
# Language Server Configuration
# =============================================================================

# The compilation database carries the project's own -I paths but not the
# toolchain's, so the compiler's include search list is written to .clangd for
# the editor to read. PYTHON3 is found by the top-level CMakeLists before this
# file is included.
execute_process(
    COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/scripts/generate_clangd.py
        --compiler ${CMAKE_CXX_COMPILER}
        --flags "${TARGET_CPU_FLAGS}"
        --out ${CMAKE_SOURCE_DIR}/.clangd
    RESULT_VARIABLE CLANGD_GEN_RESULT
)

if(NOT CLANGD_GEN_RESULT EQUAL 0)
    message(WARNING "Failed to generate .clangd; the language server keeps "
                    "the configuration it already has.")
endif()
