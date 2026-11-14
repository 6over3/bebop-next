function(run)
    execute_process(
        COMMAND ${ARGV}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGV0} failed: ${output}${error}")
    endif()
endfunction()

function(generate generator destination)
    file(MAKE_DIRECTORY "${destination}")
    run("${compiler}" build
        "${source_dir}/bebop/schemas/bebop/descriptor.bop"
        "${source_dir}/bebop/schemas/bebop/plugin.bop"
        -I "${source_dir}/bebop/schemas"
        "--plugin=cpp=${generator}" "--cpp_out=${destination}"
        -D emit_ref=false -D emit_view=false -D emit_reflection=false --quiet
    )
endfunction()

file(REMOVE_RECURSE "${output_dir}")
file(MAKE_DIRECTORY "${output_dir}/source")
foreach(source generator.cc options.cc options.h descriptor.h code_writer.h)
    file(COPY "${source_dir}/plugins/cpp/src/${source}"
        DESTINATION "${output_dir}/source")
endforeach()

generate("${plugin}" "${output_dir}/source/generated")

file(WRITE "${output_dir}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(bebop_cpp_bootstrap LANGUAGES CXX)
add_executable(bebopc-gen-cpp generator.cc options.cc)
target_compile_features(bebopc-gen-cpp PRIVATE cxx_std_23)
target_include_directories(bebopc-gen-cpp PRIVATE "${runtime_dir}")
set_target_properties(bebopc-gen-cpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(bebopc-gen-cpp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()
]=])

set(configure "${CMAKE_COMMAND}"
    -S "${output_dir}/source" -B "${output_dir}/build"
    -G "${generator}" "-DCMAKE_CXX_COMPILER=${cxx}"
    -DCMAKE_BUILD_TYPE=Release
    "-Druntime_dir=${source_dir}/plugins/cpp/include"
)
if(platform)
    list(APPEND configure -A "${platform}")
endif()
if(toolset)
    list(APPEND configure -T "${toolset}")
endif()
foreach(setting sysroot architectures deployment_target)
    if(NOT "${${setting}}" STREQUAL "")
        string(TOUPPER "${setting}" name)
        list(APPEND configure "-DCMAKE_OSX_${name}=${${setting}}")
    endif()
endforeach()
run(${configure})
run("${CMAKE_COMMAND}" --build "${output_dir}/build" --config Release --parallel 2)

set(stage_two "${output_dir}/build/bin/bebopc-gen-cpp")
if(WIN32)
    string(APPEND stage_two ".exe")
endif()
generate("${stage_two}" "${output_dir}/stage_two")

foreach(header descriptor.bb.hpp plugin.bb.hpp)
    run("${CMAKE_COMMAND}" -E compare_files
        "${output_dir}/source/generated/${header}"
        "${output_dir}/stage_two/${header}"
    )
endforeach()
