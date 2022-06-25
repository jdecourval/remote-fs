# Copyright (c) 2012 - 2017, Lars Bilke
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# CHANGES:
#
# 2012-01-31, Lars Bilke
# - Enable Code Coverage
#
# 2013-09-17, Joakim Söderberg
# - Added support for Clang.
# - Some additional usage instructions.
#
# 2016-02-03, Lars Bilke
# - Refactored functions to use named parameters
#
# 2017-06-02, Lars Bilke
# - Merged with modified version from github.com/ufz/ogs
#
#
# USAGE:
#
# 1. Copy this file into your cmake modules path.
#
# 2. Add the following line to your CMakeLists.txt:
#      include(CodeCoverage)
#
# 3. Append necessary compiler flags:
#      APPEND_COVERAGE_COMPILER_FLAGS()
#
# 4. If you need to exclude additional directories from the report, specify them
#    using the COVERAGE_LCOV_EXCLUDES variable before calling SETUP_TARGET_FOR_COVERAGE_LCOV.
#    Example:
#      set(COVERAGE_LCOV_EXCLUDES 'dir1/*' 'dir2/*')
#
# 5. Use the functions described below to create a custom make target which
#    runs your test executable and produces a code coverage report.
#
# 6. Build a Debug build:
#      cmake -DCMAKE_BUILD_TYPE=Debug ..
#      make
#      make my_coverage_target
#

include(CMakeParseArguments)
if (NOT TARGET all_coverage)
    add_custom_target(all_coverage)
endif ()

# Check prereqs
find_program(LCOV_PATH NAMES lcov lcov.bat lcov.exe lcov.perl)
find_program(GENHTML_PATH NAMES genhtml genhtml.perl genhtml.bat)
find_program(GCOVR_PATH gcovr PATHS ${CMAKE_SOURCE_DIR}/scripts/test)
find_program(SIMPLE_PYTHON_EXECUTABLE python)

if (SIMPLE_PYTHON_EXECUTABLE AND GCOVR_PATH)
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "(Apple)?[Cc]lang")
        if ("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS 3)
            message(WARNING "Clang version must be 3.0.0 or greater! Aborting...")
        endif ()
        find_program(GCOV_PATH llvm-cov)
        set(COVERAGE_EXECUTABLE ${GCOV_PATH} gcov CACHE STRING "")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        find_program(GCOV_PATH gcov)
        set(COVERAGE_EXECUTABLE ${GCOV_PATH} CACHE STRING "")
    else ()
        message(WARNING "Your compiler is not supported! Aborting...")
    endif ()
endif ()

if (NOT GCOV_PATH)
    message(WARNING "gcov not found!")
endif () # NOT GCOV_PATH


set(CMAKE_COMPILE_FLAGS_COVERAGE
        --coverage
        CACHE STRING "Flags used by the C++ compiler during coverage builds."
        FORCE)
set(CMAKE_LINKER_FLAGS_COVERAGE
        --coverage
        CACHE STRING "Flags used for linking binaries during coverage builds."
        FORCE)
mark_as_advanced(
        CMAKE_CXX_FLAGS_COVERAGE
        CMAKE_C_FLAGS_COVERAGE
        CMAKE_EXE_LINKER_FLAGS_COVERAGE
)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "Code coverage results with an optimised (non-Debug) build may be misleading")
endif () # NOT CMAKE_BUILD_TYPE STREQUAL "Debug"


