#include "bebop.c"
#include "test_common.h"
#include "unity.h"

static bebop_context_t* ctx;

void setUp(void)
{
  bebop_host_t host = test_host(NULL, 0);
  ctx = bebop_context_create(&host);
  TEST_ASSERT_NOT_NULL(ctx);
}

void tearDown(void)
{
  bebop_context_destroy(ctx);
}

static bebop_parse_result_t* parse_ok(const char* source)
{
  bebop_parse_result_t* result = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_parse_source(ctx, BEBOP_SOURCE(source, "integer.bop"), &result)
  );
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  return result;
}

void test_integer_boundaries_roundtrip(void);
void test_integer_rejects_overflow(void);
void test_integer_decimal_conversion(void);
void test_integer_decorator_roundtrip(void);
void test_integer_decorator_range(void);

void test_integer_boundaries_roundtrip(void)
{
  const char* source =
      "const int8 I8_MIN = -128;\n"
      "const byte U8_MAX = 255;\n"
      "const int16 I16_MIN = -32768;\n"
      "const uint16 U16_MAX = 65535;\n"
      "const int32 I32_MIN = -2147483648;\n"
      "const uint32 U32_MAX = 4294967295;\n"
      "const int64 I64_MIN = -9223372036854775808;\n"
      "const uint64 U64_MAX = 18446744073709551615;\n"
      "const int128 I128_MIN = -170141183460469231731687303715884105728;\n"
      "const int128 I128_MAX = 170141183460469231731687303715884105727;\n"
      "const uint128 U128_MAX = 340282366920938463463374607431768211455;\n"
      "const uint128 HEX = 0xffffffffffffffffffffffffffffffff;\n";
  static const bebop__integer_t expected[] = {
      {.low = 128, .negative = true},
      {.low = 255},
      {.low = 32768, .negative = true},
      {.low = 65535},
      {.low = UINT64_C(2147483648), .negative = true},
      {.low = UINT32_MAX},
      {.low = UINT64_C(9223372036854775808), .negative = true},
      {.low = UINT64_MAX},
      {.high = UINT64_C(9223372036854775808), .negative = true},
      {.low = UINT64_MAX, .high = INT64_MAX},
      {.low = UINT64_MAX, .high = UINT64_MAX},
      {.low = UINT64_MAX, .high = UINT64_MAX},
  };
  bebop_parse_result_t* result = parse_ok(source);
  bebop_descriptor_t* descriptor = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &descriptor)
  );
  const uint8_t* wire = NULL;
  size_t size = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(descriptor, &wire, &size));
  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, wire, size, &decoded));
  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  const bebop_descriptor_schema_t* wire_schema = bebop_descriptor_schema_at(decoded, 0);
  for (uint32_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    const bebop_literal_t* literal = bebop_def_const_value(bebop_schema_definition_at(schema, i));
    uint64_t low = 0, high = 0;
    bool negative = false;
    TEST_ASSERT_TRUE(bebop_literal_as_integer(literal, &low, &high, &negative));
    TEST_ASSERT_EQUAL_UINT64(expected[i].low, low);
    TEST_ASSERT_EQUAL_UINT64(expected[i].high, high);
    TEST_ASSERT_EQUAL(expected[i].negative, negative);
    const bebop_descriptor_literal_t* wire_literal =
        bebop_descriptor_def_const_value(bebop_descriptor_schema_def_at(wire_schema, i));
    TEST_ASSERT_TRUE(bebop_descriptor_literal_as_integer(wire_literal, &low, &high, &negative));
    TEST_ASSERT_EQUAL_UINT64(expected[i].low, low);
    TEST_ASSERT_EQUAL_UINT64(expected[i].high, high);
    TEST_ASSERT_EQUAL(expected[i].negative, negative);
    int64_t narrow;
    const bool fits = bebop__integer_to_int64(expected[i], &narrow);
    int64_t legacy = 0;
    if (fits) {
      legacy = narrow;
    }
    TEST_ASSERT_EQUAL(fits, BEBOP_HAS_VALUE(wire_literal->int_value));
    TEST_ASSERT_EQUAL(!fits, BEBOP_HAS_VALUE(wire_literal->integer_value));
    if (!fits) {
      uint64_t magnitude[2];
      memcpy(magnitude, &wire_literal->integer_value.value.magnitude, sizeof(magnitude));
      TEST_ASSERT_EQUAL_UINT64(expected[i].low, magnitude[0]);
      TEST_ASSERT_EQUAL_UINT64(expected[i].high, magnitude[1]);
      TEST_ASSERT_EQUAL(expected[i].negative, wire_literal->integer_value.value.negative);
      TEST_ASSERT_EQUAL_UINT32(
          17, Bebop_IntegerValue_encoded_size(&wire_literal->integer_value.value)
      );
    }
    TEST_ASSERT_EQUAL_INT64(legacy, bebop_literal_as_int(literal));
    TEST_ASSERT_EQUAL_INT64(legacy, bebop_descriptor_literal_as_int(wire_literal));
  }
  TEST_ASSERT_FALSE(bebop_literal_as_integer(NULL, NULL, NULL, NULL));
  TEST_ASSERT_FALSE(bebop_descriptor_literal_as_integer(NULL, NULL, NULL, NULL));
  bebop_descriptor_free(decoded);
  bebop_descriptor_free(descriptor);
}

