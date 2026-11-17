if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unity/src/unity.c")
    message(STATUS "Unity not found - run: git submodule update --init")
    return()
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
if(MSVC)
    target_compile_options(unity PRIVATE /W0)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(unity PRIVATE -Wno-double-promotion)
endif()

set(BEBOP_TEST_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(BEBOP_STD_DIR "${CMAKE_SOURCE_DIR}/bebop/schemas")
add_compile_definitions(
    BEBOP_TEST_FIXTURES_DIR="${BEBOP_TEST_FIXTURES_DIR}"
    BEBOP_STD_DIR="${BEBOP_STD_DIR}"
)

# Platform definitions for tests
if(WIN32)
    set(BEBOPC_PLATFORM_DEFS BEBOPC_WINDOWS=1)
elseif(APPLE)
    set(BEBOPC_PLATFORM_DEFS BEBOPC_POSIX=1 BEBOPC_MACOS=1)
elseif(UNIX)
    set(BEBOPC_PLATFORM_DEFS BEBOPC_POSIX=1 BEBOPC_LINUX=1)
endif()

# Internal test (includes bebop.c directly)
function(bebop_add_internal_test name)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE lua unity)
    target_sources(test_${name} PRIVATE $<TARGET_OBJECTS:lua>)
    target_include_directories(test_${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/bebop/src
        ${CMAKE_SOURCE_DIR}/bebop/include
        ${CMAKE_SOURCE_DIR}/vendor/cwisstable
        ${CMAKE_SOURCE_DIR}/vendor/lua
    )
    if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(test_${name} PRIVATE /std:c11 /experimental:c11atomics)
    endif()
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

# Public API test
function(bebop_add_test name)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE bebop::bebop unity)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

# Frontend sources shared by tests
set(BEBOPC_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_utils.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_error.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_dir.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_glob.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_log.c
)

# Frontend test helper
function(bebopc_add_test name)
    add_executable(test_bebopc_${name} test_bebopc_${name}.c ${BEBOPC_COMMON_SOURCES})
    target_link_libraries(test_bebopc_${name} PRIVATE unity bebop::bebop)
    target_include_directories(test_bebopc_${name} PRIVATE ${CMAKE_SOURCE_DIR}/bebopc/src)
    target_compile_definitions(test_bebopc_${name} PRIVATE ${BEBOPC_PLATFORM_DEFS})
    if(APPLE)
        target_link_libraries(test_bebopc_${name} PRIVATE "-framework Security")
    endif()
    add_test(NAME bebopc_${name} COMMAND test_bebopc_${name})
endfunction()

# Internal tests
bebop_add_internal_test(arena)
bebop_add_internal_test(str)
bebop_add_internal_test(context)
bebop_add_internal_test(scanner)
bebop_add_internal_test(hash)
bebop_add_internal_test(parser)
bebop_add_internal_test(integer)
add_executable(test_integer_fallback test_integer.c)
target_link_libraries(test_integer_fallback PRIVATE lua unity)
target_sources(test_integer_fallback PRIVATE $<TARGET_OBJECTS:lua>)
target_include_directories(test_integer_fallback PRIVATE
    ${CMAKE_SOURCE_DIR}/bebop/src
    ${CMAKE_SOURCE_DIR}/bebop/include
    ${CMAKE_SOURCE_DIR}/vendor/cwisstable
    ${CMAKE_SOURCE_DIR}/vendor/lua
)
target_compile_definitions(test_integer_fallback PRIVATE BEBOP_WIRE_NO_I128=1)
add_test(NAME integer_fallback COMMAND test_integer_fallback)
bebop_add_internal_test(macro)
bebop_add_internal_test(descriptor)
bebop_add_internal_test(plugin)
bebop_add_internal_test(service)

# Public API tests
bebop_add_test(emit)
bebop_add_test(enum_expression)
bebop_add_test(resolve)
bebop_add_test(map)
bebop_add_test(bebop_rt)

add_executable(test_cpp_runtime ../plugins/cpp/tests/runtime_test.cc)
target_link_libraries(test_cpp_runtime PRIVATE bebop::cpp)
add_test(NAME cpp_runtime COMMAND test_cpp_runtime)

add_test(NAME cpp_bootstrap COMMAND ${CMAKE_COMMAND}
    -Dcompiler=$<TARGET_FILE:bebopc>
    -Dplugin=$<TARGET_FILE:bebopc-gen-cpp>
    -Dcxx=${CMAKE_CXX_COMPILER}
    -Dgenerator=${CMAKE_GENERATOR}
    -Dplatform=${CMAKE_GENERATOR_PLATFORM}
    -Dtoolset=${CMAKE_GENERATOR_TOOLSET}
    -Dsysroot=${CMAKE_OSX_SYSROOT}
    "-Darchitectures=${CMAKE_OSX_ARCHITECTURES}"
    -Ddeployment_target=${CMAKE_OSX_DEPLOYMENT_TARGET}
    -Dsource_dir=${CMAKE_SOURCE_DIR}
    -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}/cpp_bootstrap
    -P ${CMAKE_SOURCE_DIR}/plugins/cpp/tests/bootstrap_test.cmake
)
set_tests_properties(cpp_bootstrap PROPERTIES TIMEOUT 180)

