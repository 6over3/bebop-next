cmake_minimum_required(VERSION 3.25)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" BEBOP_VERSION)
string(STRIP "${BEBOP_VERSION}" BEBOP_VERSION)
string(REGEX REPLACE "[\r\n]+" "" BEBOP_VERSION "${BEBOP_VERSION}")

string(REGEX REPLACE "-.*$" "" BEBOP_VERSION_NUMERIC "${BEBOP_VERSION}")

project(bebop-vnext
    VERSION ${BEBOP_VERSION_NUMERIC}
    DESCRIPTION "Bebop schema compiler"
    LANGUAGES C CXX
)

include(GNUInstallDirs)

set(BEBOP_OUTPUT_DIR ${CMAKE_BINARY_DIR}/..)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BEBOP_OUTPUT_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BEBOP_OUTPUT_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BEBOP_OUTPUT_DIR}/lib)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${config} CONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} ${BEBOP_OUTPUT_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG} ${BEBOP_OUTPUT_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG} ${BEBOP_OUTPUT_DIR}/lib)
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED BEBOP_VERSION_MAJOR)
    set(BEBOP_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(BEBOP_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(BEBOP_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    if(BEBOP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+-(.+)$")
        set(BEBOP_VERSION_SUFFIX "${CMAKE_MATCH_1}")
    else()
        set(BEBOP_VERSION_SUFFIX "")
    endif()
endif()

if(BEBOP_VERSION_SUFFIX)
    set(BEBOP_VERSION_STRING "${BEBOP_VERSION_MAJOR}.${BEBOP_VERSION_MINOR}.${BEBOP_VERSION_PATCH}-${BEBOP_VERSION_SUFFIX}")
else()
    set(BEBOP_VERSION_STRING "${BEBOP_VERSION_MAJOR}.${BEBOP_VERSION_MINOR}.${BEBOP_VERSION_PATCH}")
endif()

message(STATUS "Bebop version: ${BEBOP_VERSION_STRING}")

add_compile_definitions(
    BEBOP_VERSION_MAJOR=${BEBOP_VERSION_MAJOR}
    BEBOP_VERSION_MINOR=${BEBOP_VERSION_MINOR}
    BEBOP_VERSION_PATCH=${BEBOP_VERSION_PATCH}
    BEBOP_VERSION_SUFFIX="${BEBOP_VERSION_SUFFIX}"
    BEBOP_VERSION_STRING="${BEBOP_VERSION_STRING}"
)

option(BEBOP_BUILD_TESTS "Build tests" ON)
option(BEBOP_BUILD_FRONTEND "Build bebopc frontend" ON)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wdouble-promotion
        -Wformat=2 -Wundef
        -Wcast-qual -Wcast-align
        -Wredundant-decls -Wnull-dereference
    )

    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wshadow>)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-format-zero-length)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    else()
        add_compile_options(-g3 -O0 -fno-omit-frame-pointer)
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /WX /utf-8 /FS)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /std:c11 /experimental:c11atomics")
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_options($<$<NOT:$<CONFIG:Release>>:/Od>
                        $<$<NOT:$<CONFIG:Release>>:/Z7>
                        $<$<NOT:$<CONFIG:Release>>:/fsanitize=address>)
endif()

add_subdirectory(vendor)
add_subdirectory(bebop)
add_subdirectory(beboplsp)

if(BEBOP_BUILD_FRONTEND)
    add_subdirectory(bebopc)
endif()
add_subdirectory(plugins)

if(BEBOP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
