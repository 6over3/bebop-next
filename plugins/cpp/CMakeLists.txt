add_executable(bebopc-gen-cpp src/generator.cc src/options.cc)
target_compile_features(bebopc-gen-cpp PRIVATE cxx_std_23)
install(TARGETS bebopc-gen-cpp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_library(bebop_cpp INTERFACE)
add_library(bebop::cpp ALIAS bebop_cpp)
set_target_properties(bebop_cpp PROPERTIES EXPORT_NAME cpp)
target_compile_features(bebop_cpp INTERFACE cxx_std_23)
target_include_directories(bebop_cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(bebopc-gen-cpp PRIVATE bebop::cpp)

install(TARGETS bebop_cpp EXPORT BebopCppTargets)
install(FILES include/bebop.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT BebopCppTargets NAMESPACE bebop::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bebop-cpp
)
install(FILES cmake/bebop-cpp-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bebop-cpp
)

add_custom_target(bebop-cpp-runtime ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BEBOP_OUTPUT_DIR}/include
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/include/bebop.hpp
        ${BEBOP_OUTPUT_DIR}/include/bebop.hpp
)

if(TARGET bebopc)
    add_custom_target(cpp_bootstrap
        COMMAND $<TARGET_FILE:bebopc> build
                ${CMAKE_SOURCE_DIR}/bebop/schemas/bebop/descriptor.bop
                ${CMAKE_SOURCE_DIR}/bebop/schemas/bebop/plugin.bop
                -I ${CMAKE_SOURCE_DIR}/bebop/schemas
                --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
                --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}/src/generated
                -D emit_ref=false -D emit_view=false -D emit_reflection=false --quiet
        DEPENDS bebopc bebopc-gen-cpp
        VERBATIM
    )

    set(BEBOP_CPP_BENCHMARK_DIR "${CMAKE_CURRENT_BINARY_DIR}/benchmarks")
    set(BEBOP_CPP_BENCHMARK_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/snapshot.bop")
    add_custom_command(
        OUTPUT "${BEBOP_CPP_BENCHMARK_DIR}/snapshot.bb.hpp"
               "${BEBOP_CPP_BENCHMARK_DIR}/snapshot.bb.h"
               "${BEBOP_CPP_BENCHMARK_DIR}/snapshot.bb.c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BEBOP_CPP_BENCHMARK_DIR}"
        COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_BENCHMARK_SCHEMA}"
                --plugin=cpp=$<TARGET_FILE:bebopc-gen-cpp>
                --cpp_out=${BEBOP_CPP_BENCHMARK_DIR} -D emit_pmr=true --quiet
        COMMAND $<TARGET_FILE:bebopc> build "${BEBOP_CPP_BENCHMARK_SCHEMA}"
                --plugin=c=$<TARGET_FILE:bebopc-gen-c>
                --c_out=${BEBOP_CPP_BENCHMARK_DIR} --quiet
        DEPENDS bebopc bebopc-gen-cpp bebopc-gen-c "${BEBOP_CPP_BENCHMARK_SCHEMA}"
        VERBATIM
    )
    add_executable(benchmark_cpp EXCLUDE_FROM_ALL benchmarks/benchmark.cc
        "${BEBOP_CPP_BENCHMARK_DIR}/snapshot.bb.hpp"
        "${BEBOP_CPP_BENCHMARK_DIR}/snapshot.bb.c")
    target_link_libraries(benchmark_cpp PRIVATE bebop::cpp bebop::bebop)
    target_include_directories(benchmark_cpp PRIVATE "${BEBOP_CPP_BENCHMARK_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(benchmark_cpp PRIVATE -Wno-c99-extensions -Wno-c11-extensions)
    endif()
endif()
