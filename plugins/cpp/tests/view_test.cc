#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <ranges>
#include <source_location>
#include <string>
#include <vector>

#include "bebop.hpp"

namespace fixture {

struct TrackedString {};
std::size_t skipped_strings = 0;

}  // namespace fixture

namespace bebop::internal {

template <>
struct ViewTraits<fixture::TrackedString> : StringViewTraits {
  static void SkipTrusted(Reader& reader) noexcept {
    ++fixture::skipped_strings;
    StringViewTraits::SkipTrusted(reader);
  }
};

}  // namespace bebop::internal

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

void CheckScalars() {
  for (std::size_t offset = 0; offset < 16; ++offset) {
    std::array<std::byte, 96> storage{};
    bebop::Writer writer{std::span(storage).subspan(offset)};
    Check(writer.Write(std::uint32_t{3}).has_value());
    Check(writer.Write(std::uint32_t{1}).has_value());
    Check(writer.Write(std::uint32_t{0xffffffff}).has_value());
    Check(writer.Write(std::uint32_t{42}).has_value());
    const auto bytes = writer.written();
    WithoutAllocations([&] {
      auto view = bebop::ParseView<std::vector<std::uint32_t>>(bytes);
      Check(view.has_value());
      Check(view->size() == 3 && !view->empty());
      Check(view->encoded().data() == bytes.data());
      Check(view->At(0) == 1 && view->At(1) == 0xffffffff && view->At(2) == 42);
      Check(!view->At(3));
      std::array<std::size_t, 128> words;
      bebop::IndexBuffer buffer(words);
      auto indexed = view->Index(buffer);
      Check(indexed && indexed->At(0) == 1 && indexed->At(2) == 42);
      Check(!indexed->At(3));
      std::uint64_t sum = 0;
      for (auto value : *view) {
        sum += value;
      }
      Check(sum == 0x10000002aULL);
    });
    for (std::size_t length = 0; length < bytes.size(); ++length) {
      Check(!bebop::ParseView<std::vector<std::uint32_t>>(bytes.first(length)));
    }
  }
  static_assert(std::ranges::input_range<bebop::ArrayView<std::uint32_t>>);
}

void CheckStrings() {
  std::array<std::byte, 128> storage;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{3}).has_value());
  Check(writer.WriteString("first").has_value());
  Check(writer.WriteString(std::string_view("a\0b\xff", 4)).has_value());
  Check(writer.WriteString("").has_value());
  WithoutAllocations([&] {
    auto view = bebop::ParseView<std::vector<std::string>>(writer.written());
    Check(view.has_value());
    Check(view->At(1) == std::string_view("a\0b\xff", 4));
    std::size_t characters = 0;
    for (std::string_view value : *view) {
      characters += value.size();
      Check(value.data() >= reinterpret_cast<const char*>(storage.data()));
      Check(value.data() <
            reinterpret_cast<const char*>(storage.data() + storage.size()));
    }
    Check(characters == 9);
    auto fixed = bebop::ParseView<std::array<std::string, 3>>(
        writer.written().subspan(4));
    Check(fixed.has_value());
    std::array<std::size_t, 128> words;
    bebop::IndexBuffer buffer(words);
    auto indexed_fixed = fixed->Index(buffer);
    Check(indexed_fixed && indexed_fixed->At(0) == "first" &&
          indexed_fixed->At(2) == "");
  });
  storage[13] = std::byte{1};
  auto invalid = bebop::ParseView<std::vector<std::string>>(writer.written());
  Check(!invalid && invalid.error().code == bebop::ErrorCode::kInvalidString);
}