add_test(NAME cpp_scaling COMMAND ${CMAKE_COMMAND}
    -Dcompiler=$<TARGET_FILE:bebopc>
    -Dplugin=$<TARGET_FILE:bebopc-gen-cpp>
    -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}/cpp_scaling
    -P ${CMAKE_SOURCE_DIR}/plugins/cpp/tests/scaling_test.cmake
)
set_tests_properties(cpp_scaling PROPERTIES TIMEOUT 15)


add_executable(test_cpp_traversal ../plugins/cpp/tests/traversal_test.cc)
target_link_libraries(test_cpp_traversal PRIVATE bebop::cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_traversal PRIVATE -fno-exceptions -fno-rtti)
elseif(MSVC)
    # The traversal test deliberately uses an over-aligned frame.
    target_compile_options(test_cpp_traversal PRIVATE /wd4324)
endif()
add_test(NAME cpp_traversal COMMAND test_cpp_traversal)

add_executable(test_cpp_allocator ../plugins/cpp/tests/allocator_test.cc)
target_link_libraries(test_cpp_allocator PRIVATE bebop::cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_allocator PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_allocator COMMAND test_cpp_allocator)

set(BEBOP_CPP_PMR_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_pmr")
add_custom_command(
    OUTPUT "${BEBOP_CPP_PMR_DIR}/cpp.bb.hpp" "${BEBOP_CPP_PMR_DIR}/recursive.bb.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_PMR_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build
            "${CMAKE_CURRENT_SOURCE_DIR}/schemas/cpp.bop"
            "${CMAKE_CURRENT_SOURCE_DIR}/schemas/recursive.bop"
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_PMR_DIR} -D emit_pmr=true --quiet
    DEPENDS bebopc bebopc-gen-cpp
            "${CMAKE_CURRENT_SOURCE_DIR}/schemas/cpp.bop"
            "${CMAKE_CURRENT_SOURCE_DIR}/schemas/recursive.bop"
            "${BEBOP_STD_DIR}/bebop/cpp.bop"
    VERBATIM
)
add_custom_target(cpp_pmr_headers DEPENDS
    "${BEBOP_CPP_PMR_DIR}/cpp.bb.hpp" "${BEBOP_CPP_PMR_DIR}/recursive.bb.hpp")
add_executable(test_cpp_pmr ../plugins/cpp/tests/pmr_test.cc
    "${BEBOP_CPP_PMR_DIR}/cpp.bb.hpp" "${BEBOP_CPP_PMR_DIR}/recursive.bb.hpp")
