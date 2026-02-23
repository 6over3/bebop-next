#include "bebop.c"
#include "test_common.h"
#include "unity.h"

#define FIXTURE(name) BEBOP_TEST_FIXTURES_DIR "/valid/" name
#define FAIL_FIXTURE(name) BEBOP_TEST_FIXTURES_DIR "/should_fail/" name

static bebop_context_t* ctx;

static const char* test_include_paths[] = {BEBOP_TEST_FIXTURES_DIR "/valid", BEBOP_STD_DIR};

void setUp(void)
{
  bebop_host_t host = test_host(test_include_paths, 2);
  ctx = bebop_context_create(&host);
  TEST_ASSERT_NOT_NULL(ctx);
}

void tearDown(void)
{
  if (ctx) {
    bebop_context_destroy(ctx);
    ctx = NULL;
  }
}

static void render_diagnostics(bebop_parse_result_t* result)
{
  uint32_t count = bebop_result_diagnostic_count(result);
  if (count == 0) {
    return;
  }

  fprintf(stderr, "\n=== %u Diagnostic(s) ===\n", count);
  for (uint32_t i = 0; i < count; i++) {
    const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
    bebop_diag_severity_t sev = bebop_diagnostic_severity(diag);
    const char* msg = bebop_diagnostic_message(diag);
    const char* path = bebop_diagnostic_path(diag);
    bebop_span_t span = bebop_diagnostic_span(diag);

    fprintf(
        stderr,
        "[%s] %s:%u:%u: %s\n",
        bebop_diag_severity_name(sev),
        path ? path : "<unknown>",
        span.start_line,
        span.start_col,
        msg ? msg : "<no message>"
    );
  }
  fprintf(stderr, "========================\n");
}

static bebop_parse_result_t* parse_expect_success(const char* source)
{
  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);

  if (status != BEBOP_OK || bebop_result_error_count(result) > 0) {
    fprintf(stderr, "\nUnexpected parse failure (status=%d):\n", status);
    render_diagnostics(result);
    TEST_FAIL_MESSAGE("Parse failed unexpectedly - see diagnostics above");
  }

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(bebop_result_schema_count(result) >= 1);
  return result;
}

static bebop_parse_result_t* parse_fixture(const char* path)
{
  bebop_parse_result_t* result = NULL;
  const char* paths[] = {path};
  bebop_status_t status = bebop_parse(ctx, paths, 1, &result);

  if (status == BEBOP_ERROR || status == BEBOP_FATAL || bebop_result_error_count(result) > 0) {
    fprintf(stderr, "\nFailed to parse fixture '%s' (status=%d):\n", path, status);
    render_diagnostics(result);
    char msg[512];
    snprintf(msg, sizeof(msg), "Fixture '%s' failed to parse - see diagnostics above", path);
    TEST_FAIL_MESSAGE(msg);
  }

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(bebop_result_schema_count(result) >= 1);
  return result;
}

typedef struct {
  uint32_t line;
  uint32_t col;
  const char* message_contains;
} expected_diagnostic_t;

static void parse_should_fail(
    const char* path, const expected_diagnostic_t* expected, uint32_t expected_count
)
{
  bebop_parse_result_t* result = NULL;
  const char* paths[] = {path};
  bebop_status_t status = bebop_parse(ctx, paths, 1, &result);
  (void)status;

  uint32_t error_count = bebop_result_error_count(result);

  if (error_count == 0) {
    fprintf(stderr, "\nExpected errors but got none for '%s'\n", path);
    TEST_FAIL_MESSAGE("Expected parse errors but got none");
  }

  uint32_t diag_count = bebop_result_diagnostic_count(result);

  if (expected_count > 0 && diag_count < expected_count) {
    fprintf(
        stderr,
        "\nExpected at least %u diagnostics but got %u for '%s':\n",
        expected_count,
        diag_count,
        path
    );
    render_diagnostics(result);
    char msg[256];
    snprintf(
        msg, sizeof(msg), "Expected at least %u diagnostics, got %u", expected_count, diag_count
    );
    TEST_FAIL_MESSAGE(msg);
  }

  fprintf(stderr, "\n=== Diagnostics for %s ===\n", path);
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
    bebop_span_t s = bebop_diagnostic_span(d);
    fprintf(
        stderr,
        "  [%u] line %u, col %u: %s\n",
        i,
        s.start_line,
        s.start_col,
        bebop_diagnostic_message(d)
    );
  }
  fprintf(stderr, "========================\n");

  for (uint32_t i = 0; i < expected_count; i++) {
    const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
    TEST_ASSERT_NOT_NULL(diag);

    bebop_span_t span = bebop_diagnostic_span(diag);
    const char* msg = bebop_diagnostic_message(diag);

    char err[512];
    snprintf(
        err,
        sizeof(err),
        "Diagnostic %u: expected line %u, got %u",
        i,
        expected[i].line,
        span.start_line
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i].line, span.start_line, err);

    if (expected[i].col > 0) {
      snprintf(
          err,
          sizeof(err),
          "Diagnostic %u: expected col %u, got %u",
          i,
          expected[i].col,
          span.start_col
      );
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i].col, span.start_col, err);
    }

    if (expected[i].message_contains && msg) {
      if (!strstr(msg, expected[i].message_contains)) {
        snprintf(
            err,
            sizeof(err),
            "Diagnostic %u: expected message containing '%s', got '%s'",
            i,
            expected[i].message_contains,
            msg
        );
        TEST_FAIL_MESSAGE(err);
      }
    }
  }
}

