//! Bebop C++23 serialization runtime

#ifndef BEBOP_HPP_
#define BEBOP_HPP_

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#if __has_include(<flat_map>)
#include <flat_map>
#endif

#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

static_assert(CHAR_BIT == 8, "Bebop requires 8-bit bytes");
static_assert(sizeof(bool) == 1, "Bebop requires a one-byte bool");
static_assert(std::endian::native == std::endian::little,
    "Bebop requires a little-endian target");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::digits == 24);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53);

namespace bebop {

#if defined(__SIZEOF_INT128__) && !defined(BEBOP_CPP_NO_NATIVE_INT128)
using Int128 = __int128_t;
using UInt128 = __uint128_t;
inline constexpr bool kHasNativeInt128 = true;
#else
struct UInt128 {
  std::uint64_t low = 0;
  std::uint64_t high = 0;

  friend bool operator==(const UInt128&, const UInt128&) = default;

  friend std::strong_ordering operator<=>(
      UInt128 left, UInt128 right) noexcept {
    const auto high_order = left.high <=> right.high;
    if (high_order != 0) {
      return high_order;
    }
    return left.low <=> right.low;
  }
};

struct Int128 {
  std::uint64_t low = 0;
  std::int64_t high = 0;

  friend bool operator==(const Int128&, const Int128&) = default;

  friend std::strong_ordering operator<=>(Int128 left, Int128 right) noexcept {
    const auto high_order = left.high <=> right.high;
    if (high_order != 0) {
      return high_order;
    }
    return left.low <=> right.low;
  }
};

inline constexpr bool kHasNativeInt128 = false;
#endif

#if defined(__STDCPP_FLOAT16_T__) && !defined(BEBOP_CPP_NO_NATIVE_FLOAT16)
using Float16 = std::float16_t;
inline constexpr bool kHasNativeFloat16 = true;
#elif defined(__FLT16_MANT_DIG__) && !defined(BEBOP_CPP_NO_NATIVE_FLOAT16)
using Float16 = _Float16;
inline constexpr bool kHasNativeFloat16 = true;
#else
struct Float16 {
  std::uint16_t bits = 0;

  constexpr Float16() = default;

  constexpr explicit Float16(float value) : bits(FloatToHalf(value)) {
  }

  constexpr explicit operator float() const noexcept {
    return HalfToFloat(bits);
  }

  friend constexpr bool operator==(Float16 left, Float16 right) noexcept {
    return static_cast<float>(left) == static_cast<float>(right);
  }

 private:
  static constexpr float HalfToFloat(std::uint16_t bits) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(bits & 0x8000u) << 16;
    std::uint32_t exponent = (bits >> 10) & 31u;
    std::uint32_t fraction = bits & 1023u;

    if (exponent == 0) {
      if (fraction == 0) {
        return std::bit_cast<float>(sign);
      }

      int shift = 0;
      while ((fraction & 1024u) == 0) {
        fraction <<= 1;
        ++shift;
      }
      exponent = static_cast<std::uint32_t>(113 - shift);
      fraction &= 1023u;
    } else if (exponent == 31) {
      exponent = 255;
    } else {
      exponent += 112;
    }

    return std::bit_cast<float>(sign | (exponent << 23) | (fraction << 13));
  }

  static constexpr std::uint16_t FloatToHalf(float value) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t fraction = bits & 0x7fffffu;
    const std::uint32_t exponent = (bits >> 23) & 255u;

    if (exponent == 255) {
      std::uint32_t payload = 0;
      if (fraction != 0) {
        payload = (fraction >> 13) | 0x200u;
      }
      return static_cast<std::uint16_t>(sign | 0x7c00u | payload);
    }

    const int half_exponent = static_cast<int>(exponent) - 112;
    if (half_exponent >= 31) {
      return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (half_exponent < -10) {
      return static_cast<std::uint16_t>(sign);
    }

    std::uint32_t significand = fraction;
    unsigned shift = 13;
    if (half_exponent <= 0) {
      significand |= 0x800000u;
      shift = static_cast<unsigned>(14 - half_exponent);
    }

    const std::uint32_t halfway = std::uint32_t{1} << (shift - 1);
    const std::uint32_t retained_bit = (significand >> shift) & 1u;
    const std::uint32_t rounded =
        (significand + halfway - 1 + retained_bit) >> shift;

    std::uint32_t result = rounded;
    if (half_exponent > 0) {
      result += static_cast<std::uint32_t>(half_exponent) << 10;
    }
    return static_cast<std::uint16_t>(sign | result);
  }
};

inline constexpr bool kHasNativeFloat16 = false;
#endif

#if !defined(BEBOP_CPP_NO_NATIVE_BFLOAT16) &&                      \
    !(defined(__apple_build_version__) && __clang_major__ == 21 && \
        (defined(__x86_64__) || defined(__i386__))) &&             \
    (defined(__STDCPP_BFLOAT16_T__) || defined(__BFLT16_MAX__) ||  \
        (defined(__clang__) &&                                     \
            (defined(__aarch64__) ||                               \
                ((defined(__x86_64__) || defined(__i386__)) &&     \
                    defined(__SSE2__)))))
#if defined(__STDCPP_BFLOAT16_T__)
using BFloat16 = std::bfloat16_t;
#else
using BFloat16 = __bf16;
#endif
inline constexpr bool kHasNativeBFloat16 = true;
#else
struct BFloat16 {
  std::uint16_t bits = 0;

  constexpr BFloat16() = default;

  constexpr explicit BFloat16(float value) : bits(FloatToBFloat(value)) {
  }

  constexpr explicit operator float() const noexcept {
    return BFloatToFloat(bits);
  }

  friend constexpr bool operator==(BFloat16 left, BFloat16 right) noexcept {
    return static_cast<float>(left) == static_cast<float>(right);
  }

 private:
  static constexpr float BFloatToFloat(std::uint16_t bits) noexcept {
    return std::bit_cast<float>(static_cast<std::uint32_t>(bits) << 16);
  }

  static constexpr std::uint16_t FloatToBFloat(float value) noexcept {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    if ((bits & 0x7fffffffu) > 0x7f800000u) {
      return static_cast<std::uint16_t>((bits >> 16) | 0x40u);
    }

    bits += 0x7fffu + ((bits >> 16) & 1u);
    return static_cast<std::uint16_t>(bits >> 16);
  }
};

inline constexpr bool kHasNativeBFloat16 = false;
#endif

static_assert(sizeof(Int128) == 16 && sizeof(UInt128) == 16);
static_assert(sizeof(Float16) == 2 && sizeof(BFloat16) == 2);
static_assert(std::is_trivially_copyable_v<Float16>);
static_assert(std::is_trivially_copyable_v<BFloat16>);

//! Construct an unsigned 128-bit integer from low and high words
constexpr UInt128 MakeUInt128(std::uint64_t low, std::uint64_t high) noexcept {
  return std::bit_cast<UInt128>(std::array<std::uint64_t, 2>{low, high});
}

//! Construct a signed 128-bit integer from two's-complement words
constexpr Int128 MakeInt128(std::uint64_t low, std::uint64_t high) noexcept {
  return std::bit_cast<Int128>(MakeUInt128(low, high));
}

//! Get the binary16 representation without numeric conversion
constexpr std::uint16_t Float16Bits(const Float16& value) noexcept {
  return std::bit_cast<std::uint16_t>(value);
}

//! Construct a binary16 value without numeric conversion
constexpr Float16 Float16FromBits(std::uint16_t bits) noexcept {
  return std::bit_cast<Float16>(bits);
}

//! Get the bfloat16 representation without numeric conversion
constexpr std::uint16_t BFloat16Bits(const BFloat16& value) noexcept {
  return std::bit_cast<std::uint16_t>(value);
}

//! Construct a bfloat16 value without numeric conversion
constexpr BFloat16 BFloat16FromBits(std::uint16_t bits) noexcept {
  return std::bit_cast<BFloat16>(bits);
}

//! UUID bytes in RFC order
struct Uuid {
  std::array<std::uint8_t, 16> bytes{};

  friend auto operator<=>(const Uuid&, const Uuid&) = default;
};

//! Unix time with nanosecond precision and an offset in milliseconds
struct Timestamp {
  std::int64_t seconds = 0;
  std::int32_t nanoseconds = 0;
  std::int32_t offset_milliseconds = 0;

  friend bool operator==(const Timestamp&, const Timestamp&) = default;
};

//! Signed seconds and nanoseconds
struct Duration {
  std::int64_t seconds = 0;
  std::int32_t nanoseconds = 0;

  friend bool operator==(const Duration&, const Duration&) = default;
};

//! Serialization failure codes
//!
//! Limit errors refer to the limits supplied for the operation.
enum class ErrorCode {
  kTruncated,
  kBufferTooSmall,
  kOverflow,
  kInvalidString,
  kInvalidMessage,
  kInvalidUnion,
  kDuplicateMapKey,
  kTrailingData,
  kDepthLimit,
  kElementLimit,
  kScratchTooSmall,
};

//! Failure code and byte offset within the operation's input or output
struct Error {
  ErrorCode code;
  std::size_t offset = 0;

  friend bool operator==(const Error&, const Error&) = default;
};

template <typename T>
using Result = std::expected<T, Error>;

//! Limits for decoding, sizing, and encoding
struct DecodeOptions {
  std::size_t max_depth = 64;
  std::size_t max_elements = 16 * 1024 * 1024;
  std::size_t max_bytes = std::numeric_limits<std::size_t>::max();
  //! Optional workspace for iterative traversal; otherwise 16 KiB is used
  //! The workspace must not overlap the input, output, or record storage.
  std::span<std::byte> traversal_scratch{};
};

//! Preserved payload of an unrecognized union branch
struct UnknownUnion {
  std::uint8_t discriminator = 0;
  std::vector<std::byte> payload{};

  friend bool operator==(const UnknownUnion&, const UnknownUnion&) = default;
};

//! Preserved unknown union payload allocated from a memory resource
struct PmrUnknownUnion {
  using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

  PmrUnknownUnion() = default;

  PmrUnknownUnion(std::allocator_arg_t, allocator_type allocator)
      : payload(allocator) {
  }

  PmrUnknownUnion(std::allocator_arg_t,
      allocator_type allocator,
      const PmrUnknownUnion& other)
      : discriminator(other.discriminator), payload(other.payload, allocator) {
  }

  PmrUnknownUnion(
      std::allocator_arg_t, allocator_type allocator, PmrUnknownUnion&& other)
      : discriminator(other.discriminator),
        payload(std::move(other.payload), allocator) {
  }

  std::uint8_t discriminator = 0;
  std::pmr::vector<std::byte> payload;

  friend bool operator==(
      const PmrUnknownUnion&, const PmrUnknownUnion&) = default;
};

//! Delete an object allocated by a memory resource
template <typename T>
struct ResourceDeleter {
  std::pmr::memory_resource* resource = nullptr;

  void operator()(T* value) const noexcept {
    if (value == nullptr) {
      return;
    }
    std::destroy_at(value);
    resource->deallocate(value, sizeof(T), alignof(T));
  }
};

//! Borrowed payload of an unrecognized union branch
struct UnknownUnionRef {
  std::uint8_t discriminator = 0;
  std::span<const std::byte> payload{};
};

//! Hash support for standard types and Bebop scalar types
//!
//! Hash values are not part of the wire format and are not persistent IDs.
template <typename T>
struct Hash : std::hash<T> {};

template <>
struct Hash<Uuid> {
  std::size_t operator()(const Uuid& value) const noexcept {
    std::size_t hash = 0;
    for (std::uint8_t byte : value.bytes) {
      hash = hash * 131 + byte;
    }
    return hash;
  }
};

template <>
struct Hash<UInt128> {
  std::size_t operator()(UInt128 value) const noexcept {
    const auto words = std::bit_cast<std::array<std::uint64_t, 2>>(value);
    const std::size_t low_hash = std::hash<std::uint64_t>{}(words[0]);
    const std::size_t high_hash = std::hash<std::uint64_t>{}(words[1]);
    return low_hash ^ std::rotl(high_hash, 1);
  }
};

template <>
struct Hash<Int128> {
  std::size_t operator()(Int128 value) const noexcept {
    return Hash<UInt128>{}(std::bit_cast<UInt128>(value));
  }
};

//! Combine call operators for variant visitation
template <typename... Visitors>
struct Overload : Visitors... {
  using Visitors::operator()...;
};

//! Types with a directly readable scalar wire representation
template <typename T>
concept WireScalar = std::is_integral_v<T> || std::is_enum_v<T> ||
                     std::is_same_v<T, float> || std::is_same_v<T, double> ||
                     std::is_same_v<T, Int128> || std::is_same_v<T, UInt128> ||
                     std::is_same_v<T, Float16> || std::is_same_v<T, BFloat16>;

//! Read values from caller-owned bytes
//! The source must remain alive and unchanged while borrowed views are used.
class Reader {
 public:
  explicit Reader(
      std::span<const std::byte> source, std::size_t base_offset = 0) noexcept
      : source_(source), base_offset_(base_offset) {
  }

  std::size_t position() const noexcept {
    return position_;
  }

  std::size_t absolute_position() const noexcept {
    return base_offset_ + position_;
  }

  std::size_t remaining() const noexcept {
    return source_.size() - position_;
  }

  std::span<const std::byte> remaining_bytes() const noexcept {
    return source_.subspan(position_);
  }

  //! Read a scalar without requiring aligned input
  template <WireScalar T>
  [[nodiscard]] Result<T> Read() noexcept {
    auto bytes = ReadBytes(sizeof(T));
    if (!bytes) {
      return std::unexpected(bytes.error());
    }

    if constexpr (std::is_same_v<T, bool>) {
      return std::to_integer<unsigned char>((*bytes)[0]) != 0;
    } else {
      T value;
      std::memcpy(&value, bytes->data(), sizeof(T));
      return value;
    }
  }

  //! Borrow the next count bytes
  [[nodiscard]] Result<std::span<const std::byte>> ReadBytes(
      std::size_t count) noexcept {
    if (count > remaining()) {
      return std::unexpected(Error{ErrorCode::kTruncated, absolute_position()});
    }

    const auto bytes = source_.subspan(position_, count);
    position_ += count;
    return bytes;
  }

  //! Read a bounded region with offsets relative to the original source
  [[nodiscard]] Result<Reader> ReadRegion(std::size_t count) noexcept {
    const std::size_t start = absolute_position();
    auto bytes = ReadBytes(count);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    return Reader(*bytes, start);
  }

  //! Borrow a string after checking its length and wire terminator
  //! Failure leaves the reader position unchanged.
  [[nodiscard]] Result<std::string_view> ReadString() noexcept {
    Reader cursor = *this;
    auto length = cursor.Read<std::uint32_t>();
    if (!length) {
      return std::unexpected(length.error());
    }

    const std::size_t content_start = cursor.absolute_position();
    if (*length >= cursor.remaining()) {
      return std::unexpected(Error{ErrorCode::kTruncated, content_start});
    }

    const std::size_t encoded_size = static_cast<std::size_t>(*length) + 1;
    auto bytes = cursor.ReadBytes(encoded_size);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    if (bytes->back() != std::byte{0}) {
      return std::unexpected(
          Error{ErrorCode::kInvalidString, content_start + *length});
    }

    const std::string_view text(
        reinterpret_cast<const char*>(bytes->data()), *length);
    *this = cursor;
    return text;
  }

  //! Read a UUID in RFC byte order
  [[nodiscard]] Result<Uuid> ReadUuid() noexcept {
    auto bytes = ReadBytes(16);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }

    Uuid value;
    std::memcpy(value.bytes.data(), bytes->data(), value.bytes.size());
    return value;
  }

  //! Read seconds, nanoseconds, and a timezone offset
  //! Failure leaves the reader position unchanged.
  [[nodiscard]] Result<Timestamp> ReadTimestamp() noexcept {
    Reader cursor = *this;
    auto seconds = cursor.Read<std::int64_t>();
    if (!seconds) {
      return std::unexpected(seconds.error());
    }

    auto nanoseconds = cursor.Read<std::int32_t>();
    if (!nanoseconds) {
      return std::unexpected(nanoseconds.error());
    }

    auto offset = cursor.Read<std::int32_t>();
    if (!offset) {
      return std::unexpected(offset.error());
    }

    *this = cursor;
    return Timestamp{*seconds, *nanoseconds, *offset};
  }

  //! Read signed seconds and nanoseconds
  //! Failure leaves the reader position unchanged.
  [[nodiscard]] Result<Duration> ReadDuration() noexcept {
    Reader cursor = *this;
    auto seconds = cursor.Read<std::int64_t>();
    if (!seconds) {
      return std::unexpected(seconds.error());
    }

    auto nanoseconds = cursor.Read<std::int32_t>();
    if (!nanoseconds) {
      return std::unexpected(nanoseconds.error());
    }

    *this = cursor;
    return Duration{*seconds, *nanoseconds};
  }

 private:
  std::span<const std::byte> source_;
  std::size_t base_offset_;
  std::size_t position_ = 0;
};

//! Write values into caller-owned storage without allocating
//! The destination must remain alive while the writer is used.
class Writer {
 public:
  explicit Writer(std::span<std::byte> destination) noexcept
      : destination_(destination) {
  }

  std::size_t position() const noexcept {
    return position_;
  }

  std::size_t remaining() const noexcept {
    return destination_.size() - position_;
  }

  std::span<const std::byte> written() const noexcept {
    return destination_.first(position_);
  }

  //! Write a scalar in little-endian byte order
  template <WireScalar T>
  [[nodiscard]] Result<void> Write(const T& value) noexcept {
    if (sizeof(T) > remaining()) {
      return NoSpace();
    }

    if constexpr (std::is_same_v<T, bool>) {
      destination_[position_] = std::byte{0};
      if (value) {
        destination_[position_] = std::byte{1};
      }
    } else {
      std::memcpy(destination_.data() + position_, &value, sizeof(T));
    }
    position_ += sizeof(T);
    return {};
  }

  //! Copy bytes that do not overlap the destination
  [[nodiscard]] Result<void> WriteBytes(
      std::span<const std::byte> bytes) noexcept {
    if (bytes.size() > remaining()) {
      return NoSpace();
    }

    if (!bytes.empty()) {
      std::memcpy(destination_.data() + position_, bytes.data(), bytes.size());
    }
    position_ += bytes.size();
    return {};
  }

  //! Write a length-prefixed string and its terminator
  //! Invalid input or insufficient space leaves the writer position unchanged.
  [[nodiscard]] Result<void> WriteString(std::string_view text) noexcept {
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(Error{ErrorCode::kOverflow, position_});
    }
    if (remaining() < 5 || text.size() > remaining() - 5) {
      return NoSpace();
    }

    auto length_result = Write(static_cast<std::uint32_t>(text.size()));
    if (!length_result) {
      return length_result;
    }

    auto content_result =
        WriteBytes(std::as_bytes(std::span(text.data(), text.size())));
    if (!content_result) {
      return content_result;
    }

    return Write(std::uint8_t{0});
  }

  //! Write a UUID in RFC byte order
  [[nodiscard]] Result<void> WriteUuid(const Uuid& value) noexcept {
    return WriteBytes(std::as_bytes(std::span(value.bytes)));
  }

  //! Write seconds, nanoseconds, and a timezone offset
  [[nodiscard]] Result<void> WriteTimestamp(Timestamp value) noexcept {
    if (remaining() < 16) {
      return NoSpace();
    }

    auto seconds_result = Write(value.seconds);
    if (!seconds_result) {
      return seconds_result;
    }

    auto nanoseconds_result = Write(value.nanoseconds);
    if (!nanoseconds_result) {
      return nanoseconds_result;
    }

    return Write(value.offset_milliseconds);
  }

  //! Write signed seconds and nanoseconds
  [[nodiscard]] Result<void> WriteDuration(Duration value) noexcept {
    if (remaining() < 12) {
      return NoSpace();
    }

    auto seconds_result = Write(value.seconds);
    if (!seconds_result) {
      return seconds_result;
    }

    return Write(value.nanoseconds);
  }

  //! Replace a scalar within the bytes already written
  template <WireScalar T>
  [[nodiscard]] Result<void> Patch(
      std::size_t offset, const T& value) noexcept {
    if (offset > position_ || sizeof(T) > position_ - offset) {
      return std::unexpected(Error{ErrorCode::kTruncated, offset});
    }

    Writer writer(destination_.subspan(offset, sizeof(T)));
    return writer.Write(value);
  }

 private:
  std::unexpected<Error> NoSpace() const noexcept {
    return std::unexpected(Error{ErrorCode::kBufferTooSmall, position_});
  }

  std::span<std::byte> destination_;
  std::size_t position_ = 0;
};

}  // namespace bebop

