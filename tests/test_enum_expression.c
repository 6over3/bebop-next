#include "bebop.h"
#include "test_common.h"
#include "unity.h"

static bebop_context_t* ctx;

void setUp(void)
{
  static const char* includes[] = {BEBOP_STD_DIR};
  const bebop_host_t host = test_host(includes, 1);
  ctx = bebop_context_create(&host);
  TEST_ASSERT_NOT_NULL(ctx);
}

void tearDown(void)
{
  bebop_context_destroy(ctx);
}

static bebop_parse_result_t* parse(const char* source)
{
  bebop_parse_result_t* result = NULL;
  const bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_EQUAL(BEBOP_FATAL, status);
  TEST_ASSERT_NOT_NULL(result);
  return result;
}

static void assert_value(
    const bebop_parse_result_t* result, const char* type, const char* name, uint64_t expected
)
{
  const bebop_def_t* def = bebop_result_find(result, type);
  TEST_ASSERT_NOT_NULL(def);
  for (uint32_t index = 0; index < bebop_def_member_count(def); ++index) {
    const bebop_enum_member_t* member = bebop_def_member_at(def, index);
    if (strcmp(bebop_member_name(member), name) == 0) {
      TEST_ASSERT_EQUAL_UINT64(expected, bebop_member_value_u64(member));
      return;
    }
  }
  TEST_FAIL_MESSAGE("Enum member was not found");
}

static void assert_error(const char* source, const char* message)
{
  const bebop_parse_result_t* result = parse(source);
  TEST_ASSERT_GREATER_THAN_UINT32(0, bebop_result_error_count(result));
  for (uint32_t index = 0; index < bebop_result_diagnostic_count(result); ++index) {
    const char* actual = bebop_diagnostic_message(bebop_result_diagnostic_at(result, index));
    if (strstr(actual, message) != NULL) {
      return;
    }
  }
  TEST_FAIL_MESSAGE("Expected diagnostic was not found");
}

void test_enum_expression_precedence(void);
void test_enum_expression_signed(void);
void test_enum_expression_widths(void);
void test_enum_expression_limits(void);
void test_enum_expression_invalid(void);
void test_enum_expression_arithmetic_range(void);

