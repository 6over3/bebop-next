# Bebop for C++

C++23 code generation and [`bebop.hpp`](include/bebop.hpp), a standalone,
single-header runtime. Requires a little-endian target.

Uses native 128-bit integers and 16-bit floats when the compiler supports them.

## Build

From the repository root:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --target bebopc bebopc-gen-cpp bebop-cpp-runtime -j
```

```sh
cmake --build build/cmake --target cpp_bootstrap
```

```sh
ctest --test-dir build/cmake -R '^cpp_bootstrap$' --output-on-failure
```

## Generate

`scene.bop`:

```bebop
package game

import "bebop/cpp.bop"
import "bebop/decorators.bop"

@flags
enum EntityFlags : uint16 {
    NONE = 0;
    ACTIVE = 1;
    VISIBLE = 2;
}

mut struct EntityState {
    id: uint64;
    position: float32[3];
    flags: EntityFlags;
}

struct Metadata {
    label: string;
}

message Snapshot {
    tick(1): uint32;
    entities(2): EntityState[];
    metadata(3): Metadata;
    @cpp.map(kind: "flat_map")
    grid(4): map[uint64, EntityState];
    @cpp.map(kind: "map")
    tables(5): map[string, map[uint64, EntityState]];
    @deprecated("Use metadata instead.")
    label(6): string;
}

union Event {
    Moved(1): EntityState;
    Snapshot(2): Snapshot;
}

message Node {
    value(1): uint32;
    next(2): Node;
    children(3): Node[];
    pair(4): Node[2];
}
```

```sh
build/bin/bebopc build scene.bop -I bebop/schemas \
  --plugin=cpp=build/bin/bebopc-gen-cpp \
  --cpp_out=generated -D emit_pmr=true
```

## Encode and decode

`example.cc`:

```cpp
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "scene.bb.hpp"

int main() {
  game::EntityState entity;
  entity.id = 42;
  entity.position = {1.0f, 2.0f, 3.0f};
  entity.flags = game::EntityFlags::kActive | game::EntityFlags::kVisible;

  game::Snapshot snapshot;
  snapshot.tick = 123;
  snapshot.entities = std::vector{entity};
  snapshot.metadata = game::Metadata({.label = "Arena"});

  // Structs without `mut` expose const accessors.
  assert(snapshot.metadata->label() == "Arena");

  auto bytes = bebop::Encode(snapshot);
  assert(bytes);

  auto decoded = bebop::Decode<game::Snapshot>(*bytes);
  assert(decoded);
  assert(decoded->tick == 123);
  assert(decoded->entities->front().id == 42);

  // Message fields distinguish absence from a present default value.
  snapshot.tick = 0;
  assert(snapshot.tick.has_value());
  snapshot.tick.reset();
  assert(!snapshot.tick.has_value());
  return 0;
}
```

```sh
c++ -std=c++23 -I plugins/cpp/include -I generated example.cc -o example
./example
```

```powershell
clang-cl /clang:-std=c++23 /EHsc /Zp16 /D_CRT_PACKING=16 /clang:--rtlib=compiler-rt /Iplugins/cpp/include /Igenerated example.cc
```

For an installed runtime:

```cmake
find_package(bebop-cpp CONFIG REQUIRED)
add_executable(example example.cc)
target_link_libraries(example PRIVATE bebop::cpp)
target_include_directories(example PRIVATE generated)
```

## Borrowed input

```cpp
// Ref fields follow schema order. Keep borrowed storage alive through EncodeTo.
std::array<game::EntityState, 2> entities{};
const game::SnapshotRef snapshot{
    .tick = 123,
    .entities = entities,
    .metadata = game::MetadataRef{.label = "Arena"},
};
std::array<std::byte, 8192> packet;
auto written = bebop::EncodeTo(snapshot, packet);
assert(written);
```

```cpp
const std::array entries{
    std::pair{std::uint64_t{42}, game::EntityState{}},
};
const game::SnapshotRef snapshot{.grid = entries};
std::array<std::byte, 1024> packet;
auto written = bebop::EncodeTo(snapshot, packet);
assert(written);
```

## Your buffers and allocators

```cpp
bebop::Result<std::size_t> WriteToMemory(
    const game::SnapshotRef& snapshot, void* memory, std::size_t capacity) {
  return bebop::EncodeTo(
      snapshot, std::span(static_cast<std::byte*>(memory), capacity));
}
```

Input storage, output storage, and traversal workspace must not overlap.
`EncodeTo` can leave partial output on failure.

## Errors and limits

```cpp
// Result<T> is std::expected<T, bebop::Error>.
const bebop::DecodeOptions limits{
    .max_depth = 64,
    .max_elements = 65'536,
    .max_bytes = 1024 * 1024,
};
auto decoded = bebop::Decode<game::Snapshot>(encoded, limits);
if (!decoded) {
  std::fprintf(stderr, "Error at byte %zu\n", decoded.error().offset);
}
```

Strings preserve arbitrary bytes; UTF-8 is not validated.

## Wire views

```cpp
auto view = game::SnapshotView::Parse(encoded);
assert(view);
assert(view->tick() == 123);

