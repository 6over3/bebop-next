#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory_resource>
#include <source_location>

#include "bebop.hpp"

namespace fixture {

enum class TaggedByte : std::uint8_t {
  kValue = 7,
};

struct Recursive;

struct References {
  bebop::MapRef<std::uint32_t, Recursive> values;
};

struct Recursive {
  std::uint32_t value = 0;
};

struct Record {
  std::uint32_t id = 0;
  std::vector<std::uint16_t> values{};
  std::vector<std::string> names{};

  friend bool operator==(const Record&, const Record&) = default;
};

struct MoveTracked {
  static inline std::size_t moves = 0;
  std::uint32_t value = 0;

  MoveTracked() = default;
  MoveTracked(const MoveTracked&) = delete;
  MoveTracked& operator=(const MoveTracked&) = delete;

  MoveTracked(MoveTracked&& other) noexcept : value(other.value) {
    ++moves;
  }

  MoveTracked& operator=(MoveTracked&& other) noexcept {
    value = other.value;
    ++moves;
    return *this;
  }
};

}  // namespace fixture

namespace bebop {

template <>
struct Codec<fixture::TaggedByte> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 2;

  static Result<std::size_t> Size(fixture::TaggedByte, internal::Traversal&) {
    return 2;
  }

  static Result<void> Encode(
      Writer& writer, fixture::TaggedByte value, internal::Traversal&) {
    return writer.Write(
        static_cast<std::uint16_t>(0xa500u | std::to_underlying(value)));
  }

  static Result<void> Decode(
      Reader& reader, fixture::TaggedByte& value, internal::Traversal&) {
    auto raw = reader.Read<std::uint16_t>();
    if (!raw) {
      return std::unexpected(raw.error());
    }
    if ((*raw & 0xff00u) != 0xa500u) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage});
    }
    value = static_cast<fixture::TaggedByte>(*raw & 0xffu);
    return {};
  }
};

template <>
struct Codec<fixture::Record> {
  static constexpr bool kRecord = true;
  static constexpr std::size_t kMinSize = 12;

  [[nodiscard]] static Result<std::size_t> Size(
      const fixture::Record& value, internal::Traversal& context) {
    auto values_size =
        Codec<std::vector<std::uint16_t>>::Size(value.values, context);
    if (!values_size) {
      return std::unexpected(values_size.error());
    }

    auto names_size =
        Codec<std::vector<std::string>>::Size(value.names, context);
    if (!names_size) {
      return std::unexpected(names_size.error());
    }

    return sizeof(value.id) + *values_size + *names_size;
  }

  [[nodiscard]] static Result<void> Encode(Writer& writer,
      const fixture::Record& value,
      internal::Traversal& context) {
    auto id_result = Codec<std::uint32_t>::Encode(writer, value.id, context);
    if (!id_result) {
      return id_result;
    }

    auto values_result = Codec<std::vector<std::uint16_t>>::Encode(
        writer, value.values, context);
    if (!values_result) {
      return values_result;
    }

    return Codec<std::vector<std::string>>::Encode(
        writer, value.names, context);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, fixture::Record& value, internal::Traversal& context) {
    auto id_result = Codec<std::uint32_t>::Decode(reader, value.id, context);
    if (!id_result) {
      return id_result;
    }

    auto values_result = Codec<std::vector<std::uint16_t>>::Decode(
        reader, value.values, context);
    if (!values_result) {
      return values_result;
    }

    return Codec<std::vector<std::string>>::Decode(
        reader, value.names, context);
  }
};

template <>
struct Codec<fixture::MoveTracked> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 4;

  static Result<void> Decode(Reader& reader,
      fixture::MoveTracked& value,
      internal::Traversal& context) {
    return Codec<std::uint32_t>::Decode(reader, value.value, context);
  }
};

}  // namespace bebop

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

