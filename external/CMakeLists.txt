include(IgnoreWarnings)
include(FetchContent)
find_package(PkgConfig REQUIRED)

##------------------------------------------------------------------------------
## doctest - The fastest feature-rich C++11/14/17/20 single-header testing framework
FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest
        GIT_TAG v2.4.9
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(doctest)
IgnoreWarnings(doctest)

#------------------------------------------------------------------------------
# Quill - Asynchronous Low Latency C++ Logging Library
FetchContent_Declare(
        quill
        GIT_REPOSITORY https://github.com/odygrd/quill.git
        GIT_TAG v2.0.2
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

FetchContent_Populate(quill)
list(APPEND CMAKE_MODULE_PATH ${quill_SOURCE_DIR}/quill/cmake)
add_subdirectory(${quill_SOURCE_DIR} ${quill_BINARY_DIR})
IgnoreWarnings(quill)

#------------------------------------------------------------------------------
#  argparse - Argument Parser for Modern C++
FetchContent_Declare(
        argparse
        GIT_REPOSITORY https://github.com/p-ranav/argparse.git
        GIT_TAG v2.6
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(argparse)
IgnoreWarnings(argparse)

#------------------------------------------------------------------------------
#  liburing - io_uring library
pkg_check_modules(liburing REQUIRED IMPORTED_TARGET GLOBAL liburing)