static void assert_field(
    const bebop_field_t* field, const char* name, bebop_type_kind_t type_kind, const char* context
)
{
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: field should not be NULL", context);
  TEST_ASSERT_NOT_NULL_MESSAGE(field, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected field name '%s', got '%s'",
      context,
      name,
      bebop_field_name(field)
  );
  TEST_ASSERT_EQUAL_STRING_MESSAGE(name, bebop_field_name(field), msg);

  const bebop_type_t* type = bebop_field_type(field);
  snprintf(msg, sizeof(msg), "%s: field '%s' type should not be NULL", context, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(type, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: field '%s' expected type %d, got %d",
      context,
      name,
      type_kind,
      bebop_type_kind(type)
  );
  TEST_ASSERT_EQUAL_MESSAGE(type_kind, bebop_type_kind(type), msg);
}

static void assert_array_field(
    const bebop_field_t* field, const char* name, bebop_type_kind_t element_kind, const char* context
)
{
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: array field should not be NULL", context);
  TEST_ASSERT_NOT_NULL_MESSAGE(field, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected field name '%s', got '%s'",
      context,
      name,
      bebop_field_name(field)
  );
  TEST_ASSERT_EQUAL_STRING_MESSAGE(name, bebop_field_name(field), msg);

  const bebop_type_t* type = bebop_field_type(field);
  snprintf(msg, sizeof(msg), "%s: field '%s' should be array type", context, name);
  TEST_ASSERT_EQUAL_MESSAGE(BEBOP_TYPE_ARRAY, bebop_type_kind(type), msg);

  const bebop_type_t* element = bebop_type_element(type);
  snprintf(msg, sizeof(msg), "%s: array '%s' element type should not be NULL", context, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(element, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: array '%s' expected element type %d, got %d",
      context,
      name,
      element_kind,
      bebop_type_kind(element)
  );
  TEST_ASSERT_EQUAL_MESSAGE(element_kind, bebop_type_kind(element), msg);
}

static void assert_map_field(
    const bebop_field_t* field,
    const char* name,
    bebop_type_kind_t key_kind,
    bebop_type_kind_t value_kind,
    const char* context
)
{
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: map field should not be NULL", context);
  TEST_ASSERT_NOT_NULL_MESSAGE(field, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected field name '%s', got '%s'",
      context,
      name,
      bebop_field_name(field)
  );
  TEST_ASSERT_EQUAL_STRING_MESSAGE(name, bebop_field_name(field), msg);

  const bebop_type_t* type = bebop_field_type(field);
  snprintf(msg, sizeof(msg), "%s: field '%s' should be map type", context, name);
  TEST_ASSERT_EQUAL_MESSAGE(BEBOP_TYPE_MAP, bebop_type_kind(type), msg);

  const bebop_type_t* key = bebop_type_key(type);
  snprintf(msg, sizeof(msg), "%s: map '%s' key type should not be NULL", context, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(key, msg);
  snprintf(
      msg,
      sizeof(msg),
      "%s: map '%s' expected key type %d, got %d",
      context,
      name,
      key_kind,
      bebop_type_kind(key)
  );
  TEST_ASSERT_EQUAL_MESSAGE(key_kind, bebop_type_kind(key), msg);

  const bebop_type_t* value = bebop_type_value(type);
  snprintf(msg, sizeof(msg), "%s: map '%s' value type should not be NULL", context, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(value, msg);
  snprintf(
      msg,
      sizeof(msg),
      "%s: map '%s' expected value type %d, got %d",
      context,
      name,
      value_kind,
      bebop_type_kind(value)
  );
  TEST_ASSERT_EQUAL_MESSAGE(value_kind, bebop_type_kind(value), msg);
}

static void assert_enum_member(
    const bebop_enum_member_t* member, const char* name, int64_t value, const char* context
)
{
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: enum member should not be NULL", context);
  TEST_ASSERT_NOT_NULL_MESSAGE(member, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected member name '%s', got '%s'",
      context,
      name,
      bebop_member_name(member)
  );
  TEST_ASSERT_EQUAL_STRING_MESSAGE(name, bebop_member_name(member), msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: member '%s' expected value %lld, got %lld",
      context,
      name,
      (long long)value,
      (long long)bebop_member_value(member)
  );
  TEST_ASSERT_EQUAL_INT64_MESSAGE(value, bebop_member_value(member), msg);
}

static void assert_message_field(
    const bebop_field_t* field,
    uint32_t index,
    const char* name,
    bebop_type_kind_t type_kind,
    const char* context
)
{
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: message field should not be NULL", context);
  TEST_ASSERT_NOT_NULL_MESSAGE(field, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected field index %u, got %u",
      context,
      index,
      bebop_field_index(field)
  );
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(index, bebop_field_index(field), msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected field name '%s', got '%s'",
      context,
      name,
      bebop_field_name(field)
  );
  TEST_ASSERT_EQUAL_STRING_MESSAGE(name, bebop_field_name(field), msg);

  const bebop_type_t* type = bebop_field_type(field);
  snprintf(msg, sizeof(msg), "%s: field '%s' type should not be NULL", context, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(type, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: field '%s' expected type %d, got %d",
      context,
      name,
      type_kind,
      bebop_type_kind(type)
  );
  TEST_ASSERT_EQUAL_MESSAGE(type_kind, bebop_type_kind(type), msg);
}

void test_parse_empty(void);
void test_parse_basic_types(void);
void test_parse_extended_types(void);
void test_parse_extended_types_sizes(void);
void test_parse_fixed_arrays(void);
void test_parse_fixed_arrays_sizes(void);
void test_parse_fixed_arrays_not_fixed(void);
void test_parse_nested_fixed_arrays(void);
void test_parse_array_of_strings(void);
void test_parse_basic_arrays(void);
void test_parse_enum(void);
void test_parse_message(void);
void test_parse_map_types(void);
void test_parse_nested_types(void);
void test_parse_multiple_definitions(void);
void test_parse_documentation(void);
void test_parse_decorators(void);
void test_parse_union(void);
void test_parse_service(void);
void test_parse_const(void);
void test_parse_const_timestamp_duration_bytes(void);
void test_parse_edition(void);
void test_parse_edition_default(void);
void test_parse_toposort(void);
void test_dependency_tracking(void);
void test_context_error_message(void);

void test_parse_empty(void)
{
  bebop_parse_result_t* result = parse_expect_success("/** I am an empty file */");

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_schema_definition_count(schema));

  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));
}

void test_parse_basic_types(void)
{
  const char* source = "mut struct BasicTypes {\n"
                         "    a_bool: bool;\n"
                         "    a_byte: byte;\n"
                         "    a_int16: int16;\n"
                         "    a_uint16: uint16;\n"
                         "    a_int32: int32;\n"
                         "    a_uint32: uint32;\n"
                         "    a_int64: int64;\n"
                         "    a_uint64: uint64;\n"
                         "    a_float32: float32;\n"
                         "    a_float64: float64;\n"
                         "    a_string: string;\n"
                         "    a_guid: guid;\n"
                         "    a_timestamp: timestamp;\n"
                         "    a_duration: duration;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);

  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);
  TEST_ASSERT_EQUAL_UINT32(1, bebop_schema_definition_count(schema));

  const bebop_def_t* def = bebop_schema_definition_at(schema, 0);
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_STRING("BasicTypes", bebop_def_name(def));
  TEST_ASSERT_TRUE(bebop_def_is_mutable(def));
  TEST_ASSERT_EQUAL_UINT32(14, bebop_def_field_count(def));

  const bebop_def_t* found = bebop_result_find(result, "BasicTypes");
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL_PTR(def, found);

  assert_field(bebop_def_field_at(def, 0), "a_bool", BEBOP_TYPE_BOOL, "BasicTypes[0]");
  assert_field(bebop_def_field_at(def, 1), "a_byte", BEBOP_TYPE_BYTE, "BasicTypes[1]");
  assert_field(bebop_def_field_at(def, 2), "a_int16", BEBOP_TYPE_INT16, "BasicTypes[2]");
  assert_field(bebop_def_field_at(def, 3), "a_uint16", BEBOP_TYPE_UINT16, "BasicTypes[3]");
  assert_field(bebop_def_field_at(def, 4), "a_int32", BEBOP_TYPE_INT32, "BasicTypes[4]");
  assert_field(bebop_def_field_at(def, 5), "a_uint32", BEBOP_TYPE_UINT32, "BasicTypes[5]");
  assert_field(bebop_def_field_at(def, 6), "a_int64", BEBOP_TYPE_INT64, "BasicTypes[6]");
  assert_field(bebop_def_field_at(def, 7), "a_uint64", BEBOP_TYPE_UINT64, "BasicTypes[7]");
  assert_field(bebop_def_field_at(def, 8), "a_float32", BEBOP_TYPE_FLOAT32, "BasicTypes[8]");
  assert_field(bebop_def_field_at(def, 9), "a_float64", BEBOP_TYPE_FLOAT64, "BasicTypes[9]");
  assert_field(bebop_def_field_at(def, 10), "a_string", BEBOP_TYPE_STRING, "BasicTypes[10]");
  assert_field(bebop_def_field_at(def, 11), "a_guid", BEBOP_TYPE_UUID, "BasicTypes[11]");
  assert_field(bebop_def_field_at(def, 12), "a_timestamp", BEBOP_TYPE_TIMESTAMP, "BasicTypes[12]");
  assert_field(bebop_def_field_at(def, 13), "a_duration", BEBOP_TYPE_DURATION, "BasicTypes[13]");

  TEST_ASSERT_NULL(bebop_def_field_at(def, 14));
}

void test_parse_extended_types(void)
{
  const char* source = "struct ExtendedTypes {\n"
                         "    a_int8: int8;\n"
                         "    a_sbyte: sbyte;\n"
                         "    a_int128: int128;\n"
                         "    a_uint128: uint128;\n"
                         "    a_float16: float16;\n"
                         "    a_half: half;\n"
                         "    a_uuid: uuid;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "ExtendedTypes");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_UINT32(7, bebop_def_field_count(def));

  assert_field(bebop_def_field_at(def, 0), "a_int8", BEBOP_TYPE_INT8, "ExtendedTypes[0]");
  assert_field(bebop_def_field_at(def, 1), "a_sbyte", BEBOP_TYPE_INT8, "ExtendedTypes[1]");
  assert_field(bebop_def_field_at(def, 2), "a_int128", BEBOP_TYPE_INT128, "ExtendedTypes[2]");
  assert_field(bebop_def_field_at(def, 3), "a_uint128", BEBOP_TYPE_UINT128, "ExtendedTypes[3]");
  assert_field(bebop_def_field_at(def, 4), "a_float16", BEBOP_TYPE_FLOAT16, "ExtendedTypes[4]");
  assert_field(bebop_def_field_at(def, 5), "a_half", BEBOP_TYPE_FLOAT16, "ExtendedTypes[5]");
  assert_field(bebop_def_field_at(def, 6), "a_uuid", BEBOP_TYPE_UUID, "ExtendedTypes[6]");
}

void test_parse_extended_types_sizes(void)
{
  const char* source = "struct TypeSizes {\n"
                         "    a: int8;\n"
                         "    b: int128;\n"
                         "    c: uint128;\n"
                         "    d: float16;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "TypeSizes");
  TEST_ASSERT_NOT_NULL(def);

  const bebop_type_t* int8_type = bebop_field_type(bebop_def_field_at(def, 0));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_INT8, bebop_type_kind(int8_type));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(int8_type));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_type_fixed_size(int8_type));

  const bebop_type_t* int128_type = bebop_field_type(bebop_def_field_at(def, 1));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_INT128, bebop_type_kind(int128_type));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(int128_type));
  TEST_ASSERT_EQUAL_UINT32(16, bebop_type_fixed_size(int128_type));

  const bebop_type_t* uint128_type = bebop_field_type(bebop_def_field_at(def, 2));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_UINT128, bebop_type_kind(uint128_type));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(uint128_type));
  TEST_ASSERT_EQUAL_UINT32(16, bebop_type_fixed_size(uint128_type));

  const bebop_type_t* float16_type = bebop_field_type(bebop_def_field_at(def, 3));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FLOAT16, bebop_type_kind(float16_type));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(float16_type));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_type_fixed_size(float16_type));
}

static void assert_fixed_array_field(
    const bebop_field_t* field,
    const char* name,
    bebop_type_kind_t element_kind,
    uint32_t size,
    const char* context
)
{
  char msg[256];
  snprintf(msg, sizeof(msg), "%s: fixed array field should not be NULL", context);
  TEST_ASSERT_NOT_NULL_MESSAGE(field, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: expected field name '%s', got '%s'",
      context,
      name,
      bebop_field_name(field)
  );
  TEST_ASSERT_EQUAL_STRING_MESSAGE(name, bebop_field_name(field), msg);

  const bebop_type_t* type = bebop_field_type(field);
  snprintf(msg, sizeof(msg), "%s: field '%s' should be fixed_array type", context, name);
  TEST_ASSERT_EQUAL_MESSAGE(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(type), msg);

  snprintf(msg, sizeof(msg), "%s: fixed_array '%s' size should be %u", context, name, size);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(size, bebop_type_fixed_array_size(type), msg);

  const bebop_type_t* element = bebop_type_element(type);
  snprintf(msg, sizeof(msg), "%s: fixed_array '%s' element type should not be NULL", context, name);
  TEST_ASSERT_NOT_NULL_MESSAGE(element, msg);

  snprintf(
      msg,
      sizeof(msg),
      "%s: fixed_array '%s' expected element type %d, got %d",
      context,
      name,
      element_kind,
      bebop_type_kind(element)
  );
  TEST_ASSERT_EQUAL_MESSAGE(element_kind, bebop_type_kind(element), msg);
}

void test_parse_fixed_arrays(void)
{
  const char* source = "struct FixedArrays {\n"
                         "    fixedInts: int32[10];\n"
                         "    fixedStrings: string[5];\n"
                         "    fixedFloats: float64[100];\n"
                         "    buffer: byte[256];\n"
                         "    twoGuids: guid[2];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "FixedArrays");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_UINT32(5, bebop_def_field_count(def));

  assert_fixed_array_field(
      bebop_def_field_at(def, 0), "fixedInts", BEBOP_TYPE_INT32, 10, "FixedArrays[0]"
  );
  assert_fixed_array_field(
      bebop_def_field_at(def, 1), "fixedStrings", BEBOP_TYPE_STRING, 5, "FixedArrays[1]"
  );
  assert_fixed_array_field(
      bebop_def_field_at(def, 2), "fixedFloats", BEBOP_TYPE_FLOAT64, 100, "FixedArrays[2]"
  );
  assert_fixed_array_field(
      bebop_def_field_at(def, 3), "buffer", BEBOP_TYPE_BYTE, 256, "FixedArrays[3]"
  );
  assert_fixed_array_field(
      bebop_def_field_at(def, 4), "twoGuids", BEBOP_TYPE_UUID, 2, "FixedArrays[4]"
  );

  const bebop_type_t* fixed_ints_type = bebop_field_type(bebop_def_field_at(def, 0));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(fixed_ints_type));
  TEST_ASSERT_EQUAL_UINT32(40, bebop_type_fixed_size(fixed_ints_type));

  const bebop_type_t* buffer_type = bebop_field_type(bebop_def_field_at(def, 3));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(buffer_type));
  TEST_ASSERT_EQUAL_UINT32(256, bebop_type_fixed_size(buffer_type));

  const bebop_type_t* fixed_strings_type = bebop_field_type(bebop_def_field_at(def, 1));
  TEST_ASSERT_FALSE(bebop_type_is_fixed(fixed_strings_type));
}

