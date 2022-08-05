include(ConfigureCppProject)
include(FindPkgConfig)

add_executable(remote-fs-client
        Main.cpp
        Client.cpp
        Client.h
        FuseCmdlineOptsWrapper.cpp
        FuseCmdlineOptsWrapper.h
        )

pkg_check_modules(fuse REQUIRED IMPORTED_TARGET fuse3)

target_link_libraries(remote-fs-client PUBLIC
        remotefs::remotefs
        PkgConfig::fuse
        quill::quill
        fuse-kernel
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
