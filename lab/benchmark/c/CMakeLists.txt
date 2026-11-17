cmake_minimum_required(VERSION 3.25)
project(bebop_serialization_benchmarks C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)
FetchContent_MakeAvailable(benchmark)

option(BENCH_BEBOP    "Benchmark Bebop"      ON)
option(BENCH_PROTOBUF "Benchmark Protobuf-C" OFF)
option(BENCH_MSGPACK  "Benchmark MessagePack" OFF)
option(BENCH_SIMDJSON "Benchmark simdjson" OFF)

if(BENCH_BEBOP)
  set(BEBOP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
  add_library(bebop_runtime STATIC
    ${BEBOP_ROOT}/bebop/src/bebop_wire.c
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/benchmark.bb.c
  )
  target_include_directories(bebop_runtime PUBLIC
    ${BEBOP_ROOT}/bebop/include
    ${BEBOP_ROOT}/vendor/cwisstable
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
  )
  target_compile_definitions(bebop_runtime PRIVATE BEBOP_RUNTIME_ONLY=1 BEBOP_WIRE_SINGLE_THREADED=1)
  target_compile_options(bebop_runtime PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -flto>
  )
endif()

if(BENCH_PROTOBUF)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

  set(PROTO_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
  set(PROTO_SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/schemas/benchmark.proto")
  set(PROTO_GENERATED_C "${PROTO_GENERATED_DIR}/benchmark.pb-c.c")
  set(PROTO_GENERATED_H "${PROTO_GENERATED_DIR}/benchmark.pb-c.h")

  add_custom_command(
    OUTPUT ${PROTO_GENERATED_C} ${PROTO_GENERATED_H}
    COMMAND protoc --c_out=${PROTO_GENERATED_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/schemas benchmark.proto
    DEPENDS ${PROTO_SCHEMA}
    COMMENT "Generating protobuf-c code from benchmark.proto"
  )

  add_library(protobuf_generated STATIC
    ${PROTO_GENERATED_C}
    ${PROTO_GENERATED_DIR}/google/protobuf/duration.pb-c.c
    ${PROTO_GENERATED_DIR}/google/protobuf/timestamp.pb-c.c
  )
  target_include_directories(protobuf_generated PUBLIC
    ${PROTOBUF_C_INCLUDE_DIRS}
    ${PROTO_GENERATED_DIR}
  )
  target_link_directories(protobuf_generated PUBLIC ${PROTOBUF_C_LIBRARY_DIRS})
  target_link_libraries(protobuf_generated PUBLIC ${PROTOBUF_C_LIBRARIES})
endif()

if(BENCH_MSGPACK)
  FetchContent_Declare(
    msgpack
    GIT_REPOSITORY https://github.com/msgpack/msgpack-c.git
    GIT_TAG        c-6.1.0
  )
  set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(msgpack)
endif()

if(BENCH_SIMDJSON)
  FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v4.2.4
  )
  FetchContent_MakeAvailable(simdjson)
endif()

add_library(bench_harness STATIC
  src/harness.cpp
  src/test_data.c
)
target_include_directories(bench_harness PUBLIC include)
target_link_libraries(bench_harness PUBLIC benchmark::benchmark)

set(BENCH_SOURCES "")
set(BENCH_LIBS "")

if(BENCH_BEBOP)
  list(APPEND BENCH_SOURCES src/bench_bebop.cpp)
  list(APPEND BENCH_LIBS bebop_runtime)
endif()

if(BENCH_PROTOBUF)
  list(APPEND BENCH_SOURCES src/bench_protobuf.cpp)
  list(APPEND BENCH_LIBS protobuf_generated)
endif()

if(BENCH_MSGPACK)
  list(APPEND BENCH_SOURCES src/bench_msgpack.cpp)
  list(APPEND BENCH_LIBS msgpack-c)
endif()

if(BENCH_SIMDJSON)
  list(APPEND BENCH_SOURCES src/bench_simdjson.cpp)
  list(APPEND BENCH_LIBS simdjson)
endif()

add_executable(run_benchmarks
  src/main.cpp
  ${BENCH_SOURCES}
)
target_link_libraries(run_benchmarks PRIVATE
  bench_harness
  ${BENCH_LIBS}
)

if(BENCH_BEBOP)
  target_compile_definitions(run_benchmarks PRIVATE BENCH_BEBOP=1)
endif()
if(BENCH_PROTOBUF)
  target_compile_definitions(run_benchmarks PRIVATE BENCH_PROTOBUF=1)
endif()
if(BENCH_MSGPACK)
  target_compile_definitions(run_benchmarks PRIVATE BENCH_MSGPACK=1)
endif()
if(BENCH_SIMDJSON)
  target_compile_definitions(run_benchmarks PRIVATE BENCH_SIMDJSON=1)
endif()

target_compile_options(run_benchmarks PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -flto>
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -flto>
)
target_link_options(run_benchmarks PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-flto>
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-flto>
)

add_custom_target(bench
  COMMAND run_benchmarks --benchmark_format=console
  DEPENDS run_benchmarks
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(bench-json
  COMMAND run_benchmarks --benchmark_format=json --benchmark_out=${CMAKE_CURRENT_SOURCE_DIR}/results/benchmark.json
  DEPENDS run_benchmarks
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Wire size comparison tool
if(BENCH_BEBOP AND BENCH_PROTOBUF AND BENCH_MSGPACK)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ZSTD libzstd)
  pkg_check_modules(LZ4 liblz4)
  pkg_check_modules(BROTLI libbrotlienc)
  find_package(Snappy QUIET)

  add_executable(wire_sizes src/wire_sizes.cpp)
  target_link_libraries(wire_sizes PRIVATE
    bench_harness
    bebop_runtime
    protobuf_generated
    msgpack-c
  )
  target_compile_definitions(wire_sizes PRIVATE BENCH_BEBOP=1 BENCH_PROTOBUF=1 BENCH_MSGPACK=1)

  if(ZSTD_FOUND)
    target_compile_definitions(wire_sizes PRIVATE USE_ZSTD=1)
    target_include_directories(wire_sizes PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_directories(wire_sizes PRIVATE ${ZSTD_LIBRARY_DIRS})
    target_link_libraries(wire_sizes PRIVATE ${ZSTD_LIBRARIES})
  endif()

  if(LZ4_FOUND)
    target_compile_definitions(wire_sizes PRIVATE USE_LZ4=1)
    target_include_directories(wire_sizes PRIVATE ${LZ4_INCLUDE_DIRS})
    target_link_directories(wire_sizes PRIVATE ${LZ4_LIBRARY_DIRS})
    target_link_libraries(wire_sizes PRIVATE ${LZ4_LIBRARIES})
  endif()

  if(BROTLI_FOUND)
    target_compile_definitions(wire_sizes PRIVATE USE_BROTLI=1)
    target_include_directories(wire_sizes PRIVATE ${BROTLI_INCLUDE_DIRS})
    target_link_directories(wire_sizes PRIVATE ${BROTLI_LIBRARY_DIRS})
    target_link_libraries(wire_sizes PRIVATE ${BROTLI_LIBRARIES})
  endif()

  if(Snappy_FOUND)
    target_compile_definitions(wire_sizes PRIVATE USE_SNAPPY=1)
    target_link_libraries(wire_sizes PRIVATE Snappy::snappy)
  endif()

  target_compile_options(wire_sizes PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native>
  )
endif()
