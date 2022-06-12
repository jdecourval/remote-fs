find_program(
        CLANG_TIDY_EXE
        NAMES "clang-tidy"
        DOC "Path to clang-tidy executable"
)

if (CLANG_TIDY_EXE)
    set(TIDY_AVAILABLE True CACHE INTERNAL "")
endif ()

function(add_clang_tidy TARGET)
    if (CLANG_TIDY_EXE)
        set(DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-extra-arg=-Wno-unknown-warning-option")
        get_target_property(TidyIgnoredWarnings ${TARGET} TidyIgnoredWarnings)
        string(REPLACE ";" "," TidyIgnoredWarnings "${TidyIgnoredWarnings}")
        if (TidyIgnoredWarnings)
            set(DO_CLANG_TIDY "${DO_CLANG_TIDY}" "-checks=${TidyIgnoredWarnings}")
        endif ()
        set_target_properties(${TARGET} PROPERTIES CXX_CLANG_TIDY "${DO_CLANG_TIDY}")
    else ()
        message(FATAL_ERROR "clang-tidy not found.")
    endif ()
endfunction()

function(ignore_clang_tidy TARGET WARNING)
    if (CLANG_TIDY_EXE)
        set_property(TARGET ${TARGET} APPEND PROPERTY TidyIgnoredWarnings -${WARNING})

        get_target_property(CXX_CLANG_TIDY ${TARGET} CXX_CLANG_TIDY)
        if (CXX_CLANG_TIDY)
            # Reapply clang-tidy with the new flags
            add_clang_tidy(${TARGET})
        endif ()
    endif ()
endfunction()
