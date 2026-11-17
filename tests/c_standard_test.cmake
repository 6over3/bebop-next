foreach(standard c99 c11 c17)
    execute_process(
        COMMAND "${compiler}" build "${schema}" "--plugin=c=${plugin}"
                "--c_out=${output_dir}" -D "c_standard=${standard}" --quiet
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "requires C23")
        message(FATAL_ERROR "Expected ${standard} to be rejected: ${output}${error}")
    endif()
endforeach()