void CheckScalars() {
  for (std::size_t offset = 0; offset < 16; ++offset) {
    std::array<std::byte, 128> storage{};
    const auto buffer = std::span(storage).subspan(offset);
    bebop::Writer writer(buffer);
    Check(writer.Write(true).has_value());
    Check(writer.Write(std::uint32_t{0x78563412}).has_value());
    Check(writer.Write(std::int64_t{-2}).has_value());
    Check(writer.Write(1.0f).has_value());
    const auto integer =
        bebop::MakeUInt128(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
    Check(writer.Write(integer).has_value());
    Check(writer.Write(bebop::Float16FromBits(0x3555)).has_value());
    Check(writer.Write(bebop::BFloat16FromBits(0x3f81)).has_value());

    constexpr std::array<std::uint8_t, 5> prefix{1, 0x12, 0x34, 0x56, 0x78};
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      Check(std::to_integer<std::uint8_t>(buffer[i]) == prefix[i]);
    }
    Check(buffer[13] == std::byte{0});
    Check(buffer[15] == std::byte{0x80});
    Check(buffer[16] == std::byte{0x3f});
    Check(buffer[17] == std::byte{0xef});
    Check(buffer[32] == std::byte{0xfe});

    bebop::Reader reader(writer.written());
    Check(*reader.Read<bool>());
    Check(*reader.Read<std::uint32_t>() == 0x78563412);
    Check(*reader.Read<std::int64_t>() == -2);
    Check(*reader.Read<float>() == 1.0f);
    Check(*reader.Read<bebop::UInt128>() == integer);
    Check(bebop::Float16Bits(*reader.Read<bebop::Float16>()) == 0x3555);
    Check(bebop::BFloat16Bits(*reader.Read<bebop::BFloat16>()) == 0x3f81);
    Check(reader.remaining() == 0);
    Check(reader.Read<std::uint32_t>().error().code ==
          bebop::ErrorCode::kTruncated);
  }
  const std::array bytes{std::byte{0xff}};
  bebop::Reader reader(bytes);
  Check(*reader.Read<bool>());
}

void CheckStrings() {
  std::array<std::byte, 64> storage{};
  bebop::Writer writer(storage);
  const std::string_view text{"A\0B\xf0\x9f\x8e\xb7", 7};
  Check(writer.WriteString(text).has_value());
  Check(writer.position() == 12);
  Check(writer.WriteString("").has_value());
  bebop::Reader reader(writer.written());
  const auto decoded = reader.ReadString();
  Check(decoded.has_value() && *decoded == text);
  Check(decoded->data() == reinterpret_cast<const char*>(storage.data() + 4));
  Check(reader.ReadString()->empty());
  Check(reader.remaining() == 0);

  const std::array<std::string_view, 7> binary_strings{"\x80",
      "\xc0\x80",
      "\xe0\x80\x80",
      "\xed\xa0\x80",
      "\xf4\x90\x80\x80",
      "\xf5\x80\x80\x80",
      "\xf0\x9f"};
  for (auto value : binary_strings) {
    bebop::Writer binary_writer(storage);
    Check(binary_writer.WriteString(value).has_value());
    bebop::Reader binary_reader(binary_writer.written());
    auto binary_value = binary_reader.ReadString();
    Check(binary_value.has_value());
    Check(*binary_value == value);
    Check(binary_reader.remaining() == 0);
  }

  std::array<std::byte, 6> unterminated{std::byte{1},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{'a'},
      std::byte{1}};
  bebop::Reader invalid_reader(unterminated, 10);
  auto result = invalid_reader.ReadString();
  Check(!result &&
        result.error() == bebop::Error{bebop::ErrorCode::kInvalidString, 15});
  Check(invalid_reader.position() == 0);

  std::array<std::byte, 5> truncated{std::byte{0xff},
      std::byte{0xff},
      std::byte{0xff},
      std::byte{0xff},
      std::byte{0}};
  bebop::Reader truncated_reader(truncated);
  Check(truncated_reader.ReadString().error().code ==
        bebop::ErrorCode::kTruncated);
}

void CheckBoundaries() {
  std::array<std::byte, 3> small{};
  bebop::Writer writer(small);
  Check(writer.Write(std::uint32_t{42}).error().code ==
        bebop::ErrorCode::kBufferTooSmall);
  Check(writer.position() == 0);
  Check(
      writer.WriteString("").error().code == bebop::ErrorCode::kBufferTooSmall);
  Check(writer.position() == 0);
  Check(writer.WriteBytes({}).has_value());
  Check(writer.Write(std::uint8_t{9}).has_value());
  Check(writer.Patch(0, std::uint8_t{3}).has_value());
  Check(writer.Patch(0, std::uint32_t{1}).error().code ==
        bebop::ErrorCode::kTruncated);
  Check(small[0] == std::byte{3});
  bebop::Reader reader(small, 100);
  auto region = reader.ReadRegion(2);
  Check(region.has_value());
  Check(region->absolute_position() == 100 && region->remaining() == 2);
  Check(reader.absolute_position() == 102);
  Check(reader.ReadBytes(2).error().offset == 102);
}

