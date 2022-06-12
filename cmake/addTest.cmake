enable_testing()

include(CodeCoverage)
include(clangTidy)

# Get core count
include(ProcessorCount)
ProcessorCount(CORE_COUNT)

# Setup Catch2
if (Catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/contrib)
    include(Catch)
endif ()

if (doctest_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
    include(doctest)
endif ()


# Setup Valgrind
find_program(MEMORYCHECK_COMMAND valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS "--trace-children=yes" "--leak-check=full")
set(memcheck_command "${MEMORYCHECK_COMMAND}" "${MEMORYCHECK_COMMAND_OPTIONS}")

# Create targets to run all tests
if (NOT TARGET all_test)
    option(TEST_ON_ALL_TARGET "Execute unit tests on all target" ON)
    if (TEST_ON_ALL_TARGET)
        set(TEST_ON_ALL_TARGET_ENABLED ALL)
    endif ()
    add_custom_target(all_test ${TEST_ON_ALL_TARGET_ENABLED}
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j ${CORE_COUNT} -E '_\(valgrind|san_.*\)'
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            )
    if (MEMORYCHECK_COMMAND)
        add_custom_target(all_test_valgrind
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j ${CORE_COUNT} -R "_valgrind$"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                )
    endif ()
endif ()

# Add a custom test for TARGET named NAME that runs COMMAND
function(addCustomTest NAME TARGET COMMAND)
    add_test(NAME ${NAME}
            COMMAND ${COMMAND}
            )

    add_dependencies(all_test ${TARGET})

    if (MEMORYCHECK_COMMAND)
        add_test(NAME ${NAME}_valgrind
                COMMAND ${memcheck_command} ${COMMAND})

        add_dependencies(all_test_valgrind ${TARGET})
    endif ()
endfunction()

function(addCatchTests NAME SOURCE)
    set(SOURCES ${SOURCE} ${ARGN})
    if (TARGET ${NAME})
        message(FATAL_ERROR "There already is a target named ${NAME}")
    endif ()

    add_executable(${NAME} EXCLUDE_FROM_ALL
            ${SOURCES}
            )

    target_compile_definitions(${NAME}
            PRIVATE UNIT_TESTS
            )

    target_link_libraries(${NAME}
            PRIVATE Catch2
            )

    # Cause test to get deleted on failure
    #    add_custom_command(TARGET ${NAME}
    #            POST_BUILD
    #            COMMAND ${NAME}
    #            COMMENT "Run tests"
    #            )

    # Have to run ctest two times to detect tests
    catch_discover_tests(${NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )

    add_dependencies(all_test ${NAME})
endfunction()

function(addDoctestTests NAME SOURCE)
    set(SOURCES ${SOURCE} ${ARGN})
    if (TARGET ${NAME})
        message(FATAL_ERROR "There already is a target named ${NAME}")
    endif ()

    add_executable(${NAME} EXCLUDE_FROM_ALL
            ${SOURCES}
            )

    target_compile_definitions(${NAME}
            PRIVATE UNIT_TESTS
            )

    target_link_libraries(${NAME}
            PRIVATE doctest
            )

    # Cause test to get deleted on failure
    #    add_custom_command(TARGET ${NAME}
    #            POST_BUILD
    #            COMMAND ${NAME}
    #            COMMENT "Run tests"
    #            )

    # Have to run ctest two times to detect tests
    doctest_discover_tests(${NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            )

    add_dependencies(all_test ${NAME})
endfunction()

# Add Catch2 tests named NAME, testing the library TARGET and having sources SOURCE + ARGN
function(addLibCatchTests NAME TARGET SOURCE)
    if (NOT Catch2_SOURCE_DIR)
        message(FATAL_ERROR "Catch not found")
    endif ()

    addCatchTests(${NAME} ${SOURCE} ${ARGN})

    target_link_libraries(${NAME} PRIVATE ${TARGET})

    if (COVERAGE_EXECUTABLE)
        SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML(
                TARGET ${NAME}
                TESTED_TARGET ${TARGET}
                EXECUTABLE ${NAME}                       # Executable in PROJECT_BINARY_DIR
                DEPENDENCIES ${NAME}                     # Dependencies to build first
        )
    endif ()
endfunction()

function(addLibDoctestTests NAME TARGET SOURCE)
    if (NOT doctest_SOURCE_DIR)
        message(FATAL_ERROR "doctest not found")
    endif ()

    addDoctestTests(${NAME} ${SOURCE} ${ARGN})

    target_link_libraries(${NAME} PRIVATE ${TARGET})

    if (COVERAGE_EXECUTABLE)
        SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML(
                TARGET ${NAME}
                TESTED_TARGET ${TARGET}
                EXECUTABLE ${NAME}                       # Executable in PROJECT_BINARY_DIR
                DEPENDENCIES ${NAME}                     # Dependencies to build first
        )
    endif ()
endfunction()


# Add a compile time test named NAME having sources SOURCE + ARGN
function(addCompileTest NAME SOURCE)
    set(SOURCES ${SOURCE} ${ARGN})

    add_library(${NAME} EXCLUDE_FROM_ALL
            ${SOURCES}
            )

    target_compile_definitions(${NAME}
            PRIVATE UNIT_TESTS
            )

    add_test(NAME ${NAME}
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${NAME}
            )
endfunction()
