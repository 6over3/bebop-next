#include <stdio.h>
#include <string.h>

#include "bebop.h"
#include "test_common.h"
#include "unity.h"

static bebop_context_t* ctx;

static const char* test_include_paths[] = {BEBOP_STD_DIR};

void setUp(void)
{
  bebop_host_t host = test_host(test_include_paths, 1);
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

static void roundtrip(const char* source)
{
  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  if (status != BEBOP_OK && status != BEBOP_OK_WITH_WARNINGS) {
    fprintf(stderr, "Initial parse failed with status %d. Source:\n%s\n", status, source);
    uint32_t diag_count = bebop_result_diagnostic_count(result);
    for (uint32_t i = 0; i < diag_count; i++) {
      const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
      fprintf(stderr, "  Diagnostic: %s\n", bebop_diagnostic_message(diag));
    }
  }
  TEST_ASSERT_TRUE(status == BEBOP_OK || status == BEBOP_OK_WITH_WARNINGS);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);

  size_t emitted_len = 0;
  const char* emitted = bebop_emit_schema(schema, &emitted_len);
  TEST_ASSERT_NOT_NULL(emitted);
  TEST_ASSERT_TRUE(emitted_len > 0);

  bebop_host_t host2 = test_host(test_include_paths, 1);
  bebop_context_t* ctx2 = bebop_context_create(&host2);
  bebop_parse_result_t* result2 = NULL;
  status =
      bebop_parse_source(ctx2, &(bebop_source_t) {emitted, emitted_len, "emitted.bop"}, &result2);

  if (status != BEBOP_OK && status != BEBOP_OK_WITH_WARNINGS) {
    fprintf(stderr, "Re-parse failed. Emitted:\n%s\n", emitted);
  }
  TEST_ASSERT_TRUE_MESSAGE(
      status == BEBOP_OK || status == BEBOP_OK_WITH_WARNINGS,
      "Re-parsing emitted schema should succeed"
  );
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      0, bebop_result_error_count(result2), "Re-parsed schema should have no errors"
  );

  bebop_context_destroy(ctx2);
}

void test_emit_struct(void);
void test_emit_message(void);
void test_emit_enum(void);
void test_emit_union(void);
void test_emit_service(void);
void test_emit_const(void);
void test_emit_string_escapes(void);
void test_emit_decorators(void);
void test_emit_complex(void);
void test_emit_extended_types(void);
void test_emit_fixed_arrays(void);

void test_emit_struct(void)
{
  roundtrip("struct Point {\n" "    x: int32;\n" "    y: int32;\n" "}\n");

  roundtrip("mut struct MutablePoint {\n" "    x: int32;\n" "    y: int32;\n" "}\n");

  roundtrip("local struct PrivateData {\n" "    secret: string;\n" "}\n");

  roundtrip("local mut struct PrivateMut {\n" "    value: int32;\n" "}\n");
}

void test_emit_message(void)
{
  roundtrip("message Event {\n" "    type(1): string;\n" "    timestamp(2): int64;\n" "}\n");
}

void test_emit_enum(void)
{
  roundtrip(
        "enum Status {\n"
        "    Unknown = 0;\n"
        "    Pending = 1;\n"
        "    Active = 2;\n"
        "    Done = 3;\n"
        "}\n");

  roundtrip("enum Flags : uint16 {\n" "    None = 0;\n" "    Read = 1;\n" "    Write = 2;\n" "}\n");
}

void test_emit_union(void)
{
  roundtrip(
        "union Shape {\n"
        "    Circle(1): { radius: float32; };\n"
        "    Rectangle(2): { width: float32; height: float32; };\n"
        "}\n");

  roundtrip(
        "union Result {\n"
        "    Success(1): { value: string; };\n"
        "    export Error(2): { code: int32; };\n"
        "}\n");

  roundtrip(
        "union MutResult {\n"
        "    MutData(1): mut { x: int32; };\n"
        "    export ExportedMut(2): mut { s: string; };\n"
        "}\n");
}

void test_emit_service(void)
{
  roundtrip(
        "struct Request { query: string; }\n"
        "struct Response { result: string; }\n"
        "service Api {\n"
        "    search(Request): Response;\n"
        "}\n");

  roundtrip(
        "struct Data { value: int32; }\n"
        "service Stream {\n"
        "    upload(stream Data): Data;\n"
        "    download(Data): stream Data;\n"
        "    chat(stream Data): stream Data;\n"
        "}\n");
}