void CheckSpecialTypes() {
  std::array<std::byte, 64> storage{};
  bebop::Writer writer(storage);
  const bebop::Timestamp timestamp{123, 999999999, -32400000};
  const bebop::Duration duration{-2, -500000000};
  bebop::Uuid uuid;
  for (std::size_t i = 0; i < uuid.bytes.size(); ++i) {
    uuid.bytes[i] = static_cast<std::uint8_t>(i);
  }
  Check(writer.WriteTimestamp(timestamp).has_value());
  Check(writer.position() == 16);
  Check(writer.WriteDuration(duration).has_value());
  Check(writer.position() == 28);
  Check(writer.WriteUuid(uuid).has_value());
  Check(writer.position() == 44);
  bebop::Reader reader(writer.written());
  Check(*reader.ReadTimestamp() == timestamp);
  Check(*reader.ReadDuration() == duration);
  Check(*reader.ReadUuid() == uuid);
  Check(bebop::Hash<bebop::UInt128>{}(bebop::MakeUInt128(1, 2)) !=
        bebop::Hash<bebop::UInt128>{}(bebop::MakeUInt128(2, 1)));
}

void CheckHalfConversions() {
  for (std::uint32_t bits = 0; bits <= 0xffffu; ++bits) {
    const auto half = static_cast<std::uint16_t>(bits);
    const auto single = static_cast<float>(bebop::Float16FromBits(half));
    const auto encoded =
        bebop::Float16Bits(static_cast<bebop::Float16>(single));
    if ((half & 0x7c00u) == 0x7c00u && (half & 0x3ffu) != 0) {
      Check(std::isnan(single));
      Check((encoded & 0x7c00u) == 0x7c00u && (encoded & 0x3ffu) != 0);
    } else {
      Check(encoded == half);
    }
  }
  Check(bebop::Float16Bits(static_cast<bebop::Float16>(1.00048828125f)) ==
        0x3c00);
  Check(bebop::Float16Bits(static_cast<bebop::Float16>(1.00146484375f)) ==
        0x3c02);
  Check(bebop::Float16Bits(static_cast<bebop::Float16>(65520.0f)) == 0x7c00);
  Check(bebop::Float16Bits(static_cast<bebop::Float16>(-0.0f)) == 0x8000);
  Check(bebop::Float16FromBits(0) == bebop::Float16FromBits(0x8000));
  Check(bebop::Float16FromBits(0x3c00) == static_cast<bebop::Float16>(1.0f));
  Check(!(bebop::Float16FromBits(0x7e00) == bebop::Float16FromBits(0x7e00)));
}

void CheckBFloatConversions() {
  for (std::uint32_t bits = 0; bits <= 0xffffu; ++bits) {
    const auto narrow_bits = static_cast<std::uint16_t>(bits);
    const auto wide = static_cast<float>(bebop::BFloat16FromBits(narrow_bits));
    const auto encoded =
        bebop::BFloat16Bits(static_cast<bebop::BFloat16>(wide));
    if ((narrow_bits & 0x7f80u) == 0x7f80u && (narrow_bits & 0x7fu) != 0) {
      Check(std::isnan(wide));
      Check((encoded & 0x7f80u) == 0x7f80u && (encoded & 0x7fu) != 0);
    } else {
      Check(encoded == narrow_bits);
    }
  }
  Check(
      bebop::BFloat16Bits(static_cast<bebop::BFloat16>(1.00390625f)) == 0x3f80);
  Check(
      bebop::BFloat16Bits(static_cast<bebop::BFloat16>(1.01171875f)) == 0x3f82);
  Check(bebop::BFloat16Bits(static_cast<bebop::BFloat16>(-0.0f)) == 0x8000);
  Check(bebop::BFloat16FromBits(0) == bebop::BFloat16FromBits(0x8000));
  Check(bebop::BFloat16FromBits(0x3f80) == static_cast<bebop::BFloat16>(1.0f));
  Check(!(bebop::BFloat16FromBits(0x7fc0) == bebop::BFloat16FromBits(0x7fc0)));
}

