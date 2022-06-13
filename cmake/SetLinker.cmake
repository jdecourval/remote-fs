include(CheckCXXCompilerFlag)
include(ProcessorCount)

ProcessorCount(CORES)

execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=lld -Wl,--version ERROR_QUIET OUTPUT_VARIABLE lld_version)
execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE gold_version)
execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=bfd -Wl,--version ERROR_QUIET OUTPUT_VARIABLE bfd_version)
execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=mold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE mold_version)

if ("${gold_version}" MATCHES "GNU gold")
    set(GOLD_AVAILABLE gold CACHE INTERNAL "")

    if (NOT CORES EQUAL 0)
        check_cxx_compiler_flag(-Wl,--threads linker_threads)
        check_cxx_compiler_flag(-Wl,--thread-count,${CORES} linker_threads_count)
        if (linker_threads)
            if (linker_threads_count)
                set(gold_flags "-Wl,--threads -Wl,--thread-count,${CORES}" CACHE INTERNAL "")
            else ()
                set(gold_flags "-Wl,--threads" CACHE INTERNAL "")
            endif ()
        endif ()
    endif ()
endif ()

if ("${lld_version}" MATCHES "LLD")
    set(LLD_AVAILABLE lld CACHE INTERNAL "")

    # Recent lld use multithreading by default
endif ()

if ("${bfd_version}" MATCHES "GNU ld")
    set(BFD_AVAILABLE bfd CACHE INTERNAL "")
endif ()

if ("${mold_version}" MATCHES "mold")
    set(MOLD_AVAILABLE mold CACHE INTERNAL "")
endif ()

function(set_linker TARGET LINKER)
    if (NOT LINKER STREQUAL "default")
        target_link_libraries(${TARGET} PRIVATE "-fuse-ld=${LINKER}")

        if (${LINKER}_flags)
            target_link_libraries(${TARGET} PRIVATE ${${LINKER}_flags})
        endif ()
    endif ()
endfunction()
