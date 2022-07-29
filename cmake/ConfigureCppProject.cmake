include(clangTidy)
include(CheckIPOSupported)
include(SetLinker)

find_program(IWYU_PATH NAMES include-what-you-use iwyu)

find_program(CCACHE_PROGRAM ccache)


# Store our own path since CMAKE_CURRENT_LIST_DIR will refer to the caller path inside functions
set(CONFIGCPP_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "")


if (NOT FIRST_TIME_SETUP_EXECUTED)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(DEBUG TRUE)
        set(RELEASE FALSE)
    else ()
        set(DEBUG FALSE)
        set(RELEASE TRUE)
    endif ()

    check_ipo_supported(RESULT result)
    if (result)
        set(LTO_AVAILABLE True CACHE INTERNAL "")
    endif ()

    if (CCACHE_PROGRAM)
        set(CCACHE_AVAILABLE True CACHE INTERNAL "")
    endif ()

    if (IWYU_PATH)
        set(IWYU_AVAILABLE True CACHE INTERNAL "")
    endif ()


    option(DEFAULT_LTO "Default LTO behaviour" ${RELEASE})
    option(DEFAULT_CCACHE "Default ccache behaviour" ${CCACHE_AVAILABLE})
    option(DEFAULT_IWYU "Default IWYU behaviour" ${IWYU_AVAILABLE})
    option(DEFAULT_TIDY "Default clang-tidy behaviour" OFF)
    option(DEFAULT_PERFORMANCE "Default performance flags behaviour" ON)
    option(DEFAULT_SECURITY "Default security flags behaviour" ON)
    option(DEFAULT_WARNINGS "Default warnings behaviour" ON)
    option(DEFAULT_WERROR "Default warnings as error behaviour" OFF)
    option(DEFAULT_UBSAN_AND_ASAN "Default undefined behaviour and address sanitizer behaviour" ${DEBUG})
    option(DEFAULT_TSAN "Default thread sanitizer behaviour" OFF)
    option(DEFAULT_VALGRIND "Default valgrind target generation" ${VALGRIND_AVAILABLE})
    option(DEFAULT_COLOR "Always produce ANSI-colored output (GNU/Clang only)." ON)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND ${DEFAULT_LTO})
        # It appears that GCC's LTO can have problems with some linkers.
        set(DEFAULT_LINKER default CACHE STRING "Default linker")
    elseif (${DEBUG} AND (${MOLD_AVAILABLE} MATCHES mold))
        set(DEFAULT_LINKER mold CACHE STRING "Default linker")
    elseif (LLD_AVAILABLE)
        set(DEFAULT_LINKER lld CACHE STRING "Default linker")
    elseif (GOLD_AVAILABLE)
        set(DEFAULT_LINKER gold CACHE STRING "Default linker")
    else ()
        set(DEFAULT_LINKER default CACHE STRING "Default linker")
    endif ()

    set_property(CACHE DEFAULT_LINKER PROPERTY STRINGS ${MOLD_AVAILABLE} ${LLD_AVAILABLE} ${GOLD_AVAILABLE} ${BFD_AVAILABLE} default)

    # Be nice to visual studio
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Be nice and export compile commands by default, this is handy for clang-tidy
    # and for other tools.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    # Helpful option enable build profiling to identify slowly compiling files
    option(MEASURE_ALL "When enabled all commands will be passed through time command" OFF)
    if (MEASURE_ALL)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "time")
    endif ()

    # Disable CDash integration by default
    set(BUILD_TESTING OFF CACHE BOOL "Enable CDash targets")
    set(FIRST_TIME_SETUP_EXECUTED True CACHE INTERNAL "Whether its the first time this function is called")
endif ()

function(add_performance_flags TARGET)
    target_compile_options(${TARGET} PRIVATE
            -fno-plt
            -fno-semantic-interposition
            $<IF:$<CONFIG:Debug>,-Og,-O3>
            )

    target_link_options(${TARGET} PRIVATE
            "LINKER:SHELL:-O1"
            )
endfunction()

