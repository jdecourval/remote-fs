include(IgnoreWarnings)
include(FetchContent)

##------------------------------------------------------------------------------
## doctest - The fastest feature-rich C++11/14/17/20 single-header testing framework
FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest
        GIT_TAG v2.4.8
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(doctest)
IgnoreWarnings(doctest)

#------------------------------------------------------------------------------
# libzmq - ZeroMQ core engine in C++, implements ZMTP/3.1
FetchContent_Declare(
        libzmq
        GIT_REPOSITORY https://github.com/zeromq/libzmq.git
        GIT_TAG v4.3.4
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

set(ZMQ_BUILD_TESTS OFF CACHE BOOL "")
FetchContent_MakeAvailable(libzmq)
IgnoreWarnings(libzmq)

#------------------------------------------------------------------------------
# zmqpp - 0mq 'highlevel' C++ bindings
FetchContent_Declare(
        zmqpp
        GIT_REPOSITORY https://github.com/zeromq/zmqpp.git
        GIT_TAG ba4230d5d03d29ced9ca788e3bd1095477db08ae
        UPDATE_DISCONNECTED TRUE
        GIT_SHALLOW TRUE
)

set(ZMQPP_LIBZMQ_CMAKE ON CACHE BOOL "")
FetchContent_MakeAvailable(zmqpp)
target_include_directories(zmqpp PUBLIC ${zmqpp_SOURCE_DIR}/src)
IgnoreWarnings(zmqpp)