add_dependencies(test_cpp_pmr cpp_pmr_headers)
target_link_libraries(test_cpp_pmr PRIVATE bebop::cpp)
target_include_directories(test_cpp_pmr PRIVATE "${BEBOP_CPP_PMR_DIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_pmr PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_pmr COMMAND test_cpp_pmr)

add_executable(test_cpp_pmr_fallback ../plugins/cpp/tests/pmr_test.cc
    "${BEBOP_CPP_PMR_DIR}/cpp.bb.hpp" "${BEBOP_CPP_PMR_DIR}/recursive.bb.hpp")
add_dependencies(test_cpp_pmr_fallback test_cpp_pmr)
target_link_libraries(test_cpp_pmr_fallback PRIVATE bebop::cpp)
target_include_directories(test_cpp_pmr_fallback PRIVATE "${BEBOP_CPP_PMR_DIR}")
target_compile_definitions(test_cpp_pmr_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_pmr_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_pmr_fallback COMMAND test_cpp_pmr_fallback)

set(BEBOP_CPP_REFLECTION_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_reflection")
add_custom_command(
    OUTPUT "${BEBOP_CPP_REFLECTION_DIR}/reflection.bb.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_REFLECTION_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${CMAKE_CURRENT_SOURCE_DIR}/schemas/reflection.bop"
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_REFLECTION_DIR} -D emit_pmr=true --quiet
    DEPENDS bebopc bebopc-gen-cpp "${CMAKE_CURRENT_SOURCE_DIR}/schemas/reflection.bop"
    VERBATIM
)
add_executable(test_cpp_reflection ../plugins/cpp/tests/reflection_test.cc
    "${BEBOP_CPP_REFLECTION_DIR}/reflection.bb.hpp"
    "${BEBOP_CPP_PMR_DIR}/cpp.bb.hpp" "${BEBOP_CPP_PMR_DIR}/recursive.bb.hpp")
add_dependencies(test_cpp_reflection cpp_pmr_headers)
target_link_libraries(test_cpp_reflection PRIVATE bebop::cpp)
target_include_directories(test_cpp_reflection PRIVATE
    "${BEBOP_CPP_REFLECTION_DIR}" "${BEBOP_CPP_PMR_DIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_reflection PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_reflection COMMAND test_cpp_reflection)

add_executable(test_cpp_reflection_fallback ../plugins/cpp/tests/reflection_test.cc
    "${BEBOP_CPP_REFLECTION_DIR}/reflection.bb.hpp"
    "${BEBOP_CPP_PMR_DIR}/cpp.bb.hpp" "${BEBOP_CPP_PMR_DIR}/recursive.bb.hpp")
add_dependencies(test_cpp_reflection_fallback test_cpp_reflection)
target_link_libraries(test_cpp_reflection_fallback PRIVATE bebop::cpp)
target_include_directories(test_cpp_reflection_fallback PRIVATE
    "${BEBOP_CPP_REFLECTION_DIR}" "${BEBOP_CPP_PMR_DIR}")
target_compile_definitions(test_cpp_reflection_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_reflection_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_reflection_fallback COMMAND test_cpp_reflection_fallback)

set(BEBOP_CPP_WELL_KNOWN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_well_known")
set(BEBOP_CPP_WELL_KNOWN_SCHEMAS)
set(BEBOP_CPP_WELL_KNOWN_HEADERS)
foreach(schema any json empty descriptor plugin)
    list(APPEND BEBOP_CPP_WELL_KNOWN_SCHEMAS "${BEBOP_STD_DIR}/bebop/${schema}.bop")
    list(APPEND BEBOP_CPP_WELL_KNOWN_HEADERS "${BEBOP_CPP_WELL_KNOWN_DIR}/${schema}.bb.hpp")
endforeach()
add_custom_command(
    OUTPUT ${BEBOP_CPP_WELL_KNOWN_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_WELL_KNOWN_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build ${BEBOP_CPP_WELL_KNOWN_SCHEMAS}
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_WELL_KNOWN_DIR} -D emit_pmr=true --quiet
    DEPENDS bebopc bebopc-gen-cpp ${BEBOP_CPP_WELL_KNOWN_SCHEMAS}
            "${BEBOP_STD_DIR}/bebop/decorators.bop"
    VERBATIM
)
add_executable(test_cpp_well_known ../plugins/cpp/tests/well_known_test.cc
    ${BEBOP_CPP_WELL_KNOWN_HEADERS})
target_link_libraries(test_cpp_well_known PRIVATE bebop::cpp bebop::bebop)
target_include_directories(test_cpp_well_known PRIVATE "${BEBOP_CPP_WELL_KNOWN_DIR}")
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(test_cpp_well_known PRIVATE /Z7)
    target_link_options(test_cpp_well_known PRIVATE /DEBUG)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_well_known PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_well_known COMMAND test_cpp_well_known)

add_executable(test_cpp_well_known_fallback ../plugins/cpp/tests/well_known_test.cc
    ${BEBOP_CPP_WELL_KNOWN_HEADERS})
add_dependencies(test_cpp_well_known_fallback test_cpp_well_known)
target_link_libraries(test_cpp_well_known_fallback PRIVATE bebop::cpp bebop::bebop)
target_include_directories(test_cpp_well_known_fallback PRIVATE "${BEBOP_CPP_WELL_KNOWN_DIR}")
target_compile_definitions(test_cpp_well_known_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_well_known_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_well_known_fallback COMMAND test_cpp_well_known_fallback)

set(BEBOP_CPP_DEPRECATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_deprecated")
add_custom_command(
    OUTPUT "${BEBOP_CPP_DEPRECATED_DIR}/deprecated.bb.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_DEPRECATED_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${CMAKE_CURRENT_SOURCE_DIR}/schemas/deprecated.bop"
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_DEPRECATED_DIR} -D emit_pmr=true --quiet
    DEPENDS bebopc bebopc-gen-cpp "${CMAKE_CURRENT_SOURCE_DIR}/schemas/deprecated.bop"
            "${BEBOP_STD_DIR}/bebop/decorators.bop"
    VERBATIM
)
add_executable(test_cpp_deprecated ../plugins/cpp/tests/deprecated_test.cc
    "${BEBOP_CPP_DEPRECATED_DIR}/deprecated.bb.hpp")
target_link_libraries(test_cpp_deprecated PRIVATE bebop::cpp)
target_include_directories(test_cpp_deprecated PRIVATE "${BEBOP_CPP_DEPRECATED_DIR}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_deprecated PRIVATE
        -fno-exceptions -fno-rtti -Wno-deprecated-declarations)
elseif(MSVC)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(test_cpp_deprecated PRIVATE /clang:-Wno-deprecated-declarations)
    else()
        target_compile_options(test_cpp_deprecated PRIVATE /wd4996)
    endif()
endif()
add_test(NAME cpp_deprecated COMMAND test_cpp_deprecated)

add_executable(test_cpp_deprecated_fallback ../plugins/cpp/tests/deprecated_test.cc
    "${BEBOP_CPP_DEPRECATED_DIR}/deprecated.bb.hpp")
add_dependencies(test_cpp_deprecated_fallback test_cpp_deprecated)
target_link_libraries(test_cpp_deprecated_fallback PRIVATE bebop::cpp)
target_include_directories(test_cpp_deprecated_fallback PRIVATE "${BEBOP_CPP_DEPRECATED_DIR}")
target_compile_definitions(test_cpp_deprecated_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_deprecated_fallback PRIVATE
        -fno-exceptions -fno-rtti -Wno-deprecated-declarations)
elseif(MSVC)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(test_cpp_deprecated_fallback PRIVATE /clang:-Wno-deprecated-declarations)
    else()
        target_compile_options(test_cpp_deprecated_fallback PRIVATE /wd4996)
    endif()
endif()
add_test(NAME cpp_deprecated_fallback COMMAND test_cpp_deprecated_fallback)
add_test(NAME cpp_deprecation_diagnostics COMMAND ${CMAKE_COMMAND}
    -Dcompiler=${CMAKE_CXX_COMPILER}
    -Dmsvc=${MSVC}
    -Dcompiler_id=${CMAKE_CXX_COMPILER_ID}
    "-Dcompiler_flags=${CMAKE_CXX_FLAGS}"
    -Doutput_dir=${BEBOP_CPP_DEPRECATED_DIR}
    -Druntime_dir=${CMAKE_SOURCE_DIR}/plugins/cpp/include
    -P "${CMAKE_SOURCE_DIR}/plugins/cpp/tests/deprecated_test.cmake")

set(BEBOP_CPP_NAMING_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_naming")
add_custom_command(
    OUTPUT "${BEBOP_CPP_NAMING_DIR}/naming.bb.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_NAMING_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${CMAKE_CURRENT_SOURCE_DIR}/schemas/naming.bop"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_NAMING_DIR} -D emit_pmr=true --quiet
    DEPENDS bebopc bebopc-gen-cpp "${CMAKE_CURRENT_SOURCE_DIR}/schemas/naming.bop"
    VERBATIM
)
add_executable(test_cpp_naming ../plugins/cpp/tests/naming_test.cc
    "${BEBOP_CPP_NAMING_DIR}/naming.bb.hpp")
target_link_libraries(test_cpp_naming PRIVATE bebop::cpp)
target_include_directories(test_cpp_naming PRIVATE "${BEBOP_CPP_NAMING_DIR}")
add_test(NAME cpp_naming COMMAND test_cpp_naming)

add_executable(test_cpp_runtime_fallback ../plugins/cpp/tests/runtime_test.cc)
target_link_libraries(test_cpp_runtime_fallback PRIVATE bebop::cpp)
target_compile_definitions(test_cpp_runtime_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_runtime_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_runtime_fallback COMMAND test_cpp_runtime_fallback)

add_executable(test_cpp_message ../plugins/cpp/tests/message_test.cc)
target_link_libraries(test_cpp_message PRIVATE bebop::cpp bebop::bebop)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_cpp_message PRIVATE -Wno-c99-extensions -Wno-c11-extensions)
endif()
add_test(NAME cpp_message COMMAND test_cpp_message)

# Generate a focused schema with the current C plugin and test composed aggregate views.
set(BEBOP_C_VIEWS_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/views.bop")
set(BEBOP_C_VIEWS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/c_views")
set(BEBOP_C_VIEWS_SOURCE "${BEBOP_C_VIEWS_GENERATED_DIR}/views.bb.c")
set(BEBOP_C_VIEWS_HEADER "${BEBOP_C_VIEWS_GENERATED_DIR}/views.bb.h")
add_custom_command(
    OUTPUT "${BEBOP_C_VIEWS_SOURCE}" "${BEBOP_C_VIEWS_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_C_VIEWS_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_C_VIEWS_SCHEMA}"
            --plugin=c=$<TARGET_FILE:bebopc-gen-c>
            --c_out=${BEBOP_C_VIEWS_GENERATED_DIR}
            --quiet
    DEPENDS bebopc bebopc-gen-c "${BEBOP_C_VIEWS_SCHEMA}"
    VERBATIM
)
add_executable(test_c_views test_c_views.c "${BEBOP_C_VIEWS_SOURCE}")
target_include_directories(test_c_views PRIVATE "${BEBOP_C_VIEWS_GENERATED_DIR}")
target_link_libraries(test_c_views PRIVATE bebop::bebop unity)
add_test(NAME c_views COMMAND test_c_views)

set(BEBOP_NUMERIC_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/numeric.bop")
set(BEBOP_C_NUMERIC_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/c_numeric")
set(BEBOP_C_NUMERIC_SOURCE "${BEBOP_C_NUMERIC_DIR}/numeric.bb.c")
set(BEBOP_C_NUMERIC_HEADER "${BEBOP_C_NUMERIC_DIR}/numeric.bb.h")
add_custom_command(
    OUTPUT "${BEBOP_C_NUMERIC_SOURCE}" "${BEBOP_C_NUMERIC_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_C_NUMERIC_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_NUMERIC_SCHEMA}"
            --plugin=c=$<TARGET_FILE:bebopc-gen-c>
            --c_out=${BEBOP_C_NUMERIC_DIR}
            --quiet
    DEPENDS bebopc bebopc-gen-c "${BEBOP_NUMERIC_SCHEMA}"
    VERBATIM
)
add_library(c_numeric_generated OBJECT "${BEBOP_C_NUMERIC_SOURCE}")
target_include_directories(c_numeric_generated PRIVATE "${BEBOP_C_NUMERIC_DIR}")
target_link_libraries(c_numeric_generated PRIVATE bebop::bebop)
add_executable(test_c_constants test_c_constants.c $<TARGET_OBJECTS:c_numeric_generated>)
target_include_directories(test_c_constants PRIVATE "${BEBOP_C_NUMERIC_DIR}")
target_link_libraries(test_c_constants PRIVATE bebop::bebop unity)
add_test(NAME c_constants COMMAND test_c_constants)

add_executable(test_c_constants_fallback test_c_constants.c "${BEBOP_C_NUMERIC_SOURCE}")
add_dependencies(test_c_constants_fallback c_numeric_generated)
target_include_directories(test_c_constants_fallback PRIVATE "${BEBOP_C_NUMERIC_DIR}")
target_compile_definitions(test_c_constants_fallback PRIVATE BEBOP_WIRE_NO_I128=1)
target_link_libraries(test_c_constants_fallback PRIVATE bebop::bebop unity)
add_test(NAME c_constants_fallback COMMAND test_c_constants_fallback)

set(BEBOP_CPP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp")
set(BEBOP_CPP_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/cpp.bop")
add_custom_target(cpp_generated_headers DEPENDS
    "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.hpp" "${BEBOP_CPP_GENERATED_DIR}/numeric.bb.hpp")
add_custom_command(
    OUTPUT "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.hpp" "${BEBOP_CPP_GENERATED_DIR}/numeric.bb.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_SCHEMA}" "${BEBOP_NUMERIC_SCHEMA}"
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_GENERATED_DIR} --quiet
    DEPENDS bebopc bebopc-gen-cpp "${BEBOP_CPP_SCHEMA}" "${BEBOP_NUMERIC_SCHEMA}"
            "${BEBOP_STD_DIR}/bebop/cpp.bop"
    VERBATIM
)
add_custom_command(
    OUTPUT "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.c" "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_SCHEMA}" -I "${BEBOP_STD_DIR}"
            --plugin=c=$<TARGET_FILE:bebopc-gen-c>
            --c_out=${BEBOP_CPP_GENERATED_DIR} --quiet
    DEPENDS bebopc bebopc-gen-c "${BEBOP_CPP_SCHEMA}" "${BEBOP_STD_DIR}/bebop/cpp.bop"
    VERBATIM
)
add_library(cpp_interop_generated OBJECT "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.c")
target_link_libraries(cpp_interop_generated PRIVATE bebop::bebop)
target_include_directories(cpp_interop_generated PRIVATE "${BEBOP_CPP_GENERATED_DIR}")
add_executable(test_cpp_generated ../plugins/cpp/tests/generated_test.cc
    "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.hpp" "${BEBOP_CPP_GENERATED_DIR}/numeric.bb.hpp"
    $<TARGET_OBJECTS:cpp_interop_generated>)
add_dependencies(test_cpp_generated cpp_generated_headers)
target_include_directories(test_cpp_generated PRIVATE "${BEBOP_CPP_GENERATED_DIR}")
target_link_libraries(test_cpp_generated PRIVATE bebop::cpp bebop::bebop)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_cpp_generated PRIVATE -Wno-c99-extensions -Wno-c11-extensions)
endif()
add_test(NAME cpp_generated COMMAND test_cpp_generated)

add_executable(test_cpp_generated_fallback ../plugins/cpp/tests/generated_test.cc
    "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.hpp" "${BEBOP_CPP_GENERATED_DIR}/numeric.bb.hpp"
    $<TARGET_OBJECTS:cpp_interop_generated>)
add_dependencies(test_cpp_generated_fallback test_cpp_generated)
target_include_directories(test_cpp_generated_fallback PRIVATE "${BEBOP_CPP_GENERATED_DIR}")
target_link_libraries(test_cpp_generated_fallback PRIVATE bebop::cpp bebop::bebop)
target_compile_definitions(test_cpp_generated_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_cpp_generated_fallback PRIVATE -Wno-c99-extensions -Wno-c11-extensions)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_generated_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_generated_fallback COMMAND test_cpp_generated_fallback)

set(BEBOP_CPP_OPTIONS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_options")
add_custom_command(
    OUTPUT "${BEBOP_CPP_OPTIONS_DIR}/cpp.bb.hpp" "${BEBOP_CPP_OPTIONS_DIR}/numeric.bb.hpp"
           "${BEBOP_CPP_OPTIONS_DIR}/custom/bebop.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_OPTIONS_DIR}/custom"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/plugins/cpp/include/bebop.hpp"
            "${BEBOP_CPP_OPTIONS_DIR}/custom/bebop.hpp"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_SCHEMA}" "${BEBOP_NUMERIC_SCHEMA}"
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_OPTIONS_DIR}
            -D cpp_standard=c++23 -D namespace=test::options -D prefix=Option
            -D emit_ref=false -D emit_view=false -D emit_pmr=true -D emit_reflection=false
            -D runtime_header=custom/bebop.hpp -D services=none --quiet
    DEPENDS bebopc bebopc-gen-cpp "${BEBOP_CPP_SCHEMA}" "${BEBOP_NUMERIC_SCHEMA}"
            "${CMAKE_SOURCE_DIR}/plugins/cpp/include/bebop.hpp"
    VERBATIM
)
add_executable(test_cpp_options ../plugins/cpp/tests/options_test.cc
    "${BEBOP_CPP_OPTIONS_DIR}/cpp.bb.hpp" "${BEBOP_CPP_OPTIONS_DIR}/numeric.bb.hpp")
target_compile_features(test_cpp_options PRIVATE cxx_std_23)
target_include_directories(test_cpp_options PRIVATE "${BEBOP_CPP_OPTIONS_DIR}")
add_test(NAME cpp_options COMMAND test_cpp_options)
add_test(NAME cpp_option_diagnostics COMMAND ${CMAKE_COMMAND}
    -Dcompiler=$<TARGET_FILE:bebopc>
    -Dplugin=$<TARGET_FILE:bebopc-gen-cpp>
    -Dschema=${BEBOP_CPP_SCHEMA}
    -Dstandard_schemas=${BEBOP_STD_DIR}
    -Doutput_dir=${BEBOP_CPP_OPTIONS_DIR}
    -P "${CMAKE_SOURCE_DIR}/plugins/cpp/tests/options_test.cmake")

add_executable(test_cpp_view ../plugins/cpp/tests/view_test.cc)
target_link_libraries(test_cpp_view PRIVATE bebop::cpp)
add_test(NAME cpp_view COMMAND test_cpp_view)

add_executable(test_cpp_view_fallback ../plugins/cpp/tests/view_test.cc)
target_link_libraries(test_cpp_view_fallback PRIVATE bebop::cpp)
target_compile_definitions(test_cpp_view_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_view_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_view_fallback COMMAND test_cpp_view_fallback)

add_executable(test_cpp_generated_view ../plugins/cpp/tests/generated_view_test.cc
    "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.hpp")
add_dependencies(test_cpp_generated_view cpp_generated_headers)
target_include_directories(test_cpp_generated_view PRIVATE "${BEBOP_CPP_GENERATED_DIR}")
target_link_libraries(test_cpp_generated_view PRIVATE bebop::cpp)
add_test(NAME cpp_generated_view COMMAND test_cpp_generated_view)

add_executable(test_cpp_generated_view_fallback ../plugins/cpp/tests/generated_view_test.cc
    "${BEBOP_CPP_GENERATED_DIR}/cpp.bb.hpp")
add_dependencies(test_cpp_generated_view_fallback test_cpp_generated_view)
target_include_directories(test_cpp_generated_view_fallback PRIVATE "${BEBOP_CPP_GENERATED_DIR}")
target_link_libraries(test_cpp_generated_view_fallback PRIVATE bebop::cpp)
target_compile_definitions(test_cpp_generated_view_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_generated_view_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_generated_view_fallback COMMAND test_cpp_generated_view_fallback)

set(BEBOP_CPP_RECURSIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_recursive")
set(BEBOP_CPP_RECURSIVE_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/recursive.bop")
add_custom_command(
    OUTPUT "${BEBOP_CPP_RECURSIVE_DIR}/recursive.bb.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_RECURSIVE_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_RECURSIVE_SCHEMA}"
            -I "${BEBOP_STD_DIR}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_RECURSIVE_DIR} --quiet
    DEPENDS bebopc bebopc-gen-cpp "${BEBOP_CPP_RECURSIVE_SCHEMA}"
            "${BEBOP_STD_DIR}/bebop/cpp.bop"
    VERBATIM
)
add_executable(test_cpp_recursive ../plugins/cpp/tests/recursive_test.cc
    "${BEBOP_CPP_RECURSIVE_DIR}/recursive.bb.hpp")
target_include_directories(test_cpp_recursive PRIVATE "${BEBOP_CPP_RECURSIVE_DIR}")
target_link_libraries(test_cpp_recursive PRIVATE bebop::cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_recursive PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_recursive COMMAND test_cpp_recursive)

add_executable(test_cpp_recursive_fallback ../plugins/cpp/tests/recursive_test.cc
    "${BEBOP_CPP_RECURSIVE_DIR}/recursive.bb.hpp")
add_dependencies(test_cpp_recursive_fallback test_cpp_recursive)
target_include_directories(test_cpp_recursive_fallback PRIVATE "${BEBOP_CPP_RECURSIVE_DIR}")
target_link_libraries(test_cpp_recursive_fallback PRIVATE bebop::cpp)
target_compile_definitions(test_cpp_recursive_fallback PRIVATE
    BEBOP_CPP_NO_NATIVE_INT128=1
    BEBOP_CPP_NO_NATIVE_FLOAT16=1
    BEBOP_CPP_NO_NATIVE_BFLOAT16=1
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    target_compile_options(test_cpp_recursive_fallback PRIVATE -fno-exceptions -fno-rtti)
endif()
add_test(NAME cpp_recursive_fallback COMMAND test_cpp_recursive_fallback)

set(BEBOP_CPP_RECURSIVE_WIRE_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/recursive_wire.bop")
add_custom_command(
    OUTPUT "${BEBOP_CPP_RECURSIVE_DIR}/recursive_wire.bb.hpp"
           "${BEBOP_CPP_RECURSIVE_DIR}/recursive_wire.bb.h"
           "${BEBOP_CPP_RECURSIVE_DIR}/recursive_wire.bb.c"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_RECURSIVE_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_RECURSIVE_WIRE_SCHEMA}"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --plugin=c=$<TARGET_FILE:bebopc-gen-c>
            --cpp_out=${BEBOP_CPP_RECURSIVE_DIR}
            --c_out=${BEBOP_CPP_RECURSIVE_DIR} --quiet
    DEPENDS bebopc bebopc-gen-cpp bebopc-gen-c "${BEBOP_CPP_RECURSIVE_WIRE_SCHEMA}"
    VERBATIM
)
add_library(cpp_recursive_interop_generated OBJECT
    "${BEBOP_CPP_RECURSIVE_DIR}/recursive_wire.bb.c")
target_include_directories(cpp_recursive_interop_generated PRIVATE "${BEBOP_CPP_RECURSIVE_DIR}")
target_link_libraries(cpp_recursive_interop_generated PRIVATE bebop::bebop)
add_executable(test_cpp_recursive_interop ../plugins/cpp/tests/recursive_interop_test.cc
    "${BEBOP_CPP_RECURSIVE_DIR}/recursive_wire.bb.hpp"
    $<TARGET_OBJECTS:cpp_recursive_interop_generated>)
target_include_directories(test_cpp_recursive_interop PRIVATE "${BEBOP_CPP_RECURSIVE_DIR}")
target_link_libraries(test_cpp_recursive_interop PRIVATE bebop::cpp bebop::bebop)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_cpp_recursive_interop PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-c99-extensions;-Wno-c11-extensions>)
endif()
add_test(NAME cpp_recursive_interop COMMAND test_cpp_recursive_interop)

set(BEBOP_CPP_FULL_SCHEMA_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/cpp_full_schema")
set(BEBOP_CPP_FULL_SCHEMA_HEADERS
    "${BEBOP_CPP_FULL_SCHEMA_DIR}/codegen_base.bb.hpp"
    "${BEBOP_CPP_FULL_SCHEMA_DIR}/codegen_all.bb.hpp")
add_custom_command(
    OUTPUT ${BEBOP_CPP_FULL_SCHEMA_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_FULL_SCHEMA_DIR}"
    COMMAND $<TARGET_FILE:bebopc> build
            "${BEBOP_TEST_FIXTURES_DIR}/valid/codegen_base.bop"
            "${BEBOP_TEST_FIXTURES_DIR}/valid/codegen_all.bop"
            -I "${BEBOP_TEST_FIXTURES_DIR}/valid"
            --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
            --cpp_out=${BEBOP_CPP_FULL_SCHEMA_DIR} -D emit_pmr=true --quiet
    DEPENDS bebopc bebopc-gen-cpp
            "${BEBOP_TEST_FIXTURES_DIR}/valid/codegen_base.bop"
            "${BEBOP_TEST_FIXTURES_DIR}/valid/codegen_all.bop"
    VERBATIM
)
add_custom_target(cpp_full_schema_headers DEPENDS ${BEBOP_CPP_FULL_SCHEMA_HEADERS})
foreach(variant native fallback)
    add_executable(test_cpp_full_schema_${variant}
        ../plugins/cpp/tests/full_schema_test.cc)
    add_dependencies(test_cpp_full_schema_${variant} cpp_full_schema_headers)
    target_include_directories(test_cpp_full_schema_${variant}
        PRIVATE "${BEBOP_CPP_FULL_SCHEMA_DIR}")
    target_link_libraries(test_cpp_full_schema_${variant} PRIVATE bebop::cpp)
    if(variant STREQUAL "fallback")
        target_compile_definitions(test_cpp_full_schema_${variant} PRIVATE
            BEBOP_CPP_NO_NATIVE_INT128=1
            BEBOP_CPP_NO_NATIVE_FLOAT16=1
            BEBOP_CPP_NO_NATIVE_BFLOAT16=1)
    endif()
    add_test(NAME cpp_full_schema_${variant} COMMAND test_cpp_full_schema_${variant})
endforeach()

add_custom_target(cpp_tests DEPENDS
    test_cpp_runtime
    test_cpp_runtime_fallback
    test_cpp_traversal
    test_cpp_allocator
    test_cpp_pmr
    test_cpp_pmr_fallback
    test_cpp_reflection
    test_cpp_reflection_fallback
    test_cpp_well_known
    test_cpp_well_known_fallback
    test_cpp_deprecated
    test_cpp_deprecated_fallback
    test_cpp_naming
    test_cpp_message
    test_cpp_generated
    test_cpp_generated_fallback
    test_cpp_options
    test_cpp_view
    test_cpp_view_fallback
    test_cpp_generated_view
    test_cpp_generated_view_fallback
    test_cpp_recursive
    test_cpp_recursive_fallback
    test_cpp_recursive_interop
    test_cpp_full_schema_native
    test_cpp_full_schema_fallback
)

# Frontend tests
bebopc_add_test(dir)
bebopc_add_test(glob)
bebopc_add_test(log)

# CLI test
add_executable(test_bebopc_cli test_bebopc_cli.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_utils.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_error.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_log.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/cli/bebopc_cli.c
)
target_link_libraries(test_bebopc_cli PRIVATE unity bebop::bebop)
target_include_directories(test_bebopc_cli PRIVATE ${CMAKE_SOURCE_DIR}/bebopc/src)
target_compile_definitions(test_bebopc_cli PRIVATE ${BEBOPC_PLATFORM_DEFS})
if(APPLE)
    target_link_libraries(test_bebopc_cli PRIVATE "-framework Security")
endif()
add_test(NAME bebopc_cli COMMAND test_bebopc_cli)

# Config test
add_executable(test_bebopc_config test_bebopc_config.c
    ${BEBOPC_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/bebopc/src/cli/bebopc_cli.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/cli/bebopc_config.c
)
target_link_libraries(test_bebopc_config PRIVATE unity bebop::bebop cyaml::cyaml)
target_include_directories(test_bebopc_config PRIVATE ${CMAKE_SOURCE_DIR}/bebopc/src)
target_compile_definitions(test_bebopc_config PRIVATE ${BEBOPC_PLATFORM_DEFS})
if(APPLE)
    target_link_libraries(test_bebopc_config PRIVATE "-framework Security")
endif()
add_test(NAME bebopc_config COMMAND test_bebopc_config)

# Runner test
add_executable(test_bebopc_runner test_bebopc_runner.c
    ${BEBOPC_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/bebopc/src/cli/bebopc_cli.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/cli/bebopc_config.c
    ${CMAKE_SOURCE_DIR}/bebopc/src/cli/bebopc_runner.c
)
target_link_libraries(test_bebopc_runner PRIVATE unity bebop::bebop cyaml::cyaml)
target_include_directories(test_bebopc_runner PRIVATE ${CMAKE_SOURCE_DIR}/bebopc/src)
target_compile_definitions(test_bebopc_runner PRIVATE ${BEBOPC_PLATFORM_DEFS})
if(APPLE)
    target_link_libraries(test_bebopc_runner PRIVATE "-framework Security")
endif()
add_test(NAME bebopc_runner COMMAND test_bebopc_runner)

# File system watcher test (only on macOS with Clang, Linux/Windows not implemented)
# GCC on macOS cannot compile CoreServices headers (uses blocks)
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_executable(test_bebop_fsw test_bebop_fsw.c
        ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_utils.c
        ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_error.c
        ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_dir.c
        ${CMAKE_SOURCE_DIR}/bebopc/src/bebopc_glob.c
        ${CMAKE_SOURCE_DIR}/bebopc/src/watcher/bebop_fsw.c
        ${CMAKE_SOURCE_DIR}/bebopc/src/watcher/bebop_fsw_darwin.c
    )
    target_link_libraries(test_bebop_fsw PRIVATE unity bebop::bebop "-framework CoreServices" "-framework Security")
    target_include_directories(test_bebop_fsw PRIVATE
        ${CMAKE_SOURCE_DIR}/bebopc/src
        ${CMAKE_SOURCE_DIR}/bebopc/src/watcher
    )
    target_compile_definitions(test_bebop_fsw PRIVATE ${BEBOPC_PLATFORM_DEFS})
    add_test(NAME bebop_fsw COMMAND test_bebop_fsw)
endif()

# JSON wire test
add_executable(test_json_wire test_json_wire.c generated/document.bb.c generated/bebop/json.bb.c)
target_link_libraries(test_json_wire PRIVATE bebop::bebop unity)
target_include_directories(test_json_wire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME json_wire COMMAND test_json_wire)

# Reflection test
add_executable(test_reflection test_reflection.c generated/reflection.bb.c generated/bebop/any.bb.c)
target_link_libraries(test_reflection PRIVATE bebop::bebop unity)
target_include_directories(test_reflection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME reflection COMMAND test_reflection)