void CheckStringIndex() {
  std::array<std::byte, 128> storage;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{3}).has_value());
  Check(writer.WriteString("first").has_value());
  Check(writer.WriteString(std::string_view("a\0b\xff", 4)).has_value());
  Check(writer.WriteString("").has_value());
  std::array<std::size_t, 128> words;
  bebop::IndexBuffer buffer(words);
  words.fill(777);
  WithoutAllocations([&] {
    auto view =
        bebop::ParseView<std::vector<fixture::TrackedString>>(writer.written());
    Check(view.has_value());
    std::array<std::size_t, 3> small;
    small.fill(777);
    bebop::IndexBuffer tiny(small);
    auto insufficient = view->Index(tiny);
    Check(!insufficient &&
          insufficient.error().code == bebop::ErrorCode::kBufferTooSmall);
    Check(tiny.used() == 0);
    fixture::skipped_strings = 0;
    auto indexed = view->Index(buffer);
    Check(indexed && indexed->size() == 3 && !indexed->empty());
    Check(fixture::skipped_strings == 3);
    Check(buffer.used() == 7 && words[7] == 777);
    static_assert(std::same_as<decltype(view), decltype(indexed)>);
    for (std::size_t index = 0; index < 100; ++index) {
      Check(indexed->At(2) == "");
      Check(indexed->At(0) == "first");
      const auto middle = indexed->At(1);
      Check(middle == std::string_view("a\0b\xff", 4));
      Check(
          middle->data() == reinterpret_cast<const char*>(storage.data() + 18));
      Check(!indexed->At(3));
    }
    Check(fixture::skipped_strings == 3);
    Check(indexed->encoded().data() == writer.written().data());
    std::size_t characters = 0;
    for (auto value : *indexed) {
      characters += value.size();
    }
    Check(characters == 9);
  });
  static_assert(
      std::ranges::input_range<bebop::ArrayView<fixture::TrackedString>>);
}

void CheckNestedArrayIndex() {
  std::array<std::byte, 128> storage;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.WriteString("a").has_value());
  Check(writer.WriteString("longer").has_value());
  Check(writer.Write(std::uint32_t{1}).has_value());
  Check(writer.WriteString("last").has_value());
  using Nested = std::vector<std::vector<fixture::TrackedString>>;
  std::array<std::size_t, 128> words;
  bebop::IndexBuffer buffer(words);
  WithoutAllocations([&] {
    auto view = bebop::ParseView<Nested>(writer.written());
    Check(view.has_value());
    auto tree = view->Index(buffer);
    Check(tree.has_value());
    fixture::skipped_strings = 0;
    Check(tree->At(0)->At(1) == "longer");
    Check(tree->At(1)->At(0) == "last");
    Check(fixture::skipped_strings == 0);
    const auto used = buffer.used();
    auto limited = view->Index(buffer, {.max_depth = 1});
    Check(!limited && limited.error().code == bebop::ErrorCode::kDepthLimit);
    Check(buffer.used() == used && tree->At(0)->At(0) == "a");
    auto byte_limited = view->Index(buffer, {.max_bytes = 1});
    Check(!byte_limited &&
          byte_limited.error().code == bebop::ErrorCode::kOverflow);
    Check(buffer.used() == used);
    std::array<std::byte, 1> scratch;
    auto exhausted = view->Index(buffer, {.traversal_scratch = scratch});
    Check(!exhausted &&
          exhausted.error().code == bebop::ErrorCode::kScratchTooSmall);
    Check(buffer.used() == used);
    std::array<std::size_t, 4> small;
    bebop::IndexBuffer tiny(small);
    auto insufficient = view->Index(tiny);
    Check(!insufficient &&
          insufficient.error().code == bebop::ErrorCode::kBufferTooSmall);
    Check(tiny.used() == 0);
  });
}

void CheckNestedMapIndex() {
  std::array<std::byte, 256> storage;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{2}).has_value());
  for (std::uint32_t key : {2u, 1u}) {
    Check(writer.Write(key).has_value());
    Check(writer.Write(std::uint32_t{2}).has_value());
    Check(writer.WriteString("first").has_value());
    Check(writer.WriteString("last").has_value());
  }
  using Map = std::map<std::uint32_t, std::vector<std::string>>;
  std::array<std::size_t, 128> words;
  bebop::IndexBuffer buffer(words);
  WithoutAllocations([&] {
    auto map = bebop::ParseView<Map>(writer.written());
    Check(map.has_value());
    auto indexed = map->Index(buffer);
    Check(indexed && indexed->Find(1)->At(1) == "last");
    Check(indexed->Find(2)->At(0) == "first" && !indexed->Find(3));
    std::size_t sum = 0;
    for (auto [key, values] : *indexed) {
      sum += key;
      Check(values.At(0) == "first");
    }
    Check(sum == 3);
  });
}