void CheckNarrowFloatBits() {
  for (std::uint32_t bits = 0; bits <= 0xffffu; ++bits) {
    const auto pattern = static_cast<std::uint16_t>(bits);
    Check(bebop::Float16Bits(bebop::Float16FromBits(pattern)) == pattern);
    Check(bebop::BFloat16Bits(bebop::BFloat16FromBits(pattern)) == pattern);

    std::array<std::byte, 8> bytes;
    bebop::Writer writer(bytes);
    Check(writer.Write(bebop::Float16FromBits(pattern)).has_value());
    Check(writer.Write(bebop::BFloat16FromBits(pattern)).has_value());
    bebop::internal::Traversal context({});
    Check(bebop::Codec<bebop::Float16>::Encode(
        writer, bebop::Float16FromBits(pattern), context)
            .has_value());
    Check(bebop::Codec<bebop::BFloat16>::Encode(
        writer, bebop::BFloat16FromBits(pattern), context)
            .has_value());
    bebop::Reader reader(bytes);
    const auto half = reader.Read<bebop::Float16>();
    const auto bfloat = reader.Read<bebop::BFloat16>();
    Check(half && bebop::Float16Bits(*half) == pattern);
    Check(bfloat && bebop::BFloat16Bits(*bfloat) == pattern);
    bebop::Float16 decoded_half;
    bebop::BFloat16 decoded_bfloat;
    Check(bebop::Codec<bebop::Float16>::Decode(reader, decoded_half, context)
            .has_value());
    Check(bebop::Codec<bebop::BFloat16>::Decode(reader, decoded_bfloat, context)
            .has_value());
    Check(bebop::Float16Bits(decoded_half) == pattern);
    Check(bebop::BFloat16Bits(decoded_bfloat) == pattern);
    Check(writer.Patch(0, bebop::BFloat16FromBits(pattern)).has_value());
    bebop::Reader patched(bytes);
    const auto patched_bfloat = patched.Read<bebop::BFloat16>();
    Check(patched_bfloat && bebop::BFloat16Bits(*patched_bfloat) == pattern);
  }
}

void CheckCustomScalarCodec() {
  const std::vector values{fixture::TaggedByte::kValue};
  std::array<std::byte, 6> bytes;
  bebop::Writer writer(bytes);
  bebop::internal::Traversal context;
  auto size =
      bebop::Codec<std::vector<fixture::TaggedByte>>::Size(values, context);
  Check(size && *size == bytes.size());
  Check(bebop::Codec<std::vector<fixture::TaggedByte>>::Encode(
      writer, values, context)
          .has_value());
  Check(bytes[4] == std::byte{7} && bytes[5] == std::byte{0xa5});
  bebop::Reader reader(bytes);
  std::vector<fixture::TaggedByte> decoded;
  Check(bebop::Codec<decltype(decoded)>::Decode(reader, decoded, context)
          .has_value());
  Check(decoded == values);
}

template <typename T>
void CheckBulkNarrowFloats() {
  std::vector<T> values(65536);
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = std::bit_cast<T>(static_cast<std::uint16_t>(index));
  }
  const auto payload = std::as_bytes(std::span(values));
  std::vector<std::byte> bytes(payload.size() + 4);
  bebop::Writer writer(bytes);
  bebop::internal::Traversal context;
  Check(bebop::Codec<std::vector<T>>::Encode(writer, values, context)
          .has_value());
  Check(std::ranges::equal(std::span(bytes).subspan(4), payload));
  bebop::Reader reader(bytes);
  std::vector<T> decoded;
  Check(bebop::Codec<std::vector<T>>::Decode(reader, decoded, context)
          .has_value());
  Check(std::ranges::equal(std::as_bytes(std::span(decoded)), payload));
}

#if defined(__cpp_exceptions)
struct FailingMove {
  FailingMove() = default;

  FailingMove(FailingMove&&) {
    throw 42;
  }
};

class ObjectResource : public std::pmr::memory_resource {
 public:
  std::size_t allocations = 0;
  std::size_t deallocations = 0;

