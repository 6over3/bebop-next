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

void test_builder_create_free(void);

void test_builder_create_free(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);
  TEST_ASSERT_NOT_NULL(b);
  bebop_plugin_response_builder_free(b);
}

void test_builder_null_alloc(void);

void test_builder_null_alloc(void)
{
  TEST_ASSERT_NULL(bebop_plugin_response_builder_create(NULL));
}

void test_builder_add_file(void);

void test_builder_add_file(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);

  bebop_plugin_response_builder_add_file(b, "foo.ts", "export const x = 1;");
  bebop_plugin_response_builder_add_file(b, "bar.ts", "export const y = 2;");

  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_UINT32(2, bebop_plugin_response_file_count(resp));
  TEST_ASSERT_EQUAL_STRING("foo.ts", bebop_plugin_response_file_name(resp, 0));
  TEST_ASSERT_EQUAL_STRING("export const x = 1;", bebop_plugin_response_file_content(resp, 0));
  TEST_ASSERT_EQUAL_STRING("bar.ts", bebop_plugin_response_file_name(resp, 1));
  TEST_ASSERT_NULL(bebop_plugin_response_file_insertion_point(resp, 0));

  bebop_plugin_response_free(resp);
}

void test_builder_add_insertion(void);

void test_builder_add_insertion(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);

  bebop_plugin_response_builder_add_insertion(b, "base.ts", "namespace_scope", "// injected\n");

  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_UINT32(1, bebop_plugin_response_file_count(resp));
  TEST_ASSERT_EQUAL_STRING("base.ts", bebop_plugin_response_file_name(resp, 0));
  TEST_ASSERT_EQUAL_STRING("namespace_scope", bebop_plugin_response_file_insertion_point(resp, 0));
  TEST_ASSERT_EQUAL_STRING("// injected\n", bebop_plugin_response_file_content(resp, 0));

  bebop_plugin_response_free(resp);
}

void test_builder_set_error(void);

void test_builder_set_error(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);

  bebop_plugin_response_builder_set_error(b, "Schema validation failed");

  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_STRING("Schema validation failed", bebop_plugin_response_error(resp));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_response_file_count(resp));

  bebop_plugin_response_free(resp);
}

void test_builder_add_diagnostic(void);

void test_builder_add_diagnostic(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);

  int32_t span1[4] = {10, 5, 10, 20};
  int32_t span2[4] = {0, 0, 0, 0};
  bebop_plugin_response_builder_add_diagnostic(
      b, BEBOP_PLUGIN_SEV_ERROR, "Type mismatch", NULL, "schema.bop", span1
  );
  bebop_plugin_response_builder_add_diagnostic(
      b, BEBOP_PLUGIN_SEV_WARNING, "Deprecated field", NULL, NULL, span2
  );

  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_UINT32(2, bebop_plugin_response_diagnostic_count(resp));

  TEST_ASSERT_EQUAL_INT(BEBOP_PLUGIN_SEV_ERROR, bebop_plugin_response_diagnostic_severity(resp, 0));
  TEST_ASSERT_EQUAL_STRING("Type mismatch", bebop_plugin_response_diagnostic_text(resp, 0));
  TEST_ASSERT_EQUAL_STRING("schema.bop", bebop_plugin_response_diagnostic_file(resp, 0));
  const int32_t* s = bebop_plugin_response_diagnostic_span(resp, 0);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_INT32(10, s[0]);
  TEST_ASSERT_EQUAL_INT32(5, s[1]);

  TEST_ASSERT_EQUAL_INT(
      BEBOP_PLUGIN_SEV_WARNING, bebop_plugin_response_diagnostic_severity(resp, 1)
  );
  TEST_ASSERT_NULL(bebop_plugin_response_diagnostic_file(resp, 1));

  bebop_plugin_response_free(resp);
}

void test_builder_many_files(void);

void test_builder_many_files(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);

  for (int i = 0; i < 100; i++) {
    char name[32];
    snprintf(name, sizeof(name), "file%d.ts", i);
    bebop_plugin_response_builder_add_file(b, name, "content");
  }

  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  TEST_ASSERT_NOT_NULL(resp);
  TEST_ASSERT_EQUAL_UINT32(100, bebop_plugin_response_file_count(resp));
  TEST_ASSERT_EQUAL_STRING("file0.ts", bebop_plugin_response_file_name(resp, 0));
  TEST_ASSERT_EQUAL_STRING("file99.ts", bebop_plugin_response_file_name(resp, 99));

  bebop_plugin_response_free(resp);
}

void test_response_roundtrip_empty(void);