namespace bebop::internal {

enum class DirectoryKind : std::uint8_t {
  kEmpty = 0,
  kTiny1 = 1,
  kTiny2 = 2,
  kTiny3 = 3,
  kMask8 = 4,
  kMask16 = 5,
  kMask32 = 6,
  kBlocks = 7,
};

inline constexpr std::size_t kMessagePrefixSize = 4;
inline constexpr std::size_t kMaxMessageFields = 255;
inline constexpr std::size_t kBlockEntrySize = 5;

inline std::uint32_t LoadOffset(
    const std::byte* data, std::uint8_t width) noexcept {
  std::uint32_t value = 0;
  std::memcpy(&value, data, width);
  return value;
}

[[nodiscard]] inline Result<void> WriteOffset(
    Writer& writer, std::uint32_t value, std::uint8_t width) noexcept {
  if (width == 1) {
    return writer.Write(static_cast<std::uint8_t>(value));
  }
  if (width == 2) {
    return writer.Write(static_cast<std::uint16_t>(value));
  }
  return writer.Write(value);
}

struct MessageDirectory {
  std::span<const std::byte> bytes;
  std::uint8_t field_count = 0;
};

[[nodiscard]] inline Result<MessageDirectory> ReadMessageDirectory(
    std::span<const std::byte> encoded, DirectoryKind kind) noexcept {
  const std::size_t body_size = encoded.size() - kMessagePrefixSize;
  std::size_t directory_size = 0;
  std::size_t field_count = 0;

  switch (kind) {
    case DirectoryKind::kEmpty:
      break;
    case DirectoryKind::kTiny1:
    case DirectoryKind::kTiny2:
    case DirectoryKind::kTiny3:
      directory_size = static_cast<std::size_t>(kind);
      field_count = directory_size;
      break;
    case DirectoryKind::kMask8:
      directory_size = 1;
      break;
    case DirectoryKind::kMask16:
      directory_size = 2;
      break;
    case DirectoryKind::kMask32:
      directory_size = 4;
      break;
    case DirectoryKind::kBlocks: {
      if (body_size < 2) {
        return std::unexpected(Error{ErrorCode::kInvalidMessage, 4});
      }
      const auto block_mask =
          std::to_integer<std::uint8_t>(encoded[encoded.size() - 2]);
      directory_size = 1 + kBlockEntrySize * static_cast<std::size_t>(
                                                 std::popcount(block_mask));
      break;
    }
  }

  if (directory_size + 1 > body_size) {
    return std::unexpected(Error{ErrorCode::kInvalidMessage, 4});
  }

  const std::size_t directory_offset = encoded.size() - directory_size - 1;
  const auto bytes = encoded.subspan(directory_offset, directory_size);

  switch (kind) {
    case DirectoryKind::kEmpty:
      break;
    case DirectoryKind::kTiny1:
    case DirectoryKind::kTiny2:
    case DirectoryKind::kTiny3: {
      std::uint8_t previous = 0;
      for (std::size_t index = 0; index < field_count; ++index) {
        const auto tag = std::to_integer<std::uint8_t>(bytes[index]);
        if (tag <= previous) {
          return std::unexpected(
              Error{ErrorCode::kInvalidMessage, directory_offset + index});
        }
        previous = tag;
      }
      break;
    }
    case DirectoryKind::kMask8:
    case DirectoryKind::kMask16:
    case DirectoryKind::kMask32: {
      const auto width = static_cast<std::uint8_t>(directory_size);
      const std::uint32_t mask = LoadOffset(bytes.data(), width);
      field_count = static_cast<std::size_t>(std::popcount(mask));
      break;
    }
    case DirectoryKind::kBlocks: {
      const auto block_mask = std::to_integer<std::uint8_t>(bytes.back());
      if (block_mask == 0) {
        return std::unexpected(
            Error{ErrorCode::kInvalidMessage, directory_offset});
      }

      std::size_t entry_offset = 0;
      for (unsigned block = 0; block < 8; ++block) {
        if ((block_mask & (1u << block)) == 0) {
          continue;
        }

        const auto rank = std::to_integer<std::uint8_t>(bytes[entry_offset]);
        const std::uint32_t mask =
            LoadOffset(bytes.data() + entry_offset + 1, 4);
        const bool invalid_last_block = block == 7 && (mask & 0x80000000u) != 0;
        if (rank != field_count || mask == 0 || invalid_last_block) {
          return std::unexpected(Error{
              ErrorCode::kInvalidMessage, directory_offset + entry_offset});
        }

        field_count += static_cast<std::size_t>(std::popcount(mask));
        entry_offset += kBlockEntrySize;
      }
      break;
    }
  }

  if (kind != DirectoryKind::kEmpty && field_count == 0) {
    return std::unexpected(Error{ErrorCode::kInvalidMessage, directory_offset});
  }

  return MessageDirectory{bytes, static_cast<std::uint8_t>(field_count)};
}

struct MessageLayout {
  std::array<std::uint32_t, 8> masks{};
  std::uint32_t body_size = 0;
  DirectoryKind kind = DirectoryKind::kEmpty;
  std::uint8_t block_mask = 0;
  std::uint8_t width = 1;
  std::uint8_t directory_size = 0;
};

[[nodiscard]] inline Result<MessageLayout> PlanMessage(
    std::size_t payload_size, std::span<const std::uint8_t> tags) noexcept {
  if (payload_size > std::numeric_limits<std::uint32_t>::max() ||
      tags.size() > kMaxMessageFields) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }

  MessageLayout layout;
  if (payload_size > 65535) {
    layout.width = 4;
  } else if (payload_size > 255) {
    layout.width = 2;
  }

  std::uint8_t previous = 0;
  for (std::uint8_t tag : tags) {
    if (tag <= previous) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage});
    }
    previous = tag;

    const unsigned block = (static_cast<unsigned>(tag) - 1) / 32;
    const unsigned bit = (static_cast<unsigned>(tag) - 1) % 32;
    layout.masks[block] |= std::uint32_t{1} << bit;
    layout.block_mask |= static_cast<std::uint8_t>(1u << block);
  }

  if (tags.empty()) {
    if (payload_size != 0) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage});
    }
  } else {
    layout.kind = DirectoryKind::kBlocks;
    const auto block_count =
        static_cast<std::size_t>(std::popcount(layout.block_mask));
    layout.directory_size =
        static_cast<std::uint8_t>(1 + kBlockEntrySize * block_count);

    if (tags.back() <= 8) {
      layout.kind = DirectoryKind::kMask8;
      layout.directory_size = 1;
    } else if (tags.back() <= 16) {
      layout.kind = DirectoryKind::kMask16;
      layout.directory_size = 2;
    } else if (tags.back() <= 32) {
      layout.kind = DirectoryKind::kMask32;
      layout.directory_size = 4;
    }

    if (tags.size() <= 3 && tags.size() < layout.directory_size) {
      layout.kind = static_cast<DirectoryKind>(tags.size());
      layout.directory_size = static_cast<std::uint8_t>(tags.size());
    }
  }

  std::size_t boundary_size = 0;
  if (!tags.empty()) {
    boundary_size = (tags.size() - 1) * layout.width;
  }

  const std::size_t suffix_size = boundary_size + layout.directory_size + 1;
  if (suffix_size > std::numeric_limits<std::uint32_t>::max() - payload_size) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }

  layout.body_size = static_cast<std::uint32_t>(payload_size + suffix_size);
  return layout;
}

[[nodiscard]] inline Result<void> WriteMessageDirectory(Writer& writer,
    const MessageLayout& layout,
    std::span<const std::uint8_t> tags) noexcept {
  switch (layout.kind) {
    case DirectoryKind::kEmpty:
      return {};
    case DirectoryKind::kTiny1:
    case DirectoryKind::kTiny2:
    case DirectoryKind::kTiny3:
      return writer.WriteBytes(std::as_bytes(tags));
    case DirectoryKind::kMask8:
    case DirectoryKind::kMask16:
    case DirectoryKind::kMask32:
      return WriteOffset(writer, layout.masks[0], layout.directory_size);
    case DirectoryKind::kBlocks: {
      unsigned rank = 0;
      for (std::uint32_t mask : layout.masks) {
        if (mask == 0) {
          continue;
        }

        auto rank_result = writer.Write(static_cast<std::uint8_t>(rank));
        if (!rank_result) {
          return rank_result;
        }

        auto mask_result = writer.Write(mask);
        if (!mask_result) {
          return mask_result;
        }
        rank += static_cast<unsigned>(std::popcount(mask));
      }
      return writer.Write(layout.block_mask);
    }
  }

  return std::unexpected(Error{ErrorCode::kInvalidMessage, writer.position()});
}

}  // namespace bebop::internal

namespace bebop {

//! Locate fields in a validated message index without allocating
//! The encoded buffer must remain alive and unchanged.
class MessageIndex {
 public:
  //! Validate the index of one complete encoded message
  [[nodiscard]] static Result<MessageIndex> Parse(
      std::span<const std::byte> encoded) noexcept {
    if (encoded.size() < internal::kMessagePrefixSize + 1) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage});
    }

    const std::uint32_t body_size = internal::LoadOffset(encoded.data(), 4);
    if (body_size != encoded.size() - internal::kMessagePrefixSize ||
        body_size == 0) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage});
    }

    const auto control = std::to_integer<std::uint8_t>(encoded.back());
    const auto width_code = control & 3u;
    if ((control & 0xe0u) != 0 || width_code == 3) {
      return std::unexpected(
          Error{ErrorCode::kInvalidMessage, encoded.size() - 1});
    }

    const auto width = static_cast<std::uint8_t>(1u << width_code);
    const auto kind = static_cast<internal::DirectoryKind>((control >> 2) & 7u);
    auto directory = internal::ReadMessageDirectory(encoded, kind);
    if (!directory) {
      return std::unexpected(directory.error());
    }

    std::size_t boundary_size = 0;
    if (directory->field_count != 0) {
      boundary_size =
          (static_cast<std::size_t>(directory->field_count) - 1) * width;
    }

    const std::size_t suffix_size = boundary_size + directory->bytes.size() + 1;
    if (suffix_size > body_size) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage, 4});
    }

    const std::size_t payload_size = body_size - suffix_size;
    if (directory->field_count == 0 && payload_size != 0) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage, 4});
    }

    std::uint32_t previous = 0;
    for (std::size_t index = 0; index + 1 < directory->field_count; ++index) {
      const std::size_t offset = 4 + payload_size + index * width;
      const std::uint32_t boundary =
          internal::LoadOffset(encoded.data() + offset, width);
      if (boundary < previous || boundary > payload_size) {
        return std::unexpected(Error{ErrorCode::kInvalidMessage, offset});
      }
      previous = boundary;
    }

    MessageIndex result;
    result.encoded_ = encoded;
    result.payload_size_ = static_cast<std::uint32_t>(payload_size);
    result.directory_size_ = static_cast<std::uint8_t>(directory->bytes.size());
    result.field_count_ = directory->field_count;
    result.width_ = width;
    result.kind_ = kind;
    return result;
  }

  std::size_t field_count() const noexcept {
    return field_count_;
  }

  std::span<const std::byte> encoded() const noexcept {
    return encoded_;
  }

  //! Borrow a field by tag, returning no value when it is absent
  std::optional<std::span<const std::byte>> Field(
      std::uint8_t tag) const noexcept {
    auto rank = FindRank(tag);
    if (!rank) {
      return std::nullopt;
    }
    return FieldAt(*rank);
  }

  //! Borrow a field by its rank in ascending tag order
  std::optional<std::span<const std::byte>> FieldAt(
      std::size_t rank) const noexcept {
    if (rank >= field_count_) {
      return std::nullopt;
    }

    const auto* boundaries = encoded_.data() + 4 + payload_size_;
    std::size_t start = 0;
    if (rank != 0) {
      start = internal::LoadOffset(boundaries + (rank - 1) * width_, width_);
    }

    std::size_t end = payload_size_;
    if (rank + 1 != field_count_) {
      end = internal::LoadOffset(boundaries + rank * width_, width_);
    }
    return encoded_.subspan(4 + start, end - start);
  }

 private:
  std::optional<std::size_t> FindRank(std::uint8_t tag) const noexcept {
    if (tag == 0 || field_count_ == 0) {
      return std::nullopt;
    }

    const auto* directory =
        encoded_.data() + encoded_.size() - 1 - directory_size_;

    switch (kind_) {
      case internal::DirectoryKind::kEmpty:
        return std::nullopt;
      case internal::DirectoryKind::kTiny1:
      case internal::DirectoryKind::kTiny2:
      case internal::DirectoryKind::kTiny3:
        for (std::size_t rank = 0; rank < field_count_; ++rank) {
          if (std::to_integer<std::uint8_t>(directory[rank]) == tag) {
            return rank;
          }
        }
        return std::nullopt;
      case internal::DirectoryKind::kMask8:
      case internal::DirectoryKind::kMask16:
      case internal::DirectoryKind::kMask32: {
        if (tag > 8u * directory_size_) {
          return std::nullopt;
        }

        const std::uint32_t mask =
            internal::LoadOffset(directory, directory_size_);
        const std::uint32_t bit = std::uint32_t{1} << (tag - 1);
        if ((mask & bit) == 0) {
          return std::nullopt;
        }
        return static_cast<std::size_t>(std::popcount(mask & (bit - 1)));
      }
      case internal::DirectoryKind::kBlocks: {
        const unsigned block = (static_cast<unsigned>(tag) - 1) / 32;
        const unsigned block_bit = 1u << block;
        const auto block_mask =
            std::to_integer<std::uint8_t>(directory[directory_size_ - 1]);
        if ((block_mask & block_bit) == 0) {
          return std::nullopt;
        }

        const auto preceding_blocks = static_cast<std::size_t>(
            std::popcount(block_mask & (block_bit - 1)));
        const auto* entry =
            directory + internal::kBlockEntrySize * preceding_blocks;
        const std::uint32_t mask = internal::LoadOffset(entry + 1, 4);
        const std::uint32_t bit = std::uint32_t{1}
                                  << ((static_cast<unsigned>(tag) - 1) % 32);
        if ((mask & bit) == 0) {
          return std::nullopt;
        }

        const auto preceding_fields = std::to_integer<std::uint8_t>(entry[0]);
        return preceding_fields +
               static_cast<std::size_t>(std::popcount(mask & (bit - 1)));
      }
    }

    return std::nullopt;
  }

  std::span<const std::byte> encoded_;
  std::uint32_t payload_size_ = 0;
  std::uint8_t directory_size_ = 0;
  std::uint8_t field_count_ = 0;
  std::uint8_t width_ = 1;
  internal::DirectoryKind kind_ = internal::DirectoryKind::kEmpty;
};

//! Get the encoded size of a message from its payload size and sorted tags
[[nodiscard]] inline Result<std::size_t> MessageSize(std::size_t payload_size,
    std::span<const std::uint8_t> sorted_tags) noexcept {
  auto layout = internal::PlanMessage(payload_size, sorted_tags);
  if (!layout) {
    return std::unexpected(layout.error());
  }

  const std::size_t body_size = layout->body_size;
  if (internal::kMessagePrefixSize >
      std::numeric_limits<std::size_t>::max() - body_size) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  return internal::kMessagePrefixSize + body_size;
}

//! Append an index and patch the message length reserved at start
//! Offsets are relative to the payload; the first field starts at zero.
[[nodiscard]] inline Result<void> FinishMessage(Writer& writer,
    std::size_t start,
    std::span<const std::uint8_t> sorted_tags,
    std::span<const std::uint32_t> offsets) noexcept {
  if (start > writer.position() ||
      writer.position() - start < internal::kMessagePrefixSize ||
      offsets.size() != sorted_tags.size()) {
    return std::unexpected(Error{ErrorCode::kInvalidMessage, start});
  }

  const std::size_t payload_size =
      writer.position() - start - internal::kMessagePrefixSize;
  auto layout = internal::PlanMessage(payload_size, sorted_tags);
  if (!layout) {
    Error error = layout.error();
    error.offset += start;
    return std::unexpected(error);
  }

  std::uint32_t previous = 0;
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    const std::uint32_t offset = offsets[index];
    const bool invalid_start = index == 0 && offset != 0;
    if (invalid_start || offset < previous || offset > payload_size) {
      return std::unexpected(Error{ErrorCode::kInvalidMessage, start});
    }
    previous = offset;
  }

  const std::size_t suffix_size = layout->body_size - payload_size;
  if (suffix_size > writer.remaining()) {
    return std::unexpected(
        Error{ErrorCode::kBufferTooSmall, writer.position()});
  }

  for (std::size_t index = 1; index < offsets.size(); ++index) {
    auto boundary_result =
        internal::WriteOffset(writer, offsets[index], layout->width);
    if (!boundary_result) {
      return boundary_result;
    }
  }

  auto directory_result =
      internal::WriteMessageDirectory(writer, *layout, sorted_tags);
  if (!directory_result) {
    return directory_result;
  }

  std::uint8_t width_code = 0;
  if (layout->width == 2) {
    width_code = 1;
  } else if (layout->width == 4) {
    width_code = 2;
  }

  const auto kind_code = static_cast<std::uint8_t>(layout->kind);
  const auto control = static_cast<std::uint8_t>((kind_code << 2) | width_code);
  auto control_result = writer.Write(control);
  if (!control_result) {
    return control_result;
  }

  return writer.Patch(start, layout->body_size);
}

}  // namespace bebop

namespace bebop::internal {

class Traversal {
 public:
  class Scope {
   public:
    explicit Scope(Traversal* owner) noexcept : owner_(owner) {
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    Scope(Scope&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr)) {
    }

    ~Scope() {
      if (owner_ != nullptr) {
        --owner_->depth_;
      }
    }

   private:
    Traversal* owner_;
  };

  explicit Traversal(DecodeOptions options = {},
      std::span<std::size_t> scratch = {},
      std::pmr::memory_resource* resource = nullptr) noexcept
      : options_(options), scratch_(scratch), resource_(resource) {
  }

  std::pmr::memory_resource* resource() const noexcept {
    if (resource_ == nullptr) {
      return std::pmr::get_default_resource();
    }
    return resource_;
  }

  std::span<std::size_t> scratch() const noexcept {
    return scratch_;
  }

  std::span<std::byte> traversal_scratch() const noexcept {
    if (active_scratch_) {
      return *active_scratch_;
    }
    return options_.traversal_scratch;
  }

  bool has_traversal_scratch() const noexcept {
    return active_scratch_.has_value() || !options_.traversal_scratch.empty();
  }

  std::optional<std::span<std::byte>> active_traversal_scratch()
      const noexcept {
    return active_scratch_;
  }

  void SetTraversalScratch(
      std::optional<std::span<std::byte>> scratch) noexcept {
    active_scratch_ = scratch;
  }

  [[nodiscard]] Result<Scope> Enter() noexcept {
    if (depth_ >= options_.max_depth) {
      return std::unexpected(Error{ErrorCode::kDepthLimit});
    }

    ++depth_;
    return Scope(this);
  }

  [[nodiscard]] Result<void> Consume(std::size_t count) noexcept {
    if (count > options_.max_elements - elements_) {
      return std::unexpected(Error{ErrorCode::kElementLimit});
    }

    elements_ += count;
    return {};
  }

 private:
  DecodeOptions options_;
  std::span<std::size_t> scratch_;
  std::pmr::memory_resource* resource_;
  std::optional<std::span<std::byte>> active_scratch_;
  std::size_t depth_ = 0;
  std::size_t elements_ = 0;
};

template <typename T>
struct AllocatedValue {
  template <std::size_t = 0, typename Allocator, typename... Args>
  static T Make(const Allocator& allocator, Args&&... args) {
    return std::make_obj_using_allocator<T>(
        allocator, std::forward<Args>(args)...);
  }
};

template <typename T, typename... Args>
T* NewResourceObject(std::pmr::memory_resource& resource, Args&&... args) {
  std::pmr::polymorphic_allocator<std::byte> allocator(&resource);
  if constexpr (std::uses_allocator_v<T, decltype(allocator)>) {
    return allocator.template new_object<T>(std::forward<Args>(args)...);
  }
  return allocator.template new_object<T>(
      AllocatedValue<T>::Make(allocator, std::forward<Args>(args)...));
}

template <typename T, std::size_t N>
struct AllocatedValue<std::array<T, N>> {
  using Array = std::array<T, N>;

  template <std::size_t = 0, typename Allocator>
  static Array Make(const Allocator& allocator) {
    return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
      return Array{AllocatedValue<T>::template Make<Indices>(allocator)...};
    }(std::make_index_sequence<N>{});
  }

  template <std::size_t = 0, typename Allocator, typename Source>
  static Array Make(const Allocator& allocator, Source&& source) {
    return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
      return Array{AllocatedValue<T>::Make(
          allocator, std::get<Indices>(std::forward<Source>(source)))...};
    }(std::make_index_sequence<N>{});
  }
};

template <typename T>
struct AllocatedValue<std::optional<T>> {
  template <std::size_t = 0, typename Allocator>
  static std::optional<T> Make(const Allocator&) {
    return std::nullopt;
  }

  template <std::size_t = 0, typename Allocator, typename Source>
  static std::optional<T> Make(const Allocator& allocator, Source&& source) {
    if (!source) {
      return std::nullopt;
    }
    return std::optional<T>(std::in_place,
        AllocatedValue<T>::Make(allocator, *std::forward<Source>(source)));
  }
};

template <typename... Alternatives>
struct AllocatedValue<std::variant<Alternatives...>> {
  using Variant = std::variant<Alternatives...>;

  template <std::size_t = 0, typename Allocator>
  static Variant Make(const Allocator& allocator) {
    using First = std::variant_alternative_t<0, Variant>;
    return Variant(
        std::in_place_index<0>, AllocatedValue<First>::Make(allocator));
  }

  template <std::size_t = 0, typename Allocator, typename Source>
  static Variant Make(const Allocator& allocator, Source&& source) {
    return MakeAlternative<0>(allocator, std::forward<Source>(source));
  }

 private:
  template <std::size_t Index, typename Allocator, typename Source>
  static Variant MakeAlternative(const Allocator& allocator, Source&& source) {
    if constexpr (Index + 1 < sizeof...(Alternatives)) {
      if (source.index() != Index) {
        return MakeAlternative<Index + 1>(
            allocator, std::forward<Source>(source));
      }
    }
    using Value = std::variant_alternative_t<Index, Variant>;
    return Variant(std::in_place_index<Index>,
        AllocatedValue<Value>::Make(
            allocator, std::get<Index>(std::forward<Source>(source))));
  }
};

template <typename T>
struct AllocatedValue<std::unique_ptr<T, ResourceDeleter<T>>> {
  using Pointer = std::unique_ptr<T, ResourceDeleter<T>>;

  template <std::size_t = 0, typename Allocator>
  static Pointer Make(const Allocator& allocator) {
    return Pointer(nullptr, ResourceDeleter<T>{allocator.resource()});
  }

  template <std::size_t = 0, typename Allocator>
  static Pointer Make(const Allocator& allocator, Pointer&& source) {
    auto* resource = allocator.resource();
    if (!source) {
      return Pointer(nullptr, ResourceDeleter<T>{resource});
    }
    if (source.get_deleter().resource == resource) {
      return std::move(source);
    }
    Pointer value(NewResourceObject<T>(*resource, std::move(*source)),
        ResourceDeleter<T>{resource});
    source.reset();
    return value;
  }
};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Key,
    typename Value,
    typename Compare,
    typename Keys,
    typename Values>
