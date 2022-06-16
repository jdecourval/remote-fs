include(ConfigureCppProject)
include(FindPkgConfig)

add_executable(remote-fs-client
        main.cpp
        Client.cpp
        Client.h
        FuseCmdlineOptsWrapper.cpp
        FuseCmdlineOptsWrapper.h
        )

pkg_check_modules(fuse REQUIRED IMPORTED_TARGET fuse3)

target_link_libraries(remote-fs-client PUBLIC
        remotefs::remotefs
        zmqpp
        PkgConfig::fuse
        quill::quill
        )

target_compile_definitions(remote-fs-client PRIVATE
        FUSE_USE_VERSION=35
        _FILE_OFFSET_BITS=64
        )

target_include_directories(remote-fs-client PRIVATE ..)

configure_cpp_project(remote-fs-client)
