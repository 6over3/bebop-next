#include <cstdint>
#include <type_traits>

#include "c_enums.bb.h"

static_assert(std::is_enum_v<Enum_types_Signed8>);
static_assert(std::is_enum_v<Enum_types_Unsigned64>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Signed8>, std::int8_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Unsigned8>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Signed16>, std::int16_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Unsigned16>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Signed32>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Unsigned32>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Signed64>, std::int64_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Unsigned64>, std::uint64_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Default>, std::uint32_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Update_Tag>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<Enum_types_Event_Disc>, std::uint8_t>);

int main() {
  const Enum_types_Values values{};
  if (Enum_types_Values_encoded_size(&values) != 34) {
    return 1;
  }
  return 0;
}