void test_response_roundtrip_empty(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);
  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_encode(ctx, resp, &buf, &len));
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_TRUE(len > 0);

  bebop_plugin_response_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_decode(ctx, buf, len, &decoded));
  TEST_ASSERT_NOT_NULL(decoded);
  TEST_ASSERT_NULL(bebop_plugin_response_error(decoded));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_response_file_count(decoded));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_response_diagnostic_count(decoded));

  bebop_plugin_response_free(decoded);
  bebop_plugin_response_free(resp);
}

void test_response_roundtrip_with_files(void);

void test_response_roundtrip_with_files(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);
  bebop_plugin_response_builder_add_file(b, "output.ts", "// generated\nexport class Foo {}");
  bebop_plugin_response_builder_add_insertion(b, "base.ts", "imports", "import { X } from './x';");
  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_encode(ctx, resp, &buf, &len));

  bebop_plugin_response_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_decode(ctx, buf, len, &decoded));

  TEST_ASSERT_EQUAL_UINT32(2, bebop_plugin_response_file_count(decoded));
  TEST_ASSERT_EQUAL_STRING("output.ts", bebop_plugin_response_file_name(decoded, 0));
  TEST_ASSERT_EQUAL_STRING(
      "// generated\nexport class Foo {}", bebop_plugin_response_file_content(decoded, 0)
  );
  TEST_ASSERT_NULL(bebop_plugin_response_file_insertion_point(decoded, 0));

  TEST_ASSERT_EQUAL_STRING("base.ts", bebop_plugin_response_file_name(decoded, 1));
  TEST_ASSERT_EQUAL_STRING("imports", bebop_plugin_response_file_insertion_point(decoded, 1));

  bebop_plugin_response_free(decoded);
  bebop_plugin_response_free(resp);
}

void test_response_roundtrip_with_error(void);

void test_response_roundtrip_with_error(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);
  bebop_plugin_response_builder_set_error(b, "Unsupported feature: recursive types");
  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_encode(ctx, resp, &buf, &len));

  bebop_plugin_response_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_decode(ctx, buf, len, &decoded));

  TEST_ASSERT_EQUAL_STRING(
      "Unsupported feature: recursive types", bebop_plugin_response_error(decoded)
  );

  bebop_plugin_response_free(decoded);
  bebop_plugin_response_free(resp);
}

void test_response_roundtrip_with_diagnostics(void);

void test_response_roundtrip_with_diagnostics(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);
  int32_t span[4] = {5, 10, 5, 25};
  bebop_plugin_response_builder_add_diagnostic(
      b, BEBOP_PLUGIN_SEV_WARNING, "Naming convention", NULL, "test.bop", span
  );
  bebop_plugin_response_builder_add_file(b, "out.ts", "content");
  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_encode(ctx, resp, &buf, &len));

  bebop_plugin_response_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_response_decode(ctx, buf, len, &decoded));

  TEST_ASSERT_EQUAL_UINT32(1, bebop_plugin_response_diagnostic_count(decoded));
  TEST_ASSERT_EQUAL_INT(
      BEBOP_PLUGIN_SEV_WARNING, bebop_plugin_response_diagnostic_severity(decoded, 0)
  );
  TEST_ASSERT_EQUAL_STRING("Naming convention", bebop_plugin_response_diagnostic_text(decoded, 0));
  TEST_ASSERT_EQUAL_STRING("test.bop", bebop_plugin_response_diagnostic_file(decoded, 0));

  const int32_t* s = bebop_plugin_response_diagnostic_span(decoded, 0);
  TEST_ASSERT_EQUAL_INT32(5, s[0]);
  TEST_ASSERT_EQUAL_INT32(10, s[1]);
  TEST_ASSERT_EQUAL_INT32(5, s[2]);
  TEST_ASSERT_EQUAL_INT32(25, s[3]);

  bebop_plugin_response_free(decoded);
  bebop_plugin_response_free(resp);
}

static bebop_parse_result_t* parse_ok(const char* source)
{
  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_TRUE(status == BEBOP_OK || status == BEBOP_OK_WITH_WARNINGS);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
  return result;
}

void test_request_roundtrip(void);