 private:
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    auto* value = std::pmr::new_delete_resource()->allocate(size, alignment);
    ++allocations;
    return value;
  }

  void do_deallocate(
      void* value, std::size_t size, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(value, size, alignment);
    ++deallocations;
  }

  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

void CheckObjectConstructionFailure() {
  ObjectResource resource;
  bool failed = false;
  try {
    auto value = bebop::AllocateUnique<FailingMove>(resource);
    Check(value != nullptr);
  } catch (int code) {
    Check(code == 42);
    failed = true;
  }
  Check(failed);
  Check(resource.allocations == 1 && resource.deallocations == 1);
}
#endif

void CheckOpaqueStrings() {
  std::string bytes;
  for (unsigned value = 0; value <= 255; ++value) {
    bytes.push_back(static_cast<char>(value));
  }

  const fixture::Record original{
      .id = 42,
      .names = {bytes},
  };
  auto encoded = bebop::Encode(original);
  Check(encoded.has_value());
  auto decoded = bebop::Decode<fixture::Record>(*encoded);
  Check(decoded.has_value());
  Check(*decoded == original);
}

void CheckRecordCodec() {
  const fixture::Record value{
      .id = 7,
      .values = {0x1234, 0xabcd},
      .names = {"A", std::string("B\0C", 3)},
  };
  constexpr std::array<std::uint8_t, 30> expected{
      7,
      0,
      0,
      0,
      2,
      0,
      0,
      0,
      0x34,
      0x12,
      0xcd,
      0xab,
      2,
      0,
      0,
      0,
      1,
      0,
      0,
      0,
      'A',
      0,
      3,
      0,
      0,
      0,
      'B',
      0,
      'C',
      0,
  };

  auto size = bebop::EncodedSize(value);
  Check(size.has_value());
  Check(*size == expected.size());

  auto encoded = bebop::Encode(value);
  Check(encoded.has_value());
  Check(encoded->size() == expected.size());
  Check(std::memcmp(encoded->data(), expected.data(), expected.size()) == 0);

  auto decoded = bebop::Decode<fixture::Record>(*encoded);
  Check(decoded.has_value());
  Check(*decoded == value);

  std::array<std::byte, 128> storage{};
  std::pmr::monotonic_buffer_resource resource(
      storage.data(), storage.size(), std::pmr::null_memory_resource());
  auto allocated = bebop::Encode(value, resource);
  Check(allocated.has_value());
  Check(allocated->get_allocator().resource() == &resource);
  Check(std::equal(allocated->begin(), allocated->end(), encoded->begin()));

  encoded->push_back(std::byte{0});
  auto trailing = bebop::Decode<fixture::Record>(*encoded);
  Check(!trailing);
  Check(trailing.error().code == bebop::ErrorCode::kTrailingData);

  const bebop::DecodeOptions byte_limit{.max_bytes = expected.size() - 1};
  auto too_large = bebop::EncodedSize(value, byte_limit);
  Check(!too_large);
  Check(too_large.error().code == bebop::ErrorCode::kOverflow);

  storage.fill(std::byte{0xee});
  const auto output = std::span(storage).first(expected.size() - 1);
  auto too_small = bebop::EncodeTo(value, output);
  Check(!too_small);
  Check(too_small.error().code == bebop::ErrorCode::kBufferTooSmall);
  Check(storage[output.size()] == std::byte{0xee});

  const bebop::DecodeOptions element_limit{.max_elements = 1};
  auto too_many =
      bebop::Encode(value, std::allocator<std::byte>{}, element_limit);
  Check(!too_many);
  Check(too_many.error().code == bebop::ErrorCode::kElementLimit);
}

