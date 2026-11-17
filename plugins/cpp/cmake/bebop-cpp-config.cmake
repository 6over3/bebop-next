if(NOT TARGET bebop::cpp)
    include("${CMAKE_CURRENT_LIST_DIR}/BebopCppTargets.cmake")
endif()

if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    block()
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
        get_target_property(cpp_target bebop::cpp ALIASED_TARGET)
        if(NOT cpp_target)
            set(cpp_target bebop::cpp)
        endif()
        target_link_libraries(${cpp_target} INTERFACE "$<BUILD_INTERFACE:${builtins}>")
    endblock()
endif()
