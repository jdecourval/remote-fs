include(ConfigureCppProject)

add_library(remotefs
        remotefs/tools/FuseOp.h
        remotefs/metrics/Metrics.h
        remotefs/metrics/impl/Metrics.cpp
        remotefs/inodecache/InodeCache.h
        remotefs/inodecache/impl/InodeCache.cpp
        remotefs/uring/IoUring.h
        remotefs/messages/Messages.h
        remotefs/uring/IoUring.cpp
        )

set_target_properties(remotefs PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(remotefs
        PUBLIC .
        )

target_compile_options(remotefs PUBLIC
        -Wno-pedantic  # Flexible array members
        )

target_link_libraries(remotefs PUBLIC
        PkgConfig::liburing
        )


add_library(remotefs::remotefs ALIAS remotefs)
configure_cpp_project(remotefs)
