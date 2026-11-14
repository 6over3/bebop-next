#include "bebop.c"
#include "bebop.h"
#include "test_common.h"
#include "unity.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

static bebop_context_t* ctx;

void setUp(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);
}

void tearDown(void)
{
  if (ctx) {
    bebop_context_destroy(ctx);
    ctx = NULL;
  }
}

static bebop_parse_result_t* parse_ok(const char* source)
{
  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_TRUE(status == BEBOP_OK || status == BEBOP_OK_WITH_WARNINGS);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  return result;
}

void test_build_empty_schema(void);

void test_build_empty_schema(void)
{
  bebop_parse_result_t* result = parse_ok("");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));
  TEST_ASSERT_NOT_NULL(desc);
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_schema_count(desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_schema_def_count(s));
  TEST_ASSERT_NULL(bebop_descriptor_schema_source_code_info(s));

  bebop_descriptor_free(desc);
}

void test_build_struct(void);

void test_build_struct(void)
{
  bebop_parse_result_t* result = parse_ok(
        "struct Point {\n"
        "    x: int32;\n"
        "    y: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_schema_def_count(s));
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_STRUCT, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_STRING("Point", bebop_descriptor_def_name(d));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_def_field_count(d));
  TEST_ASSERT_EQUAL_STRING("x", bebop_descriptor_field_name(bebop_descriptor_def_field_at(d, 0)));
  TEST_ASSERT_EQUAL_STRING("y", bebop_descriptor_field_name(bebop_descriptor_def_field_at(d, 1)));

  bebop_descriptor_free(desc);
}

void test_build_message(void);

