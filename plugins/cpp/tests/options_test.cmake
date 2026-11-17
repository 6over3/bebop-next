function(expect_error option message)
    execute_process(
        COMMAND "${compiler}" build "${schema}"
                -I "${standard_schemas}"
                "--plugin=cpp=${plugin}"
                "--cpp_out=${output_dir}/invalid"
                -D "${option}" --quiet
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "Option '${option}' unexpectedly succeeded")
    endif()
    string(FIND "${output}${error}" "${message}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Option '${option}' gave an unexpected error: ${output}${error}")
    endif()
endfunction()

expect_error("cpp_standard=20" "requires C++23")
expect_error("emit_ref=invalid" "requires true or false")
expect_error("emit_view=invalid" "requires true or false")
expect_error("emit_pmr=invalid" "requires true or false")
expect_error("emit_reflection=invalid" "requires true or false")
expect_error("services=client" "services must be none")
expect_error("not_a_cpp_option=true" "Unknown C++ generator option")
expect_error("namespace=invalid..namespace" "identifier is reserved")
expect_error("prefix=1invalid" "Invalid C++ identifier")

function(expect_schema_error name source message)
    set(path "${output_dir}/${name}.bop")
    file(WRITE "${path}" "${source}")
    execute_process(
        COMMAND "${compiler}" build "${path}"
                -I "${standard_schemas}"
                "--plugin=cpp=${plugin}"
                "--cpp_out=${output_dir}/invalid"
                -D emit_pmr=true --quiet
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${message}")
        message(FATAL_ERROR "${name} gave an unexpected result: ${output}${error}")
    endif()
endfunction()

expect_schema_error(private_storage "struct Example { fields_: uint32; }"
    "C.. field name conflicts with a generated member")
expect_schema_error(allocator_type "message Example { allocator_type(1): uint32; }"
    "C.. field name conflicts with a generated member")
expect_schema_error(member_case_collision
    "struct Example { healthPoints: uint32; health_points: uint32; }"
    "C.. field name collision")
expect_schema_error(branch_factory
    "struct Payload { value: uint32; } union Choice { Event(1): Payload; FromEvent(2): Payload; }"
    "C.. union branch name collision")
expect_schema_error(constant_collision
    "import \"bebop/cpp.bop\"; const int32 VALUE = 1; @cpp.name(\"kValue\") struct Payload { value: uint32; }"
    "C.. definition name collision")
expect_schema_error(internal_collision
    "import \"bebop/cpp.bop\"; @cpp.name(\"internal\") struct Payload { struct Nested { value: uint32; } nested: Nested; }"
    "C.. definition name collision")

file(WRITE "${output_dir}/namespace_case.bop"
    "package SchemaScope.GameData; struct Record { requestID: uint32; }")
execute_process(
    COMMAND "${compiler}" build "${output_dir}/namespace_case.bop"
            "--plugin=cpp=${plugin}" "--cpp_out=${output_dir}/namespace_case" --quiet
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Namespace generation failed: ${output}${error}")
endif()
file(READ "${output_dir}/namespace_case/namespace_case.bb.hpp" namespace_code)
if(NOT namespace_code MATCHES "namespace schema_scope::game_data")
    message(FATAL_ERROR "Schema namespaces did not use snake_case")
endif()

file(WRITE "${output_dir}/first.bop" "package first; struct Record { value: uint32; }")
file(WRITE "${output_dir}/second.bop" "package second; struct Record { value: string; }")
execute_process(
    COMMAND "${compiler}" build "${output_dir}/first.bop" "${output_dir}/second.bop"
            "--plugin=cpp=${plugin}" "--cpp_out=${output_dir}/invalid"
            -D namespace=combined --quiet
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "C.. definition name collision")
    message(FATAL_ERROR "Cross-schema collision gave an unexpected result: ${output}${error}")
endif()

file(READ "${output_dir}/cpp.bb.hpp" generated)
string(FIND "${generated}" "struct OptionEntityStateRef" ref_position)
if(NOT ref_position EQUAL -1)
    message(FATAL_ERROR "emit_ref=false still generated a borrowed type")
endif()

string(FIND "${generated}" "class OptionEntityStateView" view_position)
if(NOT view_position EQUAL -1)
    message(FATAL_ERROR "emit_view=false still generated a view type")
endif()