void test_parse_fixed_arrays_sizes(void)
{
  const char* source = "struct FixedArraySizes {\n"
                         "    int8Arr: int8[10];\n"
                         "    int32Arr: int32[5];\n"
                         "    int128Arr: int128[2];\n"
                         "    f16Arr: float16[4];\n"
                         "    guidArr: guid[3];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "FixedArraySizes");
  TEST_ASSERT_NOT_NULL(def);

  const bebop_type_t* int8_arr = bebop_field_type(bebop_def_field_at(def, 0));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(int8_arr));
  TEST_ASSERT_EQUAL_UINT32(10, bebop_type_fixed_size(int8_arr));
  TEST_ASSERT_EQUAL_UINT32(10, bebop_type_fixed_array_size(int8_arr));

  const bebop_type_t* int32_arr = bebop_field_type(bebop_def_field_at(def, 1));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(int32_arr));
  TEST_ASSERT_EQUAL_UINT32(20, bebop_type_fixed_size(int32_arr));
  TEST_ASSERT_EQUAL_UINT32(5, bebop_type_fixed_array_size(int32_arr));

  const bebop_type_t* int128_arr = bebop_field_type(bebop_def_field_at(def, 2));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(int128_arr));
  TEST_ASSERT_EQUAL_UINT32(32, bebop_type_fixed_size(int128_arr));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_type_fixed_array_size(int128_arr));

  const bebop_type_t* f16_arr = bebop_field_type(bebop_def_field_at(def, 3));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(f16_arr));
  TEST_ASSERT_EQUAL_UINT32(8, bebop_type_fixed_size(f16_arr));
  TEST_ASSERT_EQUAL_UINT32(4, bebop_type_fixed_array_size(f16_arr));

  const bebop_type_t* guid_arr = bebop_field_type(bebop_def_field_at(def, 4));
  TEST_ASSERT_TRUE(bebop_type_is_fixed(guid_arr));
  TEST_ASSERT_EQUAL_UINT32(48, bebop_type_fixed_size(guid_arr));
  TEST_ASSERT_EQUAL_UINT32(3, bebop_type_fixed_array_size(guid_arr));
}

void test_parse_fixed_arrays_not_fixed(void)
{
  const char* source = "struct NotFixed {\n"
                         "    strArr: string[5];\n"
                         "    dynInFixed: int32[][3];\n"
                         "    mapArr: map[string, int32][2];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "NotFixed");
  TEST_ASSERT_NOT_NULL(def);

  const bebop_type_t* str_arr = bebop_field_type(bebop_def_field_at(def, 0));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(str_arr));
  TEST_ASSERT_FALSE(bebop_type_is_fixed(str_arr));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_type_fixed_size(str_arr));

  const bebop_type_t* dyn_arr = bebop_field_type(bebop_def_field_at(def, 1));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(dyn_arr));
  TEST_ASSERT_FALSE(bebop_type_is_fixed(dyn_arr));

  const bebop_type_t* map_arr = bebop_field_type(bebop_def_field_at(def, 2));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(map_arr));
  TEST_ASSERT_FALSE(bebop_type_is_fixed(map_arr));
}

void test_parse_nested_fixed_arrays(void)
{
  const char* source = "struct NestedFixedArrays {\n"
                         "    matrix: int32[3][2];\n"
                         "    dynamicOfFixed: byte[10][];\n"
                         "    mapToFixed: map[string, int32[5]];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "NestedFixedArrays");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL_UINT32(3, bebop_def_field_count(def));

  const bebop_field_t* matrix_field = bebop_def_field_at(def, 0);
  TEST_ASSERT_NOT_NULL(matrix_field);
  const bebop_type_t* matrix_type = bebop_field_type(matrix_field);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(matrix_type));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_type_fixed_array_size(matrix_type));

  const bebop_type_t* inner_type = bebop_type_element(matrix_type);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(inner_type));
  TEST_ASSERT_EQUAL_UINT32(3, bebop_type_fixed_array_size(inner_type));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_INT32, bebop_type_kind(bebop_type_element(inner_type)));

  TEST_ASSERT_TRUE(bebop_type_is_fixed(matrix_type));
  TEST_ASSERT_EQUAL_UINT32(24, bebop_type_fixed_size(matrix_type));

  const bebop_field_t* dof_field = bebop_def_field_at(def, 1);
  const bebop_type_t* dof_type = bebop_field_type(dof_field);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_ARRAY, bebop_type_kind(dof_type));
  const bebop_type_t* dof_element = bebop_type_element(dof_type);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(dof_element));
  TEST_ASSERT_EQUAL_UINT32(10, bebop_type_fixed_array_size(dof_element));

  const bebop_field_t* mtf_field = bebop_def_field_at(def, 2);
  const bebop_type_t* mtf_type = bebop_field_type(mtf_field);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_MAP, bebop_type_kind(mtf_type));
  const bebop_type_t* mtf_value = bebop_type_value(mtf_type);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FIXED_ARRAY, bebop_type_kind(mtf_value));
  TEST_ASSERT_EQUAL_UINT32(5, bebop_type_fixed_array_size(mtf_value));
}

void test_parse_array_of_strings(void)
{
  const char* source = "mut struct ArrayOfStrings {\n" "    strings: string[];\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "ArrayOfStrings");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_STRING("ArrayOfStrings", bebop_def_name(def));
  TEST_ASSERT_TRUE(bebop_def_is_mutable(def));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_def_field_count(def));

  assert_array_field(bebop_def_field_at(def, 0), "strings", BEBOP_TYPE_STRING, "ArrayOfStrings[0]");
}

void test_parse_basic_arrays(void)
{
  const char* source = "struct BasicArrays {\n"
                         "    bools: bool[];\n"
                         "    ints: int32[];\n"
                         "    floats: float64[];\n"
                         "    guids: guid[];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "BasicArrays");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(def));
  TEST_ASSERT_FALSE(bebop_def_is_mutable(def));
  TEST_ASSERT_EQUAL_UINT32(4, bebop_def_field_count(def));

  assert_array_field(bebop_def_field_at(def, 0), "bools", BEBOP_TYPE_BOOL, "BasicArrays[0]");
  assert_array_field(bebop_def_field_at(def, 1), "ints", BEBOP_TYPE_INT32, "BasicArrays[1]");
  assert_array_field(bebop_def_field_at(def, 2), "floats", BEBOP_TYPE_FLOAT64, "BasicArrays[2]");
  assert_array_field(bebop_def_field_at(def, 3), "guids", BEBOP_TYPE_UUID, "BasicArrays[3]");

  TEST_ASSERT_NULL(bebop_def_field_at(def, 4));
}

void test_parse_enum(void)
{
  const char* source = "enum Color {\n"
                         "    UNSPECIFIED = 0;\n"
                         "    Red = 1;\n"
                         "    Green = 2;\n"
                         "    Blue = 3;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "Color");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_ENUM, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_STRING("Color", bebop_def_name(def));
  TEST_ASSERT_EQUAL_UINT32(4, bebop_def_member_count(def));

  assert_enum_member(bebop_def_member_at(def, 0), "UNSPECIFIED", 0, "Color[0]");
  assert_enum_member(bebop_def_member_at(def, 1), "Red", 1, "Color[1]");
  assert_enum_member(bebop_def_member_at(def, 2), "Green", 2, "Color[2]");
  assert_enum_member(bebop_def_member_at(def, 3), "Blue", 3, "Color[3]");

  TEST_ASSERT_NULL(bebop_def_member_at(def, 4));
}

void test_parse_message(void)
{
  const char* source = "message Person {\n"
                         "    name(1): string;\n"
                         "    age(2): int32;\n"
                         "    email(3): string;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "Person");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_MESSAGE, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_STRING("Person", bebop_def_name(def));
  TEST_ASSERT_EQUAL_UINT32(3, bebop_def_field_count(def));

  assert_message_field(bebop_def_field_at(def, 0), 1, "name", BEBOP_TYPE_STRING, "Person[0]");
  assert_message_field(bebop_def_field_at(def, 1), 2, "age", BEBOP_TYPE_INT32, "Person[1]");
  assert_message_field(bebop_def_field_at(def, 2), 3, "email", BEBOP_TYPE_STRING, "Person[2]");

  TEST_ASSERT_NULL(bebop_def_field_at(def, 3));
}

void test_parse_map_types(void)
{
  const char* source = "struct MapTypes {\n"
                         "    stringToInt: map[string, int32];\n"
                         "    guidToString: map[guid, string];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "MapTypes");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(def));
  TEST_ASSERT_EQUAL_STRING("MapTypes", bebop_def_name(def));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_def_field_count(def));

  assert_map_field(
      bebop_def_field_at(def, 0), "stringToInt", BEBOP_TYPE_STRING, BEBOP_TYPE_INT32, "MapTypes[0]"
  );
  assert_map_field(
      bebop_def_field_at(def, 1), "guidToString", BEBOP_TYPE_UUID, BEBOP_TYPE_STRING, "MapTypes[1]"
  );

  TEST_ASSERT_NULL(bebop_def_field_at(def, 2));
}

void test_parse_nested_types(void)
{
  const char* source = "struct Nested {\n"
                         "    stringToIntArray: map[string, int32[]];\n"
                         "    nestedMap: map[string, map[int32, string]];\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* def = bebop_result_find(result, "Nested");
  TEST_ASSERT_NOT_NULL(def);
  TEST_ASSERT_EQUAL_UINT32(2, bebop_def_field_count(def));

  const bebop_field_t* f1 = bebop_def_field_at(def, 0);
  TEST_ASSERT_NOT_NULL(f1);
  TEST_ASSERT_EQUAL_STRING("stringToIntArray", bebop_field_name(f1));

  const bebop_type_t* t1 = bebop_field_type(f1);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_MAP, bebop_type_kind(t1));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_STRING, bebop_type_kind(bebop_type_key(t1)));

  const bebop_type_t* t1_value = bebop_type_value(t1);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_ARRAY, bebop_type_kind(t1_value));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_INT32, bebop_type_kind(bebop_type_element(t1_value)));

  const bebop_field_t* f2 = bebop_def_field_at(def, 1);
  TEST_ASSERT_NOT_NULL(f2);
  TEST_ASSERT_EQUAL_STRING("nestedMap", bebop_field_name(f2));

  const bebop_type_t* t2 = bebop_field_type(f2);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_MAP, bebop_type_kind(t2));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_STRING, bebop_type_kind(bebop_type_key(t2)));

  const bebop_type_t* t2_value = bebop_type_value(t2);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_MAP, bebop_type_kind(t2_value));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_INT32, bebop_type_kind(bebop_type_key(t2_value)));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_STRING, bebop_type_kind(bebop_type_value(t2_value)));
}

