#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <type_traits>

#include "cpp.bb.h"
#include "cpp.bb.hpp"
#include "numeric.bb.hpp"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

static_assert(std::is_aggregate_v<game::EntityStateRef>);
static_assert(offsetof(game::EntityState, id) == 0);
static_assert(sizeof(game::EntityState) == 24);
static_assert(((game::Access::kRead | game::Access::kWrite) &
                  game::Access::kRead) == game::Access::kRead);
static_assert(std::is_aggregate_v<game::WorldSnapshotRef>);
static_assert(
    std::is_same_v<decltype(game::Metadata{}.label()), const std::string&>);
static_assert(numeric::kMinI64 == std::numeric_limits<std::int64_t>::min());
static_assert(numeric::kMaxU64 == std::numeric_limits<std::uint64_t>::max());
static_assert(numeric::kMaxU128 ==
              bebop::MakeUInt128(0xffffffffffffffffULL, 0xffffffffffffffffULL));
static_assert(numeric::kMinI128 == bebop::MakeInt128(0, 0x8000000000000000ULL));
static_assert(numeric::kNegI128 ==
              bebop::MakeInt128(0xffffffffffffffffULL, 0xfffffffffffffffeULL));
static_assert(std::to_underlying(numeric::BitExpressions::kAll) == 0xffffffffU);
static_assert(
    std::to_underlying(numeric::BitExpressions::kLegacy) == 0xfffffffeU);
static_assert(std::to_underlying(numeric::BitExpressions::kGroup) == 12);
static_assert(std::to_underlying(numeric::BitExpressions::kLeft) == 8);
static_assert(std::to_underlying(numeric::BitExpressions::kRight) == 8);
static_assert(std::to_underlying(numeric::SignedExpressions::kHalf) == -4);
static_assert(std::to_underlying(numeric::SignedExpressions::kAll) == -1);
static_assert(std::to_underlying(numeric::SignedExpressions::kGroup) == 3);
static_assert(std::to_underlying(numeric::SignedExpressions::kRecoveredMax) ==
              std::numeric_limits<std::int64_t>::max());
static_assert(std::to_underlying(numeric::SignedExpressions::kRecoveredMin) ==
              std::numeric_limits<std::int64_t>::min());
static_assert(std::to_underlying(numeric::WideExpressions::kRecovered) ==
              std::numeric_limits<std::uint64_t>::max());
static_assert(std::to_underlying(numeric::WideExpressions::kBorrow) == 1);

void CheckEnumExpressions() {
  numeric::ExpressionValues value;
  value.flags = numeric::BitExpressions::kAll;
  value.delta = numeric::SignedExpressions::kHalf;
  auto bytes = bebop::Encode(value);
  Check(bytes && bytes->size() == 12);
  const std::array<std::uint8_t, 12> expected{
      255, 255, 255, 255, 252, 255, 255, 255, 255, 255, 255, 255};
  Check(std::ranges::equal(*bytes, std::as_bytes(std::span(expected))));
  auto decoded = bebop::Decode<numeric::ExpressionValues>(*bytes);
  Check(decoded && *decoded == value);
  auto view = numeric::ExpressionValuesView::Parse(*bytes);
  Check(view && view->flags() == value.flags && view->delta() == value.delta);
  value.delta = numeric::SignedExpressions::kRecoveredMax;
  auto maximum = bebop::Encode(value);
  Check(maximum && maximum->size() == 12);
  const std::array<std::uint8_t, 12> expected_maximum{
      255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 127};
  Check(
      std::ranges::equal(*maximum, std::as_bytes(std::span(expected_maximum))));
  auto decoded_maximum = bebop::Decode<numeric::ExpressionValues>(*maximum);
  Check(decoded_maximum && *decoded_maximum == value);
}

void CheckHalfEquality() {
  const game::HalfValues value{
      .exact = static_cast<bebop::Float16>(1.5f),
      .approximate = static_cast<bebop::BFloat16>(2.5f),
  };
  auto bytes = bebop::Encode(value);
  Check(bytes && bytes->size() == 4);
  auto decoded = bebop::Decode<game::HalfValues>(*bytes);
  Check(decoded && *decoded == value);
  auto negative_zero = value;
  negative_zero.exact = bebop::Float16FromBits(0x8000);
  auto positive_zero = value;
  positive_zero.exact = bebop::Float16FromBits(0);
  Check(positive_zero == negative_zero);
  auto nan = value;
  nan.approximate = bebop::BFloat16FromBits(0x7fc0);
  Check(!(nan == value));
}

void CheckEntity() {
  const game::EntityState entity{
      .id = 42,
      .position = {1.0f, -2.0f, 3.5f},
      .enabled = true,
      .activity = game::Activity::kMoving,
  };
  auto encoded = bebop::Encode(entity);
  Check(encoded.has_value());
  Check(encoded->size() == 22);
  auto decoded = bebop::Decode<game::EntityState>(*encoded);
  Check(decoded.has_value() && *decoded == entity);
  const game::EntityStateRef borrowed{
      .enabled = entity.enabled,
      .id = entity.id,
      .position = entity.position,
      .activity = entity.activity,
  };
  std::array<std::byte, 22> storage{};
  auto written = bebop::EncodeTo(borrowed, storage);
  Check(written.has_value() && *written == storage.size());
  Check(std::ranges::equal(storage, *encoded));

  Bebop_Context* context = bebop_context_new(nullptr);
  Check(context != nullptr);
  Game_EntityState c_entity{};
  const Bebop_View view{
      reinterpret_cast<const std::uint8_t*>(encoded->data()), encoded->size()};
  Check(Game_EntityState_decode(context, view, &c_entity) == BEBOP_RESULT_OK);
  Check(c_entity.id == entity.id && c_entity.enabled == entity.enabled);
  Check(c_entity.position[1] == -2.0f);
  Bebop_Writer* writer = bebop_context_writer(context, 22);
  Check(writer != nullptr);
  Check(Game_EntityState_encode(writer, &c_entity) == BEBOP_RESULT_OK);
  const auto c_bytes = bebop_writer_view(writer);
  Check(c_bytes.length == encoded->size());
  Check(std::memcmp(c_bytes.data, encoded->data(), c_bytes.length) == 0);
  bebop_context_free(context);
}

void CheckSnapshot() {
  game::WorldSnapshot snapshot;
  snapshot.tick = 9001;
  snapshot.class_id = 0x8877665544332211ULL;
  snapshot.entities = {{.id = 7, .position = {1, 2, 3}, .enabled = true}};
  snapshot.metadata = game::Metadata({.label = std::string("scene\0name", 10),
      .created = {
          .seconds = 10, .nanoseconds = 20, .offset_milliseconds = 30}});
  snapshot.tables.emplace();
  (*snapshot.tables)["living"][7] = snapshot.entities->front();
  snapshot.grids.emplace(1);
  snapshot.grids->front().emplace(7, snapshot.entities->front());
  snapshot.names.emplace();
  snapshot.names->emplace("main", "village");
  snapshot.flags = {true, false, true};
  auto encoded = bebop::Encode(snapshot);
  Check(encoded.has_value());
  auto decoded = bebop::Decode<game::WorldSnapshot>(*encoded);
  Check(decoded.has_value() && *decoded == snapshot);
  for (std::size_t size = 0; size < encoded->size(); ++size) {
    Check(!bebop::Decode<game::WorldSnapshot>(std::span(*encoded).first(size)));
  }
  game::WorldSnapshotRef borrowed{
      .tick = snapshot.tick,
      .entities = std::span<const game::EntityState>(*snapshot.entities),
      .class_id = snapshot.class_id,
      .metadata =
          game::MetadataRef{
              .label = snapshot.metadata->label(),
              .created = snapshot.metadata->created(),
          },
  };
  game::WorldSnapshot subset;
  subset.tick = snapshot.tick;
  subset.entities = snapshot.entities;
  subset.class_id = snapshot.class_id;
  subset.metadata = snapshot.metadata;
  auto borrowed_bytes = bebop::Encode(borrowed);
  auto subset_bytes = bebop::Encode(subset);
  Check(borrowed_bytes && subset_bytes && *borrowed_bytes == *subset_bytes);
  const auto opaque = std::string("\x80\xff\0\xc0", 4);
  auto strings = bebop::Encode(game::Metadata({.label = opaque}));
  Check(strings.has_value());
  auto string_result = bebop::Decode<game::Metadata>(*strings);
  Check(string_result.has_value() && string_result->label() == opaque);
}

