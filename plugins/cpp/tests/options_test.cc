#include <cstdlib>
#include <type_traits>

#include "cpp.bb.hpp"
#include "numeric.bb.hpp"

static_assert(std::is_same_v<decltype(test::options::OptionEntityState{}.id),
    std::uint64_t>);
static_assert(test::options::kOptionMaxU64 == 0xffffffffffffffffULL);
static_assert(std::is_same_v<test::options::OptionActivity,
    test::options::pmr::OptionActivity>);
static_assert(!bebop::reflection::Reflected<test::options::OptionEntityState>);
static_assert(
    !bebop::reflection::Reflected<test::options::pmr::OptionEntityState>);

int main() {
  test::options::OptionEntityState entity;
  entity.id = 17;
  entity.position = {1, 2, 3};
  auto encoded = bebop::Encode(entity);
  if (!encoded) {
    std::abort();
  }
  auto decoded = bebop::Decode<test::options::OptionEntityState>(*encoded);
  if (!decoded || *decoded != entity) {
    std::abort();
  }
  test::options::OptionOuter nested{
      .inner = test::options::OptionOuter::Inner({.value = 51}),
      .mode = test::options::OptionOuter::Mode::kSecond,
  };
  auto nested_bytes = bebop::Encode(nested);
  if (!nested_bytes) {
    std::abort();
  }
  auto nested_value = bebop::Decode<test::options::OptionOuter>(*nested_bytes);
  if (!nested_value || *nested_value != nested) {
    std::abort();
  }
  std::pmr::monotonic_buffer_resource resource;
  test::options::pmr::OptionWorldSnapshot snapshot(&resource);
  snapshot.tick = 29;
  snapshot.names.emplace(&resource);
  snapshot.names->emplace("name", "value");
  auto snapshot_bytes = bebop::Encode(snapshot);
  if (!snapshot_bytes) {
    std::abort();
  }
  auto snapshot_value = bebop::Decode<test::options::pmr::OptionWorldSnapshot>(
      *snapshot_bytes, resource);
  if (!snapshot_value || snapshot_value->tick != 29u ||
      snapshot_value->names->get_allocator().resource() != &resource) {
    std::abort();
  }
}
