include_guard(GLOBAL)

function(bebop_find_clang_builtins output)
    set(${output} "" PARENT_SCOPE)
    if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        return()
    endif()
    separate_arguments(compiler_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
    if(CMAKE_CXX_COMPILER_TARGET)
        list(APPEND compiler_flags "--target=${CMAKE_CXX_COMPILER_TARGET}")
    endif()
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" ${compiler_flags}
            --print-libgcc-file-name /clang:--rtlib=compiler-rt
        OUTPUT_VARIABLE builtins
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(NOT EXISTS "${builtins}")
        message(FATAL_ERROR "Clang compiler-rt builtins library not found: ${builtins}")
    endif()
    set(${output} "${builtins}" PARENT_SCOPE)
endfunction()
