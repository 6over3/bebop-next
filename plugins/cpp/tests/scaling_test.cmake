file(MAKE_DIRECTORY "${output_dir}")
set(schema "package scaling;\nmut struct Leaf { value: string; }\n")
set(previous Leaf)
foreach(index RANGE 0 23)
    set(name "Pair${index}")
    string(APPEND schema
        "mut struct ${name} { left: ${previous}; right: ${previous}; }\n")
    set(previous "${name}")
endforeach()
file(WRITE "${output_dir}/shared.bop" "${schema}")
execute_process(
    COMMAND "${compiler}" build "${output_dir}/shared.bop"
        "--plugin=cpp=${plugin}" "--cpp_out=${output_dir}" --quiet
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 10
)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Shared type graph generation failed: ${result}\n${output}${error}")
endif()
if(NOT EXISTS "${output_dir}/shared.bb.hpp")
    message(FATAL_ERROR "Shared type graph header was not generated")
endif()
