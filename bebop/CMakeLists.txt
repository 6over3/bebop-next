set(BEBOP_SOURCES
    src/bebop.c
)

set(BEBOP_HEADERS
    include/bebop.h
    include/bebop_wire.h
)

add_library(bebop STATIC ${BEBOP_SOURCES})
add_library(bebop::bebop ALIAS bebop)

target_include_directories(bebop
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/generated
        ${CMAKE_SOURCE_DIR}/vendor/cwisstable
        ${CMAKE_SOURCE_DIR}/vendor/lua
)

target_sources(bebop PRIVATE $<TARGET_OBJECTS:lua>)
target_link_libraries(bebop PRIVATE lua)

if(WIN32)
    target_compile_definitions(bebop PUBLIC BEBOP_STATIC_LIB)
endif()


# IDE support for unity build (clangd)
if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set(BEBOP_IMPL_FILES
        src/bebop_arena.c
        src/bebop_str.c
        src/bebop_utils.c
        src/bebop_schema.c
        src/bebop_scanner.c
        src/bebop_sema.c
        src/bebop_result.c
        src/bebop_parser.c
        src/bebop_macro.c
        src/bebop_emit.c
        src/bebop_validate.c
        src/bebop_wire.c
        src/bebop_descriptor.c
        src/bebop_plugin.c
    )
    add_library(bebop_ide OBJECT EXCLUDE_FROM_ALL ${BEBOP_IMPL_FILES})
    target_include_directories(bebop_ide PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/vendor/cwisstable
        ${CMAKE_SOURCE_DIR}/vendor/lua
    )
    target_compile_options(bebop_ide PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/bebop.c -w)
endif()

add_custom_command(TARGET bebop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BEBOP_OUTPUT_DIR}/include
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/include/bebop.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/bebop_wire.h
        ${BEBOP_OUTPUT_DIR}/include/
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BEBOP_OUTPUT_DIR}/share/bebop
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/schemas/bebop
        ${BEBOP_OUTPUT_DIR}/share/bebop
)

install(TARGETS bebop
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${BEBOP_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/schemas/bebop
    DESTINATION ${CMAKE_INSTALL_BINDIR}/include
)