# Defines a target for running and collection code coverage information
# Builds dependencies, runs the given executable and outputs reports.
# NOTE! The executable should always have a ZERO as exit code otherwise
# the coverage generation will not complete.
#
# SETUP_TARGET_FOR_COVERAGE_LCOV(
#     NAME testrunner_coverage                    # New target name
#     EXECUTABLE testrunner -j ${PROCESSOR_COUNT} # Executable in PROJECT_BINARY_DIR
#     DEPENDENCIES testrunner                     # Dependencies to build first
# )
function(SETUP_TARGET_FOR_COVERAGE_LCOV)

    set(options NONE)
    set(oneValueArgs NAME)
    set(multiValueArgs EXECUTABLE EXECUTABLE_ARGS DEPENDENCIES)
    cmake_parse_arguments(Coverage "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT LCOV_PATH)
        message(FATAL_ERROR "lcov not found! Aborting...")
    endif () # NOT LCOV_PATH

    if (NOT GENHTML_PATH)
        message(FATAL_ERROR "genhtml not found! Aborting...")
    endif () # NOT GENHTML_PATH

    # Setup target
    add_custom_target(${Coverage_NAME}

            # Cleanup lcov
            COMMAND ${LCOV_PATH} --directory . --zerocounters
            # Create baseline to make sure untouched files show up in the report
            COMMAND ${LCOV_PATH} -c -i -d . -o ${Coverage_NAME}.base

            # Run tests
            COMMAND ${Coverage_EXECUTABLE}

            # Capturing lcov counters and generating report
            COMMAND ${LCOV_PATH} --directory . --capture --output-file ${Coverage_NAME}.info
            # add baseline counters
            COMMAND ${LCOV_PATH} -a ${Coverage_NAME}.base -a ${Coverage_NAME}.info --output-file ${Coverage_NAME}.total
            COMMAND ${LCOV_PATH} --remove ${Coverage_NAME}.total ${COVERAGE_LCOV_EXCLUDES} --output-file ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info.cleaned
            COMMAND ${GENHTML_PATH} -o ${Coverage_NAME} ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info.cleaned
            COMMAND ${CMAKE_COMMAND} -E remove ${Coverage_NAME}.base ${Coverage_NAME}.total ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info.cleaned

            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
            DEPENDS ${Coverage_DEPENDENCIES}
            COMMENT "Resetting code coverage counters to zero.\nProcessing code coverage counters and generating report."
            )

    # Show where to find the lcov info report
    add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
            COMMAND ;
            COMMENT "Lcov code coverage info report saved in ${Coverage_NAME}.info."
            )

    # Show info where to find the report
    add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
            COMMAND ;
            COMMENT "Open ./${Coverage_NAME}/index.html in your browser to view the coverage report."
            )

endfunction() # SETUP_TARGET_FOR_COVERAGE_LCOV

# Defines a target for running and collection code coverage information
# Builds dependencies, runs the given executable and outputs reports.
# NOTE! The executable should always have a ZERO as exit code otherwise
# the coverage generation will not complete.
#
# SETUP_TARGET_FOR_COVERAGE_GCOVR_XML(
#     NAME ctest_coverage                    # New target name
#     EXECUTABLE ctest -j ${PROCESSOR_COUNT} # Executable in PROJECT_BINARY_DIR
#     DEPENDENCIES executable_target         # Dependencies to build first
# )
function(SETUP_TARGET_FOR_COVERAGE_GCOVR_XML)

    set(options NONE)
    set(oneValueArgs NAME)
    set(multiValueArgs EXECUTABLE EXECUTABLE_ARGS DEPENDENCIES)
    cmake_parse_arguments(Coverage "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT SIMPLE_PYTHON_EXECUTABLE)
        message(FATAL_ERROR "python not found! Aborting...")
    endif () # NOT SIMPLE_PYTHON_EXECUTABLE

    if (NOT GCOVR_PATH)
        message(FATAL_ERROR "gcovr not found! Aborting...")
    endif () # NOT GCOVR_PATH

    # Combine excludes to several -e arguments
    set(GCOVR_EXCLUDES "")
    foreach (EXCLUDE ${COVERAGE_GCOVR_EXCLUDES})
        list(APPEND GCOVR_EXCLUDES "-e")
        list(APPEND GCOVR_EXCLUDES "${EXCLUDE}")
    endforeach ()

    add_custom_target(${Coverage_NAME}
            # Run tests
            ${Coverage_EXECUTABLE}

            # Running gcovr
            COMMAND ${GCOVR_PATH} --xml
            -r ${CMAKE_SOURCE_DIR} ${GCOVR_EXCLUDES}
            --object-directory=${PROJECT_BINARY_DIR}
            -o ${Coverage_NAME}.xml
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
            DEPENDS ${Coverage_DEPENDENCIES}
            COMMENT "Running gcovr to produce Cobertura code coverage report."
            )

    # Show info where to find the report
    add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
            COMMAND ;
            COMMENT "Cobertura code coverage report saved in ${Coverage_NAME}.xml."
            )

endfunction() # SETUP_TARGET_FOR_COVERAGE_GCOVR_XML