void test_request_roundtrip(void)
{
  bebop_parse_result_t* result = parse_ok("struct Point { x: int32; y: int32; }\n");
  bebop_descriptor_t* desc = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_descriptor_build(result, BEBOP_DESC_FLAG_NONE, &desc));

  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_request_builder_t* b = bebop_plugin_request_builder_create(&alloc);
  TEST_ASSERT_NOT_NULL(b);

  bebop_plugin_request_builder_add_file(b, "test.bop");
  bebop_plugin_request_builder_add_file(b, "other.bop");
  bebop_plugin_request_builder_set_parameter(b, "lang=ts,style=modern");
  bebop_plugin_request_builder_set_version(b, (bebop_version_t) {1, 2, 3, ""});
  bebop_plugin_request_builder_set_descriptor(b, desc);

  bebop_plugin_request_t* req = bebop_plugin_request_builder_finish(b);
  TEST_ASSERT_NOT_NULL(req);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_request_encode(ctx, req, &buf, &len));
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_TRUE(len > 0);

  bebop_plugin_request_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_request_decode(ctx, buf, len, &decoded));
  TEST_ASSERT_NOT_NULL(decoded);

  TEST_ASSERT_EQUAL_UINT32(2, bebop_plugin_request_file_count(decoded));
  TEST_ASSERT_EQUAL_STRING("test.bop", bebop_plugin_request_file_at(decoded, 0));
  TEST_ASSERT_EQUAL_STRING("other.bop", bebop_plugin_request_file_at(decoded, 1));

  TEST_ASSERT_EQUAL_STRING("lang=ts,style=modern", bebop_plugin_request_parameter(decoded));

  bebop_version_t ver = bebop_plugin_request_compiler_version(decoded);
  TEST_ASSERT_EQUAL_INT32(1, ver.major);
  TEST_ASSERT_EQUAL_INT32(2, ver.minor);
  TEST_ASSERT_EQUAL_INT32(3, ver.patch);

  TEST_ASSERT_EQUAL_UINT32(1, bebop_plugin_request_schema_count(decoded));

  bebop_plugin_request_free(decoded);
  bebop_plugin_request_free(req);
  bebop_descriptor_free(desc);
}

void test_request_minimal(void);

void test_request_minimal(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_request_builder_t* b = bebop_plugin_request_builder_create(&alloc);
  TEST_ASSERT_NOT_NULL(b);

  bebop_plugin_request_builder_set_version(b, (bebop_version_t) {0, 0, 0, ""});

  bebop_plugin_request_t* req = bebop_plugin_request_builder_finish(b);
  TEST_ASSERT_NOT_NULL(req);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_request_encode(ctx, req, &buf, &len));

  bebop_plugin_request_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_request_decode(ctx, buf, len, &decoded));

  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_request_file_count(decoded));
  TEST_ASSERT_NULL(bebop_plugin_request_parameter(decoded));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_request_host_option_count(decoded));

  bebop_plugin_request_free(decoded);
  bebop_plugin_request_free(req);
}

void test_request_with_host_options(void);

void test_request_with_host_options(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_request_builder_t* b = bebop_plugin_request_builder_create(&alloc);
  TEST_ASSERT_NOT_NULL(b);

  bebop_plugin_request_builder_set_version(b, (bebop_version_t) {1, 0, 0, ""});
  bebop_plugin_request_builder_add_option(b, "namespace", "MyApp");
  bebop_plugin_request_builder_add_option(b, "emit_source_info", "true");
  bebop_plugin_request_builder_add_option(b, "optimize", "speed");

  bebop_plugin_request_t* req = bebop_plugin_request_builder_finish(b);
  TEST_ASSERT_NOT_NULL(req);

  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_request_encode(ctx, req, &buf, &len));

  bebop_plugin_request_t* decoded = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_OK, bebop_plugin_request_decode(ctx, buf, len, &decoded));

  TEST_ASSERT_EQUAL_UINT32(3, bebop_plugin_request_host_option_count(decoded));
  bebop_plugin_option_iterator_t options = bebop_plugin_request_host_options(decoded);
  const char* key;
  const char* value;
  TEST_ASSERT_TRUE(bebop_plugin_option_next(&options, &key, &value));
  TEST_ASSERT_EQUAL_STRING("namespace", key);
  TEST_ASSERT_EQUAL_STRING("MyApp", value);
  TEST_ASSERT_TRUE(bebop_plugin_option_next(&options, &key, &value));
  TEST_ASSERT_EQUAL_STRING("emit_source_info", key);
  TEST_ASSERT_EQUAL_STRING("true", value);
  TEST_ASSERT_TRUE(bebop_plugin_option_next(&options, &key, &value));
  TEST_ASSERT_EQUAL_STRING("optimize", key);
  TEST_ASSERT_EQUAL_STRING("speed", value);
  TEST_ASSERT_FALSE(bebop_plugin_option_next(&options, &key, &value));

  bebop_plugin_request_free(decoded);
  bebop_plugin_request_free(req);
}

void test_request_accessors_null(void);

