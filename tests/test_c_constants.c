#include <math.h>
#include <stdint.h>
#include <string.h>

#include "numeric.bb.h"
#include "unity.h"

void setUp(void) {}

void tearDown(void) {}

void test_integer_constants(void);
void test_enum_expressions(void);
void test_float_constants(void);

void test_float_constants(void)
{
  TEST_ASSERT_TRUE(isinf(Numeric_POSITIVE_INFINITY));
  TEST_ASSERT_FALSE(signbit(Numeric_POSITIVE_INFINITY));
  TEST_ASSERT_TRUE(isinf(Numeric_NEGATIVE_INFINITY));
  TEST_ASSERT_TRUE(signbit(Numeric_NEGATIVE_INFINITY));
  TEST_ASSERT_TRUE(isnan(Numeric_NOT_A_NUMBER));
  uint64_t bits;
  memcpy(&bits, &Numeric_PRECISE, sizeof(bits));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x3ff0000000000001), bits);
  memcpy(&bits, &Numeric_NEGATIVE_ZERO, sizeof(bits));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x8000000000000000), bits);
}

void test_integer_constants(void)
{
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, Numeric_MIN_I8);
  TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, Numeric_MAX_U8);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, Numeric_MIN_I16);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, Numeric_MAX_U16);
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, Numeric_MIN_I32);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, Numeric_MAX_U32);
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, Numeric_MIN_I64);
  TEST_ASSERT_EQUAL_INT64(INT64_MAX, Numeric_MAX_I64);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, Numeric_MAX_U64);
  uint64_t words[2];
  memcpy(words, &Numeric_MIN_I128, sizeof(words));
  TEST_ASSERT_EQUAL_UINT64(0, words[0]);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(1) << 63, words[1]);
  memcpy(words, &Numeric_NEG_I128, sizeof(words));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, words[0]);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 1, words[1]);
  memcpy(words, &Numeric_MAX_I128, sizeof(words));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, words[0]);
  TEST_ASSERT_EQUAL_UINT64(INT64_MAX, words[1]);
  memcpy(words, &Numeric_MAX_U128, sizeof(words));
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, words[0]);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, words[1]);
}

void test_enum_expressions(void)
{
  _Static_assert(sizeof(Numeric_BitExpressions) == sizeof(uint32_t), "enum width");
  _Static_assert(sizeof(Numeric_SignedExpressions) == sizeof(int64_t), "enum width");
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, NUMERIC_BIT_EXPRESSIONS_ALL);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1, NUMERIC_BIT_EXPRESSIONS_LEGACY);
  TEST_ASSERT_EQUAL_UINT32(12, NUMERIC_BIT_EXPRESSIONS_GROUP);
  TEST_ASSERT_EQUAL_UINT32(8, NUMERIC_BIT_EXPRESSIONS_LEFT);
  TEST_ASSERT_EQUAL_UINT32(8, NUMERIC_BIT_EXPRESSIONS_RIGHT);
  TEST_ASSERT_EQUAL_INT64(-4, NUMERIC_SIGNED_EXPRESSIONS_HALF);
  TEST_ASSERT_EQUAL_INT64(-1, NUMERIC_SIGNED_EXPRESSIONS_ALL);
  TEST_ASSERT_EQUAL_INT64(3, NUMERIC_SIGNED_EXPRESSIONS_GROUP);
  TEST_ASSERT_EQUAL_INT64(INT64_MAX, NUMERIC_SIGNED_EXPRESSIONS_RECOVERED_MAX);
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, NUMERIC_SIGNED_EXPRESSIONS_RECOVERED_MIN);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, NUMERIC_WIDE_EXPRESSIONS_RECOVERED);
  TEST_ASSERT_EQUAL_UINT64(1, NUMERIC_WIDE_EXPRESSIONS_BORROW);

  const Numeric_ExpressionValues value = {
      .flags = NUMERIC_BIT_EXPRESSIONS_ALL, .delta = NUMERIC_SIGNED_EXPRESSIONS_HALF
  };
  TEST_ASSERT_EQUAL_size_t(12, Numeric_ExpressionValues_encoded_size(&value));
  Bebop_Context* context = bebop_context_new(NULL);
  TEST_ASSERT_NOT_NULL(context);
  Bebop_Writer* writer = bebop_context_writer(context, 12);
  TEST_ASSERT_NOT_NULL(writer);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Numeric_ExpressionValues_encode(writer, &value));
  const Bebop_View bytes = bebop_writer_view(writer);
  const uint8_t expected[] = {255, 255, 255, 255, 252, 255, 255, 255, 255, 255, 255, 255};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes.data, sizeof(expected));
  Numeric_ExpressionValues decoded = {0};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Numeric_ExpressionValues_decode(context, bytes, &decoded));
  TEST_ASSERT_EQUAL_UINT32(value.flags, decoded.flags);
  TEST_ASSERT_EQUAL_INT64(value.delta, decoded.delta);
  bebop_context_free(context);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_integer_constants);
  RUN_TEST(test_enum_expressions);
  RUN_TEST(test_float_constants);
  return UNITY_END();
}