void test_emit_const(void)
{
  roundtrip("const int32 VERSION = 42;");
  roundtrip("const string NAME = \"test\";");
  roundtrip("const bool ENABLED = true;");
  roundtrip("const float64 PI = 3.14159;");
}

void test_emit_string_escapes(void)
{
  roundtrip("const string NEWLINE = \"line1\\nline2\";");
  roundtrip("const string TAB = \"col1\\tcol2\";");
  roundtrip("const string CARRIAGE = \"a\\rb\";");
  roundtrip("const string BACKSLASH = \"path\\\\to\\\\file\";");
  roundtrip("const string QUOTE = \"say \\\"hello\\\"\";");
  roundtrip("const string UNICODE = \"\\u{1F600}\";");
  roundtrip("const string EURO = \"\\u{20AC}\";");
  roundtrip("const string MIXED = \"a\\tb\\nc\\\\d\";");
  roundtrip("const string NULL_BYTE = \"with\\0null\";");
}

void test_emit_decorators(void)
{
  roundtrip(
        "import \"bebop/decorators.bop\"\n"
        "@deprecated(\"use NewStruct instead\")\n"
        "struct OldStruct {\n"
        "    value: int32;\n"
        "}\n");

  roundtrip(
        "import \"bebop/decorators.bop\"\n"
        "@flags\n"
        "enum Permissions {\n"
        "    None = 0;\n"
        "    Read = 1;\n"
        "    Write = 2;\n"
        "    Execute = 4;\n"
        "}\n");
}

void test_emit_complex(void)
{
  roundtrip(
        "import \"bebop/decorators.bop\"\n"
        "/// User account\n"
        "@deprecated(\"use NewUser instead\")\n"
        "struct User {\n"
        "    id: guid;\n"
        "    name: string;\n"
        "    email: string;\n"
        "}\n"
        "\n"
        "/// User preferences\n"
        "message Preferences {\n"
        "    darkMode(1): bool;\n"
        "    locale(2): string;\n"
        "}\n"
        "\n"
        "enum Role {\n"
        "    Unknown = 0;\n"
        "    Guest = 1;\n"
        "    User = 2;\n"
        "    Admin = 3;\n"
        "}\n"
        "\n"
        "union Account {\n"
        "    Anonymous(1): { sessionId: string; };\n"
        "    Registered(2): { user: User; role: Role; };\n"
        "}\n");
}

void test_emit_extended_types(void)
{
  roundtrip(
        "struct ExtendedTypes {\n"
        "    a: int8;\n"
        "    b: int128;\n"
        "    c: uint128;\n"
        "    d: float16;\n"
        "}\n");

  roundtrip("struct WithAliases {\n" "    x: sbyte;\n" "    y: half;\n" "    z: uuid;\n" "}\n");
}

void test_emit_fixed_arrays(void)
{
  roundtrip(
        "struct FixedArrays {\n"
        "    arr1: int32[10];\n"
        "    buffer: byte[256];\n"
        "    ids: guid[2];\n"
        "}\n");

  roundtrip(
        "struct NestedFixed {\n"
        "    matrix: int32[3][2];\n"
        "    matrix4x4: float16[4][4];\n"
        "}\n");

  roundtrip(
        "struct MixedArrays {\n"
        "    dynamicOfFixed: int32[5][];\n"
        "    mapToFixed: map[string, int128[2]];\n"
        "}\n");

  roundtrip(
        "struct FixedNewTypes {\n"
        "    bytes: int8[100];\n"
        "    bigInts: int128[4];\n"
        "    halfs: float16[8];\n"
        "}\n");
}

void test_emit_nested_types(void);

void test_emit_nested_types(void)
{
  roundtrip("struct Outer {\n" "    value: int32;\n" "    struct Inner { name: string; }\n" "}\n");

  roundtrip(
        "struct Container {\n"
        "    id: int32;\n"
        "    export struct Point { x: float32; y: float32; }\n"
        "}\n");

  roundtrip(
        "struct WithEnum {\n"
        "    name: string;\n"
        "    enum Status { Unknown = 0; Active = 1; }\n"
        "}\n");

  roundtrip(
        "message Event {\n"
        "    type(1): string;\n"
        "    struct Payload { data: string; }\n"
        "}\n");

  roundtrip(
        "union Result {\n"
        "    Success(1): { value: string; };\n"
        "    enum Code { Ok = 0; Fail = 1; }\n"
        "}\n");

  roundtrip(
        "struct Level1 {\n"
        "    a: int32;\n"
        "    struct Level2 {\n"
        "        b: int32;\n"
        "        struct Level3 { c: int32; }\n"
        "    }\n"
        "}\n");
}