void test_build_message(void)
{
  bebop_parse_result_t* result = parse_ok(
        "message Msg {\n"
        "    name(1): string;\n"
        "    value(2): int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_MESSAGE, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_def_field_count(d));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_field_index(bebop_descriptor_def_field_at(d, 0)));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_field_index(bebop_descriptor_def_field_at(d, 1)));

  bebop_descriptor_free(desc);
}

void test_build_enum(void);

void test_build_enum(void)
{
  bebop_parse_result_t* result = parse_ok(
        "enum Color {\n"
        "    UNSPECIFIED = 0;\n"
        "    Red = 1;\n"
        "    Green = 2;\n"
        "    Blue = 3;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_ENUM, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_UINT32(4, bebop_descriptor_def_member_count(d));

  bebop_descriptor_free(desc);
}

void test_build_edition_and_package(void);

void test_build_edition_and_package(void)
{
  bebop_parse_result_t* result = parse_ok(
        "edition = \"2026\"\n"
        "package my.pkg;\n"
        "struct Foo { x: int32; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  TEST_ASSERT_EQUAL_INT(BEBOP_ED_2026, bebop_descriptor_schema_edition(s));
  TEST_ASSERT_EQUAL_STRING("my.pkg", bebop_descriptor_schema_package(s));
  TEST_ASSERT_EQUAL_STRING(
      "my.pkg.Foo", bebop_descriptor_def_fqn(bebop_descriptor_schema_def_at(s, 0))
  );

  bebop_descriptor_free(desc);
}

void test_source_code_info_basic(void);

void test_source_code_info_basic(void)
{
  bebop_parse_result_t* result = parse_ok(
        "struct Point {\n"
        "    x: int32;\n"
        "    y: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  TEST_ASSERT_NOT_NULL(sci);

  TEST_ASSERT_EQUAL_UINT32(3, bebop_descriptor_location_count(sci));

  for (uint32_t i = 0; i < bebop_descriptor_location_count(sci); i++) {
    const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, i);
    TEST_ASSERT_NOT_NULL(loc);
    const int32_t* span = bebop_descriptor_location_span(loc);
    TEST_ASSERT_NOT_NULL(span);
    TEST_ASSERT_TRUE(span[0] > 0);
  }

  bebop_descriptor_free(desc);
}

void test_source_code_info_doc_comment(void);

void test_source_code_info_doc_comment(void)
{
  bebop_parse_result_t* result = parse_ok(
        "/// A point in 2D space\n"
        "struct Point {\n"
        "    /// X coordinate\n"
        "    x: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  TEST_ASSERT_NOT_NULL(sci);
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_location_count(sci));

  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 0);
  TEST_ASSERT_EQUAL_STRING("A point in 2D space", bebop_descriptor_location_leading(loc));

  loc = bebop_descriptor_location_at(sci, 1);
  TEST_ASSERT_EQUAL_STRING("X coordinate", bebop_descriptor_location_leading(loc));

  bebop_descriptor_free(desc);
}

void test_source_code_info_line_comment(void);

void test_source_code_info_line_comment(void)
{
  bebop_parse_result_t* result = parse_ok(
        "// This is a point\n"
        "struct Point {\n"
        "    x: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 0);
  TEST_ASSERT_EQUAL_STRING("This is a point", bebop_descriptor_location_leading(loc));

  bebop_descriptor_free(desc);
}

void test_source_code_info_detached_comments(void);

void test_source_code_info_detached_comments(void)
{
  bebop_parse_result_t* result = parse_ok(
        "// Copyright notice\n"
        "\n"
        "// This is Point\n"
        "struct Point {\n"
        "    x: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 0);

  TEST_ASSERT_EQUAL_STRING("This is Point", bebop_descriptor_location_leading(loc));

  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_location_detached_count(loc));
  TEST_ASSERT_EQUAL_STRING("Copyright notice", bebop_descriptor_location_detached_at(loc, 0));

  bebop_descriptor_free(desc);
}

void test_source_code_info_spans(void);

void test_source_code_info_spans(void)
{
  bebop_parse_result_t* result = parse_ok("struct Foo {\n" "    x: int32;\n" "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 0);

  const int32_t* span = bebop_descriptor_location_span(loc);
  TEST_ASSERT_EQUAL_INT32(1, span[0]);
  TEST_ASSERT_EQUAL_INT32(1, span[1]);

  bebop_descriptor_free(desc);
}

void test_source_code_info_multiple_defs(void);

void test_source_code_info_multiple_defs(void)
{
  bebop_parse_result_t* result = parse_ok(
        "/// First struct\n"
        "struct A { x: int32; }\n"
        "/// Second struct\n"
        "struct B { y: int32; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);

  TEST_ASSERT_EQUAL_UINT32(4, bebop_descriptor_location_count(sci));

  TEST_ASSERT_EQUAL_STRING(
      "First struct", bebop_descriptor_location_leading(bebop_descriptor_location_at(sci, 0))
  );

  TEST_ASSERT_EQUAL_STRING(
      "Second struct", bebop_descriptor_location_leading(bebop_descriptor_location_at(sci, 2))
  );

  bebop_descriptor_free(desc);
}

void test_source_code_info_enum_members(void);

void test_source_code_info_enum_members(void)
{
  bebop_parse_result_t* result = parse_ok(
        "enum Color {\n"
        "    UNSPECIFIED = 0;\n"
        "    /// The red one\n"
        "    Red = 1;\n"
        "    Green = 2;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);

  TEST_ASSERT_EQUAL_UINT32(4, bebop_descriptor_location_count(sci));

  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 2);
  TEST_ASSERT_EQUAL_STRING("The red one", bebop_descriptor_location_leading(loc));

  bebop_descriptor_free(desc);
}

void test_roundtrip_struct(void);

void test_roundtrip_struct(void)
{
  bebop_parse_result_t* result = parse_ok(
        "/// A point\n"
        "struct Point {\n"
        "    x: int32;\n"
        "    y: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_TRUE(buf_len > 0);

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));
  TEST_ASSERT_NOT_NULL(decoded);
  TEST_ASSERT_EQUAL_UINT32(
      bebop_descriptor_schema_count(desc), bebop_descriptor_schema_count(decoded)
  );

  const bebop_descriptor_schema_t* orig_s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_schema_t* dec_s = bebop_descriptor_schema_at(decoded, 0);
  TEST_ASSERT_EQUAL_INT(
      bebop_descriptor_schema_edition(orig_s), bebop_descriptor_schema_edition(dec_s)
  );
  TEST_ASSERT_EQUAL_UINT32(
      bebop_descriptor_schema_def_count(orig_s), bebop_descriptor_schema_def_count(dec_s)
  );

  const bebop_descriptor_def_t* orig_d = bebop_descriptor_schema_def_at(orig_s, 0);
  const bebop_descriptor_def_t* dec_d = bebop_descriptor_schema_def_at(dec_s, 0);
  TEST_ASSERT_EQUAL_UINT8(bebop_descriptor_def_kind(orig_d), bebop_descriptor_def_kind(dec_d));
  TEST_ASSERT_EQUAL_STRING(bebop_descriptor_def_name(orig_d), bebop_descriptor_def_name(dec_d));
  TEST_ASSERT_EQUAL_UINT32(
      bebop_descriptor_def_field_count(orig_d), bebop_descriptor_def_field_count(dec_d)
  );

  const bebop_descriptor_source_code_info_t* orig_sci =
      bebop_descriptor_schema_source_code_info(orig_s);
  const bebop_descriptor_source_code_info_t* dec_sci =
      bebop_descriptor_schema_source_code_info(dec_s);
  TEST_ASSERT_NOT_NULL(dec_sci);
  TEST_ASSERT_EQUAL_UINT32(
      bebop_descriptor_location_count(orig_sci), bebop_descriptor_location_count(dec_sci)
  );

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_roundtrip_complex(void);

void test_roundtrip_complex(void)
{
  bebop_parse_result_t* result = parse_ok(
        "// License header\n"
        "\n"
        "/// Color enum\n"
        "enum Color {\n"
        "    UNSPECIFIED = 0;\n"
        "    Red = 1;\n"
        "    Green = 2;\n"
        "}\n"
        "\n"
        "/// A shaped object\n"
        "message Shape {\n"
        "    name(1): string;\n"
        "    color(2): Color;\n"
        "}\n"
        "\n"
        "union Entity {\n"
        "    shape(1): Shape;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(decoded, 0);
  TEST_ASSERT_EQUAL_UINT32(3, bebop_descriptor_schema_def_count(s));
  TEST_ASSERT_EQUAL_UINT8(
      BEBOP_DEF_ENUM, bebop_descriptor_def_kind(bebop_descriptor_schema_def_at(s, 0))
  );
  TEST_ASSERT_EQUAL_UINT8(
      BEBOP_DEF_MESSAGE, bebop_descriptor_def_kind(bebop_descriptor_schema_def_at(s, 1))
  );
  TEST_ASSERT_EQUAL_UINT8(
      BEBOP_DEF_UNION, bebop_descriptor_def_kind(bebop_descriptor_schema_def_at(s, 2))
  );

  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  TEST_ASSERT_NOT_NULL(sci);
  TEST_ASSERT_TRUE(bebop_descriptor_location_count(sci) > 0);

  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 0);
  TEST_ASSERT_EQUAL_STRING("Color enum", bebop_descriptor_location_leading(loc));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_location_detached_count(loc));
  TEST_ASSERT_EQUAL_STRING("License header", bebop_descriptor_location_detached_at(loc, 0));

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_roundtrip_with_package(void);

void test_roundtrip_with_package(void)
{
  bebop_parse_result_t* result = parse_ok(
        "edition = \"2026\"\n"
        "package test.pkg;\n"
        "struct Foo { x: int32; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(decoded, 0);
  TEST_ASSERT_EQUAL_STRING("test.pkg", bebop_descriptor_schema_package(s));
  TEST_ASSERT_EQUAL_INT(BEBOP_ED_2026, bebop_descriptor_schema_edition(s));
  TEST_ASSERT_EQUAL_STRING(
      "test.pkg.Foo", bebop_descriptor_def_fqn(bebop_descriptor_schema_def_at(s, 0))
  );

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_roundtrip_service(void);

void test_roundtrip_service(void)
{
  bebop_parse_result_t* result = parse_ok(
        "struct Req { q: string; }\n"
        "struct Resp { n: int32; }\n"
        "service Search {\n"
        "    query(Req): Resp;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(decoded, 0);
  TEST_ASSERT_EQUAL_UINT32(3, bebop_descriptor_schema_def_count(s));
  const bebop_descriptor_def_t* svc = bebop_descriptor_schema_def_at(s, 2);
  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_SERVICE, bebop_descriptor_def_kind(svc));
  TEST_ASSERT_EQUAL_STRING("Search", bebop_descriptor_def_name(svc));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_def_method_count(svc));
  TEST_ASSERT_EQUAL_STRING(
      "query", bebop_descriptor_method_name(bebop_descriptor_def_method_at(svc, 0))
  );

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_roundtrip_const(void);

void test_roundtrip_const(void)
{
  bebop_parse_result_t* result = parse_ok(
      "const int32 MAX_SIZE = 1024;\n" "const string NAME = \"hello\";\n"
  );
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(decoded, 0);
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_schema_def_count(s));
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_CONST, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_STRING("MAX_SIZE", bebop_descriptor_def_name(d));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_def_const_value(d));
  TEST_ASSERT_EQUAL_INT64(
      1024, bebop_descriptor_literal_as_int(bebop_descriptor_def_const_value(d))
  );

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_roundtrip_map_type(void);

void test_roundtrip_map_type(void)
{
  bebop_parse_result_t* result = parse_ok(
        "struct Config {\n"
        "    values: map[string, int32];\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(decoded, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(d, 0);
  const bebop_descriptor_type_t* t = bebop_descriptor_field_type(f);
  TEST_ASSERT_EQUAL_INT8(BEBOP_TYPE_MAP, bebop_descriptor_type_kind(t));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_type_key(t));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_type_value(t));
  TEST_ASSERT_EQUAL_INT8(
      BEBOP_TYPE_STRING, bebop_descriptor_type_kind(bebop_descriptor_type_key(t))
  );
  TEST_ASSERT_EQUAL_INT8(
      BEBOP_TYPE_INT32, bebop_descriptor_type_kind(bebop_descriptor_type_value(t))
  );

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_roundtrip_array_type(void);

void test_roundtrip_array_type(void)
{
  bebop_parse_result_t* result = parse_ok("struct Data {\n" "    items: int32[];\n" "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const uint8_t* buf = NULL;
  size_t buf_len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_encode(desc, &buf, &buf_len));

  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_decode(ctx, buf, buf_len, &decoded));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(decoded, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(d, 0);
  const bebop_descriptor_type_t* t = bebop_descriptor_field_type(f);
  TEST_ASSERT_EQUAL_INT8(BEBOP_TYPE_ARRAY, bebop_descriptor_type_kind(t));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_type_element(t));
  TEST_ASSERT_EQUAL_INT8(
      BEBOP_TYPE_INT32, bebop_descriptor_type_kind(bebop_descriptor_type_element(t))
  );

  bebop_descriptor_free(decoded);
  bebop_descriptor_free(desc);
}

void test_accessor_schema(void);

void test_accessor_schema(void)
{
  bebop_parse_result_t* result = parse_ok(
        "edition = \"2026\"\n"
        "package my.pkg;\n"
        "struct Foo { x: int32; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_schema_count(desc));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_schema_count(NULL));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_NULL(bebop_descriptor_schema_at(desc, 99));

  TEST_ASSERT_EQUAL_STRING("test.bop", bebop_descriptor_schema_path(s));
  TEST_ASSERT_EQUAL_STRING("my.pkg", bebop_descriptor_schema_package(s));
  TEST_ASSERT_EQUAL_INT(BEBOP_ED_2026, bebop_descriptor_schema_edition(s));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_schema_def_count(s));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_schema_import_count(s));

  bebop_descriptor_free(desc);
}

void test_accessor_struct_def(void);

void test_accessor_struct_def(void)
{
  bebop_parse_result_t* result = parse_ok(
        "/// A point\n"
        "struct Point {\n"
        "    x: int32;\n"
        "    y: int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  TEST_ASSERT_NOT_NULL(d);

  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_STRUCT, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_STRING("Point", bebop_descriptor_def_name(d));
  TEST_ASSERT_EQUAL_STRING("Point", bebop_descriptor_def_fqn(d));
  TEST_ASSERT_EQUAL_STRING("A point", bebop_descriptor_def_documentation(d));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_def_decorator_count(d));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_def_nested_count(d));

  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_def_field_count(d));
  TEST_ASSERT_FALSE(bebop_descriptor_def_is_mutable(d));

  const bebop_descriptor_field_t* f0 = bebop_descriptor_def_field_at(d, 0);
  TEST_ASSERT_NOT_NULL(f0);
  TEST_ASSERT_EQUAL_STRING("x", bebop_descriptor_field_name(f0));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_field_type(f0));
  TEST_ASSERT_EQUAL_INT8(
      BEBOP_TYPE_INT32, bebop_descriptor_type_kind(bebop_descriptor_field_type(f0))
  );

  const bebop_descriptor_field_t* f1 = bebop_descriptor_def_field_at(d, 1);
  TEST_ASSERT_EQUAL_STRING("y", bebop_descriptor_field_name(f1));
  TEST_ASSERT_NULL(bebop_descriptor_def_field_at(d, 99));

  bebop_descriptor_free(desc);
}

void test_accessor_message_def(void);

void test_accessor_message_def(void)
{
  bebop_parse_result_t* result = parse_ok(
        "message Msg {\n"
        "    name(1): string;\n"
        "    value(2): int32;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);

  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_MESSAGE, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_def_field_count(d));

  const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(d, 0);
  TEST_ASSERT_EQUAL_STRING("name", bebop_descriptor_field_name(f));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_field_index(f));

  f = bebop_descriptor_def_field_at(d, 1);
  TEST_ASSERT_EQUAL_UINT32(2, bebop_descriptor_field_index(f));

  bebop_descriptor_free(desc);
}