struct AllocatedValue<std::flat_map<Key, Value, Compare, Keys, Values>> {
  using Map = std::flat_map<Key, Value, Compare, Keys, Values>;

  template <std::size_t = 0, typename Allocator>
  static Map Make(const Allocator& allocator) {
    return Map(std::sorted_unique,
        AllocatedValue<Keys>::Make(allocator),
        AllocatedValue<Values>::Make(allocator));
  }

  template <std::size_t = 0, typename Allocator>
  static Map Make(const Allocator& allocator, const Map& source) {
    return Map(std::sorted_unique,
        AllocatedValue<Keys>::Make(allocator, source.keys()),
        AllocatedValue<Values>::Make(allocator, source.values()),
        source.key_comp());
  }

  template <std::size_t = 0, typename Allocator>
  static Map Make(const Allocator& allocator, Map&& source) {
    auto compare = source.key_comp();
    auto storage = std::move(source).extract();
    return Map(std::sorted_unique,
        AllocatedValue<Keys>::Make(allocator, std::move(storage.keys)),
        AllocatedValue<Values>::Make(allocator, std::move(storage.values)),
        compare);
  }
};
#endif

template <typename T>
T& EmplaceDecoded(std::optional<T>& value, Traversal& context) {
  const std::pmr::polymorphic_allocator<std::byte> allocator(
      context.resource());
  return value.emplace(AllocatedValue<T>::Make(allocator));
}

template <typename T>
T& EmplaceDecoded(std::unique_ptr<T>& value, Traversal& context) {
  const std::pmr::polymorphic_allocator<std::byte> allocator(
      context.resource());
  value = std::make_unique<T>(AllocatedValue<T>::Make(allocator));
  return *value;
}

template <typename T>
T& EmplaceDecoded(
    std::unique_ptr<T, ResourceDeleter<T>>& value, Traversal& context) {
  auto& resource = *context.resource();
  T* object = NewResourceObject<T>(resource);
  value = std::unique_ptr<T, ResourceDeleter<T>>(
      object, ResourceDeleter<T>{&resource});
  return *value;
}

template <std::size_t Index, typename... Alternatives>
auto& EmplaceDecoded(std::variant<Alternatives...>& value, Traversal& context) {
  using T = std::variant_alternative_t<Index, std::variant<Alternatives...>>;
  const std::pmr::polymorphic_allocator<std::byte> allocator(
      context.resource());
  return value.template emplace<Index>(AllocatedValue<T>::Make(allocator));
}

template <typename Context>
class TraversalStack {
 public:
  TraversalStack(std::span<std::byte> storage, Traversal& traversal) noexcept
      : storage_(storage),
        traversal_(traversal),
        previous_scratch_(traversal.active_traversal_scratch()) {
    traversal_.SetTraversalScratch(storage_);
  }

  TraversalStack(const TraversalStack&) = delete;
  TraversalStack& operator=(const TraversalStack&) = delete;

  ~TraversalStack() {
    Clear();
    traversal_.SetTraversalScratch(previous_scratch_);
  }

  template <typename Frame, typename... Args>
  [[nodiscard]] Result<void> Push(Args&&... args) {
    auto scope = traversal_.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    if (used_ == storage_.size()) {
      return std::unexpected(Error{ErrorCode::kScratchTooSmall});
    }

    void* address = storage_.data() + used_;
    std::size_t available = storage_.size() - used_;
    void* header_address =
        std::align(alignof(Header), sizeof(Header), address, available);
    if (header_address == nullptr) {
      return std::unexpected(Error{ErrorCode::kScratchTooSmall});
    }

    address = static_cast<std::byte*>(header_address) + sizeof(Header);
    available -= sizeof(Header);
    void* frame_address =
        std::align(alignof(Frame), sizeof(Frame), address, available);
    if (frame_address == nullptr) {
      return std::unexpected(Error{ErrorCode::kScratchTooSmall});
    }

    Frame* frame = std::construct_at(
        static_cast<Frame*>(frame_address), std::forward<Args>(args)...);
    Header* header = std::construct_at(static_cast<Header*>(header_address),
        top_,
        used_,
        frame,
        Resume<Frame>,
        Destroy<Frame>,
        std::move(*scope));
    top_ = header;
    used_ = static_cast<std::size_t>(
                static_cast<std::byte*>(frame_address) - storage_.data()) +
            sizeof(Frame);
    traversal_.SetTraversalScratch(storage_.subspan(used_));
    return {};
  }

  [[nodiscard]] Result<void> Run(Context& context) {
    while (top_ != nullptr) {
      auto result = top_->resume(top_->frame, context, *this);
      if (!result) {
        Clear();
        return result;
      }
    }
    return {};
  }

  void Pop() noexcept {
    Header* header = top_;
    top_ = header->previous;
    used_ = header->previous_used;
    header->destroy(header->frame);
    std::destroy_at(header);
    traversal_.SetTraversalScratch(storage_.subspan(used_));
  }

  bool empty() const noexcept {
    return top_ == nullptr;
  }

 private:
  struct Header {
    Header* previous;
    std::size_t previous_used;
    void* frame;
    Result<void> (*resume)(void*, Context&, TraversalStack&);
    void (*destroy)(void*) noexcept;
    Traversal::Scope scope;
  };

  template <typename Frame>
  static Result<void> Resume(
      void* frame, Context& context, TraversalStack& stack) {
    return static_cast<Frame*>(frame)->Step(context, stack);
  }

  template <typename Frame>
  static void Destroy(void* frame) noexcept {
    std::destroy_at(static_cast<Frame*>(frame));
  }

  void Clear() noexcept {
    while (top_ != nullptr) {
      Pop();
    }
  }

  std::span<std::byte> storage_;
  Traversal& traversal_;
  std::optional<std::span<std::byte>> previous_scratch_;
  Header* top_ = nullptr;
  std::size_t used_ = 0;
};

enum class SequenceKind {
  kFixed,
  kDynamic,
};

[[nodiscard]] inline Result<std::size_t> AddSize(
    std::size_t size, std::size_t amount) noexcept {
  if (amount > std::numeric_limits<std::size_t>::max() - size) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  return size + amount;
}

}  // namespace bebop::internal

namespace bebop {

//! Construct an owning pointer using a memory resource
//! The resource must outlive the returned pointer.
template <typename T, typename... Args>
[[nodiscard]] std::unique_ptr<T, ResourceDeleter<T>> AllocateUnique(
    std::pmr::memory_resource& resource, Args&&... args) {
  T* value =
      internal::NewResourceObject<T>(resource, std::forward<Args>(args)...);
  return std::unique_ptr<T, ResourceDeleter<T>>(
      value, ResourceDeleter<T>{&resource});
}

//! Borrowed map entries for encoding
//! The entries and their referenced storage must outlive the encoding
//! operation.
template <typename Key, typename Value>
class MapRef {
 public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<Key, Value>;

  constexpr MapRef() noexcept = default;

  constexpr MapRef(const value_type* entries, std::size_t count) noexcept
      : entries_(entries), count_(count) {
  }

  template <typename Range>
    requires(!std::same_as<std::remove_cvref_t<Range>, MapRef>) &&
                std::ranges::contiguous_range<Range> &&
                std::ranges::sized_range<Range> &&
                std::ranges::borrowed_range<Range> &&
                std::convertible_to<decltype(std::ranges::data(
                                        std::declval<Range&>())),
                    const value_type*>
  constexpr MapRef(Range&& entries) noexcept(
      noexcept(std::ranges::data(entries)) &&
      noexcept(std::ranges::size(entries)))
      : entries_(std::ranges::data(entries)),
        count_(std::ranges::size(entries)) {
  }

  std::size_t size() const noexcept {
    return count_;
  }

  const value_type* begin() const noexcept {
    return entries_;
  }

  const value_type* end() const noexcept {
    if (count_ == 0) {
      return entries_;
    }
    return entries_ + count_;
  }

 private:
  const value_type* entries_ = nullptr;
  std::size_t count_ = 0;
};

//! Serialization traits specialized by generated records
//! Allocating codecs retain the selected allocator's failure behavior.
template <typename T>
struct Codec;

template <WireScalar T>
  requires(!std::is_enum_v<T>)
struct Codec<T> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = sizeof(T);
  static constexpr bool kDirectCopy = !std::same_as<T, bool>;

  [[nodiscard]] static Result<std::size_t> Size(
      const T&, internal::Traversal&) noexcept {
    return sizeof(T);
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, const T& value, internal::Traversal&) noexcept {
    return writer.Write(value);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, T& value, internal::Traversal&) noexcept {
    auto result = reader.Read<T>();
    if (!result) {
      return std::unexpected(result.error());
    }

    value = *result;
    return {};
  }
};

template <typename T>
  requires std::is_enum_v<T>
struct Codec<T> {
  using Underlying = std::underlying_type_t<T>;

  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = sizeof(Underlying);
  static constexpr bool kDirectCopy = true;

  [[nodiscard]] static Result<std::size_t> Size(
      T, internal::Traversal&) noexcept {
    return kMinSize;
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, T value, internal::Traversal&) noexcept {
    return writer.Write(std::to_underlying(value));
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, T& value, internal::Traversal&) noexcept {
    auto result = reader.Read<Underlying>();
    if (!result) {
      return std::unexpected(result.error());
    }
    value = static_cast<T>(*result);
    return {};
  }
};

template <>
struct Codec<Uuid> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 16;

  [[nodiscard]] static Result<std::size_t> Size(
      const Uuid&, internal::Traversal&) noexcept {
    return kMinSize;
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, const Uuid& value, internal::Traversal&) noexcept {
    return writer.WriteUuid(value);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, Uuid& value, internal::Traversal&) noexcept {
    auto result = reader.ReadUuid();
    if (!result) {
      return std::unexpected(result.error());
    }

    value = *result;
    return {};
  }
};

template <>
struct Codec<Timestamp> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 16;

  [[nodiscard]] static Result<std::size_t> Size(
      const Timestamp&, internal::Traversal&) noexcept {
    return kMinSize;
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, Timestamp value, internal::Traversal&) noexcept {
    return writer.WriteTimestamp(value);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, Timestamp& value, internal::Traversal&) noexcept {
    auto result = reader.ReadTimestamp();
    if (!result) {
      return std::unexpected(result.error());
    }

    value = *result;
    return {};
  }
};

template <>
struct Codec<Duration> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 12;

  [[nodiscard]] static Result<std::size_t> Size(
      const Duration&, internal::Traversal&) noexcept {
    return kMinSize;
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, Duration value, internal::Traversal&) noexcept {
    return writer.WriteDuration(value);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, Duration& value, internal::Traversal&) noexcept {
    auto result = reader.ReadDuration();
    if (!result) {
      return std::unexpected(result.error());
    }

    value = *result;
    return {};
  }
};

template <>
struct Codec<std::string_view> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 5;

  [[nodiscard]] static Result<std::size_t> Size(
      std::string_view value, internal::Traversal&) noexcept {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(Error{ErrorCode::kOverflow});
    }

    return internal::AddSize(value.size(), kMinSize);
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, std::string_view value, internal::Traversal&) noexcept {
    return writer.WriteString(value);
  }
};

template <typename Traits, typename Allocator>
struct Codec<std::basic_string<char, Traits, Allocator>> {
  using String = std::basic_string<char, Traits, Allocator>;

  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 5;

  [[nodiscard]] static Result<std::size_t> Size(
      const String& value, internal::Traversal& context) noexcept {
    return Codec<std::string_view>::Size(
        std::string_view(value.data(), value.size()), context);
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, const String& value, internal::Traversal&) noexcept {
    return writer.WriteString(std::string_view(value.data(), value.size()));
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, String& value, internal::Traversal&) {
    auto result = reader.ReadString();
    if (!result) {
      return std::unexpected(result.error());
    }

    value.assign(result->data(), result->size());
    return {};
  }
};

}  // namespace bebop

namespace bebop::internal {

template <typename T>
concept BulkScalar =
    WireScalar<T> && requires { requires Codec<T>::kDirectCopy; };

template <typename T, typename Range>
[[nodiscard]] Result<std::size_t> SequenceSize(
    const Range& value, Traversal& context, SequenceKind kind) {
  auto scope = context.Enter();
  if (!scope) {
    return std::unexpected(scope.error());
  }

  std::size_t size = 0;
  if (kind == SequenceKind::kDynamic) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(Error{ErrorCode::kOverflow});
    }
    size = 4;
  }

  auto count_result = context.Consume(value.size());
  if (!count_result) {
    return std::unexpected(count_result.error());
  }

  if constexpr (BulkScalar<T>) {
    if (value.size() >
        (std::numeric_limits<std::size_t>::max() - size) / sizeof(T)) {
      return std::unexpected(Error{ErrorCode::kOverflow});
    }
    return size + value.size() * sizeof(T);
  }

  for (std::size_t index = 0; index < value.size(); ++index) {
    auto element_size = Codec<T>::Size(value[index], context);
    if (!element_size) {
      return std::unexpected(element_size.error());
    }

    auto total_size = AddSize(size, *element_size);
    if (!total_size) {
      return total_size;
    }
    size = *total_size;
  }

  return size;
}

template <typename T, typename Range>
[[nodiscard]] Result<void> EncodeSequence(
    Writer& writer, const Range& value, Traversal& context, SequenceKind kind) {
  auto scope = context.Enter();
  if (!scope) {
    return std::unexpected(scope.error());
  }

  if (kind == SequenceKind::kDynamic &&
      value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(Error{ErrorCode::kOverflow, writer.position()});
  }

  auto count_result = context.Consume(value.size());
  if (!count_result) {
    return count_result;
  }

  if (kind == SequenceKind::kDynamic) {
    auto length_result = writer.Write(static_cast<std::uint32_t>(value.size()));
    if (!length_result) {
      return length_result;
    }
  }

  if constexpr (BulkScalar<T> && std::ranges::contiguous_range<const Range>) {
    return writer.WriteBytes(
        std::as_bytes(std::span(value.data(), value.size())));
  }

  for (std::size_t index = 0; index < value.size(); ++index) {
    auto element_result = Codec<T>::Encode(writer, value[index], context);
    if (!element_result) {
      return element_result;
    }
  }

  return {};
}

}  // namespace bebop::internal

namespace bebop {

template <typename T, typename Allocator>
struct Codec<std::vector<T, Allocator>> {
  using Vector = std::vector<T, Allocator>;

  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 4;

  [[nodiscard]] static Result<std::size_t> Size(
      const Vector& value, internal::Traversal& context) {
    return internal::SequenceSize<T>(
        value, context, internal::SequenceKind::kDynamic);
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, const Vector& value, internal::Traversal& context) {
    return internal::EncodeSequence<T>(
        writer, value, context, internal::SequenceKind::kDynamic);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, Vector& value, internal::Traversal& context) {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }

    auto count = reader.Read<std::uint32_t>();
    if (!count) {
      return std::unexpected(count.error());
    }

    auto count_result = context.Consume(*count);
    if (!count_result) {
      return count_result;
    }

    if constexpr (Codec<T>::kMinSize != 0) {
      if (*count > reader.remaining() / Codec<T>::kMinSize) {
        return std::unexpected(
            Error{ErrorCode::kTruncated, reader.absolute_position()});
      }
    }

    if constexpr (internal::BulkScalar<T>) {
      auto bytes =
          reader.ReadBytes(static_cast<std::size_t>(*count) * sizeof(T));
      if (!bytes) {
        return std::unexpected(bytes.error());
      }
      value.resize(*count);
      if (!bytes->empty()) {
        std::memcpy(value.data(), bytes->data(), bytes->size());
      }
      return {};
    }

    value.clear();
    value.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
      T element = std::make_obj_using_allocator<T>(value.get_allocator());
      auto element_result = Codec<T>::Decode(reader, element, context);
      if (!element_result) {
        return element_result;
      }
      value.push_back(std::move(element));
    }

    return {};
  }
};

template <typename T, std::size_t N>
struct Codec<std::array<T, N>> {
  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = Codec<T>::kMinSize * N;

  [[nodiscard]] static Result<std::size_t> Size(
      const std::array<T, N>& value, internal::Traversal& context) {
    return internal::SequenceSize<T>(
        value, context, internal::SequenceKind::kFixed);
  }

  [[nodiscard]] static Result<void> Encode(Writer& writer,
      const std::array<T, N>& value,
      internal::Traversal& context) {
    return internal::EncodeSequence<T>(
        writer, value, context, internal::SequenceKind::kFixed);
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, std::array<T, N>& value, internal::Traversal& context) {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }

    auto count_result = context.Consume(N);
    if (!count_result) {
      return count_result;
    }

    if constexpr (internal::BulkScalar<T>) {
      auto bytes = reader.ReadBytes(N * sizeof(T));
      if (!bytes) {
        return std::unexpected(bytes.error());
      }
      if (!bytes->empty()) {
        std::memcpy(value.data(), bytes->data(), bytes->size());
      }
      return {};
    }

    for (T& element : value) {
      auto element_result = Codec<T>::Decode(reader, element, context);
      if (!element_result) {
        return element_result;
      }
    }

    return {};
  }
};

template <typename T, std::size_t N>
struct Codec<std::span<T, N>> {
  static constexpr bool kRecord = false;
  static constexpr internal::SequenceKind kKind = [] {
    if constexpr (N == std::dynamic_extent) {
      return internal::SequenceKind::kDynamic;
    }
    return internal::SequenceKind::kFixed;
  }();

  static constexpr std::size_t kMinSize = [] {
    if constexpr (N == std::dynamic_extent) {
      return std::size_t{4};
    }
    return Codec<std::remove_const_t<T>>::kMinSize * N;
  }();

  [[nodiscard]] static Result<std::size_t> Size(
      std::span<T, N> value, internal::Traversal& context) {
    return internal::SequenceSize<std::remove_const_t<T>>(
        value, context, kKind);
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, std::span<T, N> value, internal::Traversal& context) {
    return internal::EncodeSequence<std::remove_const_t<T>>(
        writer, value, context, kKind);
  }
};

}  // namespace bebop

namespace bebop::internal {

template <typename Key, typename Value>
[[nodiscard]] Result<std::uint32_t> ReadMapCount(
    Reader& reader, Traversal& context) {
  auto count = reader.Read<std::uint32_t>();
  if (!count) {
    return std::unexpected(count.error());
  }
  auto consumed = context.Consume(*count);
  if (!consumed) {
    return std::unexpected(consumed.error());
  }
  constexpr auto entry_min_size = Codec<Key>::kMinSize + Codec<Value>::kMinSize;
  if constexpr (entry_min_size != 0) {
    if (*count > reader.remaining() / entry_min_size) {
      return std::unexpected(
          Error{ErrorCode::kTruncated, reader.absolute_position()});
    }
  }
  return count;
}

template <typename T, typename Container>
T MakeElement(const Container& container) {
  if constexpr (requires { container.get_allocator(); }) {
    return std::make_obj_using_allocator<T>(container.get_allocator());
  } else {
    return T{};
  }
}

template <typename Map>
struct MapCodec {
  using Key = typename Map::key_type;
  using Value = typename Map::mapped_type;

  static constexpr bool kRecord = false;
  static constexpr std::size_t kMinSize = 4;

  [[nodiscard]] static Result<std::size_t> Size(
      const Map& value, Traversal& context) {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(Error{ErrorCode::kOverflow});
    }
    auto count = context.Consume(value.size());
    if (!count) {
      return std::unexpected(count.error());
    }
    std::size_t size = kMinSize;
    for (const auto& [key, element] : value) {
      auto key_size = Codec<Key>::Size(key, context);
      if (!key_size) {
        return std::unexpected(key_size.error());
      }
      auto element_size = Codec<Value>::Size(element, context);
      if (!element_size) {
        return std::unexpected(element_size.error());
      }
      auto with_key = AddSize(size, *key_size);
      if (!with_key) {
        return with_key;
      }
      auto with_value = AddSize(*with_key, *element_size);
      if (!with_value) {
        return with_value;
      }
      size = *with_value;
    }
    return size;
  }

  [[nodiscard]] static Result<void> Encode(
      Writer& writer, const Map& value, Traversal& context) {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(Error{ErrorCode::kOverflow, writer.position()});
    }
    auto count = context.Consume(value.size());
    if (!count) {
      return count;
    }
    auto prefix = writer.Write(static_cast<std::uint32_t>(value.size()));
    if (!prefix) {
      return prefix;
    }
    for (const auto& [key, element] : value) {
      auto key_result = Codec<Key>::Encode(writer, key, context);
      if (!key_result) {
        return key_result;
      }
      auto element_result = Codec<Value>::Encode(writer, element, context);
      if (!element_result) {
        return element_result;
      }
    }
    return {};
  }

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, Map& value, Traversal& context) {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    auto count = ReadMapCount<Key, Value>(reader, context);
    if (!count) {
      return std::unexpected(count.error());
    }
    value.clear();
    if constexpr (requires { value.reserve(*count); }) {
      value.reserve(*count);
    }
    for (std::uint32_t index = 0; index < *count; ++index) {
      Key key = MakeElement<Key, true>(value);
      Value element = MakeElement<Value, false>(value);
      const std::size_t entry_offset = reader.absolute_position();
      auto key_result = Codec<Key>::Decode(reader, key, context);
      if (!key_result) {
        return key_result;
      }
      auto element_result = Codec<Value>::Decode(reader, element, context);
      if (!element_result) {
        return element_result;
      }
      const auto insertion = value.emplace(std::move(key), std::move(element));
      if (!insertion.second) {
        return std::unexpected(
            Error{ErrorCode::kDuplicateMapKey, entry_offset});
      }
    }
    return {};
  }

 private:
  template <typename T, bool IsKey>
  static T MakeElement(const Map& value) {
    if constexpr (requires { value.get_allocator(); }) {
      return std::make_obj_using_allocator<T>(value.get_allocator());
    } else if constexpr (IsKey && requires { value.keys().get_allocator(); }) {
      return std::make_obj_using_allocator<T>(value.keys().get_allocator());
    } else if constexpr (!IsKey &&
                         requires { value.values().get_allocator(); }) {
      return std::make_obj_using_allocator<T>(value.values().get_allocator());
    } else {
      return T{};
    }
  }
};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Keys, typename Values, typename Compare>
bool SortMapStorage(Keys& keys, Values& values, Compare compare) {
  const std::size_t count = keys.size();
  bool sorted = true;
  for (std::size_t index = 1; index < count; ++index) {
    if (compare(keys[index], keys[index - 1])) {
      sorted = false;
      break;
    }
    if (!compare(keys[index - 1], keys[index])) {
      return false;
    }
  }
  if (sorted) {
    return true;
  }
  auto swap = [&](std::size_t left, std::size_t right) {
    std::swap(keys[left], keys[right]);
    std::swap(values[left], values[right]);
  };
  auto sift = [&](std::size_t root, std::size_t size) {
    while (root < size / 2) {
      std::size_t child = root * 2 + 1;
      if (child + 1 < size && compare(keys[child], keys[child + 1])) {
        ++child;
      }
      if (!compare(keys[root], keys[child])) {
        break;
      }
      swap(root, child);
      root = child;
    }
  };
  for (std::size_t parent = count / 2; parent != 0; --parent) {
    sift(parent - 1, count);
  }
  for (std::size_t size = count; size > 1; --size) {
    swap(0, size - 1);
    sift(0, size - 1);
  }
  for (std::size_t index = 1; index < count; ++index) {
    if (!compare(keys[index - 1], keys[index])) {
      return false;
    }
  }
  return true;
}
#endif

}  // namespace bebop::internal

