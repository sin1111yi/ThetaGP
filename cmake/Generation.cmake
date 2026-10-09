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

# The shape every generation step of this build shares: a set of outputs that
# must exist, be non-empty and be no older than the inputs they came from, and a
# command that writes them when they are not. A step states its own lists, its
# own command and the sentence to refuse with, and nothing else.

# thetagp_generation_needed(<result-var> INPUTS <files...> OUTPUTS <files...>)
#
# Sets <result-var> TRUE when an output is missing, empty or older than an
# input: an empty file with a fresh timestamp is not a generated one.
function(thetagp_generation_needed result)
    cmake_parse_arguments(ARG "" "" "INPUTS;OUTPUTS" ${ARGN})
    set(needed FALSE)
    foreach(output IN LISTS ARG_OUTPUTS)
        if(NOT EXISTS "${output}")
            set(needed TRUE)
            continue()
        endif()
        file(SIZE "${output}" output_size)
        if(output_size EQUAL 0)
            set(needed TRUE)
            continue()
        endif()
        foreach(input IN LISTS ARG_INPUTS)
            if("${input}" IS_NEWER_THAN "${output}")
                set(needed TRUE)
            endif()
        endforeach()
    endforeach()
    set(${result} ${needed} PARENT_SCOPE)
endfunction()

# thetagp_run_generator(<result-var> <command...>)
#
# Runs one generator from the source root and prints what it wrote to either
# stream, so a refusal reaches the person configuring. <result-var> is its exit
# code.
function(thetagp_run_generator result)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT stdout STREQUAL "")
        message(STATUS "${stdout}")
    endif()
    if(NOT stderr STREQUAL "")
        message(STATUS "${stderr}")
    endif()
    set(${result} ${exit_code} PARENT_SCOPE)
endfunction()

# thetagp_generated_outputs_ok(<result-var> <files...>)
#
# Sets <result-var> TRUE when every named file is there and not empty, so a
# generator that exited 0 without writing its output is caught here rather than
# by the compiler it would hand an absent file to.
function(thetagp_generated_outputs_ok result)
    set(ok TRUE)
    foreach(output IN LISTS ARGN)
        if(NOT EXISTS "${output}")
            set(ok FALSE)
        else()
            file(SIZE "${output}" output_size)
            if(output_size EQUAL 0)
                set(ok FALSE)
            endif()
        endif()
    endforeach()
    set(${result} ${ok} PARENT_SCOPE)
endfunction()