void test_emit_multiline_doc(void);

void test_emit_multiline_doc(void)
{
  roundtrip(
        "/**\n"
        " * A documented union.\n"
        " * With multiple lines.\n"
        " */\n"
        "union Documented {\n"
        "    /** Branch A doc */\n"
        "    A(1): { x: int32; };\n"
        "    /**\n"
        "     * Branch B has\n"
        "     * multiline docs too.\n"
        "     */\n"
        "    B(2): { s: string; };\n"
        "}\n");
}

void test_emit_preserves_comments(void);

void test_emit_preserves_comments(void)
{
  const char* source = "// This is a file comment\n"
                         "\n"
                         "struct First { x: int32; }\n"
                         "\n"
                         "// Comment between definitions\n"
                         "struct Second { y: int32; }\n";

  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_TRUE(status == BEBOP_OK || status == BEBOP_OK_WITH_WARNINGS);

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);

  size_t emitted_len = 0;
  const char* emitted = bebop_emit_schema(schema, &emitted_len);
  TEST_ASSERT_NOT_NULL(emitted);

  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(emitted, "// This is a file comment"), "File comment should be preserved"
  );
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(emitted, "// Comment between definitions"),
      "Comment between definitions should be preserved"
  );
}

static int count_occurrences(const char* haystack, const char* needle)
{
  int count = 0;
  const char* p = haystack;
  size_t needle_len = strlen(needle);
  while ((p = strstr(p, needle)) != NULL) {
    count++;
    p += needle_len;
  }
  return count;
}

void test_emit_no_double_docs(void);

void test_emit_no_double_docs(void)
{
  const char* source = "/// Single line doc\n"
                         "struct A { x: int32; }\n"
                         "\n"
                         "/** Block doc */\n"
                         "struct B { y: int32; }\n"
                         "\n"
                         "/**\n"
                         " * Multiline\n"
                         " * block doc\n"
                         " */\n"
                         "struct C { z: int32; }\n"
                         "\n"
                         "/// Line one\n"
                         "/// Line two\n"
                         "struct D { w: int32; }\n";

  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_TRUE(status == BEBOP_OK || status == BEBOP_OK_WITH_WARNINGS);

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);

  size_t emitted_len = 0;
  const char* emitted = bebop_emit_schema(schema, &emitted_len);
  TEST_ASSERT_NOT_NULL(emitted);

  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "Single line doc"), "Single line doc should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "Block doc"), "Block doc should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "Multiline"), "Multiline should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "block doc"), "block doc should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "Line one"), "Line one should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "Line two"), "Line two should appear exactly once"
  );

  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "struct A"), "struct A should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "struct B"), "struct B should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "struct C"), "struct C should appear exactly once"
  );
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      1, count_occurrences(emitted, "struct D"), "struct D should appear exactly once"
  );
}

void test_emit_decorator_definition(void);

void test_emit_decorator_definition(void)
{
  roundtrip(
        "#decorator(range) {\n"
        "    targets = FIELD\n"
        "    param min?: int32\n"
        "    param max?: int32\n"
        "}\n");

  roundtrip(
        "#decorator(tag) {\n"
        "    targets = STRUCT | MESSAGE | ENUM\n"
        "    multiple = true\n"
        "    param value!: string\n"
        "    validate [[ return true ]]\n"
        "}\n");

  roundtrip("#decorator(audit) {\n" "    targets = ALL\n" "}\n");

  roundtrip(
        "#decorator(range) {\n"
        "    targets = FIELD\n"
        "    param min?: int32\n"
        "}\n"
        "\n"
        "struct Foo {\n"
        "    @range(min: 0)\n"
        "    value: int32;\n"
        "}\n");
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_emit_struct);
  RUN_TEST(test_emit_message);
  RUN_TEST(test_emit_enum);
  RUN_TEST(test_emit_union);
  RUN_TEST(test_emit_service);
  RUN_TEST(test_emit_const);
  RUN_TEST(test_emit_string_escapes);
  RUN_TEST(test_emit_decorators);
  RUN_TEST(test_emit_decorator_definition);
  RUN_TEST(test_emit_complex);
  RUN_TEST(test_emit_extended_types);
  RUN_TEST(test_emit_fixed_arrays);
  RUN_TEST(test_emit_nested_types);
  RUN_TEST(test_emit_multiline_doc);
  RUN_TEST(test_emit_preserves_comments);
  RUN_TEST(test_emit_no_double_docs);

  return UNITY_END();
}
