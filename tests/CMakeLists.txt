if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unity/src/unity.c")
    message(STATUS "Unity not found - run: git submodule update --init")
    return()
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(unity PRIVATE -Wno-double-promotion)
elseif(MSVC)
    target_compile_options(unity PRIVATE /W0)
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
    if(MSVC)
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
bebop_add_internal_test(macro)
bebop_add_internal_test(descriptor)
bebop_add_internal_test(plugin)
bebop_add_internal_test(service)

# Public API tests
bebop_add_test(emit)
bebop_add_test(resolve)
bebop_add_test(map)
bebop_add_test(bebop_rt)

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