void CheckFixedAndNested() {
  std::array<std::byte, 128> storage;
  bebop::Writer fixed_writer(storage);
  Check(fixed_writer.Write(std::uint16_t{7}).has_value());
  Check(fixed_writer.Write(std::uint16_t{11}).has_value());
  WithoutAllocations([&] {
    auto fixed =
        bebop::ParseView<std::array<std::uint16_t, 2>>(fixed_writer.written());
    Check(fixed && fixed->encoded().size() == 4);
    Check(fixed->At(1) == 11);
    std::array<std::size_t, 128> words;
    bebop::IndexBuffer buffer(words);
    auto indexed = fixed->Index(buffer);
    Check(indexed && indexed->At(1) == 11);
    Check(buffer.used() == 0);
  });
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.Write(std::uint32_t{1}).has_value());
  Check(writer.Write(std::uint16_t{17}).has_value());
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.Write(std::uint16_t{31}).has_value());
  Check(writer.Write(std::uint16_t{51}).has_value());
  using Nested = std::vector<std::vector<std::uint16_t>>;
  WithoutAllocations([&] {
    auto nested = bebop::ParseView<Nested>(writer.written());
    Check(nested && nested->size() == 2);
    std::size_t sum = 0;
    for (auto inner : *nested) {
      for (auto value : inner) {
        sum += value;
      }
    }
    Check(sum == 99);
    Check(nested->At(1)->At(1) == 51);
  });
  auto depth = bebop::ParseView<Nested>(writer.written(), {.max_depth = 1});
  Check(!depth && depth.error().code == bebop::ErrorCode::kDepthLimit);
  auto elements =
      bebop::ParseView<Nested>(writer.written(), {.max_elements = 4});
  Check(!elements && elements.error().code == bebop::ErrorCode::kElementLimit);
  auto bytes = bebop::ParseView<Nested>(writer.written(), {.max_bytes = 1});
  Check(!bytes && bytes.error().code == bebop::ErrorCode::kOverflow);
}

void CheckSpecialValues() {
  std::array<std::byte, 128> storage;
  bebop::Writer writer(storage);
  const bebop::Timestamp expected{1234, 5678, -32400000};
  Check(writer.Write(std::uint32_t{1}).has_value());
  Check(writer.WriteTimestamp(expected).has_value());
  WithoutAllocations([&] {
    auto values =
        bebop::ParseView<std::vector<bebop::Timestamp>>(writer.written());
    Check(values && values->At(0) == expected);
  });
  bebop::Writer integers(storage);
  const auto integer =
      bebop::MakeUInt128(0xfedcba9876543210ULL, 0x0123456789abcdefULL);
  Check(integers.Write(std::uint32_t{1}).has_value());
  Check(integers.Write(integer).has_value());
  WithoutAllocations([&] {
    auto values =
        bebop::ParseView<std::vector<bebop::UInt128>>(integers.written());
    Check(values && values->At(0) == integer);
  });
  const std::array<std::byte, 7> boolean_bytes{std::byte{3},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{2},
      std::byte{255},
      std::byte{0}};
  WithoutAllocations([&] {
    auto values = bebop::ParseView<std::vector<bool>>(boolean_bytes);
    Check(values && values->At(0) == true && values->At(1) == true &&
          values->At(2) == false);
  });
}

void CheckMaps() {
  std::array<std::byte, 256> storage;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{3}).has_value());
  for (std::uint32_t key : {3u, 1u, 2u}) {
    Check(writer.Write(key).has_value());
    Check(writer.WriteString("value").has_value());
  }
  using Map = std::unordered_map<std::uint32_t, std::string>;
  WithoutAllocations([&] {
    auto map = bebop::ParseView<Map>(writer.written());
    Check(map && map->size() == 3);
    Check(map->Find(1) == "value" && !map->Find(4));
    std::uint32_t sum = 0;
    for (auto [key, value] : *map) {
      sum += key;
      Check(value == "value");
    }
    Check(sum == 6);
    std::array<std::size_t, 128> words;
    bebop::IndexBuffer buffer(words);
    bebop::IndexBuffer tiny{std::span(words).first(2)};
    auto too_small = map->Index(tiny);
    Check(!too_small &&
          too_small.error().code == bebop::ErrorCode::kBufferTooSmall);
    auto indexed = map->Index(buffer);
    Check(indexed && indexed->size() == 3);
    Check(indexed->Find(1) == "value" && indexed->Find(3) == "value");
    Check(!indexed->Find(0) && !indexed->Find(4));
  });
  for (std::size_t length = 0; length < writer.position(); ++length) {
    Check(!bebop::ParseView<Map>(writer.written().first(length)));
  }
  bebop::Writer duplicates(storage);
  Check(duplicates.Write(std::uint32_t{4}).has_value());
  for (std::uint32_t key : {3u, 1u, 2u, 1u}) {
    Check(duplicates.Write(key).has_value());
    Check(duplicates.WriteString("x").has_value());
  }
  auto duplicate = bebop::ParseView<Map>(duplicates.written());
  Check(!duplicate &&
        duplicate.error().code == bebop::ErrorCode::kDuplicateMapKey);
}

