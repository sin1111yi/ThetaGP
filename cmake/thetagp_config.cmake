#[[
# Build feature switches for ThetaGP.
#
# Only switches that decide what gets compiled belong here. Firmware behaviour
# parameters live in src/conf/ThetaGP_Config.h; board properties live in
# configs/<target>/BoardConfig.toml.
#
# All values use `if(NOT DEFINED ...)` so they can be overridden from the
# CMake command line via -D<VAR>=<VALUE>.
#
# Examples:
#   cmake -B build -DTARGET=BoringTechH743
#   cmake -B build -DTARGET=BoringTechH743 -DTHETAGP_CFG_TEST=ON
#]]

# =============================================================================
# Helper function
# =============================================================================
# Sets a default value for a THETAGP_CFG_* variable if not already defined
# (i.e. not overridden via -D on the command line).
#
# Usage:
#   thetagp_config(VAR_NAME DEFAULT <val>)
#   thetagp_config(VAR_NAME DEFAULT_DEBUG <val> DEFAULT_RELEASE <val>)
#
# When only DEFAULT is given, the same value applies to all build types.
function(thetagp_config name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DEFAULT;DEFAULT_DEBUG;DEFAULT_RELEASE" "")
    if(NOT DEFINED ${name})
        if(DEFINED ARG_DEFAULT)
            set(${name} "${ARG_DEFAULT}" PARENT_SCOPE)
        elseif(DEFINED ARG_DEFAULT_DEBUG AND CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(${name} "${ARG_DEFAULT_DEBUG}" PARENT_SCOPE)
        elseif(DEFINED ARG_DEFAULT_RELEASE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(${name} "${ARG_DEFAULT_RELEASE}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# =============================================================================
# Enable test API (ON compiles CDC JSON test command infrastructure)
# =============================================================================
thetagp_config(THETAGP_CFG_TEST DEFAULT OFF)

# =============================================================================
# TinyUSB verbose debug (ON sets CFG_TUSB_DEBUG=3)
# =============================================================================
thetagp_config(THETAGP_CFG_USB_DBG DEFAULT OFF)
