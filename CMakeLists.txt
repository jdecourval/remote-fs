cmake_minimum_required(VERSION 3.22)

project(remote-fs VERSION 0.1.0)
set(VERSION_TYPE "beta" CACHE STRING "version type" FORCE)

message(STATUS "")
message(STATUS "    == ${PROJECT_NAME} Project configuration ==")
message(STATUS "")

# Make it possible for subprojects to find the cmake scripts
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

add_subdirectory(external)

# Set default install location to dist folder in build dir
# we do not want to install to /usr by default
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/dist" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
endif ()

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(lib)
add_subdirectory(source)
add_subdirectory(benchmarks)