void CheckCollectionCodecs() {
  std::array<std::byte, 16> storage{};
  const std::vector<bool> booleans{true, false, true};
  bebop::internal::Traversal encode_context;
  bebop::Writer writer(storage);
  auto encoded =
      bebop::Codec<std::vector<bool>>::Encode(writer, booleans, encode_context);
  Check(encoded.has_value());
  Check(writer.position() == 7);
  Check(storage[4] == std::byte{1});
  Check(storage[5] == std::byte{0});
  Check(storage[6] == std::byte{1});

  bebop::internal::Traversal decode_context;
  bebop::Reader reader(writer.written());
  std::vector<bool> decoded;
  auto result =
      bebop::Codec<std::vector<bool>>::Decode(reader, decoded, decode_context);
  Check(result.has_value());
  Check(decoded == booleans);

  using Fixed = std::array<std::uint16_t, 2>;
  const Fixed fixed{0x1234, 0xabcd};
  bebop::Writer fixed_writer(storage);
  bebop::internal::Traversal fixed_context;
  auto fixed_result =
      bebop::Codec<Fixed>::Encode(fixed_writer, fixed, fixed_context);
  Check(fixed_result.has_value());
  Check(fixed_writer.position() == 4);
  Check(storage[0] == std::byte{0x34});
  Check(storage[3] == std::byte{0xab});

  bebop::Writer span_writer(storage);
  bebop::internal::Traversal span_context;
  using Borrowed = std::span<const std::uint16_t>;
  const Borrowed borrowed(fixed);
  auto span_result =
      bebop::Codec<Borrowed>::Encode(span_writer, borrowed, span_context);
  Check(span_result.has_value());
  Check(span_writer.position() == 8);
  Check(storage[0] == std::byte{2});
  Check(storage[4] == std::byte{0x34});

  const std::array<std::byte, 4> excessive_count{
      std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  bebop::Reader excessive_reader(excessive_count);
  bebop::internal::Traversal limited({.max_elements = 8});
  std::vector<std::uint16_t> values;
  auto excessive_result =
      bebop::Codec<decltype(values)>::Decode(excessive_reader, values, limited);
  Check(!excessive_result);
  Check(excessive_result.error().code == bebop::ErrorCode::kElementLimit);
  Check(values.capacity() == 0);
}

void CheckReadRollback() {
  const std::array<std::byte, 11> truncated{};
  bebop::Reader duration_reader(truncated, 100);
  auto duration = duration_reader.ReadDuration();
  Check(!duration);
  Check(duration_reader.position() == 0);

  bebop::Reader timestamp_reader(truncated, 100);
  auto timestamp = timestamp_reader.ReadTimestamp();
  Check(!timestamp);
  Check(timestamp_reader.position() == 0);

  std::array<std::byte, 7> storage{};
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint8_t{1}).has_value());
  auto invalid = writer.WriteString("\xc0\x80");
  Check(!invalid);
  Check(invalid.error() == bebop::Error{bebop::ErrorCode::kBufferTooSmall, 1});
  Check(writer.position() == 1);
}

template <typename T>
std::vector<std::byte> EncodeValue(const T& value) {
  bebop::internal::Traversal size_context;
  auto size = bebop::Codec<T>::Size(value, size_context);
  Check(size.has_value());
  std::vector<std::byte> bytes(*size);
  bebop::Writer writer(bytes);
  bebop::internal::Traversal encode_context;
  Check(bebop::Codec<T>::Encode(writer, value, encode_context).has_value());
  Check(writer.position() == bytes.size());
  return bytes;
}

template <typename Map>
void CheckMapDecode(std::span<const std::byte> bytes) {
  Map value;
  bebop::Reader reader(bytes);
  bebop::internal::Traversal context;
  Check(bebop::Codec<Map>::Decode(reader, value, context).has_value());
  Check(reader.remaining() == 0);
  Check(value.size() == 2);
  Check(value.at("alpha") == std::vector<std::int32_t>({1, -2}));
  Check(value.at("omega").empty());
  const auto encoded = EncodeValue(value);
  bebop::Reader second_reader(encoded);
  bebop::internal::Traversal second_context;
  Map second;
  Check(bebop::Codec<Map>::Decode(second_reader, second, second_context)
          .has_value());
  Check(value == second);
}