void test_parse_multiple_definitions(void)
{
  const char* source = "enum Status {\n"
                         "    UNSPECIFIED = 0;\n"
                         "    Active = 1;\n"
                         "    Inactive = 2;\n"
                         "}\n"
                         "\n"
                         "struct User {\n"
                         "    name: string;\n"
                         "    status: Status;\n"
                         "}\n"
                         "\n"
                         "message Request {\n"
                         "    user(1): User;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_EQUAL_UINT32(3, bebop_schema_definition_count(schema));

  const bebop_def_t* status_def = bebop_result_find(result, "Status");
  TEST_ASSERT_NOT_NULL(status_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_ENUM, bebop_def_kind(status_def));
  TEST_ASSERT_EQUAL_UINT32(3, bebop_def_member_count(status_def));
  assert_enum_member(bebop_def_member_at(status_def, 0), "UNSPECIFIED", 0, "Status[0]");
  assert_enum_member(bebop_def_member_at(status_def, 1), "Active", 1, "Status[1]");
  assert_enum_member(bebop_def_member_at(status_def, 2), "Inactive", 2, "Status[2]");

  const bebop_def_t* user_def = bebop_result_find(result, "User");
  TEST_ASSERT_NOT_NULL(user_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(user_def));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_def_field_count(user_def));

  assert_field(bebop_def_field_at(user_def, 0), "name", BEBOP_TYPE_STRING, "User[0]");

  const bebop_field_t* status_field = bebop_def_field_at(user_def, 1);
  TEST_ASSERT_NOT_NULL(status_field);
  TEST_ASSERT_EQUAL_STRING("status", bebop_field_name(status_field));
  const bebop_type_t* status_type = bebop_field_type(status_field);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_DEFINED, bebop_type_kind(status_type));
  TEST_ASSERT_EQUAL_STRING("Status", bebop_type_name(status_type));

  const bebop_def_t* request_def = bebop_result_find(result, "Request");
  TEST_ASSERT_NOT_NULL(request_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_MESSAGE, bebop_def_kind(request_def));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_def_field_count(request_def));

  const bebop_field_t* user_field = bebop_def_field_at(request_def, 0);
  TEST_ASSERT_NOT_NULL(user_field);
  TEST_ASSERT_EQUAL_STRING("user", bebop_field_name(user_field));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_field_index(user_field));
  const bebop_type_t* user_type = bebop_field_type(user_field);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_DEFINED, bebop_type_kind(user_type));
  TEST_ASSERT_EQUAL_STRING("User", bebop_type_name(user_type));
}

void test_parse_documentation(void)
{
  const char* source = "/** This is a documented struct */\n"
                         "struct DocStruct {\n"
                         "    /** Field documentation */\n"
                         "    x: int32;\n"
                         "}\n"
                         "\n"
                         "/** Documented enum */\n"
                         "enum DocEnum {\n"
                         "    /** Zero member */\n"
                         "    UNSPECIFIED = 0;\n"
                         "    /** First member */\n"
                         "    First = 1;\n"
                         "    /** Second member */\n"
                         "    Second = 2;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* struct_def = bebop_result_find(result, "DocStruct");
  TEST_ASSERT_NOT_NULL(struct_def);
  const char* struct_doc = bebop_def_documentation(struct_def);
  TEST_ASSERT_NOT_NULL_MESSAGE(struct_doc, "Struct should have documentation");
  TEST_ASSERT_TRUE_MESSAGE(
      strstr(struct_doc, "documented struct") != NULL,
      "Struct doc should contain 'documented struct'"
  );

  const bebop_def_t* enum_def = bebop_result_find(result, "DocEnum");
  TEST_ASSERT_NOT_NULL(enum_def);
  const char* enum_doc = bebop_def_documentation(enum_def);
  TEST_ASSERT_NOT_NULL_MESSAGE(enum_doc, "Enum should have documentation");
  TEST_ASSERT_TRUE_MESSAGE(
      strstr(enum_doc, "Documented enum") != NULL, "Enum doc should contain 'Documented enum'"
  );
}

void test_parse_decorators(void)
{
  const char* source = "import \"bebop/decorators.bop\"\n"
                         "@deprecated(\"Use NewStruct instead\")\n"
                         "struct OldStruct {\n"
                         "    x: int32;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* old_def = bebop_result_find(result, "OldStruct");
  TEST_ASSERT_NOT_NULL(old_def);
  TEST_ASSERT_EQUAL_UINT32(1, bebop_def_decorator_count(old_def));

  const bebop_decorator_t* deprecated = bebop_def_decorator_find(old_def, "deprecated");
  TEST_ASSERT_NOT_NULL_MESSAGE(deprecated, "Should have @deprecated decorator");
  TEST_ASSERT_EQUAL_STRING("deprecated", bebop_decorator_name(deprecated));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_decorator_arg_count(deprecated));

  const bebop_decorator_arg_t* arg = bebop_decorator_arg_at(deprecated, 0);
  TEST_ASSERT_NOT_NULL(arg);
  const bebop_literal_t* val = bebop_arg_value(arg);
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_STRING, bebop_literal_kind(val));
  TEST_ASSERT_EQUAL_STRING("Use NewStruct instead", bebop_literal_as_string(val, NULL));
}

void test_parse_union(void)
{
  const char* source = "/** A test union */\n"
                         "union TestUnion {\n"
                         "    A(1): { x: int32; };\n"
                         "    B(2): message { name(1): string; };\n"
                         "    C(3): mut { flag: bool; };\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* union_def = bebop_result_find(result, "TestUnion");
  TEST_ASSERT_NOT_NULL(union_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_UNION, bebop_def_kind(union_def));
  TEST_ASSERT_EQUAL_STRING("TestUnion", bebop_def_name(union_def));
  TEST_ASSERT_EQUAL_UINT32(3, bebop_def_branch_count(union_def));

  const char* doc = bebop_def_documentation(union_def);
  TEST_ASSERT_NOT_NULL_MESSAGE(doc, "Union should have documentation");
  TEST_ASSERT_TRUE(strstr(doc, "test union") != NULL);

  const bebop_union_branch_t* branch1 = bebop_def_branch_at(union_def, 0);
  TEST_ASSERT_NOT_NULL(branch1);
  TEST_ASSERT_EQUAL_UINT8(1, bebop_branch_discriminator(branch1));
  const bebop_def_t* branch1_def = bebop_branch_def(branch1);
  TEST_ASSERT_NOT_NULL(branch1_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(branch1_def));
  TEST_ASSERT_EQUAL_STRING("A", bebop_def_name(branch1_def));
  TEST_ASSERT_FALSE(bebop_def_is_mutable(branch1_def));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_def_field_count(branch1_def));
  assert_field(bebop_def_field_at(branch1_def, 0), "x", BEBOP_TYPE_INT32, "Union branch A");

  const bebop_union_branch_t* branch2 = bebop_def_branch_at(union_def, 1);
  TEST_ASSERT_NOT_NULL(branch2);
  TEST_ASSERT_EQUAL_UINT8(2, bebop_branch_discriminator(branch2));
  const bebop_def_t* branch2_def = bebop_branch_def(branch2);
  TEST_ASSERT_NOT_NULL(branch2_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_MESSAGE, bebop_def_kind(branch2_def));
  TEST_ASSERT_EQUAL_STRING("B", bebop_def_name(branch2_def));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_def_field_count(branch2_def));
  assert_message_field(
      bebop_def_field_at(branch2_def, 0), 1, "name", BEBOP_TYPE_STRING, "Union branch B"
  );

  const bebop_union_branch_t* branch3 = bebop_def_branch_at(union_def, 2);
  TEST_ASSERT_NOT_NULL(branch3);
  TEST_ASSERT_EQUAL_UINT8(3, bebop_branch_discriminator(branch3));
  const bebop_def_t* branch3_def = bebop_branch_def(branch3);
  TEST_ASSERT_NOT_NULL(branch3_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_STRUCT, bebop_def_kind(branch3_def));
  TEST_ASSERT_EQUAL_STRING("C", bebop_def_name(branch3_def));
  TEST_ASSERT_TRUE(bebop_def_is_mutable(branch3_def));

  TEST_ASSERT_NULL(bebop_def_branch_at(union_def, 3));
}

void test_parse_service(void)
{
  const char* source = "struct Request { query: string; }\n"
                         "struct Response { count: int32; }\n"
                         "\n"
                         "/** A test service */\n"
                         "service TestService {\n"
                         "    /** Simple unary call */\n"
                         "    GetData(Request): Response;\n"
                         "    /** Server streaming */\n"
                         "    StreamData(Request): stream Response;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* service_def = bebop_result_find(result, "TestService");
  TEST_ASSERT_NOT_NULL(service_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_SERVICE, bebop_def_kind(service_def));
  TEST_ASSERT_EQUAL_STRING("TestService", bebop_def_name(service_def));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_def_method_count(service_def));

  const char* doc = bebop_def_documentation(service_def);
  TEST_ASSERT_NOT_NULL_MESSAGE(doc, "Service should have documentation");
  TEST_ASSERT_TRUE(strstr(doc, "test service") != NULL);

  const bebop_method_t* method1 = bebop_def_method_at(service_def, 0);
  TEST_ASSERT_NOT_NULL(method1);
  const bebop_type_t* req1 = bebop_method_request_type(method1);
  const bebop_type_t* resp1 = bebop_method_response_type(method1);
  TEST_ASSERT_NOT_NULL(req1);
  TEST_ASSERT_NOT_NULL(resp1);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_DEFINED, bebop_type_kind(req1));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_DEFINED, bebop_type_kind(resp1));
  TEST_ASSERT_EQUAL_STRING("Request", bebop_type_name(req1));
  TEST_ASSERT_EQUAL_STRING("Response", bebop_type_name(resp1));
  TEST_ASSERT_EQUAL(BEBOP_METHOD_UNARY, bebop_method_type(method1));

  const bebop_method_t* method2 = bebop_def_method_at(service_def, 1);
  TEST_ASSERT_NOT_NULL(method2);
  TEST_ASSERT_EQUAL(BEBOP_METHOD_SERVER_STREAM, bebop_method_type(method2));

  TEST_ASSERT_NULL(bebop_def_method_at(service_def, 2));
}