void CheckMapScratch() {
  constexpr std::uint32_t count = 1024;
  std::array<std::byte, 4 + count * 8> storage;
  std::array<std::size_t, count> scratch;
  using Map = std::map<std::uint32_t, std::uint32_t>;
  bebop::Writer writer(storage);
  Check(writer.Write(count).has_value());
  for (std::uint32_t index = count; index > 0; --index) {
    Check(writer.Write(index).has_value());
    Check(writer.Write(index * 2).has_value());
  }
  WithoutAllocations([&] {
    auto small = bebop::ParseView<Map>(writer.written());
    Check(!small && small.error().code == bebop::ErrorCode::kScratchTooSmall);
    auto map = bebop::ParseView<Map>(writer.written(), {}, scratch);
    Check(map && map->Find(count) == count * 2);
    scratch.fill(0);
    Check(map->Find(1) == 2);
  });
  bebop::Writer sorted(storage);
  Check(sorted.Write(count).has_value());
  for (std::uint32_t index = 0; index < count; ++index) {
    Check(sorted.Write(index).has_value());
    Check(sorted.Write(index * 3).has_value());
  }
  WithoutAllocations([&] {
    auto map = bebop::ParseView<Map>(
        sorted.written(), {}, std::span(scratch).first(1));
    Check(map && map->Find(count - 1) == (count - 1) * 3);
  });
}

void CheckNestedMapScratch() {
  constexpr std::uint32_t count = 300;
  std::array<std::byte, 4 + 2 * (8 + count * 8)> storage;
  std::array<std::size_t, count> scratch;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{2}).has_value());
  for (std::uint32_t outer : {2u, 1u}) {
    Check(writer.Write(outer).has_value());
    Check(writer.Write(count).has_value());
    for (std::uint32_t inner = count; inner > 0; --inner) {
      Check(writer.Write(inner).has_value());
      Check(writer.Write(inner + outer).has_value());
    }
  }
  using Map = std::map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>>;
  WithoutAllocations([&] {
    auto map = bebop::ParseView<Map>(writer.written(), {}, scratch);
    Check(map && map->Find(1)->Find(count) == count + 1);
  });
}

void CheckWideMapKeys() {
  std::array<std::byte, 128> storage;
  bebop::Writer writer{std::span(storage).subspan(1)};
  const auto large =
      bebop::MakeUInt128(0xffffffffffffffffULL, 0xffffffffffffffffULL);
  const auto small = bebop::MakeUInt128(1, 0);
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.Write(large).has_value());
  Check(writer.WriteString("large").has_value());
  Check(writer.Write(small).has_value());
  Check(writer.WriteString("small").has_value());
  WithoutAllocations([&] {
    auto map = bebop::ParseView<std::map<bebop::UInt128, std::string>>(
        writer.written());
    Check(map && map->Find(large) == "large" && map->Find(small) == "small");
    std::array<std::size_t, 128> words;
    bebop::IndexBuffer buffer(words);
    auto indexed = map->Index(buffer);
    Check(indexed && indexed->Find(large) == "large" &&
          indexed->Find(small) == "small");
  });
}

void CheckEmptyAndTrailing() {
  constexpr std::array<std::byte, 4> empty{};
  auto view = bebop::ParseView<std::vector<std::uint32_t>>(empty);
  Check(view && view->empty() && view->begin() == view->end());
  Check(!view->At(0));
  bebop::IndexBuffer buffer({});
  auto indexed = view->Index(buffer);
  Check(indexed && indexed->empty() && !indexed->At(0));
  Check(buffer.used() == 0 && indexed->begin() == indexed->end());
  constexpr std::array<std::byte, 5> trailing{};
  auto invalid = bebop::ParseView<std::vector<std::uint32_t>>(trailing);
  Check(!invalid && invalid.error().code == bebop::ErrorCode::kTrailingData);
  constexpr std::array<std::byte, 4> huge{
      std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
  WithoutAllocations([&] {
    auto rejected = bebop::ParseView<std::vector<std::uint32_t>>(huge);
    Check(
        !rejected && rejected.error().code == bebop::ErrorCode::kElementLimit);
  });
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
  CheckScalars();
  CheckStrings();
  CheckStringIndex();
  CheckNestedArrayIndex();
  CheckNestedMapIndex();
  CheckFixedAndNested();
  CheckSpecialValues();
  CheckEmptyAndTrailing();
  CheckMaps();
  CheckMapScratch();
  CheckNestedMapScratch();
  CheckWideMapKeys();
}
