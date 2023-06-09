include(ConfigureCppProject)
include(FindPkgConfig)

add_library(remotefs
    remotefs/tools/FuseOp.h
    remotefs/metrics/Metrics.h
    remotefs/metrics/impl/MetricsEnabled.cpp
    remotefs/metrics/impl/MetricsDisabled.cpp
    remotefs/inodecache/InodeCache.h
    remotefs/inodecache/impl/InodeCache.cpp
    remotefs/uring/IoUring.h
    remotefs/uring/IoUring.cpp
    remotefs/messages/Messages.h
    remotefs/sockets/Socket.cpp
    remotefs/sockets/Socket.h
    remotefs/tools/Bytes.h
    remotefs/tools/Casts.h
    remotefs/uring/RegisteredBufferCache.h
    remotefs/uring/Callbacks.h
    remotefs/uring/CallbacksImpl.h
    )

set_target_properties(remotefs PROPERTIES LINKER_LANGUAGE CXX)

pkg_check_modules(fuse REQUIRED IMPORTED_TARGET fuse3)

target_include_directories(remotefs
    PUBLIC .
    )

target_compile_options(remotefs PUBLIC
    -Wno-pedantic  # Flexible array members
    -Wno-gnu-variable-sized-type-not-at-end  # False positive because the struct containing the FAM is actually properly sized thanks to a manual pad.
    )

target_link_libraries(remotefs PUBLIC
    PkgConfig::liburing
    PkgConfig::fuse
    ztd::out_ptr
    )

target_compile_definitions(remotefs PUBLIC
    FUSE_USE_VERSION=35
    _FILE_OFFSET_BITS=64
    )

add_library(remotefs::remotefs ALIAS remotefs)
configure_cpp_project(remotefs)
