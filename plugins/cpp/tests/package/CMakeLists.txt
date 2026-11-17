cmake_minimum_required(VERSION 3.25)
project(bebop_cpp_package_test LANGUAGES CXX)

find_package(bebop-cpp CONFIG REQUIRED)
find_program(BEBOPC_EXECUTABLE bebopc
    PATHS "${bebop-cpp_DIR}/../../../bin" NO_DEFAULT_PATH REQUIRED)

set(header "${CMAKE_CURRENT_BINARY_DIR}/package.bb.hpp")
add_custom_command(
    OUTPUT "${header}"
    COMMAND "${BEBOPC_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/package.bop"
        "--cpp_out=${CMAKE_CURRENT_BINARY_DIR}" -D emit_pmr=true --quiet
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/package.bop"
    VERBATIM
)
add_custom_target(package_header DEPENDS "${header}")

enable_testing()
foreach(mode native fallback)
    add_executable(package_${mode} main.cc)
    add_dependencies(package_${mode} package_header)
    target_include_directories(package_${mode} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(package_${mode} PRIVATE bebop::cpp)
    if(mode STREQUAL native)
        target_compile_definitions(package_${mode} PRIVATE BEBOP_TEST_NATIVE_TYPES=1)
    endif()
    if(MSVC)
        target_compile_options(package_${mode} PRIVATE /W4 /WX)
    else()
        target_compile_options(package_${mode} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(mode STREQUAL fallback)
        target_compile_definitions(package_${mode} PRIVATE
            BEBOP_CPP_NO_NATIVE_INT128=1
            BEBOP_CPP_NO_NATIVE_FLOAT16=1
            BEBOP_CPP_NO_NATIVE_BFLOAT16=1)
    endif()
    add_test(NAME package_${mode} COMMAND package_${mode})
endforeach()
