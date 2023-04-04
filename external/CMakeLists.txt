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
fetchcontent_declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG v2.5.1
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE
)

FetchContent_Populate(quill)
list(APPEND CMAKE_MODULE_PATH ${quill_SOURCE_DIR}/quill/cmake)
add_subdirectory(${quill_SOURCE_DIR} ${quill_BINARY_DIR})
IgnoreWarnings(quill)

#------------------------------------------------------------------------------
#  argparse - Argument Parser for Modern C++
fetchcontent_declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
    GIT_TAG v2.9
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE
)

fetchcontent_makeavailable(argparse)
ignorewarnings(argparse)

#------------------------------------------------------------------------------
#  liburing - io_uring library
pkg_check_modules(liburing REQUIRED IMPORTED_TARGET GLOBAL liburing)

#------------------------------------------------------------------------------
#  EngFormat-Cpp - C++ based Engineering Notation Formatter
fetchcontent_declare(
    EngFormat-Cpp
    GIT_REPOSITORY https://github.com/DARMA-tasking/EngFormat-Cpp.git
    GIT_TAG dc0668881f4ca7ddbc907bf568cfcac8d9026d6a
    UPDATE_DISCONNECTED TRUE
    GIT_SHALLOW TRUE
)

fetchcontent_makeavailable(EngFormat-Cpp)
ignorewarnings(EngFormat-Cpp)

#------------------------------------------------------------------------------
#  out_ptr - C++11 implementation of std::out_ptr (p1132)
fetchcontent_declare(
        ztd_out_ptr
        GIT_REPOSITORY https://github.com/soasis/out_ptr.git
        GIT_TAG 0d156ac9ba7574c3ce355b3d5e8db95936e0bb86
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

fetchcontent_makeavailable(ztd_out_ptr)
ignorewarnings(ztd_out_ptr)

add_subdirectory(fuse3)
