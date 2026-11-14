#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <source_location>
#include <string>
#include <type_traits>

#include "cpp.bb.hpp"
#include "recursive.bb.hpp"
#include "reflection.bb.hpp"

namespace {

std::size_t allocations = 0;

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

using bebop::Describe;
using bebop::reflection::DefinitionKind;
using bebop::reflection::TypeKind;

static_assert(Describe<game::EntityState>().name == "EntityState");
static_assert(Describe<game::EntityState>().fields[0].name == "enabled");
static_assert(Describe<game::EntityState>().fixed_size == 22);
static_assert(Describe<game::EntityState>().fields[2].type->FixedSize() == 12);
static_assert(
    Describe<game::Activity>().underlying_type->kind == TypeKind::kByte);
static_assert(Describe<recursion::Node>().fields[1].type->definition ==
              &Describe<recursion::Node>());
static_assert(Describe<game::pmr::WorldSnapshot>().is_pmr);
static_assert(
    bebop::reflection::TypeOf<std::array<std::array<std::uint16_t, 4>, 2>>()
        .FixedSize() == 16);
static_assert(bebop::EnumName(game::Activity::kMoving) == "MOVING");
static_assert(
    bebop::EnumValue<game::Activity>("IDLE") == game::Activity::kIdle);
static_assert(bebop::EnumName(inspection::Alias::kAlias) == "ZERO");

void CheckMetadata() {
  const auto before = allocations;
  const auto& snapshot = Describe<game::WorldSnapshot>();
  Check(snapshot.kind == DefinitionKind::kMessage);
  Check(snapshot.full_name == "game.WorldSnapshot");
  Check(snapshot.cpp_name == "::game::WorldSnapshot");
  Check(snapshot.fields[2].name == "class" &&
        snapshot.fields[2].cpp_name == "class_id");
  Check(snapshot.fields[2].tag == 3);
  const auto& rename = snapshot.fields[2].decorators[0];
  Check(rename.name == "cpp.name");
  Check(
      std::get<std::string_view>(rename.exports[0].value.value) == "class_id");
  const auto& map = snapshot.fields[5];
  Check(map.type->kind == TypeKind::kMap &&
        map.type->key->kind == TypeKind::kString);
  Check(map.type->value->kind == TypeKind::kMap);
  Check(map.type->value->value->definition == &Describe<game::EntityState>());
  Check(std::get<std::string_view>(
            map.decorators[0].arguments[0].value.value) == "map");
  Check(Describe<game::Outer>().nested.size() == 2);
  Check(Describe<game::Outer::Inner>().full_name == "game.Outer.Inner");
  Check(Describe<game::Access>().is_flags);
  Check(!bebop::EnumName(game::Access::kRead | game::Access::kWrite));
  Check(!bebop::EnumValue<game::Activity>("missing"));
  Check(Describe<inspection::Signed>().members[0].value_bits ==
        (std::uint64_t{1} << 63));
  Check(Describe<inspection::Signed>().members[2].decorators[0].name ==
        "bebop.deprecated");
  Check(allocations == before);
}

void CheckVisitors() {
  game::EntityState value{.id = 17, .position = {1, 2, 3}, .enabled = true};
  const game::Metadata metadata({.label = "metadata"});
  std::array<std::byte, 128> storage;
  const auto before = allocations;
  std::size_t fields = 0;
  bebop::VisitFields(value, [&](const auto& field, auto& member) {
    static_assert(std::is_lvalue_reference_v<decltype(member)>);
    if (fields == 0) {
      Check(field.name == "enabled");
    }
    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(member)>,
                      std::uint64_t>) {
      member = 99;
    }
    ++fields;
  });
  Check(fields == 4 && value.id == 99);
  bebop::VisitFields(metadata, [&](const auto&, auto& member) {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(member)>>);
  });
  const game::EntityStateRef reference{.enabled = value.enabled,
      .id = value.id,
      .position = value.position,
      .activity = value.activity};
  fields = 0;
  bebop::VisitFields(reference, [&](const auto&, const auto&) {
    ++fields;
  });
  Check(fields == 4 && Describe(reference).is_borrowed);
  auto written = bebop::EncodeTo(value, storage);
  Check(written.has_value());
  auto view = game::EntityStateView::Parse(std::span(storage).first(*written));
  Check(view.has_value());
  fields = 0;
  bebop::VisitFields(*view, [&](const auto&, auto&&) {
    ++fields;
  });
  Check(fields == 4 && Describe(*view).is_view);
  game::WorldSnapshot message;
  message.tick = 42;
  message.flags.emplace();
  std::size_t present = 0;
  bebop::VisitPresentFields(message, [&](const auto& field, auto&) {
    Check(field.name == "tick" || field.name == "flags");
    ++present;
  });
  Check(present == 2);
  std::size_t empty = 0;
  bebop::VisitFields(message, [&](const auto&, const auto& field) {
    if (!field) {
      ++empty;
    }
  });
  Check(empty + present == Describe(message).fields.size());
  Check(allocations == before);
}

