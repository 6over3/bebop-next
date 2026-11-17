# Allow older CMake syntax from vendor projects
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

add_subdirectory(lua)

set(CYAML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CYAML_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CYAML_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(cyaml EXCLUDE_FROM_ALL)

set(LSP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LSP_INSTALL OFF CACHE BOOL "" FORCE)
set(LSP_USE_SANITIZERS OFF CACHE BOOL "" FORCE)
add_subdirectory(lsp-framework EXCLUDE_FROM_ALL)

if(MSVC)
    if(TARGET lsp)
        target_compile_options(lsp PRIVATE /W0)
    endif()
    if(TARGET lspgen)
        target_compile_options(lspgen PRIVATE /W0)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if(TARGET lsp)
        target_compile_options(lsp PRIVATE -Wno-error)
    endif()
    if(TARGET lspgen)
        target_compile_options(lspgen PRIVATE -Wno-error)
    endif()
endif()

# Suppress warnings for third-party code
if(MSVC)
    if(TARGET cyaml_static)
        target_compile_options(cyaml_static PRIVATE /W0)
    endif()
    if(TARGET lua)
        target_compile_options(lua PRIVATE /W0)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    if(TARGET cyaml_static)
        target_compile_options(cyaml_static PRIVATE
            -Wno-error -Wno-double-promotion -Wno-conversion -Wno-sign-conversion
        )
    endif()
    if(TARGET lua)
        target_compile_options(lua PRIVATE
            -Wno-error -Wno-conversion -Wno-sign-conversion -Wno-double-promotion
            -Wno-cast-qual -Wno-cast-align -Wno-redundant-decls -Wno-undef
            -Wno-shadow -Wno-missing-prototypes -Wno-strict-prototypes
            -Wno-pedantic -Wno-format -Wno-format-nonliteral
        )
    endif()
endif()

if(TARGET lua)
    set_target_properties(lua PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
endif()
