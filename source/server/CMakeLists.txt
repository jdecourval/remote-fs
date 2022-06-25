include(ConfigureCppProject)
include(FindPkgConfig)

add_executable(remote-fs-server
        main.cpp
        Server.cpp
        Server.h
        Syscalls.cpp
        Syscalls.h
        MessageReceiver.h
        InodeFinder.h
        InodeFinder.cpp
        )

pkg_check_modules(fuse REQUIRED IMPORTED_TARGET fuse3)

target_link_libraries(remote-fs-server PUBLIC
        remotefs::remotefs
        zmqpp
        PkgConfig::fuse
        quill::quill
        )

target_compile_definitions(remote-fs-server PRIVATE
        FUSE_USE_VERSION=35
        _FILE_OFFSET_BITS=64
        )

target_include_directories(remote-fs-server PRIVATE ..)

configure_cpp_project(remote-fs-server)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Disabling verbose logging for target remote-fs-server")
    target_compile_definitions(remote-fs-server PRIVATE QUILL_ACTIVE_LOG_LEVEL=QUILL_LOG_LEVEL_INFO REMOTEFS_DISABLE_METRICS)
endif ()