void test_parse_const(void)
{
  const char* source = "const int32 MY_INT = -42;\n"
                         "const uint64 MY_UINT = 0xDEADBEEF;\n"
                         "const float64 MY_FLOAT = 3.14159;\n"
                         "const bool MY_BOOL = true;\n"
                         "const string MY_STRING = \"hello world\";\n"
                         "const float64 MY_INF = inf;\n"
                         "const float64 MY_NEG_INF = -inf;\n"
                         "const guid MY_UUID = \"550e8400-e29b-41d4-a716-446655440000\";\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_EQUAL_UINT32(8, bebop_schema_definition_count(schema));

  const bebop_def_t* int_def = bebop_result_find(result, "MY_INT");
  TEST_ASSERT_NOT_NULL(int_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_CONST, bebop_def_kind(int_def));
  const bebop_type_t* int_type = bebop_def_const_type(int_def);
  TEST_ASSERT_NOT_NULL(int_type);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_INT32, bebop_type_kind(int_type));
  const bebop_literal_t* int_val = bebop_def_const_value(int_def);
  TEST_ASSERT_NOT_NULL(int_val);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_INT, bebop_literal_kind(int_val));
  TEST_ASSERT_EQUAL_INT64(-42, bebop_literal_as_int(int_val));

  const bebop_def_t* uint_def = bebop_result_find(result, "MY_UINT");
  TEST_ASSERT_NOT_NULL(uint_def);
  const bebop_literal_t* uint_val = bebop_def_const_value(uint_def);
  TEST_ASSERT_NOT_NULL(uint_val);
  TEST_ASSERT_EQUAL_INT64(0xDEADBEEF, bebop_literal_as_int(uint_val));

  const bebop_def_t* float_def = bebop_result_find(result, "MY_FLOAT");
  TEST_ASSERT_NOT_NULL(float_def);
  const bebop_literal_t* float_val = bebop_def_const_value(float_def);
  TEST_ASSERT_NOT_NULL(float_val);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_FLOAT, bebop_literal_kind(float_val));
  TEST_ASSERT_DOUBLE_WITHIN(0.00001, 3.14159, bebop_literal_as_float(float_val));

  const bebop_def_t* bool_def = bebop_result_find(result, "MY_BOOL");
  TEST_ASSERT_NOT_NULL(bool_def);
  const bebop_literal_t* bool_val = bebop_def_const_value(bool_def);
  TEST_ASSERT_NOT_NULL(bool_val);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_BOOL, bebop_literal_kind(bool_val));
  TEST_ASSERT_TRUE(bebop_literal_as_bool(bool_val));

  const bebop_def_t* str_def = bebop_result_find(result, "MY_STRING");
  TEST_ASSERT_NOT_NULL(str_def);
  const bebop_literal_t* str_val = bebop_def_const_value(str_def);
  TEST_ASSERT_NOT_NULL(str_val);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_STRING, bebop_literal_kind(str_val));
  TEST_ASSERT_EQUAL_STRING("hello world", bebop_literal_as_string(str_val, NULL));

  const bebop_def_t* inf_def = bebop_result_find(result, "MY_INF");
  TEST_ASSERT_NOT_NULL(inf_def);
  const bebop_literal_t* inf_val = bebop_def_const_value(inf_def);
  TEST_ASSERT_NOT_NULL(inf_val);
  TEST_ASSERT_TRUE(isinf(bebop_literal_as_float(inf_val)));
  TEST_ASSERT_TRUE(bebop_literal_as_float(inf_val) > 0);

  const bebop_def_t* neg_inf_def = bebop_result_find(result, "MY_NEG_INF");
  TEST_ASSERT_NOT_NULL(neg_inf_def);
  const bebop_literal_t* neg_inf_val = bebop_def_const_value(neg_inf_def);
  TEST_ASSERT_NOT_NULL(neg_inf_val);
  TEST_ASSERT_TRUE(isinf(bebop_literal_as_float(neg_inf_val)));
  TEST_ASSERT_TRUE(bebop_literal_as_float(neg_inf_val) < 0);

  const bebop_def_t* guid_def = bebop_result_find(result, "MY_UUID");
  TEST_ASSERT_NOT_NULL(guid_def);
  const bebop_literal_t* guid_val = bebop_def_const_value(guid_def);
  TEST_ASSERT_NOT_NULL(guid_val);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_UUID, bebop_literal_kind(guid_val));
}

void test_parse_const_timestamp_duration_bytes(void)
{
  const char* source =
      "const timestamp EPOCH = \"1970-01-01T00:00:00Z\";\n"
      "const timestamp Y2K = \"2000-01-01T00:00:00Z\";\n"
      "const timestamp WITH_NANOS = \"2024-01-15T10:30:00.123456789Z\";\n"
      "const timestamp WITH_OFFSET = \"2024-01-15T10:30:00+05:30\";\n"
      "const timestamp WITH_SEC = \"2024-01-15T10:30:00+05:30:45\";\n"
      "const timestamp WITH_MS = \"2024-01-15T10:30:00+05:30:45.500\";\n"
      "const timestamp NEG_OFFSET = \"2024-01-15T10:30:00-08:00\";\n"
      "const duration ONE_HOUR = \"1h\";\n"
      "const duration NINETY_MIN = \"1h30m\";\n"
      "const duration COMPLEX = \"1h30m45s500ms\";\n"
      "const duration HALF_SEC = \"500ms\";\n"
      "const byte[] MAGIC = b\"\\x89PNG\\r\\n\";\n"
      "const byte[] HELLO = b\"hello\";\n"
      "const byte[] EMPTY = b\"\";\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_EQUAL_UINT32(14, bebop_schema_definition_count(schema));

  // Test EPOCH timestamp (Unix epoch)
  const bebop_def_t* epoch_def = bebop_result_find(result, "EPOCH");
  TEST_ASSERT_NOT_NULL(epoch_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_CONST, bebop_def_kind(epoch_def));
  const bebop_type_t* epoch_type = bebop_def_const_type(epoch_def);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_TIMESTAMP, bebop_type_kind(epoch_type));
  const bebop_literal_t* epoch_val = bebop_def_const_value(epoch_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_TIMESTAMP, bebop_literal_kind(epoch_val));
  int64_t epoch_sec;
  int32_t epoch_nano, epoch_offset_ms;
  bebop_literal_as_timestamp(epoch_val, &epoch_sec, &epoch_nano, &epoch_offset_ms);
  TEST_ASSERT_EQUAL_INT64(0, epoch_sec);
  TEST_ASSERT_EQUAL_INT32(0, epoch_nano);
  TEST_ASSERT_EQUAL_INT32(0, epoch_offset_ms);

  // Test Y2K timestamp
  const bebop_def_t* y2k_def = bebop_result_find(result, "Y2K");
  TEST_ASSERT_NOT_NULL(y2k_def);
  const bebop_literal_t* y2k_val = bebop_def_const_value(y2k_def);
  int64_t y2k_sec;
  bebop_literal_as_timestamp(y2k_val, &y2k_sec, NULL, NULL);
  TEST_ASSERT_EQUAL_INT64(946684800, y2k_sec);

  // Test timestamp with nanoseconds
  const bebop_def_t* nanos_def = bebop_result_find(result, "WITH_NANOS");
  TEST_ASSERT_NOT_NULL(nanos_def);
  const bebop_literal_t* nanos_val = bebop_def_const_value(nanos_def);
  int32_t nanos;
  bebop_literal_as_timestamp(nanos_val, NULL, &nanos, NULL);
  TEST_ASSERT_EQUAL_INT32(123456789, nanos);

  // Test timestamp with offset +05:30
  const bebop_def_t* offset_def = bebop_result_find(result, "WITH_OFFSET");
  TEST_ASSERT_NOT_NULL(offset_def);
  const bebop_literal_t* offset_val = bebop_def_const_value(offset_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_TIMESTAMP, bebop_literal_kind(offset_val));
  int32_t offset_ms;
  bebop_literal_as_timestamp(offset_val, NULL, NULL, &offset_ms);
  TEST_ASSERT_EQUAL_INT32(19800000, offset_ms);  // +05:30 = 5*3600000 + 30*60000

  // Test timestamp with seconds offset +05:30:45
  const bebop_def_t* sec_def = bebop_result_find(result, "WITH_SEC");
  TEST_ASSERT_NOT_NULL(sec_def);
  const bebop_literal_t* sec_val = bebop_def_const_value(sec_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_TIMESTAMP, bebop_literal_kind(sec_val));
  int32_t sec_offset_ms;
  bebop_literal_as_timestamp(sec_val, NULL, NULL, &sec_offset_ms);
  TEST_ASSERT_EQUAL_INT32(19845000, sec_offset_ms);  // +05:30:45

  // Test timestamp with milliseconds offset +05:30:45.500
  const bebop_def_t* ms_def = bebop_result_find(result, "WITH_MS");
  TEST_ASSERT_NOT_NULL(ms_def);
  const bebop_literal_t* ms_val = bebop_def_const_value(ms_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_TIMESTAMP, bebop_literal_kind(ms_val));
  int32_t ms_offset_ms;
  bebop_literal_as_timestamp(ms_val, NULL, NULL, &ms_offset_ms);
  TEST_ASSERT_EQUAL_INT32(19845500, ms_offset_ms);  // +05:30:45.500

  // Test timestamp with negative offset -08:00
  const bebop_def_t* neg_def = bebop_result_find(result, "NEG_OFFSET");
  TEST_ASSERT_NOT_NULL(neg_def);
  const bebop_literal_t* neg_val = bebop_def_const_value(neg_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_TIMESTAMP, bebop_literal_kind(neg_val));
  int32_t neg_offset_ms;
  bebop_literal_as_timestamp(neg_val, NULL, NULL, &neg_offset_ms);
  TEST_ASSERT_EQUAL_INT32(-28800000, neg_offset_ms);  // -08:00 = -8*3600000

  // Test ONE_HOUR duration
  const bebop_def_t* hour_def = bebop_result_find(result, "ONE_HOUR");
  TEST_ASSERT_NOT_NULL(hour_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_CONST, bebop_def_kind(hour_def));
  const bebop_type_t* hour_type = bebop_def_const_type(hour_def);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_DURATION, bebop_type_kind(hour_type));
  const bebop_literal_t* hour_val = bebop_def_const_value(hour_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_DURATION, bebop_literal_kind(hour_val));
  int64_t hour_sec;
  int32_t hour_nano;
  bebop_literal_as_duration(hour_val, &hour_sec, &hour_nano);
  TEST_ASSERT_EQUAL_INT64(3600, hour_sec);
  TEST_ASSERT_EQUAL_INT32(0, hour_nano);

  // Test NINETY_MIN duration (1h30m = 5400s)
  const bebop_def_t* ninety_def = bebop_result_find(result, "NINETY_MIN");
  TEST_ASSERT_NOT_NULL(ninety_def);
  const bebop_literal_t* ninety_val = bebop_def_const_value(ninety_def);
  int64_t ninety_sec;
  bebop_literal_as_duration(ninety_val, &ninety_sec, NULL);
  TEST_ASSERT_EQUAL_INT64(5400, ninety_sec);

  // Test COMPLEX duration (1h30m45s500ms)
  const bebop_def_t* complex_def = bebop_result_find(result, "COMPLEX");
  TEST_ASSERT_NOT_NULL(complex_def);
  const bebop_literal_t* complex_val = bebop_def_const_value(complex_def);
  int64_t complex_sec;
  int32_t complex_nano;
  bebop_literal_as_duration(complex_val, &complex_sec, &complex_nano);
  TEST_ASSERT_EQUAL_INT64(5445, complex_sec);
  TEST_ASSERT_EQUAL_INT32(500000000, complex_nano);

  // Test HALF_SEC duration (500ms)
  const bebop_def_t* half_def = bebop_result_find(result, "HALF_SEC");
  TEST_ASSERT_NOT_NULL(half_def);
  const bebop_literal_t* half_val = bebop_def_const_value(half_def);
  int64_t half_sec;
  int32_t half_nano;
  bebop_literal_as_duration(half_val, &half_sec, &half_nano);
  TEST_ASSERT_EQUAL_INT64(0, half_sec);
  TEST_ASSERT_EQUAL_INT32(500000000, half_nano);

  // Test MAGIC byte array
  const bebop_def_t* magic_def = bebop_result_find(result, "MAGIC");
  TEST_ASSERT_NOT_NULL(magic_def);
  TEST_ASSERT_EQUAL(BEBOP_DEF_CONST, bebop_def_kind(magic_def));
  const bebop_type_t* magic_type = bebop_def_const_type(magic_def);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_ARRAY, bebop_type_kind(magic_type));
  const bebop_type_t* magic_elem = bebop_type_element(magic_type);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_BYTE, bebop_type_kind(magic_elem));
  const bebop_literal_t* magic_val = bebop_def_const_value(magic_def);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_BYTES, bebop_literal_kind(magic_val));
  size_t magic_len;
  const uint8_t* magic_data = bebop_literal_as_bytes(magic_val, &magic_len);
  TEST_ASSERT_NOT_NULL(magic_data);
  TEST_ASSERT_EQUAL_UINT(6, magic_len);
  TEST_ASSERT_EQUAL_UINT8(0x89, magic_data[0]);
  TEST_ASSERT_EQUAL_UINT8('P', magic_data[1]);
  TEST_ASSERT_EQUAL_UINT8('N', magic_data[2]);
  TEST_ASSERT_EQUAL_UINT8('G', magic_data[3]);
  TEST_ASSERT_EQUAL_UINT8('\r', magic_data[4]);
  TEST_ASSERT_EQUAL_UINT8('\n', magic_data[5]);

  // Test HELLO byte array
  const bebop_def_t* hello_def = bebop_result_find(result, "HELLO");
  TEST_ASSERT_NOT_NULL(hello_def);
  const bebop_literal_t* hello_val = bebop_def_const_value(hello_def);
  size_t hello_len;
  const uint8_t* hello_data = bebop_literal_as_bytes(hello_val, &hello_len);
  TEST_ASSERT_NOT_NULL(hello_data);
  TEST_ASSERT_EQUAL_UINT(5, hello_len);
  TEST_ASSERT_EQUAL_MEMORY("hello", hello_data, 5);

  // Test EMPTY byte array
  const bebop_def_t* empty_def = bebop_result_find(result, "EMPTY");
  TEST_ASSERT_NOT_NULL(empty_def);
  const bebop_literal_t* empty_val = bebop_def_const_value(empty_def);
  size_t empty_len;
  const uint8_t* empty_data = bebop_literal_as_bytes(empty_val, &empty_len);
  TEST_ASSERT_EQUAL_UINT(0, empty_len);
  (void)empty_data;
}