void test_accessor_enum_def(void);

void test_accessor_enum_def(void)
{
  bebop_parse_result_t* result = parse_ok(
        "enum Color {\n"
        "    UNSPECIFIED = 0;\n"
        "    Red = 1;\n"
        "    Green = 2;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);

  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_ENUM, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_UINT32(3, bebop_descriptor_def_member_count(d));
  TEST_ASSERT_FALSE(bebop_descriptor_def_is_flags(d));

  const bebop_descriptor_member_t* m = bebop_descriptor_def_member_at(d, 1);
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("Red", bebop_descriptor_member_name(m));
  TEST_ASSERT_EQUAL_UINT64(1, bebop_descriptor_member_value(m));
  TEST_ASSERT_NULL(bebop_descriptor_def_member_at(d, 99));

  bebop_descriptor_free(desc);
}

void test_accessor_union_def(void);

void test_accessor_union_def(void)
{
  bebop_parse_result_t* result = parse_ok(
        "struct A { x: int32; }\n"
        "union U {\n"
        "    a(1): A;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 1);

  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_UNION, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_def_branch_count(d));

  const bebop_descriptor_branch_t* b = bebop_descriptor_def_branch_at(d, 0);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_UINT8(1, bebop_descriptor_branch_discriminator(b));
  TEST_ASSERT_EQUAL_STRING("a", bebop_descriptor_branch_name(b));
  TEST_ASSERT_NULL(bebop_descriptor_def_branch_at(d, 99));

  bebop_descriptor_free(desc);
}

void test_accessor_service_def(void);

void test_accessor_service_def(void)
{
  bebop_parse_result_t* result = parse_ok(
        "struct Req { q: string; }\n"
        "struct Resp { n: int32; }\n"
        "service Search {\n"
        "    query(Req): Resp;\n"
        "}\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 2);

  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_SERVICE, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_UINT32(1, bebop_descriptor_def_method_count(d));

  const bebop_descriptor_method_t* m = bebop_descriptor_def_method_at(d, 0);
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("query", bebop_descriptor_method_name(m));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_method_request(m));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_method_response(m));
  TEST_ASSERT_NULL(bebop_descriptor_def_method_at(d, 99));

  bebop_descriptor_free(desc);
}

