#include <array>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <type_traits>

#include "codegen_all.bb.hpp"
#include "codegen_base.bb.hpp"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

template <typename T>
void CheckRoundTrip(const T& value) {
  auto encoded = bebop::Encode(value);
  Check(encoded.has_value());
  auto decoded = bebop::Decode<T>(*encoded);
  Check(decoded && *decoded == value);
  auto view = bebop::ParseView<T>(*encoded);
  Check(view.has_value());
}

void CheckFixedArrayReferences() {
  using namespace codegen::all;
  static_assert(std::is_aggregate_v<FixedScalarArraysRef>);
  static_assert(std::is_aggregate_v<KitchenSinkRef>);
  static_assert(!std::is_default_constructible_v<FixedScalarArraysRef>);
  static_assert(!std::is_default_constructible_v<KitchenSinkRef>);
  const FixedScalarArrays arrays;
  const FixedScalarArraysRef reference{
      .arr_bool = arrays.arr_bool(),
      .arr_byte = arrays.arr_byte(),
      .arr_int8 = arrays.arr_int8(),
      .arr_uint8 = arrays.arr_uint8(),
      .arr_int16 = arrays.arr_int16(),
      .arr_uint16 = arrays.arr_uint16(),
      .arr_int32 = arrays.arr_int32(),
      .arr_uint32 = arrays.arr_uint32(),
      .arr_int64 = arrays.arr_int64(),
      .arr_uint64 = arrays.arr_uint64(),
      .arr_int128 = arrays.arr_int128(),
      .arr_uint128 = arrays.arr_uint128(),
      .arr_float16 = arrays.arr_float16(),
      .arr_float32 = arrays.arr_float32(),
      .arr_float64 = arrays.arr_float64(),
      .arr_string = arrays.arr_string(),
      .arr_uuid = arrays.arr_uuid(),
      .arr_timestamp = arrays.arr_timestamp(),
      .arr_duration = arrays.arr_duration(),
  };
  auto encoded = bebop::Encode(arrays);
  auto borrowed = bebop::Encode(reference);
  Check(encoded && borrowed && *encoded == *borrowed);
}

}  // namespace

int main() {
  CheckRoundTrip(codegen::all::AllScalars{});
  CheckRoundTrip(codegen::all::KitchenSink{});
  CheckRoundTrip(codegen::all::pmr::KitchenSink{});
  CheckFixedArrayReferences();
}