void test_parse_edition(void)
{
  const char* source = "edition = \"2026\"\n" "\n" "struct Foo { x: int32; }\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);
  TEST_ASSERT_EQUAL_INT(BEBOP_ED_2026, bebop_schema_edition(schema));
}

void test_parse_edition_default(void)
{
  const char* source = "struct Foo { x: int32; }\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);
  TEST_ASSERT_EQUAL_INT(BEBOP_ED_2026, bebop_schema_edition(schema));
}

void test_parse_toposort(void)
{
  const char* source = "struct D { c: C; }\n"
                         "struct B { a: A; }\n"
                         "struct A { x: int32; }\n"
                         "struct C { b: B; }\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_EQUAL_UINT32(4, bebop_schema_definition_count(schema));

  const bebop_def_t* def0 = bebop_schema_definition_at(schema, 0);
  const bebop_def_t* def1 = bebop_schema_definition_at(schema, 1);
  const bebop_def_t* def2 = bebop_schema_definition_at(schema, 2);
  const bebop_def_t* def3 = bebop_schema_definition_at(schema, 3);

  TEST_ASSERT_NOT_NULL(def0);
  TEST_ASSERT_NOT_NULL(def1);
  TEST_ASSERT_NOT_NULL(def2);
  TEST_ASSERT_NOT_NULL(def3);

  TEST_ASSERT_EQUAL_STRING_MESSAGE("A", bebop_def_name(def0), "First should be A (no deps)");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("B", bebop_def_name(def1), "Second should be B (deps on A)");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("C", bebop_def_name(def2), "Third should be C (deps on B)");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("D", bebop_def_name(def3), "Fourth should be D (deps on C)");

  const bebop_def_t* d_def = bebop_result_find(result, "D");
  TEST_ASSERT_NOT_NULL(d_def);
  const bebop_field_t* c_field = bebop_def_field_at(d_def, 0);
  TEST_ASSERT_NOT_NULL(c_field);
  const bebop_type_t* c_type = bebop_field_type(c_field);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_DEFINED, bebop_type_kind(c_type));
  TEST_ASSERT_EQUAL_STRING("C", bebop_type_name(c_type));

  const bebop_def_t* resolved = bebop_type_resolved(c_type);
  TEST_ASSERT_NOT_NULL_MESSAGE(resolved, "Type reference should be resolved");
  TEST_ASSERT_EQUAL_STRING("C", bebop_def_name(resolved));
}

void test_dependency_tracking(void)
{
  const char* source = "struct A { x: int32; }\n"
                         "struct B { a: A; }\n"
                         "struct C { b: B; also_a: A; }\n"
                         "struct D { c: C; }\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_diagnostic_count(result));

  const bebop_def_t* a_def = bebop_result_find(result, "A");
  const bebop_def_t* b_def = bebop_result_find(result, "B");
  const bebop_def_t* c_def = bebop_result_find(result, "C");
  const bebop_def_t* d_def = bebop_result_find(result, "D");

  TEST_ASSERT_NOT_NULL(a_def);
  TEST_ASSERT_NOT_NULL(b_def);
  TEST_ASSERT_NOT_NULL(c_def);
  TEST_ASSERT_NOT_NULL(d_def);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      2, bebop_def_dependents_count(a_def), "A should have 2 dependents (B and C)"
  );

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1, bebop_def_dependents_count(b_def), "B should have 1 dependent (C)"
  );

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1, bebop_def_dependents_count(c_def), "C should have 1 dependent (D)"
  );

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, bebop_def_dependents_count(d_def), "D should have 0 dependents"
  );

  const bebop_def_t* a_dep0 = bebop_def_dependent_at(a_def, 0);
  const bebop_def_t* a_dep1 = bebop_def_dependent_at(a_def, 1);
  TEST_ASSERT_NOT_NULL(a_dep0);
  TEST_ASSERT_NOT_NULL(a_dep1);

  bool has_b = (a_dep0 == b_def || a_dep1 == b_def);
  bool has_c = (a_dep0 == c_def || a_dep1 == c_def);
  TEST_ASSERT_TRUE_MESSAGE(has_b, "A's dependents should include B");
  TEST_ASSERT_TRUE_MESSAGE(has_c, "A's dependents should include C");

  TEST_ASSERT_NULL(bebop_def_dependent_at(a_def, 2));
  TEST_ASSERT_NULL(bebop_def_dependent_at(d_def, 0));

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      2, bebop_def_references_count(a_def), "A should have 2 references"
  );

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1, bebop_def_references_count(b_def), "B should have 1 reference"
  );

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1, bebop_def_references_count(c_def), "C should have 1 reference"
  );

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, bebop_def_references_count(d_def), "D should have 0 references"
  );

  bebop_span_t a_ref0 = bebop_def_reference_at(a_def, 0);
  bebop_span_t a_ref1 = bebop_def_reference_at(a_def, 1);

  TEST_ASSERT_TRUE_MESSAGE(a_ref0.start_line > 0, "A's first reference span should be valid");
  TEST_ASSERT_TRUE_MESSAGE(a_ref1.start_line > 0, "A's second reference span should be valid");

  bebop_span_t invalid = bebop_def_reference_at(a_def, 2);
  TEST_ASSERT_EQUAL_UINT32(0, invalid.start_line);

  invalid = bebop_def_reference_at(d_def, 0);
  TEST_ASSERT_EQUAL_UINT32(0, invalid.start_line);

  TEST_ASSERT_EQUAL_UINT32(0, bebop_def_dependents_count(NULL));
  TEST_ASSERT_NULL(bebop_def_dependent_at(NULL, 0));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_def_references_count(NULL));
  bebop_span_t null_span = bebop_def_reference_at(NULL, 0);
  TEST_ASSERT_EQUAL_UINT32(0, null_span.start_line);
}

