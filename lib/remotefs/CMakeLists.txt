include(ConfigureCppProject)

add_library(remotefs
        remotefs/FuseOp.h
        )

set_target_properties(remotefs PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(remotefs
        PUBLIC .
        )

add_library(remotefs::remotefs ALIAS remotefs)
configure_cpp_project(remotefs)
