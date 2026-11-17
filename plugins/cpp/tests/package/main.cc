#include <array>
#include <cstddef>
#include <cstdio>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <vector>

#include "package.bb.hpp"

#if defined(BEBOP_TEST_NATIVE_TYPES)
#if defined(BEBOP_CPP_NO_NATIVE_INT128) ||  \
    defined(BEBOP_CPP_NO_NATIVE_FLOAT16) || \
    defined(BEBOP_CPP_NO_NATIVE_BFLOAT16)
#error "The installed C++ package must not force fallback scalar types"
#endif
#if defined(__SIZEOF_INT128__)
static_assert(std::is_same_v<bebop::Int128, __int128_t>);
static_assert(std::is_same_v<bebop::UInt128, __uint128_t>);
#endif
#if defined(__STDCPP_FLOAT16_T__)
static_assert(std::is_same_v<bebop::Float16, std::float16_t>);
#elif defined(__FLT16_MANT_DIG__)
static_assert(std::is_same_v<bebop::Float16, _Float16>);
#endif
#if defined(__STDCPP_BFLOAT16_T__)
static_assert(std::is_same_v<bebop::BFloat16, std::bfloat16_t>);
#elif defined(__BFLT16_MAX__) ||                                       \
    (defined(__clang__) && defined(__aarch64__)) ||                    \
    (defined(__clang__) && defined(__x86_64__) && defined(__SSE2__) && \
        !defined(__apple_build_version__))
static_assert(std::is_same_v<bebop::BFloat16, __bf16>);
#endif
#endif

int main() {
  std::printf("Native types: int128=%d, float16=%d, bfloat16=%d\n",
      bebop::kHasNativeInt128,
      bebop::kHasNativeFloat16,
      bebop::kHasNativeBFloat16);
  volatile float input = 1.5f;
  const auto half = static_cast<bebop::Float16>(input);
  const auto bfloat = static_cast<bebop::BFloat16>(input);
  if (static_cast<float>(half) != 1.5f ||
      static_cast<float>(bfloat) != 1.5f) {
    return 1;
  }
  packaging::Entity entity;
  entity.id = bebop::MakeUInt128(42, 7);
  entity.half = bebop::Float16FromBits(0x3555);
  entity.bfloat = bebop::BFloat16FromBits(0x3f81);
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
      bebop::Float16Bits(first->half()) != 0x3555 ||
      bebop::BFloat16Bits(first->bfloat()) != 0x3f81 ||
      first->labels().At(1) != "last") {
    return 1;
  }
  return 0;
}