void test_context_error_message(void)
{
  TEST_ASSERT_NULL(bebop_context_error_message(ctx));

  bebop_parse_result_t* result = NULL;
  const char* paths[] = {"/nonexistent/path/to/file.bop"};
  bebop_status_t status = bebop_parse(ctx, paths, 1, &result);

  TEST_ASSERT_EQUAL(BEBOP_FATAL, status);

  const char* err = bebop_context_error_message(ctx);
  TEST_ASSERT_NOT_NULL_MESSAGE(err, "Context should have error message for file not found");
}

void test_fixture_album(void);
void test_fixture_array_of_strings(void);
void test_fixture_basic_arrays(void);
void test_fixture_basic_types(void);
void test_fixture_bitflags(void);
void test_fixture_const(void);
void test_fixture_documentation(void);
void test_fixture_empty(void);
void test_fixture_enum_size(void);
void test_fixture_enum(void);
void test_fixture_fixed_arrays(void);
void test_fixture_imports(void);
void test_fixture_jazz(void);
void test_fixture_lab(void);
void test_fixture_map_types(void);
void test_fixture_message(void);
void test_fixture_msgpack_comparison(void);
void test_fixture_nested_message(void);
void test_fixture_request(void);
void test_fixture_struct(void);
void test_fixture_toposort(void);
void test_fixture_union_perf_a(void);
void test_fixture_union_perf_b(void);
void test_fixture_union_perf_c(void);
void test_fixture_union(void);

void test_fail_bad_comment(void);
void test_fail_invalid_syntax(void);
void test_fail_nested_union(void);
void test_fail_enum_zero(void);
void test_fail_invalid_map_keys(void);
void test_fail_invalid_union_reference(void);
void test_fail_rpc(void);
void test_fail_multiple_errors(void);
void test_fail_unknown_decorator(void);
void test_int128_map_key(void);
void test_fail_int128_enum_base(void);
void test_fail_fixed_array_size_zero(void);
void test_fail_fixed_array_size_too_large(void);
void test_parse_package(void);
void test_fail_package_after_import(void);
void test_fail_package_after_definition(void);
void test_parse_visibility_local(void);
void test_parse_visibility_export(void);
void test_parse_union_type_ref(void);
void test_fail_union_ref_union(void);

void test_fixture_album(void)
{
  parse_fixture(FIXTURE("album.bop"));
}

void test_fixture_array_of_strings(void)
{
  parse_fixture(FIXTURE("array_of_strings.bop"));
}

void test_fixture_basic_arrays(void)
{
  parse_fixture(FIXTURE("basic_arrays.bop"));
}

void test_fixture_basic_types(void)
{
  parse_fixture(FIXTURE("basic_types.bop"));
}

void test_fixture_bitflags(void)
{
  parse_fixture(FIXTURE("bitflags.bop"));
}

void test_fixture_const(void)
{
  parse_fixture(FIXTURE("const.bop"));
}

void test_fixture_documentation(void)
{
  parse_fixture(FIXTURE("documentation.bop"));
}

void test_fixture_empty(void)
{
  parse_fixture(FIXTURE("empty.bop"));
}

void test_fixture_enum_size(void)
{
  parse_fixture(FIXTURE("enum_size.bop"));
}

void test_fixture_enum(void)
{
  parse_fixture(FIXTURE("enum.bop"));
}

void test_fixture_fixed_arrays(void)
{
  parse_fixture(FIXTURE("fixed_arrays.bop"));
}

void test_fixture_imports(void)
{
  parse_fixture(FIXTURE("imports.bop"));
}

void test_fixture_jazz(void)
{
  parse_fixture(FIXTURE("jazz.bop"));
}

void test_fixture_lab(void)
{
  parse_fixture(FIXTURE("lab.bop"));
}

void test_fixture_map_types(void)
{
  parse_fixture(FIXTURE("map_types.bop"));
}

void test_fixture_message(void)
{
  parse_fixture(FIXTURE("message.bop"));
}

void test_fixture_msgpack_comparison(void)
{
  parse_fixture(FIXTURE("msgpack_comparison.bop"));
}

void test_fixture_nested_message(void)
{
  parse_fixture(FIXTURE("nested_message.bop"));
}

void test_fixture_request(void)
{
  parse_fixture(FIXTURE("request.bop"));
}

void test_fixture_struct(void)
{
  parse_fixture(FIXTURE("struct.bop"));
}

void test_fixture_toposort(void)
{
  parse_fixture(FIXTURE("toposort.bop"));
}

void test_fixture_union_perf_a(void)
{
  parse_fixture(FIXTURE("union_perf_a.bop"));
}

void test_fixture_union_perf_b(void)
{
  parse_fixture(FIXTURE("union_perf_b.bop"));
}

void test_fixture_union_perf_c(void)
{
  parse_fixture(FIXTURE("union_perf_c.bop"));
}

void test_fixture_union(void)
{
  parse_fixture(FIXTURE("union.bop"));
}

void test_fail_bad_comment(void)
{
  expected_diagnostic_t expected[] = {{1, 1, "Unterminated"}};
  parse_should_fail(FAIL_FIXTURE("bad_comment.bop"), expected, 1);
}

void test_fail_invalid_syntax(void)
{
  expected_diagnostic_t expected[] = {
      {1, 12, "'{'"}  //!< Expected '{' at line 1, col 12 (after "Nope")
  };
  parse_should_fail(FAIL_FIXTURE("invalid_syntax.bop"), expected, 1);
}

void test_fail_nested_union(void)
{
  expected_diagnostic_t expected[] = {{2, 9, "Expected"}};
  parse_should_fail(FAIL_FIXTURE("nested_union.bop"), expected, 1);
}

void test_fail_enum_zero(void)
{
  bebop_parse_result_t* result = NULL;
  const char* path = FAIL_FIXTURE("enum_zero.bop");
  const char* paths[] = {path};
  bebop_status_t status = bebop_parse(ctx, paths, 1, &result);
  (void)status;

  uint32_t error_count = bebop_result_error_count(result);

  if (error_count == 0) {
    fprintf(stderr, "\nExpected error for missing enum zero value but got none for '%s'\n", path);
    render_diagnostics(result);
    TEST_FAIL_MESSAGE("Expected enum missing zero error but got none");
  }

  const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, 0);
  TEST_ASSERT_NOT_NULL(d);
  const char* msg = bebop_diagnostic_message(d);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_TRUE_MESSAGE(
      strstr(msg, "0") != NULL || strstr(msg, "zero") != NULL,
      "Error message should mention zero value"
  );
}

void test_fail_invalid_map_keys(void)
{
  expected_diagnostic_t expected[] = {{2, 13, "map key"}};
  parse_should_fail(FAIL_FIXTURE("invalid_map_keys.bop"), expected, 1);
}

void test_fail_invalid_union_reference(void)
{
  expected_diagnostic_t expected[] = {{8, 6, NULL}};
  parse_should_fail(FAIL_FIXTURE("invalid_union_reference.bop"), expected, 1);
}

void test_fail_rpc(void)
{
  expected_diagnostic_t expected[] = {{8, 0, NULL}};
  parse_should_fail(FAIL_FIXTURE("rpc.bop"), expected, 1);
}

void test_fail_multiple_errors(void)
{
  expected_diagnostic_t expected[] = {{2, 0, NULL}};
  parse_should_fail(FAIL_FIXTURE("multiple_errors.bop"), expected, 1);
}

