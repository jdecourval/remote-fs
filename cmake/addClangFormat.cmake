# From https://www.linkedin.com/pulse/simple-elegant-wrong-how-integrate-clang-format-friends-brendan-drew/

find_package(clangformat)

function(add_clangformat _targetname)
    if (CLANG_FORMAT_BIN)
        if (NOT TARGET ${_targetname})
            message(FATAL_ERROR "add_clangformat should only be called on targets (got " ${_targetname} ")")
        endif ()

        # figure out which sources this should be applied to
        get_target_property(_clang_sources ${_targetname} SOURCES)
        get_target_property(_builddir ${_targetname} BINARY_DIR)


        set(_sources "")
        foreach (_source ${_clang_sources})
            if (NOT TARGET ${_source})
                get_filename_component(_source_file ${_source} NAME)
                get_source_file_property(_clang_loc "${_source}" LOCATION)

                set(_format_file ${_targetname}_${_source_file}.format)

                add_custom_command(OUTPUT ${_format_file}
                        DEPENDS ${_source}
                        COMMENT "Clang-Format ${_source}"
                        COMMAND ${CLANG_FORMAT_BIN} -style=file -fallback-style=WebKit -sort-includes -i ${_clang_loc}
                        COMMAND ${CMAKE_COMMAND} -E touch ${_format_file})

                list(APPEND _sources ${_format_file})
            endif ()
        endforeach ()

        if (_sources)
            add_custom_target(${_targetname}_clangformat
                    SOURCES ${_sources}
                    COMMENT "Clang-Format for target ${_target}")
        endif ()
    else ()
        message(FATAL_ERROR "Clang-format is not installed")
    endif ()
endfunction()