if (auto entities = view->entities()) {
  for (auto entity : *entities) {
    std::printf("entity %llu\n", static_cast<unsigned long long>(entity.id()));
    auto x = entity.position().At(0);
    assert(x.has_value());
  }
}

if (auto grid = view->grid()) {
  if (auto entity = grid->Find(42)) {
    assert(entity->id() == 42);
  }
}
```

```cpp
// Scratch for duplicate-key checks on unsorted maps; default: 256 entries.
std::array<std::size_t, 1024> key_offsets;
auto view = game::SnapshotView::Parse(encoded, {}, key_offsets);
```

```cpp
std::array<std::size_t, 8192> index_storage;
bebop::IndexBuffer index(index_storage);
auto cached = view->Index(index);  // Same view type; includes nested values.
assert(cached);
```

Views borrow the packet and any supplied index storage. Keep both alive and
unchanged. Without an index, variable-size array access and map lookup scan.
Parsing, indexing, and access do not allocate.

## PMR records

Generate with `-D emit_pmr=true` to add `game::pmr` types.

```cpp
std::array<std::byte, 64 * 1024> storage;
std::pmr::monotonic_buffer_resource arena(
    storage.data(), storage.size(), std::pmr::null_memory_resource());

auto decoded = bebop::Decode<game::pmr::Snapshot>(encoded, arena);
assert(decoded);

game::pmr::Snapshot snapshot(&arena);
snapshot.tick = 123;
snapshot.entities.emplace(&arena);
snapshot.entities->emplace_back();
snapshot.entities->back().id = 42;

auto bytes = bebop::Encode(snapshot, arena);
assert(bytes);
```

```cpp
std::pmr::polymorphic_allocator<std::byte> allocator(&arena);
auto bytes = bebop::Encode(snapshot, allocator);
```

The resource must outlive its records and buffers. Pass it when emplacing
optional containers.

## Recursive records

```cpp
game::Node root;
root.value = 1;
root.next = std::make_unique<game::Node>();
root.next->value = 2;
root.children.emplace(2);
root.pair = std::make_unique<std::array<game::Node, 2>>();

// Recursive owners are movable, not copyable.
auto moved = std::move(root);

// Borrowed recursive edges are pointers.
const game::NodeRef tail{.value = 2};
const game::NodeRef head{.value = 1, .next = &tail};

// PMR nodes use the same resource for the node and its nested allocations.
game::pmr::Node pmr_root(&arena);
pmr_root.next = bebop::AllocateUnique<game::pmr::Node>(arena);
```

```cpp
std::array<std::byte, 64 * 1024> traversal;
std::array<std::byte, 8192> packet;
const bebop::DecodeOptions limits{
    .max_depth = 128,
    .traversal_scratch = traversal,
};