void test_fail_unknown_decorator(void)
{
  const char* source = "@opcode(123)\n" "struct TestStruct {\n" "    x: int32;\n" "}\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "Unknown decorator should produce an error");

  bool found_unknown_decorator_error = false;
  for (uint32_t i = 0; i < bebop_result_diagnostic_count(result); i++) {
    const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
    if (bebop_diagnostic_severity(diag) == BEBOP_DIAG_ERROR) {
      const char* msg = bebop_diagnostic_message(diag);
      if (msg && strstr(msg, "Unknown") && strstr(msg, "opcode")) {
        found_unknown_decorator_error = true;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(
      found_unknown_decorator_error, "Should have 'Unknown decorator' error for @opcode"
  );
}

void test_int128_map_key(void)
{
  const char* source = "struct Int128MapKey {\n"
                         "    signedMap: map[int128, string];\n"
                         "    unsignedMap: map[uint128, string];\n"
                         "}\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, error_count, "int128/uint128 should be valid map keys");
}

void test_fail_int128_enum_base(void)
{
  const char* source = "enum BadEnum : int128 {\n" "    A = 1;\n" "}\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "int128 as enum base should produce an error");

  bool found_enum_base_error = false;
  for (uint32_t i = 0; i < bebop_result_diagnostic_count(result); i++) {
    const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
    if (bebop_diagnostic_severity(diag) == BEBOP_DIAG_ERROR) {
      const char* msg = bebop_diagnostic_message(diag);
      if (msg && strstr(msg, "int128")) {
        found_enum_base_error = true;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(
      found_enum_base_error, "Should have error mentioning int128 for enum base"
  );
}

void test_fail_fixed_array_size_zero(void)
{
  const char* source = "struct BadFixedArray {\n" "    empty: int32[0];\n" "}\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "Fixed array size 0 should produce an error");
}

void test_fail_fixed_array_size_too_large(void)
{
  const char* source = "struct BadFixedArray {\n" "    tooBig: int32[65536];\n" "}\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "Fixed array size > 65535 should produce an error");
}

void test_parse_package(void)
{
  const char* source = "edition = \"2026\"\n"
                         "package my.test.package;\n"
                         "\n"
                         "struct Point {\n"
                         "    x: float32;\n"
                         "    y: float32;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);
  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);

  const char* pkg = bebop_schema_package(schema);
  TEST_ASSERT_NOT_NULL_MESSAGE(pkg, "Package should not be NULL");
  TEST_ASSERT_EQUAL_STRING("my.test.package", pkg);
}

void test_fail_package_after_import(void)
{
  const char* source = "edition = \"2026\"\n"
                         "import \"other.bop\"\n"
                         "package my.package;\n"
                         "struct Foo { x: int32; }\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "Package after import should produce an error");
}

void test_fail_package_after_definition(void)
{
  const char* source = "edition = \"2026\"\n" "struct Foo { x: int32; }\n" "package my.package;\n";

  bebop_parse_result_t* result = NULL;
  (void)bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "Package after definition should produce an error");
}

void test_parse_visibility_local(void)
{
  const char* source = "local struct PrivateStruct {\n"
                         "    hidden: int32;\n"
                         "}\n"
                         "\n"
                         "struct PublicStruct {\n"
                         "    visible: int32;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);

  const bebop_def_t* private_def = bebop_result_find(result, "PrivateStruct");
  TEST_ASSERT_NOT_NULL_MESSAGE(private_def, "PrivateStruct should exist");
  TEST_ASSERT_FALSE_MESSAGE(
      bebop_def_is_accessible(private_def), "PrivateStruct should not be accessible"
  );

  const bebop_def_t* public_def = bebop_result_find(result, "PublicStruct");
  TEST_ASSERT_NOT_NULL_MESSAGE(public_def, "PublicStruct should exist");
  TEST_ASSERT_TRUE_MESSAGE(
      bebop_def_is_accessible(public_def), "PublicStruct should be accessible"
  );
}

void test_parse_visibility_export(void)
{
  const char* source =
      "local const int32 PRIVATE_VALUE = 42;\n" "const string PUBLIC_NAME = \"hello\";\n";

  bebop_parse_result_t* result = parse_expect_success(source);

  const bebop_def_t* private_const = bebop_result_find(result, "PRIVATE_VALUE");
  TEST_ASSERT_NOT_NULL_MESSAGE(private_const, "PRIVATE_VALUE should exist");
  TEST_ASSERT_FALSE_MESSAGE(
      bebop_def_is_accessible(private_const), "PRIVATE_VALUE should not be accessible"
  );

  const bebop_def_t* public_const = bebop_result_find(result, "PUBLIC_NAME");
  TEST_ASSERT_NOT_NULL_MESSAGE(public_const, "PUBLIC_NAME should exist");
  TEST_ASSERT_TRUE_MESSAGE(
      bebop_def_is_accessible(public_const), "PUBLIC_NAME should be accessible"
  );
}

void test_parse_union_type_ref(void)
{
  const char* source = "struct Metadata {\n"
                         "    version: string;\n"
                         "}\n"
                         "\n"
                         "union Event {\n"
                         "    Created(1): {\n"
                         "        id: string;\n"
                         "    };\n"
                         "    meta(2): Metadata;\n"
                         "}\n";

  bebop_parse_result_t* result = parse_expect_success(source);

  const bebop_def_t* event_def = bebop_result_find(result, "Event");
  TEST_ASSERT_NOT_NULL_MESSAGE(event_def, "Event union should exist");
  TEST_ASSERT_EQUAL_MESSAGE(BEBOP_DEF_UNION, bebop_def_kind(event_def), "Event should be a union");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      2, bebop_def_branch_count(event_def), "Event should have 2 branches"
  );

  const bebop_union_branch_t* branch1 = bebop_def_branch_at(event_def, 0);
  TEST_ASSERT_NOT_NULL(branch1);
  TEST_ASSERT_EQUAL_UINT8(1, bebop_branch_discriminator(branch1));
  TEST_ASSERT_NOT_NULL_MESSAGE(bebop_branch_def(branch1), "Branch 1 should have inline def");
  TEST_ASSERT_NULL_MESSAGE(bebop_branch_type_ref(branch1), "Branch 1 should not have type ref");

  const bebop_union_branch_t* branch2 = bebop_def_branch_at(event_def, 1);
  TEST_ASSERT_NOT_NULL(branch2);
  TEST_ASSERT_EQUAL_UINT8(2, bebop_branch_discriminator(branch2));
  TEST_ASSERT_NULL_MESSAGE(bebop_branch_def(branch2), "Branch 2 should not have inline def");
  TEST_ASSERT_NOT_NULL_MESSAGE(bebop_branch_type_ref(branch2), "Branch 2 should have type ref");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(
      "meta", bebop_branch_name(branch2), "Branch 2 should have name 'meta'"
  );
}

void test_fail_union_ref_union(void)
{
  const char* source = "union Inner {\n"
                         "    A(1): { x: int32; };\n"
                         "}\n"
                         "\n"
                         "union Outer {\n"
                         "    nested(1): Inner;\n"
                         "}\n";

  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);

  if (status == BEBOP_OK) {
    (void)bebop_validate(result);
  }

  uint32_t error_count = bebop_result_error_count(result);
  TEST_ASSERT_TRUE_MESSAGE(error_count > 0, "Union referencing union should produce an error");
}

void test_env_var_substitution(void);
void test_env_var_multiple(void);
void test_env_var_not_found(void);
void test_env_var_unclosed(void);

static bebop_context_t* make_env_ctx(const bebop_option_t* entries, uint32_t count)
{
  bebop_host_t host = test_host(test_include_paths, 2);
  host.env.entries = entries;
  host.env.count = count;
  return bebop_context_create(&host);
}

void test_env_var_substitution(void)
{
  static const bebop_option_t env[] = {{"APP_NAME", "MyApp"}};
  bebop_context_destroy(ctx);
  ctx = make_env_ctx(env, 1);

  const char* source = "const string NAME = \"$(APP_NAME)\";\n";
  bebop_parse_result_t* result = parse_expect_success(source);

  const bebop_def_t* def = bebop_result_find(result, "NAME");
  TEST_ASSERT_NOT_NULL(def);
  const bebop_literal_t* val = bebop_def_const_value(def);
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_STRING("MyApp", bebop_literal_as_string(val, NULL));
}

void test_env_var_multiple(void)
{
  static const bebop_option_t env[] = {{"HOST", "localhost"}, {"PORT", "8080"}};
  bebop_context_destroy(ctx);
  ctx = make_env_ctx(env, 2);

  const char* source = "const string URL = \"$(HOST):$(PORT)\";\n";
  bebop_parse_result_t* result = parse_expect_success(source);

  const bebop_def_t* def = bebop_result_find(result, "URL");
  TEST_ASSERT_NOT_NULL(def);
  const bebop_literal_t* val = bebop_def_const_value(def);
  TEST_ASSERT_EQUAL_STRING("localhost:8080", bebop_literal_as_string(val, NULL));
}

void test_env_var_not_found(void)
{
  bebop_context_destroy(ctx);
  ctx = make_env_ctx(NULL, 0);

  const char* source = "const string X = \"$(MISSING)\";\n";
  bebop_parse_result_t* result = NULL;
  bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);
}

void test_env_var_unclosed(void)
{
  bebop_context_destroy(ctx);
  ctx = make_env_ctx(NULL, 0);

  const char* source = "const string X = \"$(OOPS\";\n";
  bebop_parse_result_t* result = NULL;
  bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_parse_empty);
  RUN_TEST(test_parse_basic_types);
  RUN_TEST(test_parse_extended_types);
  RUN_TEST(test_parse_extended_types_sizes);
  RUN_TEST(test_parse_fixed_arrays);
  RUN_TEST(test_parse_fixed_arrays_sizes);
  RUN_TEST(test_parse_fixed_arrays_not_fixed);
  RUN_TEST(test_parse_nested_fixed_arrays);
  RUN_TEST(test_parse_array_of_strings);
  RUN_TEST(test_parse_basic_arrays);
  RUN_TEST(test_parse_enum);
  RUN_TEST(test_parse_message);
  RUN_TEST(test_parse_map_types);
  RUN_TEST(test_parse_nested_types);
  RUN_TEST(test_parse_multiple_definitions);

  RUN_TEST(test_parse_documentation);
  RUN_TEST(test_parse_decorators);
  RUN_TEST(test_parse_union);
  RUN_TEST(test_parse_service);
  RUN_TEST(test_parse_const);
  RUN_TEST(test_parse_const_timestamp_duration_bytes);
  RUN_TEST(test_parse_edition);
  RUN_TEST(test_parse_edition_default);
  RUN_TEST(test_parse_toposort);
  RUN_TEST(test_dependency_tracking);
  RUN_TEST(test_context_error_message);

  RUN_TEST(test_fixture_album);
  RUN_TEST(test_fixture_array_of_strings);
  RUN_TEST(test_fixture_basic_arrays);
  RUN_TEST(test_fixture_basic_types);
  RUN_TEST(test_fixture_bitflags);
  RUN_TEST(test_fixture_const);
  RUN_TEST(test_fixture_documentation);
  RUN_TEST(test_fixture_empty);
  RUN_TEST(test_fixture_enum_size);
  RUN_TEST(test_fixture_enum);
  RUN_TEST(test_fixture_fixed_arrays);
  RUN_TEST(test_fixture_imports);
  RUN_TEST(test_fixture_jazz);
  RUN_TEST(test_fixture_lab);
  RUN_TEST(test_fixture_map_types);
  RUN_TEST(test_fixture_message);
  RUN_TEST(test_fixture_msgpack_comparison);
  RUN_TEST(test_fixture_nested_message);
  RUN_TEST(test_fixture_request);
  RUN_TEST(test_fixture_struct);
  RUN_TEST(test_fixture_toposort);
  RUN_TEST(test_fixture_union_perf_a);
  RUN_TEST(test_fixture_union_perf_b);
  RUN_TEST(test_fixture_union_perf_c);
  RUN_TEST(test_fixture_union);

  RUN_TEST(test_fail_bad_comment);
  RUN_TEST(test_fail_invalid_syntax);
  RUN_TEST(test_fail_nested_union);
  RUN_TEST(test_fail_enum_zero);
  RUN_TEST(test_fail_invalid_map_keys);
  RUN_TEST(test_fail_invalid_union_reference);
  RUN_TEST(test_fail_rpc);
  RUN_TEST(test_fail_multiple_errors);
  RUN_TEST(test_fail_unknown_decorator);
  RUN_TEST(test_int128_map_key);
  RUN_TEST(test_fail_int128_enum_base);
  RUN_TEST(test_fail_fixed_array_size_zero);
  RUN_TEST(test_fail_fixed_array_size_too_large);

  RUN_TEST(test_parse_package);
  RUN_TEST(test_fail_package_after_import);
  RUN_TEST(test_fail_package_after_definition);
  RUN_TEST(test_parse_visibility_local);
  RUN_TEST(test_parse_visibility_export);
  RUN_TEST(test_parse_union_type_ref);
  RUN_TEST(test_fail_union_ref_union);

  RUN_TEST(test_env_var_substitution);
  RUN_TEST(test_env_var_multiple);
  RUN_TEST(test_env_var_not_found);
  RUN_TEST(test_env_var_unclosed);

  return UNITY_END();
}
