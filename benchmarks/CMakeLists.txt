find_package(Git)

execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --abbrev=40 --dirty --abbrev
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(OUTPUT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/results/${GIT_HASH}/${CMAKE_CXX_COMPILER_ID}-${CMAKE_BUILD_TYPE}")
file(MAKE_DIRECTORY "${OUTPUT_FOLDER}")
file(GLOB SCENARIOS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/scenarios/" scenarios/*)
set_property(GLOBAL PROPERTY JOB_POOLS serial_pool=1)
add_custom_target(benchmark-all)

foreach (SCENARIO ${SCENARIOS})
    add_custom_target(benchmark-${SCENARIO}
            COMMAND ./bench.sh ${SCENARIO} $<TARGET_FILE:remote-fs-server> $<TARGET_FILE:remote-fs-client> 2> "${OUTPUT_FOLDER}/${SCENARIO}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS remote-fs-server remote-fs-client
            JOB_POOL serial_pool
            VERBATIM
            )
    add_dependencies(benchmark-all benchmark-${SCENARIO})
endforeach ()
