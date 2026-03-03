include(CheckLanguage)
check_language(Swift)
if(CMAKE_Swift_COMPILER)
    get_filename_component(SWIFT_BIN_DIR "${CMAKE_Swift_COMPILER}" DIRECTORY)
    find_program(SWIFT_EXECUTABLE swift HINTS "${SWIFT_BIN_DIR}")
else()
    find_program(SWIFT_EXECUTABLE swift)
endif()
if(NOT SWIFT_EXECUTABLE)
    message(STATUS "Swift not found, skipping bebopc-gen-swift")
    return()
endif()

execute_process(
    COMMAND "${SWIFT_EXECUTABLE}" --version
    OUTPUT_VARIABLE SWIFT_VERSION_OUTPUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(SWIFT_VERSION_OUTPUT MATCHES "Swift version ([0-9]+)\\.([0-9]+)")
    set(SWIFT_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(SWIFT_VERSION_MINOR ${CMAKE_MATCH_2})
    if(SWIFT_VERSION_MAJOR LESS 6 OR (SWIFT_VERSION_MAJOR EQUAL 6 AND SWIFT_VERSION_MINOR LESS 2))
        message(STATUS "Swift ${SWIFT_VERSION_MAJOR}.${SWIFT_VERSION_MINOR} found, need 6.2+, skipping bebopc-gen-swift")
        return()
    endif()
else()
    message(STATUS "Could not determine Swift version, skipping bebopc-gen-swift")
    return()
endif()

set(SWIFT_BUILD_CONFIG "$<IF:$<CONFIG:Release>,release,debug>")
set(SWIFT_BUILD_DIR "${CMAKE_SOURCE_DIR}/.build/${SWIFT_BUILD_CONFIG}")
set(SWIFT_GEN_SWIFT "${SWIFT_BUILD_DIR}/bebopc-gen-swift${CMAKE_EXECUTABLE_SUFFIX}")

add_custom_target(bebopc-gen-swift ALL
    COMMAND ${SWIFT_EXECUTABLE} build
        --product bebopc-gen-swift
        --configuration ${SWIFT_BUILD_CONFIG}
        --package-path "${CMAKE_SOURCE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SWIFT_GEN_SWIFT}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bebopc-gen-swift${CMAKE_EXECUTABLE_SUFFIX}"
    COMMENT "Building bebopc-gen-swift"
    VERBATIM
)

install(PROGRAMS "${SWIFT_GEN_SWIFT}" DESTINATION ${CMAKE_INSTALL_BINDIR})