namespace bebop {

template <typename Key, typename Value>
struct Codec<MapRef<Key, Value>> : internal::MapCodec<MapRef<Key, Value>> {
  static Result<void> Decode(
      Reader&, MapRef<Key, Value>&, internal::Traversal&) = delete;
};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct Codec<std::map<Key, Value, Compare, Allocator>>
    : internal::MapCodec<std::map<Key, Value, Compare, Allocator>> {};

template <typename Key,
    typename Value,
    typename Hasher,
    typename Equal,
    typename Allocator>
struct Codec<std::unordered_map<Key, Value, Hasher, Equal, Allocator>>
    : internal::MapCodec<
          std::unordered_map<Key, Value, Hasher, Equal, Allocator>> {};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Key,
    typename Value,
    typename Compare,
    typename KeyContainer,
    typename ValueContainer>
struct Codec<std::flat_map<Key, Value, Compare, KeyContainer, ValueContainer>>
    : internal::MapCodec<
          std::flat_map<Key, Value, Compare, KeyContainer, ValueContainer>> {
  using Map = std::flat_map<Key, Value, Compare, KeyContainer, ValueContainer>;

  [[nodiscard]] static Result<void> Decode(
      Reader& reader, Map& value, internal::Traversal& context) {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    auto count = internal::ReadMapCount<Key, Value>(reader, context);
    if (!count) {
      return std::unexpected(count.error());
    }
    const std::size_t entries_start = reader.absolute_position();
    auto compare = value.key_comp();
    auto storage = std::move(value).extract();
    storage.keys.clear();
    storage.values.clear();
    if constexpr (requires { storage.keys.reserve(*count); }) {
      storage.keys.reserve(*count);
    }
    if constexpr (requires { storage.values.reserve(*count); }) {
      storage.values.reserve(*count);
    }
    for (std::uint32_t index = 0; index < *count; ++index) {
      Key key = internal::MakeElement<Key>(storage.keys);
      Value element = internal::MakeElement<Value>(storage.values);
      auto key_result = Codec<Key>::Decode(reader, key, context);
      if (!key_result) {
        return key_result;
      }
      auto value_result = Codec<Value>::Decode(reader, element, context);
      if (!value_result) {
        return value_result;
      }
      storage.keys.push_back(std::move(key));
      storage.values.push_back(std::move(element));
    }
    if (!internal::SortMapStorage(storage.keys, storage.values, compare)) {
      return std::unexpected(Error{ErrorCode::kDuplicateMapKey, entries_start});
    }
    Map decoded(std::sorted_unique,
        std::move(storage.keys),
        std::move(storage.values),
        compare);
    value = std::move(decoded);
    return {};
  }
};
#endif

}  // namespace bebop

namespace bebop::internal {

enum class TraversalMode {
  kSize,
  kEncode,
  kDecode,
};

template <typename T>
struct MessageTraversal;

template <typename T>
struct StructTraversal;

template <typename T>
struct UnionTraversal;

template <std::uint8_t Tag, auto Member>
struct TraversalField {
  static constexpr std::uint8_t kTag = Tag;

  template <typename T>
  static decltype(auto) Get(T& value) {
    return std::invoke(Member, value);
  }
};

template <typename T>
struct OwnershipFields;

class OwnershipQueue;
class OwnershipMoveQueue;
struct OwnershipNode;

struct OwnershipOperations {
  void (*collect)(OwnershipNode&, OwnershipQueue&) noexcept;
  void (*clear)(OwnershipNode&) noexcept;
  void (*move)(OwnershipNode&, OwnershipNode&, OwnershipMoveQueue&);
  const OwnershipOperations* finish;
  const OwnershipOperations* initial;
};

struct OwnershipNode {
  OwnershipNode* next = nullptr;
  const OwnershipOperations* operations;
};

template <typename T>
const OwnershipOperations& GetOwnershipOperations() noexcept;

template <typename T>
class OwnedNode : public OwnershipNode {
 protected:
  OwnedNode() noexcept : OwnershipNode{nullptr, &GetOwnershipOperations<T>()} {
  }

  OwnedNode(const OwnedNode&) noexcept : OwnedNode() {
  }

  OwnedNode(OwnedNode&&) noexcept : OwnedNode() {
  }

  OwnedNode& operator=(const OwnedNode&) noexcept {
    return *this;
  }

  OwnedNode& operator=(OwnedNode&&) noexcept {
    return *this;
  }
};

enum class OwnershipKind {
  kLeaf,
  kRecord,
  kOptional,
  kPointer,
  kArray,
  kVector,
  kMap,
  kFlatMap,
  kVariant,
};

template <typename T>
struct OwnershipShape {
  static constexpr auto kKind = OwnershipKind::kLeaf;
  static constexpr bool kHasNodes = false;
};

template <typename T>
  requires requires { OwnershipFields<T>::kFields; }
struct OwnershipShape<T> {
  static constexpr auto kKind = OwnershipKind::kRecord;
  static constexpr bool kHasNodes = true;
};

template <typename T>
struct OwnershipShape<std::optional<T>> {
  static constexpr auto kKind = OwnershipKind::kOptional;
  static constexpr bool kHasNodes = OwnershipShape<T>::kHasNodes;
};

template <typename T, typename Deleter>
struct OwnershipShape<std::unique_ptr<T, Deleter>> {
  static constexpr auto kKind = OwnershipKind::kPointer;
  static constexpr bool kHasNodes = OwnershipShape<T>::kHasNodes;
};

template <typename T, std::size_t N>
struct OwnershipShape<std::array<T, N>> {
  static constexpr auto kKind = OwnershipKind::kArray;
  static constexpr bool kHasNodes = OwnershipShape<T>::kHasNodes;
};

template <typename T, typename Allocator>
struct OwnershipShape<std::vector<T, Allocator>> {
  static constexpr auto kKind = OwnershipKind::kVector;
  static constexpr bool kHasNodes = OwnershipShape<T>::kHasNodes;
};

template <typename K, typename V, typename C, typename A>
struct OwnershipShape<std::map<K, V, C, A>> {
  static constexpr auto kKind = OwnershipKind::kMap;
  static constexpr bool kHasNodes = OwnershipShape<V>::kHasNodes;
};

template <typename K, typename V, typename H, typename E, typename A>
struct OwnershipShape<std::unordered_map<K, V, H, E, A>> {
  static constexpr auto kKind = OwnershipKind::kMap;
  static constexpr bool kHasNodes = OwnershipShape<V>::kHasNodes;
};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename K, typename V, typename C, typename Keys, typename Values>
struct OwnershipShape<std::flat_map<K, V, C, Keys, Values>> {
  static constexpr auto kKind = OwnershipKind::kFlatMap;
  static constexpr bool kHasNodes = OwnershipShape<V>::kHasNodes;
};
#endif

template <typename... T>
struct OwnershipShape<std::variant<T...>> {
  static constexpr auto kKind = OwnershipKind::kVariant;
  static constexpr bool kHasNodes = (OwnershipShape<T>::kHasNodes || ...);
};

template <typename T, typename Fn>
void VisitOwnershipFields(T& value, Fn&& fn) {
  std::apply(
      [&](auto... fields) {
        (std::invoke(fn, fields.Get(value)), ...);
      },
      OwnershipFields<std::remove_cv_t<T>>::kFields);
}

class OwnershipQueue {
 public:
  void Push(OwnershipNode& value) noexcept {
    value.next = head_;
    head_ = &value;
  }

  void Run() noexcept {
    while (head_ != nullptr) {
      auto& value = *head_;
      head_ = value.next;
      value.next = nullptr;
      const auto* operations = value.operations;
      if (operations->collect == nullptr) {
        operations->clear(value);
        value.operations = operations->initial;
      } else {
        value.operations = operations->finish;
        Push(value);
        operations->collect(value, *this);
      }
    }
  }