# Defines a target for running and collection code coverage information
# Builds dependencies, runs the given executable and outputs reports.
# NOTE! The executable should always have a ZERO as exit code otherwise
# the coverage generation will not complete.
#
# SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML(
#     NAME ctest_coverage                    # New target name
#     EXECUTABLE ctest -j ${PROCESSOR_COUNT} # Executable in PROJECT_BINARY_DIR
#     DEPENDENCIES executable_target         # Dependencies to build first
# )
function(SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML)

    set(options NONE)
    set(oneValueArgs NAME TARGET TESTED_TARGET OUTPUT_PATH)
    set(multiValueArgs EXECUTABLE EXECUTABLE_ARGS DEPENDENCIES)
    cmake_parse_arguments(Coverage "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT SIMPLE_PYTHON_EXECUTABLE)
        message(FATAL_ERROR "python not found! Aborting...")
    endif () # NOT SIMPLE_PYTHON_EXECUTABLE

    if (NOT GCOVR_PATH)
        message(FATAL_ERROR "gcovr not found! Aborting...")
    endif () # NOT GCOVR_PATH

    if (NOT Coverage_TESTED_TARGET)
        message(FATAL_ERROR "Undefined TESTED_TARGET")
    endif ()

    if (Coverage_TARGET)
        get_target_property(TARGET_SOURCE_DIR ${Coverage_TARGET} SOURCE_DIR)
        file(RELATIVE_PATH TARGET_SOURCE_DIR ${PROJECT_BINARY_DIR} ${TARGET_SOURCE_DIR})
        list(APPEND COVERAGE_GCOVR_EXCLUDES ${TARGET_SOURCE_DIR})
    endif ()

    if (NOT Coverage_OUTPUT_PATH)
        if (Coverage_NAME)
            set(Coverage_OUTPUT_PATH "${PROJECT_BINARY_DIR}/coverage/${Coverage_NAME}")
        elseif (Coverage_TARGET)
            set(Coverage_OUTPUT_PATH "${PROJECT_BINARY_DIR}/coverage/${Coverage_TARGET}")
        elseif (Coverage_TESTED_TARGET)
            set(Coverage_OUTPUT_PATH "${PROJECT_BINARY_DIR}/coverage/${Coverage_TESTED_TARGET}")
        else ()
            message(FATAL_ERROR "Cannot guess an output path")
        endif ()
    endif ()

    if (NOT Coverage_NAME)
        if (Coverage_TARGET)
            set(Coverage_NAME ${Coverage_TARGET}_coverage)
        elseif (Coverage_TESTED_TARGET)
            set(Coverage_NAME ${Coverage_TESTED_TARGET}_coverage)
        else ()
            message(FATAL_ERROR "Could not guess a target name")
        endif ()
    endif ()

    target_compile_options(${Coverage_TESTED_TARGET} PUBLIC ${CMAKE_COMPILE_FLAGS_COVERAGE})
    set_property(TARGET ${Coverage_TESTED_TARGET} PROPERTY INTERFACE_LINK_LIBRARIES "${CMAKE_LINKER_FLAGS_COVERAGE}")
    get_target_property(TESTED_TARGET_SOURCE_DIR ${Coverage_TESTED_TARGET} SOURCE_DIR)

    # Combine excludes to several -e arguments
    set(GCOVR_EXCLUDES "")
    foreach (EXCLUDE ${COVERAGE_GCOVR_EXCLUDES})
        list(APPEND GCOVR_EXCLUDES "-e")
        list(APPEND GCOVR_EXCLUDES "${EXCLUDE}")
    endforeach ()

    add_custom_target(${Coverage_NAME}
            # Run tests
            ${Coverage_EXECUTABLE}

            # Create folder
            COMMAND ${CMAKE_COMMAND} -E make_directory ${Coverage_OUTPUT_PATH}
            # Running gcovr
            COMMAND ${GCOVR_PATH} --html --html-details
            --gcov-executable '${COVERAGE_EXECUTABLE}'
            -r ${TESTED_TARGET_SOURCE_DIR} ${GCOVR_EXCLUDES}
            --object-directory=${PROJECT_BINARY_DIR}
            -o ${Coverage_OUTPUT_PATH}/index.html
            -d
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
            DEPENDS ${Coverage_DEPENDENCIES}
            COMMENT "Running gcovr to produce HTML code coverage report."
            )

    # Show info where to find the report
    add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
            COMMAND ;
            COMMENT "Open ${Coverage_OUTPUT_PATH}/index.html in your browser to view the coverage report."
            )


    add_dependencies(all_coverage ${Coverage_NAME})
endfunction() # SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML
