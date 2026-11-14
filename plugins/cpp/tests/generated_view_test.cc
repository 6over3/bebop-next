#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <source_location>
#include <type_traits>

#include "cpp.bb.hpp"

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

template <typename Body>
void WithoutAllocations(Body&& body) {
  const auto before = allocations;
  body();
  Check(allocations == before);
}

static_assert(!std::is_default_constructible_v<game::EntityStateView>);
static_assert(
    sizeof(game::EntityStateView) == sizeof(std::span<const std::byte>));
static_assert(sizeof(game::WorldSnapshotView) ==
              sizeof(bebop::MessageIndex) + sizeof(const std::size_t*));

void CheckSnapshot() {
  game::WorldSnapshot snapshot;
  snapshot.tick = 71;
  snapshot.entities = {{.id = 19, .position = {1, 2, 3}, .enabled = true}};
  snapshot.metadata = game::Metadata({.label = std::string("scene\0name", 10)});
  snapshot.names.emplace();
  snapshot.names->emplace("one", "value");
  snapshot.names->emplace("two", std::string("\x80\xff", 2));
  snapshot.tables.emplace();
  (*snapshot.tables)["living"][19] = snapshot.entities->front();
  snapshot.grids.emplace(1);
  snapshot.grids->front().emplace(19, snapshot.entities->front());
  snapshot.flags = {true, false, true};
  snapshot.events = {game::Event::FromMoved(snapshot.entities->front()),
      game::Event::FromPrompt({.text = "ready"})};
  auto encoded = bebop::Encode(snapshot);
  Check(encoded.has_value());
  WithoutAllocations([&] {
    auto view = game::WorldSnapshotView::Parse(*encoded);
    Check(view.has_value());
    Check(view->encoded().data() == encoded->data());
    Check(view->tick() == snapshot.tick);
    Check(!view->class_id());
    Check(view->metadata()->label() == snapshot.metadata->label());
    const auto entities = view->entities();
    Check(entities && entities->size() == 1);
    for (auto entity : *entities) {
      Check(entity.id() == 19 && entity.enabled());
      Check(entity.position().At(2) == 3.0f);
    }
    Check(view->names()->Find("two") == std::string_view("\x80\xff", 2));
    Check(view->tables()->Find("living")->Find(19)->position().At(1) == 2.0f);
    Check(view->grids()->At(0)->Find(19)->id() == 19);
    Check(view->flags()->At(0) == true && view->flags()->At(1) == false);
    const auto events = view->events();
    Check(events && events->size() == 2);
    Check(std::get<0>(events->At(0)->value()).id() == 19);
    Check(std::get<4>(events->At(1)->value()).text() == "ready");
    std::array<std::size_t, 512> event_offsets;
    bebop::IndexBuffer event_offsets_buffer(event_offsets);
    auto indexed_events = events->Index(event_offsets_buffer);
    Check(indexed_events.has_value());
    Check(std::get<4>(indexed_events->At(1)->value()).text() == "ready");
    Check(std::get<0>(indexed_events->At(0)->value()).id() == 19);
    std::array<std::size_t, 512> grid_offsets;
    bebop::IndexBuffer grid_offsets_buffer(grid_offsets);
    auto indexed_grids = view->grids()->Index(grid_offsets_buffer);
    Check(indexed_grids && indexed_grids->At(0)->Find(19)->id() == 19);
    std::array<std::size_t, 512> words;
    bebop::IndexBuffer buffer(words);
    auto cached = view->Index(buffer);
    static_assert(std::same_as<decltype(view), decltype(cached)>);
    Check(cached && cached->tick() == snapshot.tick);
    Check(cached->metadata()->label() == snapshot.metadata->label());
    Check(cached->tables()->Find("living")->Find(19)->id() == 19);
    Check(cached->grids()->At(0)->Find(19)->position().At(2) == 3.0f);
    Check(cached->names()->Find("two") == std::string_view("\x80\xff", 2));
    Check(std::get<4>(cached->events()->At(1)->value()).text() == "ready");
    for (auto grid : *cached->grids()) {
      Check(grid.Find(19)->id() == 19);
    }
    Check(!cached->class_id());
    const auto used = buffer.used();
    auto limited = view->Index(buffer, {.max_elements = 1});
    Check(!limited && limited.error().code == bebop::ErrorCode::kElementLimit);
    Check(buffer.used() == used && cached->names()->Find("one") == "value");
  });
  for (std::size_t size = 0; size < encoded->size(); ++size) {
    Check(!game::WorldSnapshotView::Parse(std::span(*encoded).first(size)));
  }
  auto limited = game::WorldSnapshotView::Parse(*encoded, {.max_elements = 1});
  Check(!limited && limited.error().code == bebop::ErrorCode::kElementLimit);
}

void CheckVariableStruct() {
  game::Inventory inventory;
  inventory.description = "equipment";
  inventory.tags = {"weapon", "rare"};
  inventory.slots[0][42] = {"sword", "shield"};
  inventory.slots[1][7] = {"potion"};
  inventory.revision = 91;
  auto encoded = bebop::Encode(inventory);
  Check(encoded.has_value());
  WithoutAllocations([&] {
    auto view = game::InventoryView::Parse(*encoded);
    Check(view && view->revision() == 91);
    std::array<std::size_t, 256> words;
    bebop::IndexBuffer buffer(words);
    auto cached = view->Index(buffer);
    static_assert(std::same_as<decltype(view), decltype(cached)>);
    Check(cached.has_value());
    for (int repeat = 0; repeat < 100; ++repeat) {
      Check(cached->description() == "equipment");
      Check(cached->tags().At(1) == "rare");
      Check(cached->slots().At(0)->Find(42)->At(1) == "shield");
      Check(cached->slots().At(1)->Find(7)->At(0) == "potion");
      Check(cached->revision() == 91);
    }
    for (auto slots : cached->slots()) {
      for (auto [key, names] : slots) {
        Check(key == 42 || key == 7);
        Check(!names.At(0)->empty());
      }
    }
  });
}

void CheckNestedAndEmpty() {
  const game::Outer value{
      .inner = game::Outer::Inner({.value = 99}),
      .mode = game::Outer::Mode::kSecond,
  };
  auto encoded = bebop::Encode(value);
  Check(encoded.has_value());
  WithoutAllocations([&] {
    auto view = game::OuterView::Parse(*encoded);
    Check(view && view->inner().value() == 99);
    Check(view->mode() == game::Outer::Mode::kSecond);
  });
  auto empty_message = bebop::Encode(game::EmptyMessage{});
  Check(empty_message.has_value());
  WithoutAllocations([&] {
    Check(game::EmptyView::Parse({}).has_value());
    auto view = game::EmptyMessageView::Parse(*empty_message);
    Check(view && view->encoded().size() == 5);
    auto empty_records = bebop::ParseView<std::array<game::Empty, 3>>({});
    Check(empty_records.has_value());
    std::array<std::size_t, 8> words;
    bebop::IndexBuffer buffer(words);
    auto indexed = empty_records->Index(buffer);
    Check(indexed && indexed->size() == 3);
    Check(indexed->At(2)->encoded().empty());
    Check(buffer.used() == 7);
  });
}

void CheckUnions() {
  const std::array events{
      game::Event::FromMoved({.id = 33, .position = {1, 2, 3}}),
      game::Event::FromBounce({.velocity = {3, 2, 1}}),
      game::Event::FromPrompt({.text = "prompt"}),
      game::Event{.value = bebop::UnknownUnion{.discriminator = 200,
                      .payload = {std::byte{0}, std::byte{0xff}}}},
  };
  for (const auto& event : events) {
    auto encoded = bebop::Encode(event);
    Check(encoded.has_value());
    WithoutAllocations([&] {
      auto view = game::EventView::Parse(*encoded);
      Check(view && view->kind() == event.kind());
      const auto value = view->value();
      Check(value.index() == event.value.index());
      if (value.index() == 0) {
        Check(std::get<0>(value).id() == 33);
      } else if (value.index() == 3) {
        Check(std::get<3>(value).velocity().At(0) == 3.0f);
      } else if (value.index() == 4) {
        Check(std::get<4>(value).text() == "prompt");
      } else {
        const auto& unknown = std::get<bebop::UnknownUnionRef>(value);
        Check(unknown.discriminator == 200 && unknown.payload.size() == 2);
      }
    });
  }
}

void CheckFieldValidation() {
  std::array<std::byte, 128> storage;
  const std::array<std::uint8_t, 1> tags{1};
  const std::array<std::uint32_t, 1> offsets{0};
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{0}).has_value());
  Check(writer.Write(std::uint8_t{7}).has_value());
  Check(bebop::FinishMessage(writer, 0, tags, offsets).has_value());
  auto bad_field = game::WorldSnapshotView::Parse(writer.written());
  Check(!bad_field && bad_field.error().code == bebop::ErrorCode::kTruncated);

  bebop::Writer unknown(storage);
  Check(unknown.Write(std::uint32_t{0}).has_value());
  Check(unknown.Write(std::uint32_t{42}).has_value());
  Check(unknown.Write(std::uint8_t{0xff}).has_value());
  const std::array<std::uint8_t, 2> unknown_tags{1, 250};
  const std::array<std::uint32_t, 2> unknown_offsets{0, 4};
  Check(bebop::FinishMessage(unknown, 0, unknown_tags, unknown_offsets)
          .has_value());
  auto valid = game::WorldSnapshotView::Parse(unknown.written());
  Check(valid && valid->tick() == 42);
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
  CheckSnapshot();
  CheckVariableStruct();
  CheckNestedAndEmpty();
  CheckUnions();
  CheckFieldValidation();
}