void test_accessor_const_def(void);

void test_accessor_const_def(void)
{
  bebop_parse_result_t* result = parse_ok("const int32 MAX = 42;\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);

  TEST_ASSERT_EQUAL_UINT8(BEBOP_DEF_CONST, bebop_descriptor_def_kind(d));
  TEST_ASSERT_EQUAL_STRING("MAX", bebop_descriptor_def_name(d));

  const bebop_descriptor_type_t* t = bebop_descriptor_def_const_type(d);
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_EQUAL_INT8(BEBOP_TYPE_INT32, bebop_descriptor_type_kind(t));

  const bebop_descriptor_literal_t* lit = bebop_descriptor_def_const_value(d);
  TEST_ASSERT_NOT_NULL(lit);
  TEST_ASSERT_EQUAL_UINT8(BEBOP_LITERAL_INT, bebop_descriptor_literal_kind(lit));
  TEST_ASSERT_EQUAL_INT64(42, bebop_descriptor_literal_as_int(lit));

  bebop_descriptor_free(desc);
}

void test_accessor_type_map(void);

void test_accessor_type_map(void)
{
  bebop_parse_result_t* result = parse_ok("struct M { vals: map[string, int32]; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(d, 0);
  const bebop_descriptor_type_t* t = bebop_descriptor_field_type(f);

  TEST_ASSERT_EQUAL_INT8(BEBOP_TYPE_MAP, bebop_descriptor_type_kind(t));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_type_key(t));
  TEST_ASSERT_NOT_NULL(bebop_descriptor_type_value(t));
  TEST_ASSERT_EQUAL_INT8(
      BEBOP_TYPE_STRING, bebop_descriptor_type_kind(bebop_descriptor_type_key(t))
  );
  TEST_ASSERT_EQUAL_INT8(
      BEBOP_TYPE_INT32, bebop_descriptor_type_kind(bebop_descriptor_type_value(t))
  );

  bebop_descriptor_free(desc);
}

void test_accessor_type_array(void);

void test_accessor_type_array(void)
{
  bebop_parse_result_t* result = parse_ok("struct A { items: int32[]; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, 0);
  const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(d, 0);
  const bebop_descriptor_type_t* t = bebop_descriptor_field_type(f);

  TEST_ASSERT_EQUAL_INT8(BEBOP_TYPE_ARRAY, bebop_descriptor_type_kind(t));
  const bebop_descriptor_type_t* elem = bebop_descriptor_type_element(t);
  TEST_ASSERT_NOT_NULL(elem);
  TEST_ASSERT_EQUAL_INT8(BEBOP_TYPE_INT32, bebop_descriptor_type_kind(elem));

  bebop_descriptor_free(desc);
}

void test_accessor_source_code_info(void);

void test_accessor_source_code_info(void)
{
  bebop_parse_result_t* result = parse_ok("/// Doc\n" "struct Foo { x: int32; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(
      BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_SOURCE_INFO, &desc)
  );

  const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, 0);
  const bebop_descriptor_source_code_info_t* sci = bebop_descriptor_schema_source_code_info(s);
  TEST_ASSERT_NOT_NULL(sci);
  TEST_ASSERT_TRUE(bebop_descriptor_location_count(sci) > 0);

  const bebop_descriptor_location_t* loc = bebop_descriptor_location_at(sci, 0);
  TEST_ASSERT_NOT_NULL(loc);

  uint32_t path_count = 0;
  const int32_t* path = bebop_descriptor_location_path(loc, &path_count);
  TEST_ASSERT_TRUE(path_count > 0);
  TEST_ASSERT_NOT_NULL(path);

  const int32_t* span = bebop_descriptor_location_span(loc);
  TEST_ASSERT_NOT_NULL(span);
  TEST_ASSERT_TRUE(span[0] > 0);

  TEST_ASSERT_EQUAL_STRING("Doc", bebop_descriptor_location_leading(loc));
  TEST_ASSERT_NULL(bebop_descriptor_location_trailing(loc));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_location_detached_count(loc));

  bebop_descriptor_free(desc);
}