void test_enum_expression_precedence(void)
{
  const char* source =
      "import \"bebop/decorators.bop\";\n"
      "@flags enum Bits : byte {\n"
      "  NONE = 0; A = 1; B = 2; SUM = A + B;\n"
      "  ALL = ~NONE; LEGACY = ~0 - 1;\n"
      "  LEFT = 1 << 1 + 2; RIGHT = 32 >> 1 + 1;\n"
      "  GROUP = (A | B) << 2; MASK = ~(A | B) & 15;\n"
      "  ASSOCIATIVE = 20 - 5 - 3; MIXED = 1 | 6 & 3;\n"
      "  CHAIN = ~~A; SHIFT_CHAIN = 32 >> 1 << 2;\n"
      "}\n";
  const bebop_parse_result_t* result = parse(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  assert_value(result, "Bits", "SUM", 3);
  assert_value(result, "Bits", "ALL", 255);
  assert_value(result, "Bits", "LEGACY", 254);
  assert_value(result, "Bits", "LEFT", 8);
  assert_value(result, "Bits", "RIGHT", 8);
  assert_value(result, "Bits", "GROUP", 12);
  assert_value(result, "Bits", "MASK", 12);
  assert_value(result, "Bits", "ASSOCIATIVE", 12);
  assert_value(result, "Bits", "MIXED", 3);
  assert_value(result, "Bits", "CHAIN", 1);
  assert_value(result, "Bits", "SHIFT_CHAIN", 64);
}

void test_enum_expression_signed(void)
{
  const char* source =
      "import \"bebop/decorators.bop\";\n"
      "@flags enum Signed : int64 {\n"
      "  ZERO = 0; NEGATIVE = -8; HALF = NEGATIVE >> 1;\n"
      "  ALL = ~0; GROUP = -(-5 + 2); MINUS_NOT = -~1;\n"
      "  NOT_MINUS = ~-1; DOUBLE_MINUS = --1;\n"
      "  MINIMUM = -9223372036854775808;\n"
      "  MINIMUM_SHIFT = MINIMUM >> 63; LARGE_SHIFT = NEGATIVE >> 64;\n"
      "}\n";
  const bebop_parse_result_t* result = parse(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  assert_value(result, "Signed", "HALF", UINT64_MAX - 3);
  assert_value(result, "Signed", "ALL", UINT64_MAX);
  assert_value(result, "Signed", "GROUP", 3);
  assert_value(result, "Signed", "MINUS_NOT", 2);
  assert_value(result, "Signed", "NOT_MINUS", 0);
  assert_value(result, "Signed", "DOUBLE_MINUS", 1);
  assert_value(result, "Signed", "MINIMUM_SHIFT", UINT64_MAX);
  assert_value(result, "Signed", "LARGE_SHIFT", UINT64_MAX);
}

void test_enum_expression_widths(void)
{
  const char* source =
      "import \"bebop/decorators.bop\";\n"
      "@flags enum U8 : byte { ZERO = 0; ALL = ~0; }\n"
      "@flags enum U16 : uint16 { ZERO = 0; ALL = ~0; }\n"
      "@flags enum U32 : uint32 { ZERO = 0; ALL = ~0; }\n"
      "@flags enum U64 : uint64 { ZERO = 0; ALL = ~0; TOP = 1 << 63;\n"
      "  LOW = TOP >> 63; LARGE_LEFT = 1 << 64; LARGE_RIGHT = TOP >> 64; }\n"
      "@flags enum I8 : int8 { ZERO = 0; ALL = ~0; VALUE = ~127; }\n"
      "@flags enum I16 : int16 { ZERO = 0; ALL = ~0; VALUE = ~32767; }\n"
      "@flags enum I32 : int32 { ZERO = 0; ALL = ~0; VALUE = ~2147483647; }\n";
  const bebop_parse_result_t* result = parse(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  assert_value(result, "U8", "ALL", UINT8_MAX);
  assert_value(result, "U16", "ALL", UINT16_MAX);
  assert_value(result, "U32", "ALL", UINT32_MAX);
  assert_value(result, "U64", "ALL", UINT64_MAX);
  assert_value(result, "U64", "LOW", 1);
  assert_value(result, "U64", "LARGE_LEFT", 0);
  assert_value(result, "U64", "LARGE_RIGHT", 0);
  assert_value(result, "I8", "ALL", UINT64_MAX);
  assert_value(result, "I8", "VALUE", UINT64_MAX - 127);
  assert_value(result, "I16", "VALUE", UINT64_MAX - 32767);
  assert_value(result, "I32", "VALUE", UINT64_MAX - 2147483647);
}

void test_enum_expression_limits(void)
{
  char operators[34];
  memset(operators, '~', 32);
  operators[32] = '\0';
  char source[256];
  snprintf(source, sizeof(source), "enum E { ZERO = 0; VALUE = %s1; }", operators);
  const bebop_parse_result_t* result = parse(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  assert_value(result, "E", "VALUE", 1);

  operators[32] = '~';
  operators[33] = '\0';
  snprintf(source, sizeof(source), "enum E { ZERO = 0; VALUE = %s1; }", operators);
  assert_error(source, "Expression too complex");

  char open[33];
  char close[33];
  memset(open, '(', 31);
  memset(close, ')', 31);
  open[31] = '\0';
  close[31] = '\0';
  snprintf(source, sizeof(source), "enum E { ZERO = 0; VALUE = %s1%s; }", open, close);
  result = parse(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  assert_value(result, "E", "VALUE", 1);

  open[31] = '(';
  close[31] = ')';
  open[32] = '\0';
  close[32] = '\0';
  snprintf(source, sizeof(source), "enum E { ZERO = 0; VALUE = %s1%s; }", open, close);
  assert_error(source, "Expression too deeply nested");
}

void test_enum_expression_invalid(void)
{
  assert_error("enum E { ZERO = 0; VALUE = 1 + ; }", "Expected number");
  assert_error("enum E { ZERO = 0; VALUE = 1 >> ; }", "Expected number");
  assert_error("enum E { ZERO = 0; VALUE = ~; }", "Expected number");
  assert_error("enum E : byte { ZERO = 0; VALUE = 255 + 1; }", "outside the range");
}

void test_enum_expression_arithmetic_range(void)
{
  const char* source =
      "import \"bebop/decorators.bop\";\n"
      "@flags enum Signed : int64 {\n"
      "  ZERO = 0; MAX = 9223372036854775807; MIN = -9223372036854775808;\n"
      "  RECOVERED_MAX = MAX + 1 - 1; RECOVERED_MIN = MIN - 1 + 1;\n"
      "  DIFFERENCE = MAX + MIN; CORRECTED = (18446744073709551615 - MAX) - 1;\n"
      "  TOP_BIT = 1 << 63;\n"
      "}\n"
      "@flags enum Unsigned : uint64 {\n"
      "  ZERO = 0; MAX = 18446744073709551615;\n"
      "  RECOVERED_MAX = MAX + 1 - 1; TWO_WORDS = MAX + MAX - MAX;\n"
      "  BORROW = (MAX + 1) - MAX; NEGATIVE = -1 & MAX;\n"
      "}\n";
  const bebop_parse_result_t* result = parse(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  assert_value(result, "Signed", "RECOVERED_MAX", INT64_MAX);
  assert_value(result, "Signed", "RECOVERED_MIN", UINT64_C(1) << 63);
  assert_value(result, "Signed", "DIFFERENCE", UINT64_MAX);
  assert_value(result, "Signed", "CORRECTED", INT64_MAX);
  assert_value(result, "Signed", "TOP_BIT", UINT64_C(1) << 63);
  assert_value(result, "Unsigned", "RECOVERED_MAX", UINT64_MAX);
  assert_value(result, "Unsigned", "TWO_WORDS", UINT64_MAX);
  assert_value(result, "Unsigned", "BORROW", 1);
  assert_value(result, "Unsigned", "NEGATIVE", UINT64_MAX);

  const char* invalid[] = {
      "enum E : int64 { ZERO = 0; VALUE = 9223372036854775808; }",
      "enum E : int64 { ZERO = 0; VALUE = 0xffffffffffffffff; }",
      "enum E : int64 { ZERO = 0; VALUE = 9223372036854775807 + 1; }",
      "enum E : int64 { ZERO = 0; VALUE = -9223372036854775808 - 1; }",
      "enum E : int64 { ZERO = 0; VALUE = --9223372036854775808; }",
      "enum E : uint64 { ZERO = 0; VALUE = 18446744073709551615 + 1; }",
      "enum E : uint64 { ZERO = 0; VALUE = 0 - 1; }",
      "enum E : int8 { ZERO = 0; VALUE = 127 + 1; }",
      "enum E : uint32 { ZERO = 0; VALUE = 4294967295 + 1; }",
  };
  for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
    result = parse(invalid[index]);
    TEST_ASSERT_EQUAL_UINT32(1, bebop_result_error_count(result));
    const bebop_diagnostic_t* diagnostic = bebop_result_diagnostic_at(result, 0);
    TEST_ASSERT_NOT_NULL(strstr(bebop_diagnostic_message(diagnostic), "outside the range"));
  }
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_enum_expression_precedence);
  RUN_TEST(test_enum_expression_signed);
  RUN_TEST(test_enum_expression_widths);
  RUN_TEST(test_enum_expression_limits);
  RUN_TEST(test_enum_expression_invalid);
  RUN_TEST(test_enum_expression_arithmetic_range);
  return UNITY_END();
}