void CheckNested() {
  const game::Outer outer{
      .inner = game::Outer::Inner({.value = 51}),
      .mode = game::Outer::Mode::kSecond,
  };
  auto encoded = bebop::Encode(outer);
  Check(encoded.has_value());
  Check(encoded->size() == 5);
  auto decoded = bebop::Decode<game::Outer>(*encoded);
  Check(decoded.has_value() && decoded->inner.value() == 51);
  Check(decoded->mode == game::Outer::Mode::kSecond);
}

void CheckUnions() {
  auto variant = game::Variant::FromEntity({.id = 7});
  static_assert(
      std::same_as<decltype(variant.value), game::Variant::VariantType>);
  auto variant_bytes = bebop::Encode(variant);
  Check(variant_bytes.has_value());
  auto variant_decoded = bebop::Decode<game::Variant>(*variant_bytes);
  Check(variant_decoded && *variant_decoded == variant);

  const game::EntityState entity{.id = 17, .position = {1, 2, 3}};
  const std::array events{
      game::Event::FromMoved(entity),
      game::Event::FromCaption(game::Metadata({.label = "caption"})),
      game::Event::FromMovedAgain(entity),
      game::Event::FromBounce({.velocity = {3, 2, 1}}),
      game::Event::FromPrompt({.text = "hello"}),
      game::Event{
          .value = bebop::UnknownUnion{.discriminator = 222,
              .payload = {std::byte{0xff}, std::byte{0}, std::byte{0x81}}}},
  };
  for (const auto& event : events) {
    auto encoded = bebop::Encode(event);
    Check(encoded.has_value());
    auto decoded = bebop::Decode<game::Event>(*encoded);
    Check(decoded.has_value() && *decoded == event);
    Check(decoded->kind() == event.kind());
    for (std::size_t size = 0; size < encoded->size(); ++size) {
      Check(!bebop::Decode<game::Event>(std::span(*encoded).first(size)));
    }
  }
  auto moved = bebop::Encode(events[0]);
  Check(moved && moved->size() == 27);
  Check((*moved)[0] == std::byte{23} && (*moved)[4] == std::byte{1});
  const game::EntityStateRef entity_ref{
      .id = entity.id, .position = entity.position};
  auto borrowed = bebop::Encode(game::EventRef::FromMoved(entity_ref));
  Check(borrowed && *borrowed == *moved);
  game::Event invalid{.value = bebop::UnknownUnion{.discriminator = 1}};
  auto rejected = bebop::Encode(invalid);
  Check(!rejected && rejected.error().code == bebop::ErrorCode::kInvalidUnion);
  const std::array<std::byte, 5> zero_tag{std::byte{1}};
  Check(!bebop::Decode<game::Event>(zero_tag));
}

void CheckEmpty() {
  auto encoded = bebop::Encode(game::Empty{});
  Check(encoded && encoded->empty());
  Check(bebop::Decode<game::Empty>(*encoded).has_value());
  auto message = bebop::Encode(game::EmptyMessage{});
  Check(message && message->size() == 5);
  Check(bebop::Decode<game::EmptyMessage>(*message).has_value());
}

}  // namespace

int main() {
  CheckHalfEquality();
  CheckEnumExpressions();
  CheckEntity();
  CheckSnapshot();
  CheckEmpty();
  CheckNested();
  CheckUnions();
}