 private:
  OwnershipNode* head_ = nullptr;
};

inline thread_local bool ownership_draining = false;

template <typename T>
void DestroyOwned(T& value) noexcept {
  if (ownership_draining) {
    return;
  }
  ownership_draining = true;
  OwnershipQueue queue;
  queue.Push(OwnershipFields<T>::Node(value));
  queue.Run();
  ownership_draining = false;
}

template <typename T>
void CollectOwned(T& value, OwnershipQueue& queue) noexcept {
  using Shape = OwnershipShape<T>;
  if constexpr (!Shape::kHasNodes) {
    return;
  } else if constexpr (Shape::kKind == OwnershipKind::kRecord) {
    if constexpr (OwnershipFields<T>::kNode) {
      queue.Push(OwnershipFields<T>::Node(value));
    } else {
      VisitOwnershipFields(value, [&](auto& field) {
        CollectOwned(field, queue);
      });
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kOptional ||
                       Shape::kKind == OwnershipKind::kPointer) {
    if (value) {
      CollectOwned(*value, queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kVariant) {
    if (!value.valueless_by_exception()) {
      std::visit(
          [&](auto& element) {
            CollectOwned(element, queue);
          },
          value);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kMap) {
    for (auto& [key, element] : value) {
      CollectOwned(element, queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kFlatMap) {
    for (auto element : value) {
      CollectOwned(element.second, queue);
    }
  } else {
    for (auto& element : value) {
      CollectOwned(element, queue);
    }
  }
}

template <typename T>
void ClearOwned(T& value) noexcept {
  using Shape = OwnershipShape<T>;
  if constexpr (!Shape::kHasNodes) {
    return;
  } else if constexpr (Shape::kKind == OwnershipKind::kRecord) {
    if constexpr (!OwnershipFields<T>::kNode) {
      VisitOwnershipFields(value, [](auto& field) {
        ClearOwned(field);
      });
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kOptional ||
                       Shape::kKind == OwnershipKind::kPointer) {
    value.reset();
  } else if constexpr (Shape::kKind == OwnershipKind::kArray) {
    for (auto& element : value) {
      ClearOwned(element);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kVariant) {
    if (!value.valueless_by_exception()) {
      std::visit(
          [](auto& element) {
            ClearOwned(element);
          },
          value);
    }
  } else {
    value.clear();
  }
}

class OwnershipMoveQueue {
 public:
  explicit OwnershipMoveQueue(std::pmr::memory_resource& resource)
      : allocator(&resource) {
  }

  void Push(OwnershipNode& destination, OwnershipNode& source) noexcept {
    source.next = head_;
    destination.next = &source;
    head_ = &destination;
  }

  void Run() {
    while (head_ != nullptr) {
      auto& destination = *head_;
      auto& source = *destination.next;
      head_ = source.next;
      source.next = nullptr;
      destination.next = nullptr;
      source.operations->move(destination, source, *this);
    }
  }

  const std::pmr::polymorphic_allocator<std::byte> allocator;

 private:
  OwnershipNode* head_ = nullptr;
};

template <typename T>
bool CanStealOwned(const T& value, std::pmr::memory_resource& resource) {
  using Shape = OwnershipShape<T>;
  if constexpr (Shape::kKind == OwnershipKind::kRecord) {
    bool equal = true;
    VisitOwnershipFields(value, [&](const auto& field) {
      equal = equal && CanStealOwned(field, resource);
    });
    return equal;
  } else if constexpr (Shape::kKind == OwnershipKind::kOptional) {
    return !value || CanStealOwned(*value, resource);
  } else if constexpr (Shape::kKind == OwnershipKind::kPointer) {
    if constexpr (requires { value.get_deleter().resource; }) {
      return !value || value.get_deleter().resource == &resource;
    }
    return true;
  } else if constexpr (requires { value.get_allocator().resource(); }) {
    return value.get_allocator().resource() == &resource;
  } else if constexpr (Shape::kKind == OwnershipKind::kArray) {
    return std::ranges::all_of(value, [&](const auto& element) {
      return CanStealOwned(element, resource);
    });
  } else if constexpr (Shape::kKind == OwnershipKind::kVariant) {
    if (value.valueless_by_exception()) {
      return true;
    }
    return std::visit(
        [&](const auto& element) {
          return CanStealOwned(element, resource);
        },
        value);
  } else if constexpr (Shape::kKind == OwnershipKind::kFlatMap) {
    return value.keys().get_allocator().resource() == &resource &&
           value.values().get_allocator().resource() == &resource;
  } else {
    return true;
  }
}

template <typename T>
void MoveOwnedValue(T& destination, T& source, OwnershipMoveQueue& queue);

template <std::size_t Index = 0, typename T>
void MoveOwnedVariant(T& destination, T& source, OwnershipMoveQueue& queue) {
  if (source.index() == Index) {
    using Value = std::variant_alternative_t<Index, T>;
    auto& target = destination.template emplace<Index>(
        AllocatedValue<Value>::Make(queue.allocator));
    MoveOwnedValue(target, std::get<Index>(source), queue);
    return;
  }
  if constexpr (Index + 1 < std::variant_size_v<T>) {
    MoveOwnedVariant<Index + 1>(destination, source, queue);
  }
}

template <typename T>
void MoveOwnedValue(T& destination, T& source, OwnershipMoveQueue& queue) {
  using Shape = OwnershipShape<T>;
  if constexpr (!Shape::kHasNodes) {
    destination = AllocatedValue<T>::Make(queue.allocator, std::move(source));
  } else if constexpr (Shape::kKind == OwnershipKind::kRecord) {
    if constexpr (OwnershipFields<T>::kNode) {
      queue.Push(OwnershipFields<T>::Node(destination),
          OwnershipFields<T>::Node(source));
    } else {
      std::apply(
          [&](auto... fields) {
            (MoveOwnedValue(fields.Get(destination), fields.Get(source), queue),
                ...);
          },
          OwnershipFields<T>::kFields);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kOptional) {
    if (source) {
      using Value = typename T::value_type;
      auto& target =
          destination.emplace(AllocatedValue<Value>::Make(queue.allocator));
      MoveOwnedValue(target, *source, queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kPointer) {
    if (source) {
      using Value = typename T::element_type;
      if constexpr (requires { source.get_deleter().resource; }) {
        auto& resource = *queue.allocator.resource();
        destination = T(NewResourceObject<Value>(resource),
            ResourceDeleter<Value>{&resource});
      } else {
        destination = std::make_unique<Value>(
            AllocatedValue<Value>::Make(queue.allocator));
      }
      MoveOwnedValue(*destination, *source, queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kArray) {
    for (std::size_t index = 0; index < source.size(); ++index) {
      MoveOwnedValue(destination[index], source[index], queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kVector) {
    destination.reserve(source.size());
    for (auto& element : source) {
      using Value = typename T::value_type;
      destination.push_back(AllocatedValue<Value>::Make(queue.allocator));
      MoveOwnedValue(destination.back(), element, queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kMap) {
    for (auto& [key, element] : source) {
      using Key = typename T::key_type;
      using Value = typename T::mapped_type;
      auto [entry, inserted] =
          destination.emplace(AllocatedValue<Key>::Make(queue.allocator, key),
              AllocatedValue<Value>::Make(queue.allocator));
      if (!inserted) {
        std::terminate();
      }
      MoveOwnedValue(entry->second, element, queue);
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kFlatMap) {
    using Keys = typename T::key_container_type;
    using Values = typename T::mapped_container_type;
    Keys keys = AllocatedValue<Keys>::Make(queue.allocator, source.keys());
    Values values = AllocatedValue<Values>::Make(queue.allocator);
    values.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
      values.push_back(
          AllocatedValue<typename T::mapped_type>::Make(queue.allocator));
    }
    T target(std::sorted_unique,
        std::move(keys),
        std::move(values),
        source.key_comp());
    destination = std::move(target);
    auto target_entry = destination.begin();
    for (auto source_entry : source) {
      MoveOwnedValue((*target_entry).second, source_entry.second, queue);
      ++target_entry;
    }
  } else if constexpr (Shape::kKind == OwnershipKind::kVariant) {
    MoveOwnedVariant(destination, source, queue);
  }
}

template <typename T>
const OwnershipOperations& GetOwnershipOperations() noexcept {
  struct Tables {
    OwnershipOperations initial;
    OwnershipOperations finish;
  };
  static constexpr Tables tables{
      {[](OwnershipNode& node, OwnershipQueue& queue) noexcept {
         auto& value = OwnershipFields<T>::Value(node);
         VisitOwnershipFields(value, [&](auto& field) {
           CollectOwned(field, queue);
         });
       },
          nullptr,
          [](OwnershipNode& destination,
              OwnershipNode& source,
              OwnershipMoveQueue& queue) {
            auto& target = OwnershipFields<T>::Value(destination);
            auto& original = OwnershipFields<T>::Value(source);
            std::apply(
                [&](auto... fields) {
                  (MoveOwnedValue(
                       fields.Get(target), fields.Get(original), queue),
                      ...);
                },
                OwnershipFields<T>::kFields);
          },
          &tables.finish,
          &tables.initial},
      {nullptr,
          [](OwnershipNode& node) noexcept {
            auto& value = OwnershipFields<T>::Value(node);
            VisitOwnershipFields(value, [](auto& field) {
              ClearOwned(field);
            });
          },
          nullptr,
          nullptr,
          &tables.initial}};
  return tables.initial;
}

template <typename T>
void MoveOwned(T& destination,
    T& source,
    std::pmr::polymorphic_allocator<std::byte> allocator) {
  if (CanStealOwned(source, *allocator.resource())) {
    std::apply(
        [&](auto... fields) {
          ((fields.Get(destination) = AllocatedValue<std::remove_cvref_t<
                   decltype(fields.Get(source))>>::Make(allocator,
                std::move(fields.Get(source)))),
              ...);
        },
        OwnershipFields<T>::kFields);
    return;
  }
  OwnershipMoveQueue queue(*allocator.resource());
  MoveOwnedValue(destination, source, queue);
  queue.Run();
  DestroyOwned(source);
}

template <typename T>
concept RecursiveMessage = requires { MessageTraversal<T>::kFields; };

template <typename T>
concept RecursiveStruct = requires { StructTraversal<T>::kFields; };

template <typename T>
concept RecursiveUnion = requires { UnionTraversal<T>::kTags; };

template <typename T>
struct RecursiveValue
    : std::bool_constant<RecursiveMessage<T> || RecursiveStruct<T> ||
                         RecursiveUnion<T>> {};

template <typename T, typename Allocator>
struct RecursiveValue<std::vector<T, Allocator>> : RecursiveValue<T> {};

template <typename T, std::size_t N>
struct RecursiveValue<std::array<T, N>> : RecursiveValue<T> {};

template <typename T, std::size_t N>
struct RecursiveValue<std::span<T, N>>
    : RecursiveValue<std::remove_const_t<T>> {};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct RecursiveValue<std::map<Key, Value, Compare, Allocator>>
    : RecursiveValue<Value> {};

template <typename Key,
    typename Value,
    typename Hasher,
    typename Equal,
    typename Allocator>
struct RecursiveValue<std::unordered_map<Key, Value, Hasher, Equal, Allocator>>
    : RecursiveValue<Value> {};

template <typename Key, typename Value>
struct RecursiveValue<MapRef<Key, Value>> : RecursiveValue<Value> {};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Key,
    typename Value,
    typename Compare,
    typename Keys,
    typename Values>
struct RecursiveValue<std::flat_map<Key, Value, Compare, Keys, Values>>
    : RecursiveValue<Value> {};
#endif

template <typename T, TraversalMode Mode>
using TraversalValue =
    std::conditional_t<Mode == TraversalMode::kDecode, T, const T>;

template <typename T, TraversalMode Mode>
struct TraversalFrame;

template <TraversalMode Mode>
class CodecTraversal {
 public:
  CodecTraversal(Traversal& context,
      std::span<std::byte> scratch,
      Writer* writer = nullptr)
      : context(context), stack(scratch, context), writer(writer) {
  }

  template <typename T>
  Result<void> Visit(T& value, Reader* reader = nullptr) {
    using Value = std::remove_cvref_t<T>;
    if constexpr (RecursiveValue<Value>::value) {
      return stack.template Push<TraversalFrame<Value, Mode>>(value, reader);
    } else if constexpr (Mode == TraversalMode::kSize) {
      auto value_size = Codec<Value>::Size(value, context);
      if (!value_size) {
        return std::unexpected(value_size.error());
      }
      return Add(*value_size);
    } else if constexpr (Mode == TraversalMode::kEncode) {
      return Codec<Value>::Encode(*writer, value, context);
    } else {
      return Codec<Value>::Decode(*reader, value, context);
    }
  }

  Result<void> Add(std::size_t amount) {
    auto total = AddSize(size, amount);
    if (!total) {
      return std::unexpected(total.error());
    }
    size = *total;
    return {};
  }

  Traversal& context;
  TraversalStack<CodecTraversal> stack;
  Writer* writer;
  std::size_t size = 0;
};

template <typename Callback, std::size_t... Indices>
Result<void> DispatchTraversalIndex(
    std::size_t index, Callback&& callback, std::index_sequence<Indices...>) {
  Result<void> result;
  auto visit = [&]<std::size_t Index>() {
    if (index != Index) {
      return false;
    }
    result = callback(std::integral_constant<std::size_t, Index>{});
    return true;
  };
  const bool found = (visit.template operator()<Indices>() || ...);
  if (!found) {
    return std::unexpected(Error{ErrorCode::kInvalidMessage});
  }
  return result;
}

template <std::size_t Count, typename Callback>
Result<void> DispatchTraversalIndex(std::size_t index, Callback&& callback) {
  return DispatchTraversalIndex(index,
      std::forward<Callback>(callback),
      std::make_index_sequence<Count>{});
}

template <typename Tuple, typename Callback>
Result<void> VisitTraversalField(
    const Tuple& fields, std::size_t index, Callback&& callback) {
  return DispatchTraversalIndex<std::tuple_size_v<Tuple>>(
      index, [&](auto position) {
        return callback(std::get<decltype(position)::value>(fields));
      });
}

template <typename T, TraversalMode Mode>
  requires RecursiveMessage<T>
struct TraversalFrame<T, Mode> {
  static constexpr auto& kFields = MessageTraversal<T>::kFields;
  static constexpr std::size_t kFieldCount =
      std::tuple_size_v<std::remove_cvref_t<decltype(kFields)>>;

  explicit TraversalFrame(
      TraversalValue<T, Mode>& value, Reader* reader = nullptr)
      : value(value), reader(reader) {
  }

  Result<void> Step(CodecTraversal<Mode>& machine,
      TraversalStack<CodecTraversal<Mode>>& stack) {
    if (!started) {
      started = true;
      auto result = Start(machine);
      if (!result) {
        return result;
      }
    }
    if constexpr (Mode == TraversalMode::kDecode) {
      if (pending && field_reader.remaining() != 0) {
        return std::unexpected(
            Error{ErrorCode::kTrailingData, field_reader.absolute_position()});
      }
      pending = false;
    }
    if (next == kFieldCount) {
      auto result = Finish(machine);
      if (!result) {
        return result;
      }
      stack.Pop();
      return {};
    }
    return VisitTraversalField(
        kFields, next++, [&](auto field) -> Result<void> {
          auto& member = field.Get(value);
          if constexpr (Mode == TraversalMode::kDecode) {
            member.reset();
            auto bytes = index->Field(field.kTag);
            if (!bytes) {
              return {};
            }
            field_reader = Reader(*bytes,
                start +
                    static_cast<std::size_t>(bytes->data() - encoded.data()));
            auto& payload = EmplaceDecoded(member, machine.context);
            pending = true;
            return machine.Visit(payload, &field_reader);
          } else {
            if (!member) {
              return {};
            }
            tags[count] = field.kTag;
            if constexpr (Mode == TraversalMode::kEncode) {
              const std::size_t offset = machine.writer->position() - start - 4;
              if (offset > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(
                    Error{ErrorCode::kOverflow, machine.writer->position()});
              }
              offsets[count] = static_cast<std::uint32_t>(offset);
            }
            ++count;
            return machine.Visit(*member);
          }
        });
  }

  Result<void> Start(CodecTraversal<Mode>& machine) {
    if constexpr (Mode == TraversalMode::kSize) {
      start = machine.size;
      return {};
    } else if constexpr (Mode == TraversalMode::kEncode) {
      start = machine.writer->position();
      return machine.writer->Write(std::uint32_t{0});
    } else {
      start = reader->absolute_position();
      encoded = reader->remaining_bytes();
      auto length = reader->Read<std::uint32_t>();
      if (!length) {
        return std::unexpected(length.error());
      }
      auto body = reader->ReadBytes(*length);
      if (!body) {
        return std::unexpected(body.error());
      }
      auto parsed = MessageIndex::Parse(
          encoded.first(4 + static_cast<std::size_t>(*length)));
      if (!parsed) {
        auto error = parsed.error();
        error.offset += start;
        return std::unexpected(error);
      }
      if constexpr (requires {
                      MessageTraversal<T>::ClearDeprecatedFields(value);
                    }) {
        MessageTraversal<T>::ClearDeprecatedFields(value);
      }
      index = *parsed;
      return {};
    }
  }

  Result<void> Finish(CodecTraversal<Mode>& machine) {
    if constexpr (Mode == TraversalMode::kSize) {
      const std::size_t payload = machine.size - start;
      auto total = MessageSize(payload, std::span(tags).first(count));
      if (!total) {
        return std::unexpected(total.error());
      }
      return machine.Add(*total - payload);
    } else if constexpr (Mode == TraversalMode::kEncode) {
      return FinishMessage(*machine.writer,
          start,
          std::span(tags).first(count),
          std::span(offsets).first(count));
    } else {
      return {};
    }
  }

  TraversalValue<T, Mode>& value;
  Reader* reader;
  Reader field_reader{{}};
  std::optional<MessageIndex> index;
  std::span<const std::byte> encoded;
  std::array<std::uint8_t, kFieldCount> tags;
  std::array<std::uint32_t, kFieldCount> offsets;
  std::size_t next = 0;
  std::size_t count = 0;
  std::size_t start = 0;
  bool started = false;
  bool pending = false;
};

template <RecursiveStruct T, TraversalMode Mode>
struct TraversalFrame<T, Mode> {
  static constexpr auto& kFields = StructTraversal<T>::kFields;
  static constexpr std::size_t kFieldCount =
      std::tuple_size_v<std::remove_cvref_t<decltype(kFields)>>;

  explicit TraversalFrame(
      TraversalValue<T, Mode>& value, Reader* reader = nullptr)
      : value(value), reader(reader) {
  }

  Result<void> Step(CodecTraversal<Mode>& machine,
      TraversalStack<CodecTraversal<Mode>>& stack) {
    if (next == kFieldCount) {
      stack.Pop();
      return {};
    }
    return VisitTraversalField(kFields, next++, [&](auto field) {
      return machine.Visit(field.Get(value), reader);
    });
  }

  TraversalValue<T, Mode>& value;
  Reader* reader;
  std::size_t next = 0;
};

template <RecursiveUnion T, TraversalMode Mode>
struct TraversalFrame<T, Mode> {
  static constexpr auto& kTags = UnionTraversal<T>::kTags;
  static constexpr std::size_t kBranches = kTags.size();

  explicit TraversalFrame(
      TraversalValue<T, Mode>& value, Reader* reader = nullptr)
      : value(value), reader(reader) {
  }

  Result<void> Step(CodecTraversal<Mode>& machine,
      TraversalStack<CodecTraversal<Mode>>& stack) {
    if (started) {
      auto result = Finish(machine);
      if (!result) {
        return result;
      }
      stack.Pop();
      return {};
    }
    started = true;
    auto result = Start(machine);
    if (!result) {
      return result;
    }
    if (branch == kBranches) {
      return Unknown(machine);
    }
    return DispatchTraversalIndex<kBranches>(branch, [&](auto position) {
      constexpr auto index = decltype(position)::value;
      if constexpr (Mode == TraversalMode::kDecode) {
        auto& payload = EmplaceDecoded<index>(value.value, machine.context);
        return machine.Visit(payload, &region);
      } else {
        return machine.Visit(std::get<index>(value.value));
      }
    });
  }

  Result<void> Start(CodecTraversal<Mode>& machine) {
    if constexpr (Mode == TraversalMode::kDecode) {
      auto length = reader->Read<std::uint32_t>();
      if (!length) {
        return std::unexpected(length.error());
      }
      if (*length == 0) {
        return std::unexpected(
            Error{ErrorCode::kInvalidUnion, reader->absolute_position()});
      }
      auto payload = reader->ReadRegion(*length);
      if (!payload) {
        return std::unexpected(payload.error());
      }
      region = *payload;
      auto discriminator = region.Read<std::uint8_t>();
      if (!discriminator) {
        return std::unexpected(discriminator.error());
      }
      tag = *discriminator;
      if (tag == 0) {
        return std::unexpected(
            Error{ErrorCode::kInvalidUnion, region.absolute_position() - 1});
      }
      branch = static_cast<std::size_t>(
          std::find(kTags.begin(), kTags.end(), tag) - kTags.begin());
      return {};
    } else {
      branch = value.value.index();
      if (branch > kBranches) {
        return std::unexpected(Error{ErrorCode::kInvalidUnion});
      }
      if (branch == kBranches) {
        tag = std::get<kBranches>(value.value).discriminator;
        if (tag == 0 ||
            std::find(kTags.begin(), kTags.end(), tag) != kTags.end()) {
          return std::unexpected(Error{ErrorCode::kInvalidUnion});
        }
      } else {
        tag = kTags[branch];
      }
      if constexpr (Mode == TraversalMode::kSize) {
        start = machine.size;
        return machine.Add(5);
      } else {
        start = machine.writer->position();
        auto length = machine.writer->Write(std::uint32_t{0});
        if (!length) {
          return length;
        }
        return machine.writer->Write(tag);
      }
    }
  }

  Result<void> Unknown(CodecTraversal<Mode>& machine) {
    if constexpr (Mode == TraversalMode::kDecode) {
      auto consumed = machine.context.Consume(region.remaining());
      if (!consumed) {
        return consumed;
      }
      auto& unknown = EmplaceDecoded<kBranches>(value.value, machine.context);
      unknown.discriminator = tag;
      auto bytes = region.ReadBytes(region.remaining());
      if (!bytes) {
        return std::unexpected(bytes.error());
      }
      unknown.payload.assign(bytes->begin(), bytes->end());
      return {};
    } else {
      const auto& unknown = std::get<kBranches>(value.value);
      auto consumed = machine.context.Consume(unknown.payload.size());
      if (!consumed) {
        return consumed;
      }
      if constexpr (Mode == TraversalMode::kSize) {
        return machine.Add(unknown.payload.size());
      } else {
        return machine.writer->WriteBytes(unknown.payload);
      }
    }
  }

  Result<void> Finish(CodecTraversal<Mode>& machine) {
    if constexpr (Mode == TraversalMode::kDecode) {
      if (region.remaining() != 0) {
        return std::unexpected(
            Error{ErrorCode::kTrailingData, region.absolute_position()});
      }
      return {};
    } else {
      std::size_t length = 0;
      if constexpr (Mode == TraversalMode::kSize) {
        length = machine.size - start - 4;
      } else {
        length = machine.writer->position() - start - 4;
      }
      if (length > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error{ErrorCode::kOverflow, start});
      }
      if constexpr (Mode == TraversalMode::kEncode) {
        return machine.writer->Patch(start, static_cast<std::uint32_t>(length));
      } else {
        return {};
      }
    }
  }

  TraversalValue<T, Mode>& value;
  Reader* reader;
  Reader region{{}};
  std::size_t branch = 0;
  std::size_t start = 0;
  std::uint8_t tag = 0;
  bool started = false;
};

template <typename Sequence, TraversalMode Mode, bool Dynamic>
struct SequenceTraversalFrame {
  using Element = std::remove_cv_t<typename Sequence::value_type>;

  explicit SequenceTraversalFrame(
      TraversalValue<Sequence, Mode>& value, Reader* reader = nullptr)
      : value(value), reader(reader) {
  }

  Result<void> Step(CodecTraversal<Mode>& machine,
      TraversalStack<CodecTraversal<Mode>>& stack) {
    if (!started) {
      started = true;
      if constexpr (Mode == TraversalMode::kDecode && Dynamic) {
        auto length = reader->Read<std::uint32_t>();
        if (!length) {
          return std::unexpected(length.error());
        }
        count = *length;
      } else {
        count = value.size();
      }
      auto consumed = machine.context.Consume(count);
      if (!consumed) {
        return consumed;
      }
      if constexpr (Mode == TraversalMode::kDecode) {
        if constexpr (Codec<Element>::kMinSize != 0) {
          if (count > reader->remaining() / Codec<Element>::kMinSize) {
            return std::unexpected(
                Error{ErrorCode::kTruncated, reader->absolute_position()});
          }
        }
        if constexpr (Dynamic) {
          value.clear();
          value.resize(count);
        }
      } else if constexpr (Dynamic) {
        if (count > std::numeric_limits<std::uint32_t>::max()) {
          return std::unexpected(Error{ErrorCode::kOverflow});
        }
        if constexpr (Mode == TraversalMode::kSize) {
          auto result = machine.Add(4);
          if (!result) {
            return result;
          }
        } else {
          auto result =
              machine.writer->Write(static_cast<std::uint32_t>(count));
          if (!result) {
            return result;
          }
        }
      }
    }
    if (next == count) {
      stack.Pop();
      return {};
    }
    return machine.Visit(value[next++], reader);
  }

  TraversalValue<Sequence, Mode>& value;
  Reader* reader;
  std::size_t next = 0;
  std::size_t count = 0;
  bool started = false;
};

template <typename T, typename Allocator, TraversalMode Mode>
struct TraversalFrame<std::vector<T, Allocator>, Mode>
    : SequenceTraversalFrame<std::vector<T, Allocator>, Mode, true> {
  using SequenceTraversalFrame<std::vector<T, Allocator>, Mode, true>::
      SequenceTraversalFrame;
};

template <typename T, std::size_t N, TraversalMode Mode>
struct TraversalFrame<std::array<T, N>, Mode>
    : SequenceTraversalFrame<std::array<T, N>, Mode, false> {
  using SequenceTraversalFrame<std::array<T, N>, Mode, false>::
      SequenceTraversalFrame;
};

template <typename T, std::size_t N, TraversalMode Mode>
struct TraversalFrame<std::span<T, N>, Mode>
    : SequenceTraversalFrame<std::span<T, N>, Mode, N == std::dynamic_extent> {
  using SequenceTraversalFrame<std::span<T, N>,
      Mode,
      N == std::dynamic_extent>::SequenceTraversalFrame;
};

template <typename Map, TraversalMode Mode>
struct MapTraversalFrame {
  using Key = typename Map::key_type;
  using Value = typename Map::mapped_type;
  using Iterator = decltype(std::declval<TraversalValue<Map, Mode>&>().begin());

  explicit MapTraversalFrame(
      TraversalValue<Map, Mode>& value, Reader* reader = nullptr)
      : value(value), reader(reader) {
  }

  Result<void> Step(CodecTraversal<Mode>& machine,
      TraversalStack<CodecTraversal<Mode>>& stack) {
    if (!started) {
      started = true;
      if constexpr (Mode == TraversalMode::kDecode) {
        auto length = ReadMapCount<Key, Value>(*reader, machine.context);
        if (!length) {
          return std::unexpected(length.error());
        }
        count = *length;
        value.clear();
        if constexpr (requires { value.reserve(count); }) {
          value.reserve(count);
        }
      } else {
        count = value.size();
        if (count > std::numeric_limits<std::uint32_t>::max()) {
          return std::unexpected(Error{ErrorCode::kOverflow});
        }
        auto consumed = machine.context.Consume(count);
        if (!consumed) {
          return consumed;
        }
        if constexpr (Mode == TraversalMode::kSize) {
          auto result = machine.Add(4);
          if (!result) {
            return result;
          }
        } else {
          auto result =
              machine.writer->Write(static_cast<std::uint32_t>(count));
          if (!result) {
            return result;
          }
        }
        iterator = value.begin();
      }
    }
    if (next == count) {
      stack.Pop();
      return {};
    }
    ++next;
    if constexpr (Mode == TraversalMode::kDecode) {
      Key key = MakeElement<Key>(value);
      const std::size_t offset = reader->absolute_position();
      auto decoded = Codec<Key>::Decode(*reader, key, machine.context);
      if (!decoded) {
        return decoded;
      }
      auto [entry, inserted] = value.try_emplace(std::move(key));
      if (!inserted) {
        return std::unexpected(Error{ErrorCode::kDuplicateMapKey, offset});
      }
      return machine.Visit(entry->second, reader);
    } else {
      auto entry = *iterator;
      ++*iterator;
      auto key = machine.Visit(entry->first);
      if (!key) {
        return key;
      }
      return machine.Visit(entry->second);
    }
  }

  TraversalValue<Map, Mode>& value;
  Reader* reader;
  std::optional<Iterator> iterator;
  std::size_t next = 0;
  std::size_t count = 0;
  bool started = false;
};

template <typename Key,
    typename Value,
    typename Compare,
    typename Allocator,
    TraversalMode Mode>
struct TraversalFrame<std::map<Key, Value, Compare, Allocator>, Mode>
    : MapTraversalFrame<std::map<Key, Value, Compare, Allocator>, Mode> {
  using MapTraversalFrame<std::map<Key, Value, Compare, Allocator>,
      Mode>::MapTraversalFrame;
};

template <typename Key,
    typename Value,
    typename Hasher,
    typename Equal,
    typename Allocator,
    TraversalMode Mode>
struct TraversalFrame<std::unordered_map<Key, Value, Hasher, Equal, Allocator>,
    Mode>
    : MapTraversalFrame<
          std::unordered_map<Key, Value, Hasher, Equal, Allocator>,
          Mode> {
  using MapTraversalFrame<
      std::unordered_map<Key, Value, Hasher, Equal, Allocator>,
      Mode>::MapTraversalFrame;
};

template <typename Key, typename Value, TraversalMode Mode>
struct TraversalFrame<MapRef<Key, Value>, Mode>
    : MapTraversalFrame<MapRef<Key, Value>, Mode> {
  using MapTraversalFrame<MapRef<Key, Value>, Mode>::MapTraversalFrame;
};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Map>
struct FlatMapTraversalFrame {
  using Key = typename Map::key_type;
  using Value = typename Map::mapped_type;

  explicit FlatMapTraversalFrame(Map& value, Reader* reader)
      : value(value), reader(reader) {
  }

  Result<void> Step(CodecTraversal<TraversalMode::kDecode>& machine,
      TraversalStack<CodecTraversal<TraversalMode::kDecode>>& stack) {
    if (!storage) {
      auto length = ReadMapCount<Key, Value>(*reader, machine.context);
      if (!length) {
        return std::unexpected(length.error());
      }
      count = *length;
      start = reader->absolute_position();
      storage = std::move(value).extract();
      storage->keys.clear();
      storage->values.clear();
      storage->keys.reserve(count);
      storage->values.reserve(count);
    }
    if (next == count) {
      if (!SortMapStorage(storage->keys, storage->values, value.key_comp())) {
        return std::unexpected(Error{ErrorCode::kDuplicateMapKey, start});
      }
      Map decoded(std::sorted_unique,
          std::move(storage->keys),
          std::move(storage->values),
          value.key_comp());
      value = std::move(decoded);
      stack.Pop();
      return {};
    }
    ++next;
    Key key = MakeElement<Key>(storage->keys);
    auto result = Codec<Key>::Decode(*reader, key, machine.context);
    if (!result) {
      return result;
    }
    storage->keys.push_back(std::move(key));
    storage->values.push_back(MakeElement<Value>(storage->values));
    return machine.Visit(storage->values.back(), reader);
  }

  Map& value;
  Reader* reader;
  std::optional<typename Map::containers> storage;
  std::size_t start = 0;
  std::size_t count = 0;
  std::size_t next = 0;
};

template <typename Key,
    typename Value,
    typename Compare,
    typename Keys,
    typename Values,
    TraversalMode Mode>
struct TraversalFrame<std::flat_map<Key, Value, Compare, Keys, Values>, Mode>
    : MapTraversalFrame<std::flat_map<Key, Value, Compare, Keys, Values>,
          Mode> {
  using MapTraversalFrame<std::flat_map<Key, Value, Compare, Keys, Values>,
      Mode>::MapTraversalFrame;
};

template <typename Key,
    typename Value,
    typename Compare,
    typename Keys,
    typename Values>
struct TraversalFrame<std::flat_map<Key, Value, Compare, Keys, Values>,
    TraversalMode::kDecode>
    : FlatMapTraversalFrame<std::flat_map<Key, Value, Compare, Keys, Values>> {
  using FlatMapTraversalFrame<
      std::flat_map<Key, Value, Compare, Keys, Values>>::FlatMapTraversalFrame;
};
#endif

template <typename Callback>
decltype(auto) WithTraversalStorage(Traversal& context, Callback&& callback) {
  auto scratch = context.traversal_scratch();
  if (context.has_traversal_scratch()) {
    return callback(scratch);
  }
  std::array<std::byte, 16 * 1024> storage;
  return callback(std::span(storage));
}

template <typename T>
  requires RecursiveValue<T>::value
Result<std::size_t> TraverseSize(const T& value, Traversal& context) {
  return WithTraversalStorage(
      context, [&](std::span<std::byte> scratch) -> Result<std::size_t> {
        CodecTraversal<TraversalMode::kSize> machine(context, scratch);
        auto root = machine.Visit(value);
        if (!root) {
          return std::unexpected(root.error());
        }
        auto result = machine.stack.Run(machine);
        if (!result) {
          return std::unexpected(result.error());
        }
        return machine.size;
      });
}

template <typename T>
  requires RecursiveValue<T>::value
Result<void> TraverseEncode(
    Writer& writer, const T& value, Traversal& context) {
  return WithTraversalStorage(
      context, [&](std::span<std::byte> scratch) -> Result<void> {
        CodecTraversal<TraversalMode::kEncode> machine(
            context, scratch, &writer);
        auto root = machine.Visit(value);
        if (!root) {
          return root;
        }
        return machine.stack.Run(machine);
      });
}

template <typename T>
  requires RecursiveValue<T>::value
Result<void> TraverseDecode(Reader& reader, T& value, Traversal& context) {
  return WithTraversalStorage(
      context, [&](std::span<std::byte> scratch) -> Result<void> {
        CodecTraversal<TraversalMode::kDecode> machine(context, scratch);
        auto root = machine.Visit(value, &reader);
        if (!root) {
          return root;
        }
        return machine.stack.Run(machine);
      });
}

}  // namespace bebop::internal

namespace bebop {

//! Generated record types supported by the public serialization functions
template <typename T>
concept Record = requires { Codec<T>::kRecord; } && Codec<T>::kRecord;

}  // namespace bebop

namespace bebop::internal {

template <Record T, typename Factory>
Result<T> DecodeRecord(std::span<const std::byte> source,
    DecodeOptions options,
    std::pmr::memory_resource* resource,
    Factory&& factory) {
  if (source.size() > options.max_bytes) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  Reader reader(source);
  Traversal context(options, {}, resource);
  T value = std::invoke(std::forward<Factory>(factory));
  auto result = Codec<T>::Decode(reader, value, context);
  if (!result) {
    return std::unexpected(result.error());
  }
  if (reader.remaining() != 0) {
    return std::unexpected(
        Error{ErrorCode::kTrailingData, reader.absolute_position()});
  }
  return value;
}

}  // namespace bebop::internal

namespace bebop {

//! Get the encoded size of a record
//! No output buffer is allocated.
template <Record T>
[[nodiscard]] Result<std::size_t> EncodedSize(
    const T& value, DecodeOptions options = {}) {
  internal::Traversal context(options);
  auto size = Codec<T>::Size(value, context);
  if (!size) {
    return std::unexpected(size.error());
  }
  if (*size > options.max_bytes) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  return size;
}

//! Encode a record into caller-owned storage
//! @return Bytes written, or an error; failure may leave partial output
//!
//! The destination must not overlap storage borrowed by value.
template <Record T>
[[nodiscard]] Result<std::size_t> EncodeTo(const T& value,
    std::span<std::byte> destination,
    DecodeOptions options = {}) {
  const std::size_t capacity = std::min(destination.size(), options.max_bytes);
  Writer writer(destination.first(capacity));
  internal::Traversal context(options);

  auto result = Codec<T>::Encode(writer, value, context);
  if (!result) {
    return std::unexpected(result.error());
  }
  return writer.position();
}

//! Encode a record using the supplied byte allocator
//! Input ranges must remain unchanged between sizing and encoding.
template <Record T, typename Allocator = std::allocator<std::byte>>
  requires std::is_same_v<typename Allocator::value_type, std::byte>
[[nodiscard]] Result<std::vector<std::byte, Allocator>> Encode(const T& value,
    const Allocator& allocator = {},
    DecodeOptions options = {}) {
  auto size = EncodedSize(value, options);
  if (!size) {
    return std::unexpected(size.error());
  }

  std::vector<std::byte, Allocator> bytes(*size, std::byte{0}, allocator);
  auto written = EncodeTo(value, bytes, options);
  if (!written) {
    return std::unexpected(written.error());
  }
  if (*written != bytes.size()) {
    return std::unexpected(Error{ErrorCode::kInvalidMessage, *written});
  }
  return bytes;
}

//! Encode a record using a memory resource
//! The resource must outlive the returned buffer.
template <Record T>
[[nodiscard]] Result<std::pmr::vector<std::byte>> Encode(const T& value,
    std::pmr::memory_resource& resource,
    DecodeOptions options = {}) {
  const std::pmr::polymorphic_allocator<std::byte> allocator(&resource);
  return Encode(value, allocator, options);
}

//! Decode one complete record into owned storage
//! Trailing bytes produce an error.
template <Record T>
[[nodiscard]] Result<T> Decode(
    std::span<const std::byte> source, DecodeOptions options = {}) {
  return internal::DecodeRecord<T>(
      source, options, std::pmr::get_default_resource(), [] {
        return T{};
      });
}

//! Decode into a record constructed with the supplied allocator
template <Record T, typename Allocator>
  requires std::uses_allocator_v<T, Allocator>
[[nodiscard]] Result<T> Decode(std::span<const std::byte> source,
    const Allocator& allocator,
    DecodeOptions options = {}) {
  auto* resource = std::pmr::get_default_resource();
  if constexpr (requires { allocator.resource(); }) {
    resource = allocator.resource();
  }
  return internal::DecodeRecord<T>(source, options, resource, [&] {
    return internal::AllocatedValue<T>::Make(allocator);
  });
}

//! Decode an allocator-aware record using a memory resource
//! The resource must outlive the returned record.
template <Record T>
  requires std::uses_allocator_v<T, std::pmr::polymorphic_allocator<std::byte>>
[[nodiscard]] Result<T> Decode(std::span<const std::byte> source,
    std::pmr::memory_resource& resource,
    DecodeOptions options = {}) {
  return Decode<T>(
      source, std::pmr::polymorphic_allocator<std::byte>(&resource), options);
}

class IndexBuffer;

}  // namespace bebop

namespace bebop::internal {

template <typename T>
struct ViewTraits;

template <typename T>
struct IndexTraits;

template <typename T>
struct IndexRecordFields;

template <typename T>
struct SequenceIndexFrame;

inline constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

struct ViewIndexRef {
  const std::size_t* node = nullptr;

  explicit operator bool() const noexcept {
    return node != nullptr;
  }

  std::span<const std::size_t> Offsets(std::size_t count) const noexcept {
    return {node + node[0], count};
  }

  ViewIndexRef Child(std::size_t position) const noexcept {
    if (node == nullptr || node[1] == kNoIndex) {
      return {};
    }
    const auto offset = node[node[1] + position];
    if (offset == kNoIndex) {
      return {};
    }
    return {node + offset};
  }

  std::span<const std::size_t> Order(std::size_t count) const noexcept {
    return {node + node[2], count};
  }
};

template <typename T>
typename ViewTraits<T>::Type MakeView(
    std::span<const std::byte> encoded, ViewIndexRef index = {}) noexcept;

class IndexTraversal;

template <typename Callback>
Result<ViewIndexRef> BuildViewIndex(
    IndexBuffer& buffer, DecodeOptions options, Callback&& start) noexcept;

template <typename T, std::size_t N>
struct SequenceIndexTraits;

template <typename T, std::size_t N>
struct SequenceViewTraits;

template <typename Key, typename Value>
struct MapViewTraits;

template <typename Key, typename Value>
struct MapIndexTraits;

inline void AdvanceTrusted(Reader& reader, std::size_t size) noexcept {
  if (!reader.ReadBytes(size)) {
    std::unreachable();
  }
}

template <typename T, auto ReadValue, std::size_t Size, bool Trivial>
struct ValueViewTraits {
  using Type = T;
  static constexpr std::size_t kFixedSize = Size;
  static constexpr std::size_t kMinSize = Size;
  static constexpr bool kTrivial = Trivial;

  static Result<Type> Read(Reader& reader, Traversal&) noexcept {
    return std::invoke(ReadValue, reader);
  }

  static Type ReadTrusted(Reader& reader) noexcept {
    auto value = std::invoke(ReadValue, reader);
    if (!value) {
      std::unreachable();
    }
    return *value;
  }

  static Type FromValidated(std::span<const std::byte> encoded) noexcept {
    Reader reader(encoded);
    return ReadTrusted(reader);
  }

  static void SkipTrusted(Reader& reader) noexcept {
    AdvanceTrusted(reader, kFixedSize);
  }
};

template <WireScalar T>
struct ViewTraits<T> : ValueViewTraits<T, &Reader::Read<T>, sizeof(T), true> {};

struct StringViewTraits {
  using Type = std::string_view;
  static constexpr std::size_t kFixedSize = 0;
  static constexpr std::size_t kMinSize = 5;
  static constexpr bool kTrivial = false;

  static Result<Type> Read(Reader& reader, Traversal&) noexcept {
    return reader.ReadString();
  }

  static Type ReadTrusted(Reader& reader) noexcept {
    auto value = reader.ReadString();
    if (!value) {
      std::unreachable();
    }
    return *value;
  }

  static Type FromValidated(std::span<const std::byte> encoded) noexcept {
    Reader reader(encoded);
    return ReadTrusted(reader);
  }

  static void SkipTrusted(Reader& reader) noexcept {
    const auto length = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    AdvanceTrusted(reader, static_cast<std::size_t>(length) + 1);
  }
};

template <>
struct ViewTraits<std::string_view> : StringViewTraits {};

template <typename Traits, typename Allocator>
struct ViewTraits<std::basic_string<char, Traits, Allocator>>
    : StringViewTraits {};

template <>
struct ViewTraits<Uuid> : ValueViewTraits<Uuid, &Reader::ReadUuid, 16, true> {};

template <>
struct ViewTraits<Timestamp>
    : ValueViewTraits<Timestamp, &Reader::ReadTimestamp, 16, false> {};

template <>
struct ViewTraits<Duration>
    : ValueViewTraits<Duration, &Reader::ReadDuration, 12, false> {};

template <typename Traits>
class ViewIterator {
 public:
  using value_type = typename Traits::Type;
  using difference_type = std::ptrdiff_t;
  using iterator_concept = std::input_iterator_tag;

  ViewIterator() = default;

  ViewIterator(std::span<const std::byte> bytes,
      std::size_t count,
      ViewIndexRef index = {}) noexcept
      : reader_(bytes), remaining_(count), count_(count), index_(index) {
    ReadNext();
  }

  value_type operator*() const noexcept {
    return *current_;
  }

  ViewIterator& operator++() noexcept {
    --remaining_;
    ReadNext();
    return *this;
  }

  void operator++(int) noexcept {
    ++*this;
  }

  friend bool operator==(
      const ViewIterator& iterator, std::default_sentinel_t) noexcept {
    return iterator.remaining_ == 0;
  }

 private:
  void ReadNext() noexcept {
    if (remaining_ != 0) {
      if (index_) {
        const auto position = count_ - remaining_;
        const auto offsets = index_.Offsets(count_ + 1);
        current_ = Traits::FromIndex(
            reader_.remaining_bytes().subspan(
                offsets[position], offsets[position + 1] - offsets[position]),
            index_.Child(position));
      } else {
        current_ = Traits::ReadTrusted(reader_);
      }
    } else {
      current_.reset();
    }
  }

  Reader reader_{std::span<const std::byte>{}};
  std::size_t remaining_ = 0;
  std::size_t count_ = 0;
  ViewIndexRef index_;
  std::optional<value_type> current_;
};

template <typename T>
struct ArrayElementViewTraits : ViewTraits<T> {
  static typename ViewTraits<T>::Type FromIndex(
      std::span<const std::byte> bytes, ViewIndexRef index) noexcept {
    return MakeView<T>(bytes, index);
  }
};

}  // namespace bebop::internal

namespace bebop {

//! Validated array elements borrowed from encoded bytes
//! The bytes must remain alive and unchanged while the view is used.
template <typename T>
class ArrayView {
 public:
  using value_type = typename internal::ViewTraits<T>::Type;

  using Iterator = internal::ViewIterator<internal::ArrayElementViewTraits<T>>;

  std::size_t size() const noexcept {
    return count_;
  }

  bool empty() const noexcept {
    return count_ == 0;
  }

  Iterator begin() const noexcept {
    if (index_) {
      return Iterator(encoded_, count_, index_);
    }
    return Iterator(Elements(), count_);
  }

  std::default_sentinel_t end() const noexcept {
    return {};
  }

  //! Read an element, returning no value when the index is out of range
  //! Fixed-width and indexed access is direct; otherwise preceding elements
  //! are scanned.
  std::optional<value_type> At(std::size_t index) const noexcept {
    if (index >= count_) {
      return std::nullopt;
    }
    if (index_) {
      const auto offsets = index_.Offsets(count_ + 1);
      return internal::MakeView<T>(
          encoded_.subspan(offsets[index], offsets[index + 1] - offsets[index]),
          index_.Child(index));
    }
    Reader reader(Elements());
    if constexpr (internal::ViewTraits<T>::kFixedSize != 0) {
      internal::AdvanceTrusted(
          reader, index * internal::ViewTraits<T>::kFixedSize);
    } else {
      for (std::size_t preceding = 0; preceding < index; ++preceding) {
        internal::ViewTraits<T>::SkipTrusted(reader);
      }
    }
    return internal::ViewTraits<T>::ReadTrusted(reader);
  }

  //! Index this array and its nested variable-size values in caller-owned
  //! storage Keep the storage and encoded bytes alive and unchanged with the
  //! view.
  [[nodiscard]] Result<ArrayView> Index(
      IndexBuffer& buffer, DecodeOptions options = {}) const noexcept;

  //! Borrow the complete array encoding, including its count when dynamic
  std::span<const std::byte> encoded() const noexcept {
    return encoded_;
  }

 private:
  template <typename, std::size_t>
  friend struct internal::SequenceViewTraits;

  template <typename, std::size_t>
  friend struct internal::SequenceIndexTraits;
  friend struct internal::SequenceIndexFrame<T>;

  ArrayView(std::span<const std::byte> encoded,
      std::size_t count,
      bool dynamic) noexcept
      : encoded_(encoded), count_(count), dynamic_(dynamic) {
  }

  std::span<const std::byte> Elements() const noexcept {
    if (dynamic_) {
      return encoded_.subspan(4);
    }
    return encoded_;
  }

  std::span<const std::byte> encoded_;
  std::size_t count_;
  bool dynamic_;
  internal::ViewIndexRef index_;
};

}  // namespace bebop

namespace bebop::internal {

template <typename T, std::size_t N>
struct SequenceViewTraits {
  using Type = ArrayView<T>;
  static constexpr bool kTrivial = false;
  static constexpr std::size_t kMinSize = [] {
    if constexpr (N == std::dynamic_extent) {
      return std::size_t{4};
    }
    return ViewTraits<T>::kMinSize * N;
  }();
  static constexpr std::size_t kFixedSize = [] {
    if constexpr (N == std::dynamic_extent) {
      return std::size_t{0};
    }
    return ViewTraits<T>::kFixedSize * N;
  }();

  static Result<Type> Read(Reader& reader, Traversal& context) noexcept {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    const auto encoded = reader.remaining_bytes();
    std::size_t count = N;
    if constexpr (N == std::dynamic_extent) {
      auto length = reader.Read<std::uint32_t>();
      if (!length) {
        return std::unexpected(length.error());
      }
      count = *length;
    }
    auto consumed = context.Consume(count);
    if (!consumed) {
      return std::unexpected(consumed.error());
    }
    if constexpr (ViewTraits<T>::kFixedSize != 0) {
      if (count > reader.remaining() / ViewTraits<T>::kFixedSize) {
        return std::unexpected(
            Error{ErrorCode::kTruncated, reader.absolute_position()});
      }
    }
    if constexpr (ViewTraits<T>::kTrivial) {
      auto bytes = reader.ReadBytes(count * ViewTraits<T>::kFixedSize);
      if (!bytes) {
        return std::unexpected(bytes.error());
      }
    } else {
      for (std::size_t index = 0; index < count; ++index) {
        auto value = ViewTraits<T>::Read(reader, context);
        if (!value) {
          return std::unexpected(value.error());
        }
      }
    }
    const std::size_t size = encoded.size() - reader.remaining();
    return Type(encoded.first(size), count, N == std::dynamic_extent);
  }

  static Type ReadTrusted(Reader& reader) noexcept {
    const auto encoded = reader.remaining_bytes();
    std::size_t count = N;
    if constexpr (N == std::dynamic_extent) {
      count = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    }
    SkipElements(reader, count);
    const std::size_t size = encoded.size() - reader.remaining();
    return Type(encoded.first(size), count, N == std::dynamic_extent);
  }

  static Type FromValidated(std::span<const std::byte> encoded) noexcept {
    std::size_t count = N;
    if constexpr (N == std::dynamic_extent) {
      Reader reader(encoded);
      count = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    }
    return Type(encoded, count, N == std::dynamic_extent);
  }

  static void SkipTrusted(Reader& reader) noexcept {
    std::size_t count = N;
    if constexpr (N == std::dynamic_extent) {
      count = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    }
    SkipElements(reader, count);
  }

 private:
  static void SkipElements(Reader& reader, std::size_t count) noexcept {
    if constexpr (ViewTraits<T>::kFixedSize != 0) {
      AdvanceTrusted(reader, count * ViewTraits<T>::kFixedSize);
    } else {
      for (std::size_t index = 0; index < count; ++index) {
        ViewTraits<T>::SkipTrusted(reader);
      }
    }
  }
};

template <typename T, typename Allocator>
struct ViewTraits<std::vector<T, Allocator>>
    : SequenceViewTraits<T, std::dynamic_extent> {};

template <typename T, std::size_t N>
struct ViewTraits<std::array<T, N>> : SequenceViewTraits<T, N> {};

template <typename Key, typename Value>
struct MapEntryViewTraits {
  using Type = std::pair<typename ViewTraits<Key>::Type,
      typename ViewTraits<Value>::Type>;

  static Type FromIndex(
      std::span<const std::byte> bytes, ViewIndexRef index) noexcept {
    Reader reader(bytes);
    auto key = ViewTraits<Key>::ReadTrusted(reader);
    return {key, MakeView<Value>(reader.remaining_bytes(), index)};
  }

  static Type ReadTrusted(Reader& reader) noexcept {
    auto key = ViewTraits<Key>::ReadTrusted(reader);
    auto value = ViewTraits<Value>::ReadTrusted(reader);
    return {key, value};
  }
};

}  // namespace bebop::internal

namespace bebop {

//! Validated map entries borrowed from encoded bytes
//! The bytes must remain alive and unchanged while the view is used.
template <typename Key, typename Value>
class MapView {
 public:
  using key_type = typename internal::ViewTraits<Key>::Type;
  using mapped_type = typename internal::ViewTraits<Value>::Type;
  using value_type = std::pair<key_type, mapped_type>;
  using Iterator =
      internal::ViewIterator<internal::MapEntryViewTraits<Key, Value>>;

  std::size_t size() const noexcept {
    return count_;
  }

  bool empty() const noexcept {
    return count_ == 0;
  }

  Iterator begin() const noexcept {
    if (index_) {
      return Iterator(encoded_, count_, index_);
    }
    return Iterator(encoded_.subspan(4), count_);
  }

  std::default_sentinel_t end() const noexcept {
    return {};
  }

  //! Find a key using binary search when indexed, or a sequential scan
  //! otherwise
  std::optional<mapped_type> Find(const key_type& key) const noexcept {
    if (index_) {
      const auto order = index_.Order(count_);
      const auto found =
          std::ranges::lower_bound(order, key, {}, [&](std::size_t position) {
            Reader reader(Entry(position));
            return internal::ViewTraits<Key>::ReadTrusted(reader);
          });
      if (found == order.end()) {
        return std::nullopt;
      }
      Reader reader(Entry(*found));
      if (internal::ViewTraits<Key>::ReadTrusted(reader) != key) {
        return std::nullopt;
      }
      return internal::MakeView<Value>(
          reader.remaining_bytes(), index_.Child(*found));
    }
    Reader reader(encoded_.subspan(4));
    for (std::size_t index = 0; index < count_; ++index) {
      const auto candidate = internal::ViewTraits<Key>::ReadTrusted(reader);
      if (candidate == key) {
        return internal::ViewTraits<Value>::ReadTrusted(reader);
      }
      internal::ViewTraits<Value>::SkipTrusted(reader);
    }
    return std::nullopt;
  }

  //! Index this map and its nested variable-size values in caller-owned storage
  //! Keep the storage and encoded bytes alive and unchanged with the view.
  [[nodiscard]] Result<MapView> Index(
      IndexBuffer& buffer, DecodeOptions options = {}) const noexcept;

  //! Borrow the complete map encoding, including its entry count
  std::span<const std::byte> encoded() const noexcept {
    return encoded_;
  }

 private:
  template <typename, typename>
  friend struct internal::MapViewTraits;

  template <typename, typename>
  friend struct internal::MapIndexTraits;

  MapView(std::span<const std::byte> encoded, std::size_t count) noexcept
      : encoded_(encoded), count_(count) {
  }

  std::span<const std::byte> Entry(std::size_t position) const noexcept {
    const auto offsets = index_.Offsets(count_ + 1);
    return encoded_.subspan(
        offsets[position], offsets[position + 1] - offsets[position]);
  }

  std::span<const std::byte> encoded_;
  std::size_t count_;
  internal::ViewIndexRef index_;
};

}  // namespace bebop

namespace bebop::internal {

template <typename Key, typename Value>
struct MapViewTraits {
  using Type = MapView<Key, Value>;
  using KeyView = typename ViewTraits<Key>::Type;
  static constexpr bool kTrivial = false;
  static constexpr std::size_t kFixedSize = 0;
  static constexpr std::size_t kMinSize = 4;

  static Result<Type> Read(Reader& reader, Traversal& context) noexcept {
    auto scope = context.Enter();
    if (!scope) {
      return std::unexpected(scope.error());
    }
    const auto encoded = reader.remaining_bytes();
    auto count = reader.Read<std::uint32_t>();
    if (!count) {
      return std::unexpected(count.error());
    }
    auto consumed = context.Consume(*count);
    if (!consumed) {
      return std::unexpected(consumed.error());
    }
    constexpr auto entry_min_size =
        ViewTraits<Key>::kMinSize + ViewTraits<Value>::kMinSize;
    if constexpr (entry_min_size != 0) {
      if (*count > reader.remaining() / entry_min_size) {
        return std::unexpected(
            Error{ErrorCode::kTruncated, reader.absolute_position()});
      }
    }
    const std::size_t entries_start = reader.absolute_position();
    std::optional<KeyView> previous;
    bool sorted = true;
    for (std::uint32_t index = 0; index < *count; ++index) {
      const std::size_t offset = reader.absolute_position();
      auto key = ViewTraits<Key>::Read(reader, context);
      if (!key) {
        return std::unexpected(key.error());
      }
      if (previous) {
        if (*previous == *key) {
          return std::unexpected(Error{ErrorCode::kDuplicateMapKey, offset});
        }
        if (*key < *previous) {
          sorted = false;
        }
      }
      previous = *key;
      auto value = ViewTraits<Value>::Read(reader, context);
      if (!value) {
        return std::unexpected(value.error());
      }
    }
    const std::size_t size = encoded.size() - reader.remaining();
    if (!sorted) {
      auto unique = ValidateUniqueKeys(encoded.first(size).subspan(4),
          *count,
          entries_start,
          context.scratch());
      if (!unique) {
        return std::unexpected(unique.error());
      }
    }
    return Type(encoded.first(size), *count);
  }

  static Type ReadTrusted(Reader& reader) noexcept {
    const auto encoded = reader.remaining_bytes();
    const auto count = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    SkipEntries(reader, count);
    const auto size = encoded.size() - reader.remaining();
    return Type(encoded.first(size), count);
  }

  static Type FromValidated(std::span<const std::byte> encoded) noexcept {
    Reader reader(encoded);
    const auto count = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    return Type(encoded, count);
  }

  static void SkipTrusted(Reader& reader) noexcept {
    const auto count = ViewTraits<std::uint32_t>::ReadTrusted(reader);
    SkipEntries(reader, count);
  }

 private:
  static void SkipEntries(Reader& reader, std::size_t count) noexcept {
    if constexpr (ViewTraits<Key>::kFixedSize != 0 &&
                  ViewTraits<Value>::kFixedSize != 0) {
      constexpr auto entry_size =
          ViewTraits<Key>::kFixedSize + ViewTraits<Value>::kFixedSize;
      AdvanceTrusted(reader, count * entry_size);
    } else {
      for (std::size_t index = 0; index < count; ++index) {
        ViewTraits<Key>::SkipTrusted(reader);
        ViewTraits<Value>::SkipTrusted(reader);
      }
    }
  }

 public:
  static Result<void> ValidateUniqueKeys(std::span<const std::byte> entries,
      std::size_t count,
      std::size_t base_offset,
      std::span<std::size_t> scratch) noexcept {
    if (count > scratch.size()) {
      return std::unexpected(Error{ErrorCode::kScratchTooSmall, base_offset});
    }
    auto offsets = scratch.first(count);
    Reader reader(entries);
    for (auto& offset : offsets) {
      offset = reader.position();
      ViewTraits<Key>::SkipTrusted(reader);
      ViewTraits<Value>::SkipTrusted(reader);
    }
    auto key_at = [entries](std::size_t offset) {
      Reader key_reader(entries.subspan(offset));
      return ViewTraits<Key>::ReadTrusted(key_reader);
    };
    auto compare = [&key_at](std::size_t left, std::size_t right) {
      return key_at(left) < key_at(right);
    };
    std::make_heap(offsets.begin(), offsets.end(), compare);
    std::sort_heap(offsets.begin(), offsets.end(), compare);
    for (std::size_t index = 1; index < offsets.size(); ++index) {
      if (key_at(offsets[index - 1]) == key_at(offsets[index])) {
        return std::unexpected(
            Error{ErrorCode::kDuplicateMapKey, base_offset + offsets[index]});
      }
    }
    return {};
  }
};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct ViewTraits<std::map<Key, Value, Compare, Allocator>>
    : MapViewTraits<Key, Value> {};

template <typename Key,
    typename Value,
    typename Hasher,
    typename Equal,
    typename Allocator>
struct ViewTraits<std::unordered_map<Key, Value, Hasher, Equal, Allocator>>
    : MapViewTraits<Key, Value> {};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Key,
    typename Value,
    typename Compare,
    typename KeyContainer,
    typename ValueContainer>
struct ViewTraits<
    std::flat_map<Key, Value, Compare, KeyContainer, ValueContainer>>
    : MapViewTraits<Key, Value> {};
#endif

template <typename T>
struct TraversalPayload;

template <typename T>
struct TraversalPayload<std::optional<T>> {
  using Type = T;
};

template <typename T, typename Deleter>
struct TraversalPayload<std::unique_ptr<T, Deleter>> {
  using Type = T;
};

template <typename T>
struct ValidationFrame;

class ValidationTraversal {
 public:
  ValidationTraversal(Traversal& context, std::span<std::byte> scratch)
      : context(context), stack(scratch, context) {
  }

  template <typename T>
  Result<void> Visit(Reader& reader) {
    if constexpr (RecursiveValue<T>::value) {
      return stack.Push<ValidationFrame<T>>(reader);
    } else {
      auto value = ViewTraits<T>::Read(reader, context);
      if (!value) {
        return std::unexpected(value.error());
      }
      return {};
    }
  }

  Traversal& context;
  TraversalStack<ValidationTraversal> stack;
};

template <RecursiveMessage T>
struct ValidationFrame<T> {
  static constexpr auto& kFields = MessageTraversal<T>::kFields;
  static constexpr std::size_t kFieldCount =
      std::tuple_size_v<std::remove_cvref_t<decltype(kFields)>>;

  explicit ValidationFrame(
      Reader& reader, std::optional<MessageIndex>* result = nullptr)
      : reader(reader), result(result) {
  }

  Result<void> Step(ValidationTraversal& machine,
      TraversalStack<ValidationTraversal>& stack) {
    if (!index) {
      start = reader.absolute_position();
      encoded = reader.remaining_bytes();
      auto length = reader.Read<std::uint32_t>();
      if (!length) {
        return std::unexpected(length.error());
      }
      auto body = reader.ReadBytes(*length);
      if (!body) {
        return std::unexpected(body.error());
      }
      auto parsed = MessageIndex::Parse(
          encoded.first(4 + static_cast<std::size_t>(*length)));
      if (!parsed) {
        auto error = parsed.error();
        error.offset += start;
        return std::unexpected(error);
      }
      index = *parsed;
      if (result != nullptr) {
        *result = *parsed;
      }
    }
    if (pending && field_reader.remaining() != 0) {
      return std::unexpected(
          Error{ErrorCode::kTrailingData, field_reader.absolute_position()});
    }
    pending = false;
    if (next == kFieldCount) {
      stack.Pop();
      return {};
    }
    return VisitTraversalField(
        kFields, next++, [&](auto field) -> Result<void> {
          auto bytes = index->Field(field.kTag);
          if (!bytes) {
            return {};
          }
          field_reader = Reader(*bytes,
              start + static_cast<std::size_t>(bytes->data() - encoded.data()));
          using Storage =
              std::remove_cvref_t<decltype(field.Get(std::declval<T&>()))>;
          using Value = typename TraversalPayload<Storage>::Type;
          pending = true;
          return machine.Visit<Value>(field_reader);
        });
  }

  Reader& reader;
  std::optional<MessageIndex>* result;
  Reader field_reader{{}};
  std::optional<MessageIndex> index;
  std::span<const std::byte> encoded;
  std::size_t next = 0;
  std::size_t start = 0;
  bool pending = false;
};

template <RecursiveStruct T>
struct ValidationFrame<T> {
  static constexpr auto& kFields = StructTraversal<T>::kFields;
  static constexpr std::size_t kFieldCount =
      std::tuple_size_v<std::remove_cvref_t<decltype(kFields)>>;

  explicit ValidationFrame(Reader& reader) : reader(reader) {
  }

  Result<void> Step(ValidationTraversal& machine,
      TraversalStack<ValidationTraversal>& stack) {
    if (next == kFieldCount) {
      stack.Pop();
      return {};
    }
    return VisitTraversalField(kFields, next++, [&](auto field) {
      using Value =
          std::remove_cvref_t<decltype(field.Get(std::declval<T&>()))>;
      return machine.Visit<Value>(reader);
    });
  }

  Reader& reader;
  std::size_t next = 0;
};

template <RecursiveUnion T>
struct ValidationFrame<T> {
  static constexpr auto& kTags = UnionTraversal<T>::kTags;
  static constexpr std::size_t kBranches = kTags.size();

  explicit ValidationFrame(Reader& reader) : reader(reader) {
  }

  Result<void> Step(ValidationTraversal& machine,
      TraversalStack<ValidationTraversal>& stack) {
    if (started) {
      if (region.remaining() != 0) {
        return std::unexpected(
            Error{ErrorCode::kTrailingData, region.absolute_position()});
      }
      stack.Pop();
      return {};
    }
    started = true;
    auto length = reader.Read<std::uint32_t>();
    if (!length) {
      return std::unexpected(length.error());
    }
    if (*length == 0) {
      return std::unexpected(
          Error{ErrorCode::kInvalidUnion, reader.absolute_position()});
    }
    auto bytes = reader.ReadRegion(*length);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    region = *bytes;
    auto tag = region.Read<std::uint8_t>();
    if (!tag) {
      return std::unexpected(tag.error());
    }
    if (*tag == 0) {
      return std::unexpected(
          Error{ErrorCode::kInvalidUnion, region.absolute_position() - 1});
    }
    const auto branch = static_cast<std::size_t>(
        std::find(kTags.begin(), kTags.end(), *tag) - kTags.begin());
    if (branch == kBranches) {
      auto consumed = machine.context.Consume(region.remaining());
      if (!consumed) {
        return consumed;
      }
      auto payload = region.ReadBytes(region.remaining());
      if (!payload) {
        return std::unexpected(payload.error());
      }
      return {};
    }
    return DispatchTraversalIndex<kBranches>(branch, [&](auto position) {
      using Value = std::variant_alternative_t<decltype(position)::value,
          decltype(T::value)>;
      return machine.Visit<Value>(region);
    });
  }

  Reader& reader;
  Reader region{{}};
  bool started = false;
};

template <typename T, std::size_t N>
struct SequenceValidationFrame {
  explicit SequenceValidationFrame(Reader& reader) : reader(reader) {
  }

  Result<void> Step(ValidationTraversal& machine,
      TraversalStack<ValidationTraversal>& stack) {
    if (!started) {
      started = true;
      if constexpr (N == std::dynamic_extent) {
        auto length = reader.Read<std::uint32_t>();
        if (!length) {
          return std::unexpected(length.error());
        }
        count = *length;
      } else {
        count = N;
      }
      auto consumed = machine.context.Consume(count);
      if (!consumed) {
        return consumed;
      }
      if constexpr (ViewTraits<T>::kMinSize != 0) {
        if (count > reader.remaining() / ViewTraits<T>::kMinSize) {
          return std::unexpected(
              Error{ErrorCode::kTruncated, reader.absolute_position()});
        }
      }
    }
    if (next == count) {
      stack.Pop();
      return {};
    }
    ++next;
    return machine.Visit<T>(reader);
  }

  Reader& reader;
  std::size_t count = 0;
  std::size_t next = 0;
  bool started = false;
};

template <typename T, typename Allocator>
struct ValidationFrame<std::vector<T, Allocator>>
    : SequenceValidationFrame<T, std::dynamic_extent> {
  using SequenceValidationFrame<T,
      std::dynamic_extent>::SequenceValidationFrame;
};

template <typename T, std::size_t N>
struct ValidationFrame<std::array<T, N>> : SequenceValidationFrame<T, N> {
  using SequenceValidationFrame<T, N>::SequenceValidationFrame;
};

template <typename Key, typename Value>
struct MapValidationFrame {
  using KeyView = typename ViewTraits<Key>::Type;

  explicit MapValidationFrame(Reader& reader) : reader(reader) {
  }

  Result<void> Step(ValidationTraversal& machine,
      TraversalStack<ValidationTraversal>& stack) {
    if (!started) {
      started = true;
      auto length = reader.Read<std::uint32_t>();
      if (!length) {
        return std::unexpected(length.error());
      }
      count = *length;
      auto consumed = machine.context.Consume(count);
      if (!consumed) {
        return consumed;
      }
      constexpr auto entry_min_size =
          ViewTraits<Key>::kMinSize + ViewTraits<Value>::kMinSize;
      if constexpr (entry_min_size != 0) {
        if (count > reader.remaining() / entry_min_size) {
          return std::unexpected(
              Error{ErrorCode::kTruncated, reader.absolute_position()});
        }
      }
      start = reader.absolute_position();
      entries = reader.remaining_bytes();
    }
    if (next == count) {
      if (!sorted) {
        const auto size = entries.size() - reader.remaining();
        auto unique = MapViewTraits<Key, Value>::ValidateUniqueKeys(
            entries.first(size), count, start, machine.context.scratch());
        if (!unique) {
          return unique;
        }
      }
      stack.Pop();
      return {};
    }
    ++next;
    const auto offset = reader.absolute_position();
    auto key = ViewTraits<Key>::Read(reader, machine.context);
    if (!key) {
      return std::unexpected(key.error());
    }
    if (previous) {
      if (*previous == *key) {
        return std::unexpected(Error{ErrorCode::kDuplicateMapKey, offset});
      }
      if (*key < *previous) {
        sorted = false;
      }
    }
    previous = *key;
    return machine.Visit<Value>(reader);
  }

  Reader& reader;
  std::span<const std::byte> entries;
  std::optional<KeyView> previous;
  std::size_t start = 0;
  std::size_t count = 0;
  std::size_t next = 0;
  bool sorted = true;
  bool started = false;
};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct ValidationFrame<std::map<Key, Value, Compare, Allocator>>
    : MapValidationFrame<Key, Value> {
  using MapValidationFrame<Key, Value>::MapValidationFrame;
};

template <typename Key,
    typename Value,
    typename Hasher,
    typename Equal,
    typename Allocator>
struct ValidationFrame<std::unordered_map<Key, Value, Hasher, Equal, Allocator>>
    : MapValidationFrame<Key, Value> {
  using MapValidationFrame<Key, Value>::MapValidationFrame;
};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Key,
    typename Value,
    typename Compare,
    typename Keys,
    typename Values>
struct ValidationFrame<std::flat_map<Key, Value, Compare, Keys, Values>>
    : MapValidationFrame<Key, Value> {
  using MapValidationFrame<Key, Value>::MapValidationFrame;
};
#endif

template <typename T>
  requires RecursiveValue<T>::value
Result<void> ValidateRecursiveValue(
    Reader& reader, Traversal& context) noexcept {
  return WithTraversalStorage(
      context, [&](std::span<std::byte> scratch) -> Result<void> {
        ValidationTraversal machine(context, scratch);
        auto root = machine.Visit<T>(reader);
        if (!root) {
          return root;
        }
        return machine.stack.Run(machine);
      });
}

template <RecursiveMessage T>
Result<MessageIndex> ValidateRecursiveMessage(
    Reader& reader, Traversal& context) noexcept {
  return WithTraversalStorage(
      context, [&](std::span<std::byte> scratch) -> Result<MessageIndex> {
        ValidationTraversal machine(context, scratch);
        std::optional<MessageIndex> index;
        auto root = machine.stack.Push<ValidationFrame<T>>(reader, &index);
        if (!root) {
          return std::unexpected(root.error());
        }
        auto result = machine.stack.Run(machine);
        if (!result) {
          return std::unexpected(result.error());
        }
        return *index;
      });
}

}  // namespace bebop::internal

namespace bebop {

//! Validate a complete value and borrow its contents without allocating
//! The source must remain alive and unchanged while the returned view is used.
//! Scratch holds temporary key offsets for maps with unsorted keys. An empty
//! span uses 256 stack slots. Scratch can be reused as soon as this function
//! returns.
template <typename T>
[[nodiscard]] Result<typename internal::ViewTraits<T>::Type> ParseView(
    std::span<const std::byte> source,
    DecodeOptions options = {},
    std::span<std::size_t> scratch = {}) noexcept {
  if (source.size() > options.max_bytes) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  Reader reader(source);
  std::array<std::size_t, 256> local_scratch;
  if (scratch.empty()) {
    scratch = local_scratch;
  }
  internal::Traversal context(options, scratch);
  auto value = internal::ViewTraits<T>::Read(reader, context);
  if (!value) {
    return std::unexpected(value.error());
  }
  if (reader.remaining() != 0) {
    return std::unexpected(
        Error{ErrorCode::kTrailingData, reader.absolute_position()});
  }
  return value;
}

//! Storage for a packet's nested view indexes
//! Reset invalidates all indexes built in this storage.
class IndexBuffer {
 public:
  explicit IndexBuffer(std::span<std::size_t> storage) noexcept
      : storage_(storage) {
  }

  std::size_t used() const noexcept {
    return used_;
  }

  void Reset() noexcept {
    used_ = 0;
  }

 private:
  friend class internal::IndexTraversal;

  template <typename Callback>
  friend Result<internal::ViewIndexRef> internal::BuildViewIndex(
      IndexBuffer& buffer, DecodeOptions options, Callback&& start) noexcept;

  std::span<std::size_t> storage() const noexcept {
    return storage_;
  }

  Result<std::size_t> Allocate(std::size_t count) noexcept {
    if (count > storage_.size() - used_) {
      return std::unexpected(Error{ErrorCode::kBufferTooSmall});
    }
    const auto position = used_;
    used_ += count;
    return position;
  }

  void Rewind(std::size_t position) noexcept {
    used_ = position;
  }

  std::span<std::size_t> storage_;
  std::size_t used_ = 0;
};

}  // namespace bebop

namespace bebop::internal {

template <typename T>
struct IndexTraits {
  static constexpr bool kEnabled = false;
};

template <typename T>
typename ViewTraits<T>::Type MakeView(
    std::span<const std::byte> encoded, ViewIndexRef index) noexcept {
  if constexpr (IndexTraits<T>::kEnabled) {
    return IndexTraits<T>::Make(encoded, index);
  } else {
    return ViewTraits<T>::FromValidated(encoded);
  }
}

class IndexTraversal {
 public:
  IndexTraversal(IndexBuffer& buffer,
      Traversal& context,
      std::span<std::byte> scratch) noexcept
      : buffer(buffer), context(context), stack(scratch, context) {
  }

  Result<std::size_t> Header() noexcept {
    auto node = buffer.Allocate(3);
    if (node) {
      std::ranges::fill(buffer.storage().subspan(*node, 3), kNoIndex);
    }
    return node;
  }

  Result<std::span<std::size_t>> AllocateSlots(
      std::size_t node, std::size_t member, std::size_t count) noexcept {
    auto position = buffer.Allocate(count);
    if (!position) {
      return std::unexpected(position.error());
    }
    buffer.storage()[node + member] = *position - node;
    auto slots = buffer.storage().subspan(*position, count);
    if (member == 1) {
      std::ranges::fill(slots, kNoIndex);
    }
    return slots;
  }

  std::span<std::size_t> Slots(
      std::size_t node, std::size_t member, std::size_t count) noexcept {
    return buffer.storage().subspan(
        node + buffer.storage()[node + member], count);
  }

  template <typename T>
  Result<std::size_t> Visit(std::span<const std::byte> encoded) {
    if constexpr (!IndexTraits<T>::kEnabled) {
      return kNoIndex;
    } else {
      auto node = Header();
      if (!node) {
        return node;
      }
      auto pushed = IndexTraits<T>::Push(*this, encoded, *node);
      if (!pushed) {
        return std::unexpected(pushed.error());
      }
      return *node;
    }
  }

  IndexBuffer& buffer;
  Traversal& context;
  TraversalStack<IndexTraversal> stack;
};

template <typename Callback>
Result<ViewIndexRef> BuildViewIndex(
    IndexBuffer& buffer, DecodeOptions options, Callback&& start) noexcept {
  const auto mark = buffer.used();
  Traversal context(options);
  auto result = WithTraversalStorage(
      context, [&](std::span<std::byte> scratch) -> Result<ViewIndexRef> {
        IndexTraversal machine(buffer, context, scratch);
        auto node = start(machine);
        if (!node) {
          return std::unexpected(node.error());
        }
        auto indexed = machine.stack.Run(machine);
        if (!indexed) {
          return std::unexpected(indexed.error());
        }
        return ViewIndexRef{buffer.storage().data() + *node};
      });
  if (!result) {
    buffer.Rewind(mark);
  }
  return result;
}

template <typename T>
struct SequenceIndexFrame {
  SequenceIndexFrame(ArrayView<T> array, std::size_t node) noexcept
      : array(array),
        node(node),
        reader(array.Elements()),
        prefix(array.encoded().size() - array.Elements().size()) {
  }

  Result<void> Step(
      IndexTraversal& machine, TraversalStack<IndexTraversal>& stack) {
    if (!initialized) {
      if (array.size() == std::numeric_limits<std::size_t>::max()) {
        return std::unexpected(Error{ErrorCode::kBufferTooSmall});
      }
      auto consumed = machine.context.Consume(array.size());
      if (!consumed) {
        return consumed;
      }
      auto offsets = machine.AllocateSlots(node, 0, array.size() + 1);
      if (!offsets) {
        return std::unexpected(offsets.error());
      }
      if constexpr (IndexTraits<T>::kEnabled) {
        auto children = machine.AllocateSlots(node, 1, array.size());
        if (!children) {
          return std::unexpected(children.error());
        }
      }
      (*offsets)[0] = prefix;
      initialized = true;
    }
    auto offsets = machine.Slots(node, 0, array.size() + 1);
    while (next < array.size()) {
      const auto position = next++;
      const auto start = reader.position();
      ViewTraits<T>::SkipTrusted(reader);
      offsets[position + 1] = prefix + reader.position();
      if constexpr (IndexTraits<T>::kEnabled) {
        auto child = machine.Visit<T>(
            array.encoded().subspan(prefix + start, reader.position() - start));
        if (!child) {
          return std::unexpected(child.error());
        }
        machine.Slots(node, 1, array.size())[position] = *child - node;
        return {};
      }
    }
    stack.Pop();
    return {};
  }

  ArrayView<T> array;
  std::size_t node;
  Reader reader;
  std::size_t prefix;
  std::size_t next = 0;
  bool initialized = false;
};

template <typename T, std::size_t N>
struct SequenceIndexTraits {
  static constexpr bool kEnabled = N != 0 && ViewTraits<T>::kFixedSize == 0;
  using Type = ArrayView<T>;

  static Type Make(
      std::span<const std::byte> encoded, ViewIndexRef index) noexcept {
    if constexpr (N == std::dynamic_extent) {
      auto view = ViewTraits<std::vector<T>>::FromValidated(encoded);
      view.index_ = index;
      return view;
    } else {
      auto view = ViewTraits<std::array<T, N>>::FromValidated(encoded);
      view.index_ = index;
      return view;
    }
  }

  static Result<void> Push(IndexTraversal& machine,
      std::span<const std::byte> encoded,
      std::size_t node) {
    if constexpr (N == std::dynamic_extent) {
      return machine.stack.template Push<SequenceIndexFrame<T>>(
          ViewTraits<std::vector<T>>::FromValidated(encoded), node);
    } else {
      return machine.stack.template Push<SequenceIndexFrame<T>>(
          ViewTraits<std::array<T, N>>::FromValidated(encoded), node);
    }
  }
};

template <typename T, typename A>
struct IndexTraits<std::vector<T, A>>
    : SequenceIndexTraits<T, std::dynamic_extent> {};

template <typename T, std::size_t N>
struct IndexTraits<std::array<T, N>> : SequenceIndexTraits<T, N> {};

template <typename Key, typename Value>
struct MapIndexFrame {
  MapIndexFrame(MapView<Key, Value> map, std::size_t node) noexcept
      : map(map), node(node), reader(map.encoded()) {
    AdvanceTrusted(reader, 4);
  }

  Result<void> Step(
      IndexTraversal& machine, TraversalStack<IndexTraversal>& stack) {
    if (!initialized) {
      if (map.size() == std::numeric_limits<std::size_t>::max()) {
        return std::unexpected(Error{ErrorCode::kBufferTooSmall});
      }
      auto consumed = machine.context.Consume(map.size());
      if (!consumed) {
        return consumed;
      }
      for (const auto& [member, count] :
          {std::pair{std::size_t{0}, map.size() + 1},
              std::pair{std::size_t{2}, map.size()}}) {
        auto slots = machine.AllocateSlots(node, member, count);
        if (!slots) {
          return std::unexpected(slots.error());
        }
      }
      if constexpr (IndexTraits<Value>::kEnabled) {
        auto children = machine.AllocateSlots(node, 1, map.size());
        if (!children) {
          return std::unexpected(children.error());
        }
      }
      machine.Slots(node, 0, map.size() + 1)[0] = 4;
      initialized = true;
    }
    auto offsets = machine.Slots(node, 0, map.size() + 1);
    while (next < map.size()) {
      const auto position = next++;
      ViewTraits<Key>::SkipTrusted(reader);
      const auto start = reader.position();
      ViewTraits<Value>::SkipTrusted(reader);
      offsets[position + 1] = reader.position();
      machine.Slots(node, 2, map.size())[position] = position;
      if constexpr (IndexTraits<Value>::kEnabled) {
        auto child = machine.Visit<Value>(
            map.encoded().subspan(start, reader.position() - start));
        if (!child) {
          return std::unexpected(child.error());
        }
        machine.Slots(node, 1, map.size())[position] = *child - node;
        return {};
      }
    }
    std::ranges::sort(
        machine.Slots(node, 2, map.size()), {}, [&](std::size_t position) {
          Reader key(map.encoded().subspan(
              offsets[position], offsets[position + 1] - offsets[position]));
          return ViewTraits<Key>::ReadTrusted(key);
        });
    stack.Pop();
    return {};
  }

  MapView<Key, Value> map;
  std::size_t node;
  Reader reader;
  std::size_t next = 0;
  bool initialized = false;
};

template <typename Key, typename Value>
struct MapIndexTraits {
  static constexpr bool kEnabled = true;
  using Type = MapView<Key, Value>;

  static Type Make(
      std::span<const std::byte> encoded, ViewIndexRef index) noexcept {
    auto view = ViewTraits<std::map<Key, Value>>::FromValidated(encoded);
    view.index_ = index;
    return view;
  }

  static Result<void> Push(IndexTraversal& machine,
      std::span<const std::byte> encoded,
      std::size_t node) {
    return machine.stack.template Push<MapIndexFrame<Key, Value>>(
        ViewTraits<std::map<Key, Value>>::FromValidated(encoded), node);
  }
};

template <typename K, typename V, typename C, typename A>
struct IndexTraits<std::map<K, V, C, A>> : MapIndexTraits<K, V> {};

template <typename K, typename V, typename H, typename E, typename A>
struct IndexTraits<std::unordered_map<K, V, H, E, A>> : MapIndexTraits<K, V> {};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename K, typename V, typename C, typename Keys, typename Values>
struct IndexTraits<std::flat_map<K, V, C, Keys, Values>>
    : MapIndexTraits<K, V> {};
#endif

template <std::uint8_t Tag, typename T, bool Deprecated = false>
struct IndexField {
  using Value = T;
  static constexpr auto kTag = Tag;
  static constexpr bool kDeprecated = Deprecated;
  static constexpr bool kHasIndex = !Deprecated && IndexTraits<T>::kEnabled;
};

template <typename Tuple, std::size_t... N>
constexpr std::size_t IndexChildCount(std::index_sequence<N...>) {
  return (std::size_t{0} + ... + std::tuple_element_t<N, Tuple>::kHasIndex);
}

template <typename T>
struct RecordIndexFrame {
  using Fields = std::remove_cvref_t<decltype(IndexRecordFields<T>::kFields)>;
  static constexpr auto kCount = std::tuple_size_v<Fields>;
  static constexpr auto kChildren =
      IndexChildCount<Fields>(std::make_index_sequence<kCount>{});

  RecordIndexFrame(
      std::span<const std::byte> encoded, std::size_t node) noexcept
      : encoded(encoded), node(node), reader(encoded) {
  }

  Result<void> Step(
      IndexTraversal& machine, TraversalStack<IndexTraversal>& stack) {
    if (!initialized) {
      if constexpr (IndexRecordFields<T>::kMessage) {
        auto parsed = MessageIndex::Parse(encoded);
        if (!parsed) {
          return std::unexpected(parsed.error());
        }
        message = *parsed;
      } else if constexpr (!IndexRecordFields<T>::kUnion) {
        auto offsets = machine.AllocateSlots(node, 0, kCount + 1);
        if (!offsets) {
          return std::unexpected(offsets.error());
        }
        (*offsets)[0] = 0;
      }
      auto children = machine.AllocateSlots(node, 1, kChildren);
      if (!children) {
        return std::unexpected(children.error());
      }
      initialized = true;
    }
    while (next < kCount) {
      const auto position = next++;
      bool pushed = false;
      auto result = DispatchTraversalIndex<kCount>(
          position, [&](auto index) -> Result<void> {
            constexpr auto field_index = decltype(index)::value;
            using Field = std::tuple_element_t<field_index, Fields>;
            using Value = typename Field::Value;
            std::optional<std::span<const std::byte>> bytes;
            if constexpr (IndexRecordFields<T>::kMessage) {
              if constexpr (!Field::kDeprecated) {
                bytes = message->Field(Field::kTag);
              }
            } else if constexpr (IndexRecordFields<T>::kUnion) {
              if (std::to_integer<std::uint8_t>(encoded[4]) == Field::kTag) {
                bytes = encoded.subspan(5);
              }
            } else {
              const auto start = reader.position();
              ViewTraits<Value>::SkipTrusted(reader);
              bytes = encoded.subspan(start, reader.position() - start);
              auto offsets = machine.Slots(node, 0, kCount + 1);
              offsets[position + 1] = reader.position();
            }
            if constexpr (Field::kHasIndex) {
              constexpr auto slot = IndexChildCount<Fields>(
                  std::make_index_sequence<field_index>{});
              if (bytes) {
                auto child = machine.Visit<Value>(*bytes);
                if (!child) {
                  return std::unexpected(child.error());
                }
                machine.Slots(node, 1, kChildren)[slot] = *child - node;
                pushed = true;
              }
            }
            return {};
          });
      if (!result) {
        return result;
      }
      if (pushed) {
        return {};
      }
    }
    stack.Pop();
    return {};
  }

  std::span<const std::byte> encoded;
  std::size_t node;
  Reader reader;
  std::optional<MessageIndex> message;
  std::size_t next = 0;
  bool initialized = false;
};

template <typename T, std::size_t Position>
auto ReadViewField(
    std::span<const std::byte> encoded, ViewIndexRef index) noexcept {
  using Fields = std::remove_cvref_t<decltype(IndexRecordFields<T>::kFields)>;
  using Field = std::tuple_element_t<Position, Fields>;
  constexpr auto slot =
      IndexChildCount<Fields>(std::make_index_sequence<Position>{});
  const auto offsets = index.Offsets(std::tuple_size_v<Fields> + 1);
  const auto bytes = encoded.subspan(
      offsets[Position], offsets[Position + 1] - offsets[Position]);
  if constexpr (Field::kHasIndex) {
    return MakeView<typename Field::Value>(bytes, index.Child(slot));
  } else {
    return MakeView<typename Field::Value>(bytes);
  }
}

template <typename T>
Result<typename ViewTraits<T>::Type> IndexView(
    typename ViewTraits<T>::Type view,
    IndexBuffer& buffer,
    DecodeOptions options = {}) noexcept {
  if (view.encoded().size() > options.max_bytes) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  if constexpr (!IndexTraits<T>::kEnabled) {
    return view;
  } else {
    auto index = BuildViewIndex(buffer, options, [&](IndexTraversal& machine) {
      return machine.template Visit<T>(view.encoded());
    });
    if (!index) {
      return std::unexpected(index.error());
    }
    return MakeView<T>(view.encoded(), *index);
  }
}

}  // namespace bebop::internal

namespace bebop {

template <typename T>
Result<ArrayView<T>> ArrayView<T>::Index(
    IndexBuffer& buffer, DecodeOptions options) const noexcept {
  if (encoded_.size() > options.max_bytes) {
    return std::unexpected(Error{ErrorCode::kOverflow});
  }
  if constexpr (internal::ViewTraits<T>::kFixedSize != 0) {
    return *this;
  }
  auto index = internal::BuildViewIndex(buffer,
      options,
      [&](internal::IndexTraversal& machine) -> Result<std::size_t> {
        auto node = machine.Header();
        if (!node) {
          return node;
        }
        auto pushed =
            machine.stack.template Push<internal::SequenceIndexFrame<T>>(
                *this, *node);
        if (!pushed) {
          return std::unexpected(pushed.error());
        }
        return *node;
      });
  if (!index) {
    return std::unexpected(index.error());
  }
  auto view = *this;
  view.index_ = *index;
  return view;
}

template <typename Key, typename Value>
Result<MapView<Key, Value>> MapView<Key, Value>::Index(
    IndexBuffer& buffer, DecodeOptions options) const noexcept {
  return internal::IndexView<std::map<Key, Value>>(*this, buffer, options);
}

}  // namespace bebop

namespace bebop::reflection {

enum class TypeKind {
  kBool,
  kByte,
  kInt8,
  kInt16,
  kUint16,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kInt128,
  kUint128,
  kFloat16,
  kFloat32,
  kFloat64,
  kBfloat16,
  kString,
  kUuid,
  kTimestamp,
  kDuration,
  kArray,
  kFixedArray,
  kMap,
  kDefined,
};

enum class DefinitionKind {
  kEnum,
  kStruct,
  kMessage,
  kUnion,
};

enum class Visibility {
  kDefault,
  kExport,
  kLocal,
};

struct Definition;

//! Wire type information independent of C++ storage layout
struct Type {
  TypeKind kind;
  std::uint32_t scalar_size = 0;
  std::uint32_t extent = 0;
  const Type* element = nullptr;
  const Type* key = nullptr;
  const Type* value = nullptr;
  const Definition* definition = nullptr;

  //! Fixed wire size, or zero for variable-sized values
  constexpr std::uint32_t FixedSize() const noexcept;
};

//! Exact signed-magnitude representation of a decorator integer
struct Integer {
  UInt128 magnitude;
  bool negative = false;
};

//! Decorator value and optional unexpanded source text
struct Literal {
  std::variant<std::monostate,
      bool,
      Integer,
      double,
      std::string_view,
      Uuid,
      std::span<const std::byte>,
      Timestamp,
      Duration>
      value;
  std::string_view raw_text;
};

struct Argument {
  std::string_view name;
  Literal value;
};

struct Decorator {
  std::string_view name;
  std::span<const Argument> arguments;
  std::span<const Argument> exports;
};

struct Field {
  std::string_view name;
  std::string_view cpp_name;
  std::string_view documentation;
  const Type* type;
  std::uint8_t tag = 0;
  std::span<const Decorator> decorators;
};

struct EnumMember {
  std::string_view name;
  std::string_view cpp_name;
  std::string_view documentation;
  std::uint64_t value_bits;
  std::span<const Decorator> decorators;
};

struct Branch {
  std::string_view name;
  std::string_view cpp_name;
  std::string_view documentation;
  const Type* type;
  std::uint8_t discriminator;
  std::span<const Decorator> decorators;
};

//! Schema definition and its generated C++ representation
struct Definition {
  std::string_view name;
  std::string_view full_name;
  std::string_view cpp_name;
  std::string_view documentation;
  DefinitionKind kind;
  Visibility visibility = Visibility::kDefault;
  std::uint32_t fixed_size = 0;
  bool is_mutable = false;
  bool is_flags = false;
  bool is_borrowed = false;
  bool is_view = false;
  bool is_pmr = false;
  const Type* underlying_type = nullptr;
  std::span<const Field> fields{};
  std::span<const EnumMember> members{};
  std::span<const Branch> branches{};
  std::span<const Definition* const> nested{};
  std::span<const Decorator> decorators{};
};

constexpr std::uint32_t Type::FixedSize() const noexcept {
  if (kind == TypeKind::kDefined) {
    return definition->fixed_size;
  }
  if (kind == TypeKind::kFixedArray) {
    const auto size = element->FixedSize();
    if (size == 0 ||
        extent > std::numeric_limits<std::uint32_t>::max() / size) {
      return 0;
    }
    return size * extent;
  }
  return scalar_size;
}

//! Reflection traits specialized by generated headers
template <typename T>
struct Traits;

template <typename T>
concept Reflected = requires { Traits<std::remove_cvref_t<T>>::kDefinition; };

template <typename T>
struct TypeMetadata {
  static inline constexpr Type kType{
      .kind = TypeKind::kDefined, .definition = &Traits<T>::kDefinition};
};

template <TypeKind Kind, std::uint32_t Size>
struct ScalarType {
  static inline constexpr Type kType{.kind = Kind, .scalar_size = Size};
};

template <>
struct TypeMetadata<bool> : ScalarType<TypeKind::kBool, 1> {};

template <>
struct TypeMetadata<std::uint8_t> : ScalarType<TypeKind::kByte, 1> {};

template <>
struct TypeMetadata<std::int8_t> : ScalarType<TypeKind::kInt8, 1> {};

template <>
struct TypeMetadata<std::int16_t> : ScalarType<TypeKind::kInt16, 2> {};

template <>
struct TypeMetadata<std::uint16_t> : ScalarType<TypeKind::kUint16, 2> {};

template <>
struct TypeMetadata<std::int32_t> : ScalarType<TypeKind::kInt32, 4> {};

template <>
struct TypeMetadata<std::uint32_t> : ScalarType<TypeKind::kUint32, 4> {};

template <>
struct TypeMetadata<std::int64_t> : ScalarType<TypeKind::kInt64, 8> {};

template <>
struct TypeMetadata<std::uint64_t> : ScalarType<TypeKind::kUint64, 8> {};

template <>
struct TypeMetadata<Int128> : ScalarType<TypeKind::kInt128, 16> {};

template <>
struct TypeMetadata<UInt128> : ScalarType<TypeKind::kUint128, 16> {};

template <>
struct TypeMetadata<Float16> : ScalarType<TypeKind::kFloat16, 2> {};

template <>
struct TypeMetadata<float> : ScalarType<TypeKind::kFloat32, 4> {};

template <>
struct TypeMetadata<double> : ScalarType<TypeKind::kFloat64, 8> {};

template <>
struct TypeMetadata<BFloat16> : ScalarType<TypeKind::kBfloat16, 2> {};

template <>
struct TypeMetadata<Uuid> : ScalarType<TypeKind::kUuid, 16> {};

template <>
struct TypeMetadata<Timestamp> : ScalarType<TypeKind::kTimestamp, 16> {};

template <>
struct TypeMetadata<Duration> : ScalarType<TypeKind::kDuration, 12> {};

template <typename Traits, typename Allocator>
struct TypeMetadata<std::basic_string<char, Traits, Allocator>>
    : ScalarType<TypeKind::kString, 0> {};

template <>
struct TypeMetadata<std::string_view> : ScalarType<TypeKind::kString, 0> {};

template <typename T>
struct TypeMetadata<std::optional<T>> : TypeMetadata<T> {};

template <typename T, typename Deleter>
struct TypeMetadata<std::unique_ptr<T, Deleter>> : TypeMetadata<T> {};

template <typename T>
struct TypeMetadata<T*> : TypeMetadata<std::remove_const_t<T>> {};

template <typename T, typename Allocator>
struct TypeMetadata<std::vector<T, Allocator>> {
  static inline constexpr Type kType{
      .kind = TypeKind::kArray, .element = &TypeMetadata<T>::kType};
};

template <typename T, std::size_t N>
struct TypeMetadata<std::array<T, N>> {
  static inline constexpr Type kType{.kind = TypeKind::kFixedArray,
      .extent = static_cast<std::uint32_t>(N),
      .element = &TypeMetadata<T>::kType};
};

template <typename T, std::size_t N>
struct TypeMetadata<std::span<T, N>> {
  static inline constexpr Type kType = [] {
    if constexpr (N == std::dynamic_extent) {
      return Type{.kind = TypeKind::kArray,
          .element = &TypeMetadata<std::remove_const_t<T>>::kType};
    } else {
      return Type{.kind = TypeKind::kFixedArray,
          .extent = static_cast<std::uint32_t>(N),
          .element = &TypeMetadata<std::remove_const_t<T>>::kType};
    }
  }();
};

template <typename Key, typename Value>
struct MapType {
  static inline constexpr Type kType{.kind = TypeKind::kMap,
      .key = &TypeMetadata<Key>::kType,
      .value = &TypeMetadata<Value>::kType};
};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct TypeMetadata<std::map<Key, Value, Compare, Allocator>>
    : MapType<Key, Value> {};

template <typename Key,
    typename Value,
    typename Hasher,
    typename Equal,
    typename Allocator>
struct TypeMetadata<std::unordered_map<Key, Value, Hasher, Equal, Allocator>>
    : MapType<Key, Value> {};

template <typename Key, typename Value>
struct TypeMetadata<MapRef<Key, Value>> : MapType<Key, Value> {};

#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
template <typename Key,
    typename Value,
    typename Compare,
    typename Keys,
    typename Values>
struct TypeMetadata<std::flat_map<Key, Value, Compare, Keys, Values>>
    : MapType<Key, Value> {};
#endif

template <typename T>
constexpr const Type& TypeOf() noexcept {
  return TypeMetadata<std::remove_cvref_t<T>>::kType;
}

template <auto Member, bool Optional, std::size_t Index>
struct Accessor {
  static constexpr bool kOptional = Optional;
  static constexpr std::size_t kIndex = Index;

  template <typename T>
  constexpr decltype(auto) Get(T&& value) const {
    return std::invoke(Member, std::forward<T>(value));
  }
};

}  // namespace bebop::reflection

namespace bebop {

//! Inspect the schema definition of a generated type
template <reflection::Reflected T>
constexpr const reflection::Definition& Describe() noexcept {
  return reflection::Traits<std::remove_cvref_t<T>>::kDefinition;
}

template <reflection::Reflected T>
constexpr const reflection::Definition& Describe(const T&) noexcept {
  return Describe<T>();
}

//! Visit fields in schema declaration order, including absent message fields
//! The callback receives field metadata and its typed value.
template <reflection::Reflected T, typename Visitor>
  requires requires { reflection::Traits<std::remove_cvref_t<T>>::kFields; }
constexpr void VisitFields(T&& value, Visitor&& visitor) {
  using Metadata = reflection::Traits<std::remove_cvref_t<T>>;
  std::apply(
      [&](const auto&... field) {
        (std::invoke(visitor,
             Metadata::kFieldData[field.kIndex],
             field.Get(std::forward<T>(value))),
            ...);
      },
      Metadata::kFields);
}

//! Visit present message fields and all struct fields in declaration order
//! Present optional fields are unwrapped before invoking the callback.
template <reflection::Reflected T, typename Visitor>
  requires requires { reflection::Traits<std::remove_cvref_t<T>>::kFields; }
constexpr void VisitPresentFields(T&& value, Visitor&& visitor) {
  using Metadata = reflection::Traits<std::remove_cvref_t<T>>;
  auto visit = [&](const auto& field) {
    auto&& item = field.Get(std::forward<T>(value));
    const auto& metadata = Metadata::kFieldData[field.kIndex];
    if constexpr (std::remove_cvref_t<decltype(field)>::kOptional) {
      if (item) {
        std::invoke(visitor, metadata, *item);
      }
    } else {
      std::invoke(visitor, metadata, std::forward<decltype(item)>(item));
    }
  };
  std::apply(
      [&](const auto&... field) {
        (visit(field), ...);
      },
      Metadata::kFields);
}

//! Visit the active union branch with its typed payload
//! Branch metadata is null for an unknown discriminator.
template <reflection::Reflected T, typename Visitor>
  requires requires { reflection::Traits<std::remove_cvref_t<T>>::kValue; }
constexpr decltype(auto) VisitUnion(T&& value, Visitor&& visitor) {
  using Metadata = reflection::Traits<std::remove_cvref_t<T>>;
  decltype(auto) variant =
      std::invoke(Metadata::kValue, std::forward<T>(value));
  const reflection::Branch* branch = nullptr;
  if (variant.index() < Metadata::kBranches.size()) {
    branch = &Metadata::kBranches[variant.index()];
  }
  return std::visit(
      [&](auto&& payload) -> decltype(auto) {
        return std::invoke(
            visitor, branch, std::forward<decltype(payload)>(payload));
      },
      std::forward<decltype(variant)>(variant));
}

//! Find the schema name of an exactly matching enum member
template <typename T>
  requires std::is_enum_v<T> && reflection::Reflected<T>
constexpr std::optional<std::string_view> EnumName(T value) noexcept {
  using Metadata = reflection::Traits<T>;
  for (std::size_t index = 0; index < Metadata::kValues.size(); ++index) {
    if (Metadata::kValues[index] == value) {
      return Metadata::kMembers[index].name;
    }
  }
  return std::nullopt;
}

//! Find an enum member by its schema name
template <typename T>
  requires std::is_enum_v<T> && reflection::Reflected<T>
constexpr std::optional<T> EnumValue(std::string_view name) noexcept {
  using Metadata = reflection::Traits<T>;
  for (std::size_t index = 0; index < Metadata::kValues.size(); ++index) {
    if (Metadata::kMembers[index].name == name) {
      return Metadata::kValues[index];
    }
  }
  return std::nullopt;
}

}  // namespace bebop

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif  // BEBOP_HPP_