void test_request_accessors_null(void)
{
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_request_file_count(NULL));
  TEST_ASSERT_NULL(bebop_plugin_request_file_at(NULL, 0));
  TEST_ASSERT_NULL(bebop_plugin_request_parameter(NULL));

  bebop_version_t v = bebop_plugin_request_compiler_version(NULL);
  TEST_ASSERT_EQUAL_INT32(0, v.major);

  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_request_schema_count(NULL));
  TEST_ASSERT_NULL(bebop_plugin_request_schema_at(NULL, 0));

  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_request_host_option_count(NULL));
  bebop_plugin_option_iterator_t options = bebop_plugin_request_host_options(NULL);
  TEST_ASSERT_FALSE(bebop_plugin_option_next(&options, NULL, NULL));
}

void test_response_accessors_null(void);

void test_response_accessors_null(void)
{
  TEST_ASSERT_NULL(bebop_plugin_response_error(NULL));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_response_file_count(NULL));
  TEST_ASSERT_NULL(bebop_plugin_response_file_name(NULL, 0));
  TEST_ASSERT_NULL(bebop_plugin_response_file_insertion_point(NULL, 0));
  TEST_ASSERT_NULL(bebop_plugin_response_file_content(NULL, 0));
  TEST_ASSERT_EQUAL_UINT32(0, bebop_plugin_response_diagnostic_count(NULL));
  TEST_ASSERT_EQUAL_INT(BEBOP_PLUGIN_SEV_ERROR, bebop_plugin_response_diagnostic_severity(NULL, 0));
  TEST_ASSERT_NULL(bebop_plugin_response_diagnostic_text(NULL, 0));
  TEST_ASSERT_NULL(bebop_plugin_response_diagnostic_file(NULL, 0));
  TEST_ASSERT_NULL(bebop_plugin_response_diagnostic_span(NULL, 0));
}

void test_response_accessors_bounds(void);

void test_response_accessors_bounds(void)
{
  bebop_host_allocator_t alloc = test_allocator();
  bebop_plugin_response_builder_t* b = bebop_plugin_response_builder_create(&alloc);
  bebop_plugin_response_builder_add_file(b, "a.ts", "x");
  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);

  TEST_ASSERT_NULL(bebop_plugin_response_file_name(resp, 99));
  TEST_ASSERT_NULL(bebop_plugin_response_file_content(resp, 99));
  TEST_ASSERT_NULL(bebop_plugin_response_diagnostic_text(resp, 0));

  bebop_plugin_response_free(resp);
}

void test_free_null(void);

void test_free_null(void)
{
  bebop_plugin_request_free(NULL);
  bebop_plugin_response_free(NULL);
  bebop_plugin_response_builder_free(NULL);
}

void test_encode_null(void);

void test_encode_null(void)
{
  const uint8_t* buf = NULL;
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(BEBOP_FATAL, bebop_plugin_request_encode(ctx, NULL, &buf, &len));
  TEST_ASSERT_EQUAL_INT(BEBOP_FATAL, bebop_plugin_response_encode(ctx, NULL, &buf, &len));
}

void test_decode_null(void);

void test_decode_null(void)
{
  bebop_plugin_request_t* req = NULL;
  bebop_plugin_response_t* resp = NULL;
  TEST_ASSERT_EQUAL_INT(BEBOP_FATAL, bebop_plugin_request_decode(ctx, NULL, 0, &req));
  TEST_ASSERT_EQUAL_INT(BEBOP_FATAL, bebop_plugin_response_decode(ctx, NULL, 0, &resp));
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_builder_create_free);
  RUN_TEST(test_builder_null_alloc);
  RUN_TEST(test_builder_add_file);
  RUN_TEST(test_builder_add_insertion);
  RUN_TEST(test_builder_set_error);
  RUN_TEST(test_builder_add_diagnostic);
  RUN_TEST(test_builder_many_files);

  RUN_TEST(test_response_roundtrip_empty);
  RUN_TEST(test_response_roundtrip_with_files);
  RUN_TEST(test_response_roundtrip_with_error);
  RUN_TEST(test_response_roundtrip_with_diagnostics);

  RUN_TEST(test_request_roundtrip);
  RUN_TEST(test_request_minimal);
  RUN_TEST(test_request_with_host_options);

  RUN_TEST(test_request_accessors_null);
  RUN_TEST(test_response_accessors_null);
  RUN_TEST(test_response_accessors_bounds);

  RUN_TEST(test_free_null);
  RUN_TEST(test_encode_null);
  RUN_TEST(test_decode_null);

  return UNITY_END();
}
