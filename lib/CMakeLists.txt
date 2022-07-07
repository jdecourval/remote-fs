include(ConfigureCppProject)

add_library(remotefs
        remotefs/tools/FuseOp.h
        remotefs/tools/MessageWrappers.h
        remotefs/metrics/Metrics.h
        remotefs/metrics/impl/Metrics.cpp
        remotefs/inodecache/InodeCache.h
        remotefs/inodecache/impl/InodeCache.cpp
        )

set_target_properties(remotefs PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(remotefs
        PUBLIC .
        )

add_library(remotefs::remotefs ALIAS remotefs)
configure_cpp_project(remotefs)