void CheckMapContainers() {
  const std::map<std::string, std::vector<std::int32_t>> value{
      {"alpha", {1, -2}}, {"omega", {}}};
  const auto encoded = EncodeValue(value);
  CheckMapDecode<std::map<std::string, std::vector<std::int32_t>>>(encoded);
  CheckMapDecode<std::unordered_map<std::string, std::vector<std::int32_t>>>(
      encoded);
  using Entry = std::pair<std::string_view, std::vector<std::int32_t>>;
  using Reference = bebop::MapRef<std::string_view, std::vector<std::int32_t>>;
  const std::array<Entry, 2> entries{
      Entry{"alpha", {1, -2}}, Entry{"omega", {}}};
  const Reference reference(entries);
  Check(reference.size() == 2);
  const auto borrowed = EncodeValue(reference);
  Check(borrowed == encoded);
  const Reference empty;
  Check(empty.size() == 0 && empty.begin() == empty.end());
  static_assert(std::is_trivially_copyable_v<Reference>);
  static_assert(!std::is_constructible_v<Reference, std::vector<Entry>>);
  static_assert(std::is_constructible_v<Reference, std::span<const Entry>>);
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
  CheckMapDecode<std::flat_map<std::string, std::vector<std::int32_t>>>(
      encoded);
#endif

  const std::map<std::uint8_t, std::int32_t> simple{{1, 100}, {2, 200}};
  const auto simple_encoded = EncodeValue(simple);
  constexpr std::array<std::byte, 14> expected{std::byte{2},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{1},
      std::byte{100},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{2},
      std::byte{200},
      std::byte{0},
      std::byte{0},
      std::byte{0}};
  Check(std::ranges::equal(simple_encoded, expected));
  const auto nested = EncodeValue(std::vector{value, value});
  std::vector<std::map<std::string, std::vector<std::int32_t>>> decoded;
  bebop::Reader reader(nested);
  bebop::internal::Traversal context;
  Check(bebop::Codec<decltype(decoded)>::Decode(reader, decoded, context)
          .has_value());
  Check(decoded == std::vector{value, value});
}

template <typename Key>
void CheckMapKey(Key key) {
  using Map = std::unordered_map<Key, std::uint32_t, bebop::Hash<Key>>;
  const Map value{{key, 42}};
  const auto bytes = EncodeValue(value);
  Map decoded;
  bebop::Reader reader(bytes);
  bebop::internal::Traversal context;
  Check(bebop::Codec<Map>::Decode(reader, decoded, context).has_value());
  Check(decoded == value);
}

void CheckMapKeys() {
  CheckMapKey(true);
  CheckMapKey(std::uint8_t{255});
  CheckMapKey(std::int8_t{-128});
  CheckMapKey(std::int16_t{-32768});
  CheckMapKey(std::uint16_t{65535});
  CheckMapKey(std::int32_t{-2147483647 - 1});
  CheckMapKey(std::uint32_t{0xffffffff});
  CheckMapKey(std::int64_t{-9223372036854775807LL - 1});
  CheckMapKey(std::uint64_t{0xffffffffffffffffULL});
  CheckMapKey(bebop::MakeUInt128(0xffffffffffffffffULL, 0xffffffffffffffffULL));
  CheckMapKey(bebop::MakeInt128(1, 0x8000000000000000ULL));
  CheckMapKey(bebop::Uuid{{0, 1, 2, 3}});
  CheckMapKey(std::string("key\0bytes", 9));
}

template <typename Map>
void CheckInvalidMap() {
  const auto valid =
      EncodeValue(std::map<std::uint8_t, std::uint32_t>{{1, 4}, {2, 8}});
  auto duplicate = valid;
  duplicate[9] = std::byte{1};
  Map value;
  bebop::Reader duplicate_reader(duplicate, 100);
  bebop::internal::Traversal duplicate_context;
  auto result =
      bebop::Codec<Map>::Decode(duplicate_reader, value, duplicate_context);
  Check(!result && result.error().code == bebop::ErrorCode::kDuplicateMapKey);
  Check(result.error().offset >= 104 && result.error().offset <= 109);
  for (std::size_t size = 0; size < valid.size(); ++size) {
    bebop::Reader reader{std::span(valid).first(size)};
    bebop::internal::Traversal context;
    result = bebop::Codec<Map>::Decode(reader, value, context);
    Check(!result && result.error().code == bebop::ErrorCode::kTruncated);
  }
  bebop::Reader limited_reader(valid);
  bebop::internal::Traversal limited({.max_elements = 1});
  result = bebop::Codec<Map>::Decode(limited_reader, value, limited);
  Check(!result && result.error().code == bebop::ErrorCode::kElementLimit);
}