auto written = bebop::EncodeTo(head, packet, limits);
assert(written);
auto bytes = std::span(packet).first(*written);
auto decoded = bebop::Decode<game::Node>(bytes, limits);
auto view = game::NodeView::Parse(bytes, limits);
assert(decoded && view);
```

Traversal is iterative; the default 16 KiB workspace returns `kScratchTooSmall`
when exhausted.

## Maps and names

```cpp
using Grid = std::flat_map<std::uint64_t, game::EntityState>;
using Tables =
    std::map<std::string, std::map<std::uint64_t, game::EntityState>>;

static_assert(
    std::same_as<decltype(game::Snapshot::grid), std::optional<Grid>>);
static_assert(
    std::same_as<decltype(game::Snapshot::tables), std::optional<Tables>>);
```

`@cpp.map` accepts `map`, `unordered_map`, or `flat_map`. It applies throughout
the field's inline arrays and maps, stopping at named record references.
The default is `unordered_map` with `bebop::Hash<Key>`.

```bebop
import "bebop/cpp.bop"

message External {
    @cpp.name(value: "class_id")
    class(1): uint64;
}
```

```cpp
// The field above becomes:
std::optional<std::uint64_t> class_id;
```

`@deprecated` emits compiler warnings. Deprecated message fields are retained
in C++ and reflection, skipped on the wire, and return `std::nullopt` from views.

## Unions

```cpp
auto event = game::Event::FromMoved(game::EntityState{.id = 42});
assert(event.kind() == game::Event::Kind::kMoved);
assert(std::get<game::Event::Moved>(event.value).id == 42);

// Unknown discriminators and their payloads survive decode/encode.
if (auto unknown = std::get_if<bebop::UnknownUnion>(&event.value)) {
  std::uint8_t tag = unknown->discriminator;
  std::span<const std::byte> payload = unknown->payload;
  std::printf("Unknown event %u: %zu bytes\n",
      static_cast<unsigned>(tag),
      payload.size());
}
```

## Reflection

```cpp
constexpr const auto& metadata = bebop::Describe<game::Snapshot>();
static_assert(metadata.full_name == "game.Snapshot");

bebop::VisitPresentFields(snapshot, [](const auto& field, const auto&) {
  std::printf("%.*s: tag %u\n",
      static_cast<int>(field.name.size()),
      field.name.data(),
      static_cast<unsigned>(field.tag));
});

auto name = bebop::EnumName(game::EntityFlags::kActive);  // "ACTIVE"
auto flag = bebop::EnumValue<game::EntityFlags>("VISIBLE");
assert(name && flag);
```

`VisitFields` also visits absent message fields. Visitors preserve constness;
reflection does not allocate.

## Generator options

```sh
bebopc build scene.bop --cpp_out=generated \
  -D namespace=simulation -D prefix=Net \
  -D runtime_header=vendor/bebop.hpp -D emit_pmr=true
```

| Option | Default | Accepted values |
| --- | --- | --- |
| `cpp_standard` | `c++23` | `23`, `c++23` |
| `namespace` | Schema package | Dot-separated or `::`-separated; empty for global |
| `prefix` | Empty | Type and constant name prefix |
| `runtime_header` | `bebop.hpp` | Include path |
| `emit_ref` | `true` | `true`, `false` |
| `emit_view` | `true` | `true`, `false` |
| `emit_pmr` | `false` | `true`, `false` |
| `emit_reflection` | `true` | `true`, `false` |
| `services` | `none` | `none`; RPC is not implemented |

## Test

From the repository root:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake --target cpp_tests -j
ctest --test-dir build/cmake -R '^cpp_' --output-on-failure
```

## Benchmark

```sh
cmake -S . -B build/benchmark/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/benchmark/cmake --target benchmark_cpp -j
build/benchmark/bin/benchmark_cpp
```

```sh
cmake -S . -B build/benchmark-lto/cmake -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build/benchmark-lto/cmake --target benchmark_cpp -j
build/benchmark-lto/bin/benchmark_cpp
```
