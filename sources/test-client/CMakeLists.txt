include(ConfigureCppProject)
include(FindPkgConfig)

add_executable(test-client
    Main.cpp
    TestClient.cpp
    TestClient.h
    )

target_link_libraries(test-client PUBLIC
    remotefs::remotefs
    quill::quill
    argparse::argparse
    EngFormat-Cpp
    )

target_include_directories(test-client PRIVATE ..)

configure_cpp_project(test-client)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Disabling verbose logging for target test-client")
    target_compile_definitions(test-client PRIVATE QUILL_ACTIVE_LOG_LEVEL=QUILL_LOG_LEVEL_INFO)
endif()
