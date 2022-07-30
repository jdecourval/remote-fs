include(ConfigureCppProject)
include(FindPkgConfig)

add_executable(remote-fs-test-client
        Main.cpp
        )

target_link_libraries(remote-fs-client PUBLIC
        remotefs::remotefs
        quill::quill
        )

target_compile_definitions(remote-fs-client PRIVATE
        FUSE_USE_VERSION=35
        _FILE_OFFSET_BITS=64
        )

target_include_directories(remote-fs-client PRIVATE ..)

configure_cpp_project(remote-fs-client)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Disabling verbose logging for target remote-fs-client")
    target_compile_definitions(remote-fs-client PRIVATE QUILL_ACTIVE_LOG_LEVEL=QUILL_LOG_LEVEL_INFO)
endif ()
