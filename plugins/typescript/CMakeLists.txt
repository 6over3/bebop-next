find_program(BUN_EXECUTABLE bun)
if(NOT BUN_EXECUTABLE)
    message(STATUS "Bun not found, skipping bebopc-gen-typescript")
    return()
endif()

set(TYPESCRIPT_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(TYPESCRIPT_GENERATOR_DIR "${TYPESCRIPT_PLUGIN_DIR}/packages/generator")
set(TYPESCRIPT_GENERATOR "${TYPESCRIPT_GENERATOR_DIR}/dist/bebopc-gen-typescript${CMAKE_EXECUTABLE_SUFFIX}")

file(GLOB_RECURSE TYPESCRIPT_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${TYPESCRIPT_PLUGIN_DIR}/packages/*/src/*.ts"
    "${TYPESCRIPT_PLUGIN_DIR}/packages/*/package.json"
    "${TYPESCRIPT_PLUGIN_DIR}/package.json"
    "${TYPESCRIPT_PLUGIN_DIR}/bun.lock"
    "${TYPESCRIPT_PLUGIN_DIR}/tsconfig*.json"
)

add_custom_command(
    OUTPUT "${TYPESCRIPT_GENERATOR}"
    COMMAND "${BUN_EXECUTABLE}" install --frozen-lockfile
    COMMAND "${BUN_EXECUTABLE}" run --cwd packages/runtime build
    COMMAND "${BUN_EXECUTABLE}" run --cwd packages/plugin build
    COMMAND "${BUN_EXECUTABLE}" run --cwd packages/generator build:binary
    WORKING_DIRECTORY "${TYPESCRIPT_PLUGIN_DIR}"
    DEPENDS ${TYPESCRIPT_PLUGIN_SOURCES}
    COMMENT "Building bebopc-gen-typescript"
    VERBATIM
)

add_custom_target(bebopc-gen-typescript ALL
    DEPENDS "${TYPESCRIPT_GENERATOR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TYPESCRIPT_GENERATOR}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bebopc-gen-typescript${CMAKE_EXECUTABLE_SUFFIX}"
)

install(PROGRAMS "${TYPESCRIPT_GENERATOR}" DESTINATION ${CMAKE_INSTALL_BINDIR})