void test_integer_rejects_overflow(void)
{
  const char* sources[] = {
      "const int8 X = 128;",
      "const int8 X = -129;",
      "const byte X = 256;",
      "const byte X = -1;",
      "const int16 X = 32768;",
      "const int16 X = -32769;",
      "const uint16 X = 65536;",
      "const int32 X = 2147483648;",
      "const int32 X = -2147483649;",
      "const uint32 X = 4294967296;",
      "const int64 X = 9223372036854775808;",
      "const int64 X = -9223372036854775809;",
      "const uint64 X = 18446744073709551616;",
      "const uint64 X = -1;",
      "const int128 X = 170141183460469231731687303715884105728;",
      "const int128 X = -170141183460469231731687303715884105729;",
      "const uint128 X = 340282366920938463463374607431768211456;",
      "const uint128 X = 0x100000000000000000000000000000000;",
      "const uint128 X = -1;",
      "const uint64 X = 184467440737095516160;",
      "const int64 X = 1.0;",
      "const int128 X = 1e3;",
  };
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
    bebop_parse_result_t* result = NULL;
    const bebop_status_t status =
        bebop_parse_source(ctx, BEBOP_SOURCE(sources[i], "bad.bop"), &result);
    TEST_ASSERT_TRUE_MESSAGE(status == BEBOP_ERROR, sources[i]);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE_MESSAGE(bebop_result_error_count(result) > 0, sources[i]);
  }
}

void test_integer_decimal_conversion(void)
{
  uint64_t seed = UINT64_C(0x5d92cace29348107);
  for (unsigned i = 0; i < 10000; ++i) {
    seed = seed * UINT64_C(6364136223846793005) + 1;
    bebop__integer_t value = {.low = seed, .negative = (i & 1) != 0};
    seed = seed * UINT64_C(6364136223846793005) + 1;
    value.high = seed;
    char text[41];
    const size_t size = bebop__integer_format(value, text);
    bebop__integer_t parsed;
    TEST_ASSERT_TRUE(bebop__integer_parse(text, size, &parsed));
    TEST_ASSERT_TRUE(bebop__integer_equal(value, parsed));
  }
  int64_t signed_value;
  uint64_t unsigned_value;
  TEST_ASSERT_TRUE(bebop_util_parse_int("-9223372036854775808", 20, &signed_value));
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, signed_value);
  TEST_ASSERT_TRUE(bebop_util_parse_uint("18446744073709551615", 20, &unsigned_value));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, unsigned_value);
  TEST_ASSERT_FALSE(bebop_util_parse_uint("184467440737095516160", 21, &unsigned_value));
  TEST_ASSERT_FALSE(bebop_util_parse_uint("0x10000000000000000", 19, &unsigned_value));
}

void test_integer_decorator_roundtrip(void)
{
  bebop_parse_result_t* result = parse_ok(
      "#decorator(exact) {\n"
      " targets = FIELD\n"
      " param big!: uint128\n"
      " param same!: uint128\n"
      " param low!: int128\n"
      " validate [[\n"
      "  assert(big == same and big > 9223372036854775807)\n"
      "  assert(low < -9223372036854775807 and low < big)\n"
      "  assert(tostring(big) == '340282366920938463463374607431768211455')\n"
      "  assert(tostring(low) == '-170141183460469231731687303715884105728')\n"
      " ]]\n"
      " export [[ return { big = big, low = low, text = tostring(big) } ]]\n"
      "}\n"
      "message Data {\n"
      " @exact(big: 340282366920938463463374607431768211455,\n"
      "        same: 0xffffffffffffffffffffffffffffffff,\n"
      "        low: -170141183460469231731687303715884105728) value(1): byte;\n"
      "}\n");
  const bebop_decorator_t* decorator =
      bebop_field_decorator_at(bebop_def_field_at(bebop_result_find(result, "Data"), 0), 0);
  uint64_t low, high;
  bool negative;
  TEST_ASSERT_TRUE(
      bebop_literal_as_integer(bebop_decorator_export(decorator, "big"), &low, &high, &negative)
  );
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, low);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, high);
  TEST_ASSERT_FALSE(negative);
  TEST_ASSERT_TRUE(
      bebop_literal_as_integer(bebop_decorator_export(decorator, "low"), &low, &high, &negative)
  );
  TEST_ASSERT_EQUAL_UINT64(0, low);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(9223372036854775808), high);
  TEST_ASSERT_TRUE(negative);
}

void test_integer_decorator_range(void)
{
  const char* source = "#decorator(tiny) { targets = FIELD param value!: byte }\n"
                       "message Data { @tiny(256) value(1): byte; }";
  bebop_parse_result_t* result = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_ERROR, bebop_parse_source(ctx, BEBOP_SOURCE(source, "bad.bop"), &result)
  );
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);
}

void test_integer_enum_u64(void);

void test_integer_enum_u64(void)
{
  bebop_parse_result_t* result = parse_ok(
      "enum Wide : uint64 { ZERO = 0; ALL = 18446744073709551615; TOP = 1 << 63; }\n"
      "enum Signed : int64 { ZERO = 0; MIN = -9223372036854775808; }\n");
  const bebop_def_t* wide = bebop_result_find(result, "Wide");
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, bebop_member_value_u64(bebop_def_member_at(wide, 1)));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(1) << 63, bebop_member_value_u64(bebop_def_member_at(wide, 2)));
  const bebop_def_t* signed_enum = bebop_result_find(result, "Signed");
  TEST_ASSERT_EQUAL_UINT64(
      UINT64_C(1) << 63, bebop_member_value_u64(bebop_def_member_at(signed_enum, 1))
  );
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_integer_boundaries_roundtrip);
  RUN_TEST(test_integer_rejects_overflow);
  RUN_TEST(test_integer_decimal_conversion);
  RUN_TEST(test_integer_decorator_roundtrip);
  RUN_TEST(test_integer_decorator_range);
  RUN_TEST(test_integer_enum_u64);
  return UNITY_END();
}
