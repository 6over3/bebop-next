set(BEBOPC_SOURCES
    src/main.c
    src/bebopc.c
    src/bebopc_utils.c
    src/bebopc_error.c
    src/bebopc_dir.c
    src/bebopc_glob.c
    src/bebopc_log.c
    src/bebopc_diag.c
    src/cli/bebopc_cli.c
    src/cli/bebopc_config.c
    src/cli/bebopc_runner.c
    src/cli/bebopc_cli_completion.c
    src/cli/bebopc_compile.c
    src/watcher/bebop_fsw.c
)

add_executable(bebopc ${BEBOPC_SOURCES})

target_link_libraries(bebopc PRIVATE cyaml::cyaml beboplsp)

target_include_directories(bebopc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    target_sources(bebopc PRIVATE src/watcher/bebop_fsw_windows.c)
    target_compile_definitions(bebopc PRIVATE BEBOPC_WINDOWS=1)
    target_link_libraries(bebopc PRIVATE shlwapi)
elseif(APPLE)
    target_sources(bebopc PRIVATE src/watcher/bebop_fsw_darwin.c)
    target_compile_definitions(bebopc PRIVATE BEBOPC_POSIX=1 BEBOPC_MACOS=1)
    target_link_libraries(bebopc PRIVATE "-framework Security" "-framework CoreServices")
elseif(UNIX)
    target_sources(bebopc PRIVATE src/watcher/bebop_fsw_linux.c)
    target_compile_definitions(bebopc PRIVATE BEBOPC_POSIX=1 BEBOPC_LINUX=1)
    target_link_libraries(bebopc PRIVATE m)
endif()

install(TARGETS bebopc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
