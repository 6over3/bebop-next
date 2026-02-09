find_program(SWIFT_EXECUTABLE swift)
if(NOT SWIFT_EXECUTABLE)
    message(STATUS "Swift not found, skipping bebopc-gen-swift")
    return()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SWIFT_BUILD_CONFIG release)
else()
    set(SWIFT_BUILD_CONFIG debug)
endif()

set(SWIFT_BUILD_DIR "${CMAKE_SOURCE_DIR}/.build/${SWIFT_BUILD_CONFIG}")
set(SWIFT_GEN_SWIFT "${SWIFT_BUILD_DIR}/bebopc-gen-swift")

add_custom_command(
    OUTPUT "${SWIFT_GEN_SWIFT}"
    COMMAND ${SWIFT_EXECUTABLE} build
        --product bebopc-gen-swift
        --configuration ${SWIFT_BUILD_CONFIG}
        --package-path "${CMAKE_SOURCE_DIR}"
    COMMENT "Building bebopc-gen-swift"
    VERBATIM
)

add_custom_target(bebopc-gen-swift ALL DEPENDS "${SWIFT_GEN_SWIFT}")
        
add_custom_command(
    TARGET bebopc-gen-swift POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SWIFT_GEN_SWIFT}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bebopc-gen-swift"
)

install(PROGRAMS "${SWIFT_GEN_SWIFT}" DESTINATION ${CMAKE_INSTALL_BINDIR})
