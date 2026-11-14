#include <array>
#include <cstddef>
#include <memory_resource>
#include <utility>
#include <vector>

#include "package.bb.hpp"

int main() {
  packaging::Entity entity;
  entity.id = bebop::MakeUInt128(42, 7);
  entity.labels = {"first", "last"};
  packaging::Snapshot snapshot;
  snapshot.groups.emplace();
  snapshot.groups->emplace(9, std::vector{std::move(entity)});

  auto bytes = bebop::Encode(snapshot);
  if (!bytes) {
    return 1;
  }
  std::array<std::byte, 8192> arena_storage;
  std::pmr::monotonic_buffer_resource arena(arena_storage.data(),
      arena_storage.size(),
      std::pmr::null_memory_resource());
  auto decoded = bebop::Decode<packaging::pmr::Snapshot>(*bytes, arena);
  if (!decoded || !decoded->groups ||
      decoded->groups->values().get_allocator().resource() != &arena) {
    return 1;
  }
  auto view = packaging::SnapshotView::Parse(*bytes);
  if (!view) {
    return 1;
  }
  std::array<std::size_t, 256> words;
  bebop::IndexBuffer index(words);
  auto cached = view->Index(index);
  if (!cached) {
    return 1;
  }
  auto groups = cached->groups();
  if (!groups) {
    return 1;
  }
  auto entities = groups->Find(9);
  if (!entities) {
    return 1;
  }
  auto first = entities->At(0);
  if (!first || first->id() != bebop::MakeUInt128(42, 7) ||
      first->labels().At(1) != "last") {
    return 1;
  }
  return 0;
}
