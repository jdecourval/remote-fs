include(ConfigureCppProject)

add_library(mylib
        mylib/lib.h
        )

set_target_properties(mylib PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(mylib
        PUBLIC .
        )

add_library(mylib::mylib ALIAS mylib)
configure_cpp_project(mylib)