void test_accessor_null_safety(void);

void test_accessor_null_safety(void)
{
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_schema_count(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_schema_at(NULL, 0));
  TEST_ASSERT_NULL(bebop_descriptor_schema_path(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_schema_package(NULL));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_schema_def_count(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_schema_def_at(NULL, 0));

  TEST_ASSERT_EQUAL_UINT8(0, bebop_descriptor_def_kind(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_def_name(NULL));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_def_field_count(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_def_field_at(NULL, 0));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_def_member_count(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_def_member_at(NULL, 0));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_def_branch_count(NULL));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_def_method_count(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_def_const_type(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_def_const_value(NULL));

  TEST_ASSERT_NULL(bebop_descriptor_field_name(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_field_type(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_member_name(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_method_name(NULL));

  TEST_ASSERT_EQUAL_INT8(0, bebop_descriptor_type_kind(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_type_element(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_type_key(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_type_fqn(NULL));

  TEST_ASSERT_EQUAL_UINT8(0, bebop_descriptor_literal_kind(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_literal_as_string(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_usage_fqn(NULL));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_usage_arg_count(NULL));

  TEST_ASSERT_EQUAL_UINT32(0, bebop_descriptor_location_count(NULL));
  TEST_ASSERT_NULL(bebop_descriptor_location_at(NULL, 0));

  uint32_t count = 99;
  TEST_ASSERT_NULL(bebop_descriptor_location_path(NULL, &count));
  TEST_ASSERT_EQUAL_UINT32(0, count);
}

void test_free_null(void);

void test_free_null(void)
{
  bebop_descriptor_free(NULL);
}

void test_encode_null(void);

void test_encode_null(void)
{
  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_FATAL, bebop_descriptor_encode(NULL, &buf, &len));
}

void test_decode_null(void);

void test_decode_null(void)
{
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_FATAL, bebop_descriptor_decode(ctx, NULL, 0, &desc));
}

void test_roundtrip_embedded_nul(void);

void test_roundtrip_embedded_nul(void)
{
  bebop_parse_result_t* result = parse_ok("const string TEXT = \"A\\0B\";");
  bebop_descriptor_t* descriptor = NULL;
  TEST_ASSERT_EQUAL(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &descriptor));
  const uint8_t* encoded = NULL;
  size_t encoded_len = 0;
  TEST_ASSERT_EQUAL(BEBOP_OK, bebop_descriptor_encode(descriptor, &encoded, &encoded_len));
  bebop_descriptor_t* decoded = NULL;
  TEST_ASSERT_EQUAL(BEBOP_OK, bebop_descriptor_decode(ctx, encoded, encoded_len, &decoded));
  const bebop_descriptor_def_t* def = bebop_descriptor_schema_def_at(
      bebop_descriptor_schema_at(decoded, 0), 0
  );
  size_t length = 0;
  const char* value = bebop_descriptor_literal_as_string_n(
      bebop_descriptor_def_const_value(def), &length
  );
  TEST_ASSERT_EQUAL_UINT32(3, length);
  TEST_ASSERT_EQUAL_MEMORY("A\0B", value, 3);
  length = 99;
  TEST_ASSERT_NULL(bebop_descriptor_literal_as_string_n(NULL, &length));
  TEST_ASSERT_EQUAL_UINT32(0, length);
  bebop_descriptor_free(decoded);
  bebop_descriptor_free(descriptor);
}

void test_timestamp_wire_size(void);

void test_timestamp_wire_size(void)
{
  bebop_parse_result_t* result = parse_ok("mut struct Time { value: timestamp; items: timestamp[2]; }");
  const bebop_def_t* def = bebop_result_find(result, "Time");
  TEST_ASSERT_EQUAL_UINT32(16, bebop_type_fixed_size(bebop_field_type(bebop_def_field_at(def, 0))));
  TEST_ASSERT_EQUAL_UINT32(48, bebop_def_fixed_size(def));
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_build_empty_schema);
  RUN_TEST(test_build_struct);
  RUN_TEST(test_build_message);
  RUN_TEST(test_build_enum);
  RUN_TEST(test_build_edition_and_package);

  RUN_TEST(test_source_code_info_basic);
  RUN_TEST(test_source_code_info_doc_comment);
  RUN_TEST(test_source_code_info_line_comment);
  RUN_TEST(test_source_code_info_detached_comments);
  RUN_TEST(test_source_code_info_spans);
  RUN_TEST(test_source_code_info_multiple_defs);
  RUN_TEST(test_source_code_info_enum_members);

  RUN_TEST(test_roundtrip_struct);
  RUN_TEST(test_roundtrip_complex);
  RUN_TEST(test_roundtrip_with_package);
  RUN_TEST(test_roundtrip_service);
  RUN_TEST(test_roundtrip_const);
  RUN_TEST(test_roundtrip_map_type);
  RUN_TEST(test_roundtrip_array_type);
  RUN_TEST(test_roundtrip_embedded_nul);
  RUN_TEST(test_timestamp_wire_size);

  RUN_TEST(test_accessor_schema);
  RUN_TEST(test_accessor_struct_def);
  RUN_TEST(test_accessor_message_def);
  RUN_TEST(test_accessor_enum_def);
  RUN_TEST(test_accessor_union_def);
  RUN_TEST(test_accessor_service_def);
  RUN_TEST(test_accessor_const_def);
  RUN_TEST(test_accessor_type_map);
  RUN_TEST(test_accessor_type_array);
  RUN_TEST(test_accessor_source_code_info);
  RUN_TEST(test_accessor_null_safety);

  RUN_TEST(test_free_null);
  RUN_TEST(test_encode_null);
  RUN_TEST(test_decode_null);

  return UNITY_END();
}
