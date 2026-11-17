#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#include "c_enums.bb.h"
#include "unity.h"

static_assert(sizeof(Enum_types_Signed8) == sizeof(int8_t));
static_assert(sizeof(Enum_types_Unsigned8) == sizeof(uint8_t));
static_assert(sizeof(Enum_types_Signed16) == sizeof(int16_t));
static_assert(sizeof(Enum_types_Unsigned16) == sizeof(uint16_t));
static_assert(sizeof(Enum_types_Signed32) == sizeof(int32_t));
static_assert(sizeof(Enum_types_Unsigned32) == sizeof(uint32_t));
static_assert(sizeof(Enum_types_Signed64) == sizeof(int64_t));
static_assert(sizeof(Enum_types_Unsigned64) == sizeof(uint64_t));
static_assert(sizeof(Enum_types_Default) == sizeof(uint32_t));
static_assert(sizeof(Enum_types_Update_Tag) == sizeof(uint8_t));
static_assert(sizeof(Enum_types_Event_Disc) == sizeof(uint8_t));
static_assert(alignof(Enum_types_Signed64) == alignof(int64_t));
static_assert(alignof(Enum_types_Unsigned64) == alignof(uint64_t));

#ifdef ENUM_TYPES_UNSIGNED_64_MAX
#error Enum members must not be macros
#endif

void setUp(void) {}

void tearDown(void) {}

void test_enum_wire_roundtrip(void);

void test_enum_wire_roundtrip(void)
{
  const Enum_types_Values value = {
      .signed8 = ENUM_TYPES_SIGNED_8_MIN,
      .unsigned8 = ENUM_TYPES_UNSIGNED_8_MAX,
      .signed16 = ENUM_TYPES_SIGNED_16_MIN,
      .unsigned16 = ENUM_TYPES_UNSIGNED_16_MAX,
      .signed32 = ENUM_TYPES_SIGNED_32_MIN,
      .unsigned32 = ENUM_TYPES_UNSIGNED_32_MAX,
      .signed64 = ENUM_TYPES_SIGNED_64_MIN,
      .unsigned64 = ENUM_TYPES_UNSIGNED_64_MAX,
      .default_value = ENUM_TYPES_DEFAULT_ONE,
  };
  const uint8_t expected[] = {
      0x80, 0xff,
      0x00, 0x80, 0xff, 0xff,
      0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x01, 0x00, 0x00, 0x00,
  };
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), Enum_types_Values_encoded_size(&value));
  Bebop_Context* context = bebop_context_new(NULL);
  TEST_ASSERT_NOT_NULL(context);
  Bebop_Writer* writer = bebop_context_writer(context, sizeof(expected));
  TEST_ASSERT_NOT_NULL(writer);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Enum_types_Values_encode(writer, &value));
  const Bebop_View bytes = bebop_writer_view(writer);
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), bytes.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes.data, sizeof(expected));

  Enum_types_Values decoded = {0};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Enum_types_Values_decode(context, bytes, &decoded));
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, decoded.signed8);
  TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, decoded.unsigned8);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, decoded.signed16);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, decoded.unsigned16);
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, decoded.signed32);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, decoded.unsigned32);
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, decoded.signed64);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, decoded.unsigned64);
  TEST_ASSERT_EQUAL_UINT32(1, decoded.default_value);
  bebop_context_free(context);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_enum_wire_roundtrip);
  return UNITY_END();
}