void CheckMapMemoryResource() {
  const std::map<std::string, std::vector<std::string>> value{
      {std::string(100, 'k'), {std::string(100, 'v')}}};
  const auto bytes = EncodeValue(value);
  std::array<std::byte, 8192> storage;
  std::pmr::monotonic_buffer_resource resource(
      storage.data(), storage.size(), std::pmr::null_memory_resource());
  using Strings = std::pmr::vector<std::pmr::string>;
  using Map = std::pmr::map<std::pmr::string, Strings>;
  Map decoded(&resource);
  auto* previous =
      std::pmr::set_default_resource(std::pmr::null_memory_resource());
  bebop::Reader reader(bytes);
  bebop::internal::Traversal context;
  auto result = bebop::Codec<Map>::Decode(reader, decoded, context);
  std::pmr::set_default_resource(previous);
  Check(result.has_value());
  Check(decoded.begin()->first.get_allocator().resource() == &resource);
  const auto& strings = decoded.begin()->second;
  Check(strings.get_allocator().resource() == &resource);
  Check(strings[0].get_allocator().resource() == &resource);
  Check(strings[0].size() == 100 && strings[0][0] == 'v');
}

enum class Status : std::int16_t { kMissing = -1 };

void CheckEnumCodec() {
  const auto bytes = EncodeValue(Status::kMissing);
  Check(bytes == std::vector{std::byte{255}, std::byte{255}});
  Status value{};
  bebop::Reader reader(bytes);
  bebop::internal::Traversal context;
  Check(bebop::Codec<Status>::Decode(reader, value, context).has_value());
  Check(value == Status::kMissing);
}

void CheckFlatMapScaling() {
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
  constexpr std::uint32_t count = 2048;
  std::vector<std::byte> bytes(4 + 8 * count);
  bebop::Writer writer(bytes);
  Check(writer.Write(count).has_value());
  for (std::uint32_t key = count; key > 0; --key) {
    Check(writer.Write(key - 1).has_value());
    Check(writer.Write(7 * (key - 1)).has_value());
  }
  fixture::MoveTracked::moves = 0;
  using Map = std::flat_map<std::uint32_t, fixture::MoveTracked>;
  Map decoded;
  bebop::Reader reader(bytes);
  bebop::internal::Traversal context;
  Check(bebop::Codec<Map>::Decode(reader, decoded, context).has_value());
  Check(decoded.size() == count &&
        decoded.at(count - 1).value == 7 * (count - 1));
  Check(fixture::MoveTracked::moves < count * 100);
  for (std::uint32_t key = 0; key < count; ++key) {
    Check(decoded.at(key).value == 7 * key);
  }

  std::flat_map<std::uint32_t, fixture::MoveTracked, std::greater<>> descending;
  bebop::Reader descending_reader(bytes);
  bebop::internal::Traversal descending_context;
  fixture::MoveTracked::moves = 0;
  Check(bebop::Codec<decltype(descending)>::Decode(
      descending_reader, descending, descending_context)
          .has_value());
  Check(descending.begin()->first == count - 1);
  Check(fixture::MoveTracked::moves <= count * 2);

  Check(writer.Patch(4 + (count - 1) * 8, count - 1).has_value());
  bebop::Reader duplicate_reader(bytes);
  bebop::internal::Traversal duplicate_context;
  auto duplicate =
      bebop::Codec<Map>::Decode(duplicate_reader, decoded, duplicate_context);
  Check(!duplicate &&
        duplicate.error().code == bebop::ErrorCode::kDuplicateMapKey);
#endif
}

}  // namespace

int main() {
  CheckScalars();
  CheckStrings();
  CheckBoundaries();
  CheckSpecialTypes();
  CheckHalfConversions();
  CheckBFloatConversions();
  CheckNarrowFloatBits();
  CheckCustomScalarCodec();
  CheckBulkNarrowFloats<bebop::Float16>();
  CheckBulkNarrowFloats<bebop::BFloat16>();
#if defined(__cpp_exceptions)
  CheckObjectConstructionFailure();
#endif
  CheckOpaqueStrings();
  CheckEnumCodec();
  CheckMapContainers();
  CheckMapKeys();
  CheckInvalidMap<std::map<std::uint8_t, std::uint32_t>>();
  CheckInvalidMap<std::unordered_map<std::uint8_t, std::uint32_t>>();
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
  CheckInvalidMap<std::flat_map<std::uint8_t, std::uint32_t>>();
#endif
  CheckMapMemoryResource();
  CheckFlatMapScaling();
  CheckRecordCodec();
  CheckCollectionCodecs();
  CheckReadRollback();
}