void CheckUnionVisitors() {
  auto event = game::Event::FromMovedAgain({.id = 23});
  std::array<std::byte, 128> storage;
  const auto before = allocations;
  const auto tag =
      bebop::VisitUnion(event, [&](const auto* branch, auto& payload) {
        Check(branch && branch->name == "MovedAgain");
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(payload)>,
                          game::EntityState>) {
          payload.id = 29;
        }
        return branch->discriminator;
      });
  Check(tag == 3 && std::get<2>(event.value).id == 29);
  const auto& constant = event;
  bebop::VisitUnion(constant, [&](const auto*, auto& payload) {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(payload)>>);
  });
  auto written = bebop::EncodeTo(event, storage);
  Check(written.has_value());
  auto view = game::EventView::Parse(std::span(storage).first(*written));
  Check(view.has_value());
  bebop::VisitUnion(*view, [&](const auto* branch, const auto&) {
    Check(branch && branch->discriminator == 3);
  });
  game::Event unknown{game::Event::Variant(
      std::in_place_index<5>, bebop::UnknownUnion{201, {}})};
  bebop::VisitUnion(unknown, [&](const auto* branch, const auto&) {
    Check(branch == nullptr);
  });
  Check(allocations == before);
}

void CheckDecoratorValues() {
  const auto before = allocations;
  const auto& definition = Describe<inspection::Values>();
  Check(definition.documentation == "Values exposed through reflection.");
  const auto& decorator = definition.decorators[0];
  Check(decorator.name == "inspection.metadata" &&
        decorator.arguments.size() == 8);
  const auto& integer =
      std::get<bebop::reflection::Integer>(decorator.arguments[0].value.value);
  Check(integer.magnitude ==
        bebop::MakeUInt128(~std::uint64_t{0}, ~std::uint64_t{0}));
  Check(!integer.negative);
  const auto& negative =
      std::get<bebop::reflection::Integer>(decorator.arguments[1].value.value);
  Check(negative.magnitude == bebop::MakeUInt128(0, std::uint64_t{1} << 63) &&
        negative.negative);
  Check(std::get<std::string_view>(decorator.arguments[2].value.value) ==
        std::string_view("left\0right", 10));
  Check(std::get<bebop::Uuid>(decorator.arguments[3].value.value).bytes[15] ==
        255);
  Check(
      std::get<bebop::Timestamp>(decorator.arguments[4].value.value).seconds ==
      0);
  const auto duration =
      std::get<bebop::Duration>(decorator.arguments[5].value.value);
  Check(duration.seconds == 1 && duration.nanoseconds == 500000000);
  Check(std::bit_cast<std::uint64_t>(std::get<double>(
            decorator.arguments[6].value.value)) == (std::uint64_t{1} << 63));
  Check(std::get<bool>(decorator.arguments[7].value.value));
  Check(decorator.exports.size() == 5);
  std::size_t typed_exports = 0;
  for (const auto& exported : decorator.exports) {
    if (exported.name == "id") {
      Check(std::get<bebop::Uuid>(exported.value.value).bytes[15] == 255);
      ++typed_exports;
    } else if (exported.name == "at") {
      Check(std::get<bebop::Timestamp>(exported.value.value).seconds == 0);
      ++typed_exports;
    } else if (exported.name == "delay") {
      const auto delay = std::get<bebop::Duration>(exported.value.value);
      Check(delay.seconds == 1 && delay.nanoseconds == 500000000);
      ++typed_exports;
    }
  }
  Check(typed_exports == 3);
  Check(allocations == before);
}

}  // namespace

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new(std::size_t size) {
  ++allocations;
  if (size == 0) {
    size = 1;
  }
  void* pointer = std::malloc(size);
  if (pointer == nullptr) {
    std::abort();
  }
  return pointer;
}
void* operator new[](std::size_t size) {
  return ::operator new(size);
}
void operator delete(void* pointer) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
  std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main() {
  CheckMetadata();
  CheckVisitors();
  CheckUnionVisitors();
  CheckDecoratorValues();
}