function(add_ubsan_and_asan TARGET)
    target_compile_options(${TARGET} PRIVATE
            -fsanitize=address,undefined
            -fno-optimize-sibling-calls
            -fno-omit-frame-pointer
            -fno-common
            -fsanitize-address-use-after-scope
            -U_FORTIFY_SOURCE
            )

    target_link_options(${TARGET} PRIVATE
            -fsanitize=address,undefined
            -fno-optimize-sibling-calls
            -fno-omit-frame-pointer
            -fno-common
            -fsanitize-address-use-after-scope
            -U_FORTIFY_SOURCE
            )
endfunction()

function(add_tsan TARGET)
    target_compile_options(${TARGET} PRIVATE
            -fsanitize=thread
            -fno-optimize-sibling-calls
            -fno-omit-frame-pointer
            -fno-common
            )

    target_link_options(${TARGET} PRIVATE
            -fsanitize=thread
            -fno-optimize-sibling-calls
            -fno-omit-frame-pointer
            -fno-common
            )
endfunction()

function(add_lto TARGET)
    check_ipo_supported(RESULT result)
    if (result)
        set_property(TARGET ${TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else ()
        message(FATAL_ERROR "LTO is not supported on this platform")
    endif ()
endfunction()

function(add_warnings TARGET)
    if (MSVC)
        target_compile_options(${TARGET} PRIVATE /W4)
    else ()
        target_compile_options(${TARGET} PRIVATE
                "-pipe"                     # Use pipe instead of temporary files. This slightly speed up compilation.

                "-Wall"                     # Activate most warnings.
                "-Wextra"                   # Activate more warnings.
                "-Wshadow"                  # Warn when a name shadows another name
                "-Wnon-virtual-dtor"        # Warn whenever a class with virtual function does not declare a virtual destructor
                "-Wnull-dereference"        # Warn of undefined behaviors due to dereferencing a null pointer.
                "-Wmissing-include-dirs"    # Warn about missing user-supplied include directories.
                "-Wuninitialized"           # Warn if an automatic variable is used without first being initialized. Also warn if a non-static reference or non-static const member appears in a class without constructors.
                "-Wstrict-overflow"         # Warn about cases where the compiler optimizes based on the assumption that signed overflow does not occur.
                "-Wundef"                   # Warn if an undefined identifier is evaluated in an #if directive.
                "-Wcast-align"              # Warn whenever a pointer is cast such that the required alignment of the target is increased.
                "-Wredundant-decls"         # Warn if anything is declared more than once in the same scope.
                "-pedantic"                 # Issue all the warnings demanded by strict ISO C and ISO C++
                "-Wsuggest-override"        # Warn about overriding virtual functions that are not marked with the override keyword.
                #            "-Wconversion"             # Activate conversion warnings. This yield a large number of false positive.

                "-Wno-unknown-pragmas"      # Make GCC not scream about CLion/clang pragmas

                "-Werror=return-type"       # This warning is bad enough to warrant an error
                )

        if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
            target_compile_options(${TARGET} PRIVATE
                    "-Wsuggest-final-types"     # Warn where code quality would be improved by making a class final.
                    "-Wsuggest-final-methods"   # Warn where code quality would be improved by making a method final.
                    "-Wuseless-cast"            # Warn when an expression is casted to its own type.
                    )
        endif ()
    endif ()
endfunction()

function(add_ccache TARGET)
    # From https://crascit.com/2016/04/09/using-ccache-with-cmake/
    if (CCACHE_PROGRAM)
        # Set up wrapper scripts
        set(C_LAUNCHER "${CCACHE_PROGRAM}")
        set(CXX_LAUNCHER "${CCACHE_PROGRAM}")

        configure_file(${CONFIGCPP_DIR}/launch-c.in launch-c)
        configure_file(${CONFIGCPP_DIR}/launch-cxx.in launch-cxx)
        execute_process(COMMAND chmod a+rx
                "${CMAKE_CURRENT_BINARY_DIR}/launch-c"
                "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx"
                )

        if (CMAKE_GENERATOR STREQUAL "Xcode")
            # Set Xcode project attributes to route compilation and linking
            # through our scripts
            set_property(TARGET ${TARGET} PROPERTY XCODE_ATTRIBUTE_CC "${CMAKE_CURRENT_BINARY_DIR}/launch-c")
            set_property(TARGET ${TARGET} PROPERTY XCODE_ATTRIBUTE_CXX "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx")
            set_property(TARGET ${TARGET} PROPERTY XCODE_ATTRIBUTE_LD "${CMAKE_CURRENT_BINARY_DIR}/launch-c")
            set_property(TARGET ${TARGET} PROPERTY XCODE_ATTRIBUTE_LDPLUSPLUS "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx")
        else ()
            # Support Unix Makefiles and Ninja
            set_property(TARGET ${TARGET} PROPERTY C_COMPILER_LAUNCHER "${CMAKE_CURRENT_BINARY_DIR}/launch-c")
            set_property(TARGET ${TARGET} PROPERTY CXX_COMPILER_LAUNCHER "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx")
        endif ()
    else ()
        message(FATAL_ERROR "Could not find the program ccache")
    endif ()
endfunction()

function(add_iwyu TARGET)
    if (IWYU_PATH)
        set_property(TARGET ${TARGET} PROPERTY C_INCLUDE_WHAT_YOU_USE ${IWYU_PATH} "-Xiwyu" "--no_comments" "-Wno-unknown-warning-option")
        set_property(TARGET ${TARGET} PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${IWYU_PATH} "-Xiwyu" "--no_comments" "-Wno-unknown-warning-option")
    else ()
        message(FATAL_ERROR "Could not find the program include-what-you-use")
    endif ()
endfunction()

function(add_warnings_as_errors TARGET)
    if (MSVC)
        target_compile_options(${TARGET} PRIVATE "/WX")
    else ()
        target_compile_options(${TARGET} PRIVATE "-Werror")
    endif ()
endfunction()

function(add_security_flags TARGET)
    if (MSVC)
    else ()
        target_compile_options(${TARGET} PRIVATE "-fstack-protector-strong")
        set_property(TARGET ${TARGET} PROPERTY POSITION_INDEPENDENT_CODE ON)

        if (NOT CMAKE_BUILD_TYPE MATCHES Debug)
            # Only works jointly with optimizations
            target_compile_definitions(${TARGET} PRIVATE "_FORTIFY_SOURCE=2")
        endif ()

        if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
            # Currently unavailable on Clang
            target_compile_options(${TARGET} PRIVATE -fstack-clash-protection)
        endif ()

        target_compile_definitions(${TARGET} PRIVATE "_GLIBCXX_ASSERTIONS=1")
        target_link_options(${TARGET} PRIVATE
                "LINKER:SHELL:-z noexecstack"
                "LINKER:SHELL:-z relro"
                "LINKER:SHELL:-z now"
                )
    endif ()
endfunction()

function(add_color TARGET)
    if ("${${TARGET}_LINKER}" STREQUAL "mold" OR "${${TARGET}_LINKER}" STREQUAL "lld")
        target_link_options(${TARGET} PRIVATE "LINKER:SHELL:--color-diagnostics=always")
    endif ()
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        target_compile_options(${TARGET} PRIVATE -fdiagnostics-color=always)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        target_compile_options(${TARGET} PRIVATE -fcolor-diagnostics)
    endif ()
endfunction()

macro(configure_cpp_project_helper TARGET)
    if (NOT ${TARGET}_LINKER STREQUAL "default")
        set_linker(${TARGET} ${${TARGET}_LINKER})
        message(STATUS "Linker set to ${${TARGET}_LINKER} for ${TARGET}")
    endif ()

    if (${TARGET}_LTO)
        add_lto(${TARGET})
        message(STATUS "LTO activated for ${TARGET}")
    endif ()

    if (${TARGET}_CCACHE)
        add_ccache(${TARGET})
        message(STATUS "Ccache activated for ${TARGET}")
    endif ()

    if (${TARGET}_IWYU)
        add_iwyu(${TARGET})
        message(STATUS "Include What You Use activated for ${TARGET}")
    endif ()

    if (${TARGET}_TIDY)
        add_clang_tidy(${TARGET})
        message(STATUS "Clang-tidy activated for ${TARGET}")
    endif ()

    if (${TARGET}_WARNINGS)
        add_warnings(${TARGET})
        message(STATUS "Extensive warnings activated for ${TARGET}")
    endif ()

    if (${TARGET}_SECURITY)
        add_security_flags(${TARGET})
        message(STATUS "Security flags activated for ${TARGET}")
    endif ()

    if (${TARGET}_WERROR)
        add_warnings_as_errors(${TARGET})
        message(STATUS "Warnings considered as errors for ${TARGET}")
    endif ()

    if (${TARGET}_UBSAN_AND_ASAN)
        add_ubsan_and_asan(${TARGET})
        message(STATUS "Undefined behaviours and address sanitizers activated for ${TARGET}")
    endif ()

    if (${TARGET}_TSAN)
        add_tsan(${TARGET})
        message(STATUS "Thread sanitizer activated for ${TARGET}")
    endif ()

    if (${TARGET}_PERFORMANCE)
        add_performance_flags(${TARGET})
        message(STATUS "Performance flags added to ${TARGET}")
    endif ()

    if (${TARGET}_COLOR)
        add_color(${TARGET})
        message(STATUS "Forcing color while compiling ${TARGET}")
    endif ()
endmacro()

function(configure_cpp_project TARGET)
    option(${TARGET}_LTO "Override default LTO behaviour (${DEFAULT_LTO}) for project ${TARGET}" ${DEFAULT_LTO})
    option(${TARGET}_CCACHE "Override default ccache behaviour (${DEFAULT_CCACHE}) for project ${TARGET}" ${DEFAULT_CCACHE})
    option(${TARGET}_IWYU "Override default IWYU behaviour (${DEFAULT_IWYU}) for project ${TARGET}" ${DEFAULT_IWYU})
    option(${TARGET}_TIDY "Override default clang-tidy behaviour (${DEFAULT_TIDY}) for project ${TARGET}" ${DEFAULT_TIDY})
    option(${TARGET}_WARNINGS "Override default warnings behaviour (${DEFAULT_WARNINGS}) for project ${TARGET}" ${DEFAULT_WARNINGS})
    option(${TARGET}_SECURITY "Override security flags behaviour (${DEFAULT_SECURITY}) for project ${TARGET}" ${DEFAULT_SECURITY})
    option(${TARGET}_WERROR "Override default warnings as error behaviour (${DEFAULT_WERROR}) for project ${TARGET}" ${DEFAULT_WERROR})
    option(${TARGET}_PERFORMANCE "Override default performance flags behaviour (${DEFAULT_PERFORMANCE}) for project ${TARGET}" ${DEFAULT_PERFORMANCE})
    option(${TARGET}_UBSAN_AND_ASAN "Override default undefined behaviours and address sanitizers behaviour (${DEFAULT_UBSAN_AND_ASAN}) for project ${TARGET}" ${DEFAULT_UBSAN_AND_ASAN})
    option(${TARGET}_TSAN "Override default thread sanitizer behaviour (${DEFAULT_TSAN}) for project ${TARGET}" ${DEFAULT_TSAN})
    option(${TARGET}_COLOR "Override always produce ANSI-colored output (GNU/Clang only) (${DEFAULT_COLOR}) for project ${TARGET}" ${DEFAULT_COLOR})

    set(${TARGET}_LINKER ${DEFAULT_LINKER} CACHE STRING "Override default linker ${DEFAULT_LINKER} for project ${TARGET}")
    set_property(CACHE ${TARGET}_LINKER PROPERTY STRINGS ${LLD_AVAILABLE} ${GOLD_AVAILABLE} ${BFD_AVAILABLE} default)

    configure_cpp_project_helper(${TARGET})
endfunction()
