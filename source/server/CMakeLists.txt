include(ConfigureCppProject)
include(FindPkgConfig)

add_executable(remote-fs-server
        main.cpp
        server.cpp
        server.h
        )

pkg_check_modules(fuse REQUIRED IMPORTED_TARGET fuse3)

target_link_libraries(remote-fs-server PUBLIC
        mylib
        zmqpp
        PkgConfig::fuse
        )

target_compile_definitions(remote-fs-server PRIVATE
        FUSE_USE_VERSION=35
        _FILE_OFFSET_BITS=64
        )

target_include_directories(remote-fs-server PRIVATE ..)

configure_cpp_project(remote-fs-server)
