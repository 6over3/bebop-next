#include "bebop.c"
#include "test_common.h"
#include "unity.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

#define FIXTURE(name) BEBOP_TEST_FIXTURES_DIR "/valid/" name

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

static bebop_parse_result_t* parse_expect_success(const char* source)
{
  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);

  if (status != BEBOP_OK || bebop_result_error_count(result) > 0) {
    uint32_t count = bebop_result_diagnostic_count(result);
    for (uint32_t i = 0; i < count; i++) {
      const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
      fprintf(
          stderr,
          "[%s] %u:%u: %s\n",
          bebop_diag_severity_name(bebop_diagnostic_severity(diag)),
          bebop_diagnostic_span(diag).start_line,
          bebop_diagnostic_span(diag).start_col,
          bebop_diagnostic_message(diag)
      );
    }
    TEST_FAIL_MESSAGE("Parse failed unexpectedly");
  }

  TEST_ASSERT_NOT_NULL(result);
  return result;
}

static bebop_parse_result_t* parse_fixture(const char* path)
{
  bebop_parse_result_t* result = NULL;
  const char* paths[] = {path};
  bebop_status_t status = bebop_parse(ctx, paths, 1, &result);

  if (status != BEBOP_OK || bebop_result_error_count(result) > 0) {
    fprintf(stderr, "\nFixture '%s' failed (status=%d):\n", path, status);
    uint32_t count = bebop_result_diagnostic_count(result);
    for (uint32_t i = 0; i < count; i++) {
      const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
      fprintf(
          stderr,
          "  [%s] %u:%u: %s\n",
          bebop_diag_severity_name(bebop_diagnostic_severity(diag)),
          bebop_diagnostic_span(diag).start_line,
          bebop_diagnostic_span(diag).start_col,
          bebop_diagnostic_message(diag)
      );
    }
    TEST_FAIL_MESSAGE("Fixture parse failed");
  }

  TEST_ASSERT_NOT_NULL(result);
  return result;
}

typedef struct {
  uint32_t line;
  uint32_t col;
  const char* message_substr;
} expected_diag_t;

static void parse_expect_diagnostics(
    const char* source, const expected_diag_t* expected, uint32_t expected_count
)
{
  bebop_parse_result_t* result = NULL;
  bebop_parse_source(ctx, BEBOP_SOURCE(source, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE_MESSAGE(
      bebop_result_error_count(result) > 0, "Expected parse errors but got none"
  );

  uint32_t diag_count = bebop_result_diagnostic_count(result);

  fprintf(stderr, "\n--- Diagnostics (%u) ---\n", diag_count);
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
    bebop_span_t s = bebop_diagnostic_span(d);
    fprintf(
        stderr,
        "  [%u] %s line %u, col %u: %s\n",
        i,
        bebop_diag_severity_name(bebop_diagnostic_severity(d)),
        s.start_line,
        s.start_col,
        bebop_diagnostic_message(d)
    );
  }
  fprintf(stderr, "---\n");

  if (diag_count < expected_count) {
    char msg[256];
    snprintf(
        msg, sizeof(msg), "Expected at least %u diagnostics, got %u", expected_count, diag_count
    );
    TEST_FAIL_MESSAGE(msg);
  }

  for (uint32_t i = 0; i < expected_count; i++) {
    const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
    TEST_ASSERT_NOT_NULL(diag);

    bebop_span_t span = bebop_diagnostic_span(diag);
    const char* msg = bebop_diagnostic_message(diag);
    char err[512];

    snprintf(
        err,
        sizeof(err),
        "Diag %u: expected line %u, got %u (msg: %s)",
        i,
        expected[i].line,
        span.start_line,
        msg ? msg : "(null)"
    );
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i].line, span.start_line, err);

    if (expected[i].col > 0) {
      snprintf(
          err,
          sizeof(err),
          "Diag %u: expected col %u, got %u (msg: %s)",
          i,
          expected[i].col,
          span.start_col,
          msg ? msg : "(null)"
      );
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i].col, span.start_col, err);
    }

    if (expected[i].message_substr && msg) {
      if (!strstr(msg, expected[i].message_substr)) {
        snprintf(
            err,
            sizeof(err),
            "Diag %u: expected message containing '%s', got '%s'",
            i,
            expected[i].message_substr,
            msg
        );
        TEST_FAIL_MESSAGE(err);
      }
    }
  }
}

static bebop_schema_t* get_schema(bebop_parse_result_t* result)
{
  TEST_ASSERT_EQUAL_UINT32(1, bebop_result_schema_count(result));
  const bebop_schema_t* s = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(s);
  return BEBOP_DISCARD_CONST(bebop_schema_t*, s);
}

static uint32_t count_decorator_defs(bebop_schema_t* schema)
{
  uint32_t count = 0;
  for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
    if (schema->sorted_defs[i]->kind == BEBOP_DEF_DECORATOR) {
      count++;
    }
  }
  return count;
}

static bebop_def_t* get_decorator_def(bebop_schema_t* schema, uint32_t idx)
{
  uint32_t count = 0;
  for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
    if (schema->sorted_defs[i]->kind == BEBOP_DEF_DECORATOR) {
      if (count == idx) {
        return schema->sorted_defs[i];
      }
      count++;
    }
  }
  return NULL;
}

static const char* get_validate_source(bebop_schema_t* schema, bebop_def_t* def)
{
  if (def->decorator_def.validate_span.len == 0) {
    return NULL;
  }
  return schema->source + def->decorator_def.validate_span.off;
}

static const char* get_export_source(bebop_schema_t* schema, bebop_def_t* def)
{
  if (def->decorator_def.export_span.len == 0) {
    return NULL;
  }
  return schema->source + def->decorator_def.export_span.off;
}

void test_macro_minimal(void);
void test_macro_targets_single(void);
void test_macro_targets_combined(void);
void test_macro_targets_all(void);
void test_macro_targets_branch(void);
void test_macro_multiple_true(void);
void test_macro_multiple_false(void);
void test_macro_param_required(void);
void test_macro_param_optional(void);
void test_macro_param_default_value(void);
void test_macro_param_in_constraint(void);
void test_macro_param_multiple(void);
void test_macro_validate_block(void);
void test_macro_export_block(void);
void test_macro_validate_and_export(void);
void test_macro_no_validate_no_export(void);
void test_macro_description(void);
void test_macro_param_description(void);
void test_macro_multiple_decorators(void);
void test_macro_coexists_with_definitions(void);

void test_macro_error_missing_name(void);
void test_macro_error_missing_lparen(void);
void test_macro_error_missing_rparen(void);
void test_macro_error_missing_lbrace(void);
void test_macro_error_missing_targets(void);
void test_macro_error_bad_target(void);
void test_macro_error_missing_bang_or_question(void);
void test_macro_error_bad_param_type(void);
void test_macro_error_unterminated_raw_block(void);
void test_macro_error_missing_raw_block(void);
void test_macro_error_unknown_body_keyword(void);
void test_macro_error_param_missing_colon(void);
void test_macro_error_param_missing_type(void);
void test_macro_error_invalid_in_constraint(void);
void test_macro_error_empty_body(void);
void test_macro_error_multiple_errors(void);
void test_macro_error_duplicate_same_file(void);
void test_macro_error_duplicate_cross_schema(void);
void test_macro_error_decorator_conflicts_with_def_cross_schema(void);
void test_macro_error_def_conflicts_with_decorator_cross_schema(void);

void test_lua_wrap_no_params(void);
void test_lua_wrap_with_params(void);
void test_lua_wrap_multiline(void);
void test_lua_wrap_null_source(void);
void test_lua_wrap_decorator_validate(void);
void test_lua_wrap_decorator_export(void);

void test_lua_eval_state_lifecycle(void);
void test_lua_eval_sandbox_safe(void);
void test_lua_eval_sandbox_blocks_unsafe(void);
void test_lua_eval_constants(void);
void test_lua_eval_error_emits_diagnostic(void);
void test_lua_eval_error_with_span(void);
void test_lua_eval_warn_emits_diagnostic(void);
void test_lua_eval_warn_with_span(void);
void test_lua_eval_validate_pass(void);
void test_lua_eval_validate_fail(void);
void test_lua_eval_validate_with_params(void);
void test_lua_eval_export_returns_table(void);
void test_lua_eval_export_captures_data(void);
void test_lua_eval_export_non_string_key(void);
void test_lua_eval_export_non_literal_value(void);
void test_lua_eval_export_no_export_block(void);
void test_lua_eval_bit_library(void);
void test_lua_eval_helpers(void);
void test_lua_compile_syntax_error(void);

void test_macro_minimal(void)
{
  const char* src = "#decorator(opcode) {\n" "    targets = STRUCT\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);

  TEST_ASSERT_EQUAL_UINT32(1, count_decorator_defs(schema));
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_NOT_NULL(def);

  const char* name = BEBOP_STR(ctx, def->name);
  TEST_ASSERT_EQUAL_STRING("opcode", name);
  TEST_ASSERT_EQUAL(BEBOP_TARGET_STRUCT, def->decorator_def.targets);
  TEST_ASSERT_FALSE(def->decorator_def.allow_multiple);
  TEST_ASSERT_EQUAL_UINT32(0, def->decorator_def.param_count);
  TEST_ASSERT_NULL(get_validate_source(schema, def));
  TEST_ASSERT_NULL(get_export_source(schema, def));
}

void test_macro_targets_single(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = FIELD\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_EQUAL(BEBOP_TARGET_FIELD, def->decorator_def.targets);
}

void test_macro_targets_combined(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = STRUCT | MESSAGE | UNION\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  bebop_decorator_target_t expected =
      BEBOP_TARGET_STRUCT | BEBOP_TARGET_MESSAGE | BEBOP_TARGET_UNION;
  TEST_ASSERT_EQUAL(expected, def->decorator_def.targets);
}

void test_macro_targets_all(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = ALL\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_EQUAL(BEBOP_TARGET_ALL, def->decorator_def.targets);
}

void test_macro_targets_branch(void)
{
  const char* src = "#decorator(branch_tag) {\n" "    targets = BRANCH\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_EQUAL(BEBOP_TARGET_BRANCH, def->decorator_def.targets);
}

void test_macro_multiple_true(void)
{
  const char* src = "#decorator(tag) {\n" "    targets = FIELD\n" "    multiple = true\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_TRUE(def->decorator_def.allow_multiple);
}

void test_macro_multiple_false(void)
{
  const char* src = "#decorator(tag) {\n" "    targets = FIELD\n" "    multiple = false\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_FALSE(def->decorator_def.allow_multiple);
}

void test_macro_param_required(void)
{
  const char* src = "#decorator(opcode) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  TEST_ASSERT_EQUAL_UINT32(1, def->decorator_def.param_count);
  bebop_macro_param_def_t* param = &def->decorator_def.params[0];

  TEST_ASSERT_EQUAL_STRING("value", BEBOP_STR(ctx, param->name));
  TEST_ASSERT_TRUE(param->required);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_UINT32, param->type);
  TEST_ASSERT_NULL(param->default_value);
  TEST_ASSERT_EQUAL_UINT32(0, param->allowed_value_count);
}

void test_macro_param_optional(void)
{
  const char* src = "#decorator(note) {\n"
                      "    targets = ALL\n"
                      "    param message?: string\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  bebop_macro_param_def_t* param = &def->decorator_def.params[0];

  TEST_ASSERT_EQUAL_STRING("message", BEBOP_STR(ctx, param->name));
  TEST_ASSERT_FALSE(param->required);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_STRING, param->type);
}

void test_macro_param_default_value(void)
{
  const char* src = "#decorator(priority) {\n"
                      "    targets = FIELD\n"
                      "    param level?: uint32 = 5\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  bebop_macro_param_def_t* param = &def->decorator_def.params[0];

  TEST_ASSERT_FALSE(param->required);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_UINT32, param->type);
  TEST_ASSERT_NOT_NULL(param->default_value);
  TEST_ASSERT_EQUAL_INT64(5, param->default_value->int_val);
}

void test_macro_param_in_constraint(void)
{
  const char* src = "#decorator(http) {\n"
                      "    targets = METHOD\n"
                      "    param method!: string in [\"GET\", \"POST\", \"PUT\", \"DELETE\"]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  bebop_macro_param_def_t* param = &def->decorator_def.params[0];

  TEST_ASSERT_TRUE(param->required);
  TEST_ASSERT_EQUAL(BEBOP_TYPE_STRING, param->type);
  TEST_ASSERT_EQUAL_UINT32(4, param->allowed_value_count);

  TEST_ASSERT_EQUAL_STRING("GET", BEBOP_STR(ctx, param->allowed_values[0].string_val));
  TEST_ASSERT_EQUAL_STRING("POST", BEBOP_STR(ctx, param->allowed_values[1].string_val));
  TEST_ASSERT_EQUAL_STRING("PUT", BEBOP_STR(ctx, param->allowed_values[2].string_val));
  TEST_ASSERT_EQUAL_STRING("DELETE", BEBOP_STR(ctx, param->allowed_values[3].string_val));
}

void test_macro_param_multiple(void)
{
  const char* src = "#decorator(range) {\n"
                      "    targets = FIELD\n"
                      "    param min?: float64\n"
                      "    param max?: float64\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  TEST_ASSERT_EQUAL_UINT32(2, def->decorator_def.param_count);
  TEST_ASSERT_EQUAL_STRING("min", BEBOP_STR(ctx, def->decorator_def.params[0].name));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FLOAT64, def->decorator_def.params[0].type);
  TEST_ASSERT_FALSE(def->decorator_def.params[0].required);
  TEST_ASSERT_EQUAL_STRING("max", BEBOP_STR(ctx, def->decorator_def.params[1].name));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FLOAT64, def->decorator_def.params[1].type);
  TEST_ASSERT_FALSE(def->decorator_def.params[1].required);
}

void test_macro_validate_block(void)
{
  const char* src = "#decorator(opcode) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32\n"
                      "    validate [[\n"
                      "        if value <= 0 then\n"
                      "            error(\"opcode must be positive\")\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  const char* val_src = get_validate_source(schema, def);
  TEST_ASSERT_NOT_NULL(val_src);
  TEST_ASSERT_TRUE(def->decorator_def.validate_span.len > 0);

  TEST_ASSERT_NOT_NULL(strstr(val_src, "if value <= 0 then"));

  TEST_ASSERT_TRUE(def->decorator_def.validate_span.start_line >= 4);
}

void test_macro_export_block(void)
{
  const char* src = "#decorator(bitfield) {\n"
                      "    targets = ENUM\n"
                      "    export [[\n"
                      "        return { combined = 42 }\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  TEST_ASSERT_NULL(get_validate_source(schema, def));
  const char* exp_src = get_export_source(schema, def);
  TEST_ASSERT_NOT_NULL(exp_src);
  TEST_ASSERT_TRUE(def->decorator_def.export_span.len > 0);
  TEST_ASSERT_NOT_NULL(strstr(exp_src, "return { combined = 42 }"));
}

void test_macro_validate_and_export(void)
{
  const char* src = "#decorator(range) {\n"
                      "    targets = FIELD\n"
                      "    param min?: float64\n"
                      "    param max?: float64\n"
                      "    validate [[\n"
                      "        if not min and not max then\n"
                      "            error(\"need min or max\")\n"
                      "        end\n"
                      "    ]]\n"
                      "    export [[\n"
                      "        return { expr = \"x >= \" .. min }\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  const char* val_src = get_validate_source(schema, def);
  const char* exp_src = get_export_source(schema, def);
  TEST_ASSERT_NOT_NULL(val_src);
  TEST_ASSERT_NOT_NULL(exp_src);
  TEST_ASSERT_NOT_NULL(strstr(val_src, "if not min and not max then"));
  TEST_ASSERT_NOT_NULL(strstr(exp_src, "return { expr ="));
}

void test_macro_no_validate_no_export(void)
{
  const char* src = "#decorator(tag) {\n" "    targets = FIELD\n" "    param name!: string\n" "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  TEST_ASSERT_NULL(get_validate_source(schema, def));
  TEST_ASSERT_NULL(get_export_source(schema, def));
  TEST_ASSERT_EQUAL_UINT32(1, def->decorator_def.param_count);
}

void test_macro_description(void)
{
  const char* src = "/// Assigns a unique wire protocol identifier\n"
                      "#decorator(opcode) {\n"
                      "    targets = STRUCT\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_NOT_NULL(def);

  TEST_ASSERT_FALSE(bebop_str_is_null(def->documentation));
  const char* desc = BEBOP_STR(ctx, def->documentation);
  TEST_ASSERT_NOT_NULL(strstr(desc, "Assigns a unique wire protocol identifier"));
}

void test_macro_param_description(void)
{
  const char* src = "#decorator(opcode) {\n"
                      "    targets = STRUCT\n"
                      "    /// The opcode value (1-16777215)\n"
                      "    param value!: uint32\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);
  TEST_ASSERT_NOT_NULL(def);
  bebop_macro_param_def_t* param = &def->decorator_def.params[0];

  TEST_ASSERT_FALSE(bebop_str_is_null(param->description));
  const char* desc = BEBOP_STR(ctx, param->description);
  TEST_ASSERT_NOT_NULL(strstr(desc, "The opcode value"));
}

void test_macro_multiple_decorators(void)
{
  const char* src = "#decorator(min) {\n"
                      "    targets = FIELD\n"
                      "    param value!: float64\n"
                      "}\n"
                      "\n"
                      "#decorator(max) {\n"
                      "    targets = FIELD\n"
                      "    param value!: float64\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);

  TEST_ASSERT_EQUAL_UINT32(2, count_decorator_defs(schema));
  bebop_def_t* d0 = get_decorator_def(schema, 0);
  bebop_def_t* d1 = get_decorator_def(schema, 1);
  TEST_ASSERT_EQUAL_STRING("min", BEBOP_STR(ctx, d0->name));
  TEST_ASSERT_EQUAL_STRING("max", BEBOP_STR(ctx, d1->name));
}

void test_macro_coexists_with_definitions(void)
{
  const char* src = "#decorator(opcode) {\n"
                      "    targets = STRUCT | MESSAGE\n"
                      "    param value!: uint32\n"
                      "}\n"
                      "\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);

  TEST_ASSERT_EQUAL_UINT32(1, count_decorator_defs(schema));
  bebop_def_t* dec = get_decorator_def(schema, 0);
  TEST_ASSERT_EQUAL_STRING("opcode", BEBOP_STR(ctx, dec->name));

  TEST_ASSERT_EQUAL_UINT32(2, schema->definition_count);
}

void test_macro_error_missing_name(void)
{
  const char* src = "#decorator() {\n" "    targets = STRUCT\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 12, .message_substr = "name"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_missing_lparen(void)
{
  const char* src = "#decorator foo {\n" "    targets = STRUCT\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 2, .message_substr = "'('"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_missing_rparen(void)
{
  const char* src = "#decorator(foo {\n" "    targets = STRUCT\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 12, .message_substr = "')'"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_missing_lbrace(void)
{
  const char* src = "#decorator(foo)\n" "    targets = STRUCT\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .message_substr = "'{'"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_missing_targets(void)
{
  const char* src = "#decorator(foo) {\n" "    param x!: uint32\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 12, .message_substr = "targets"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_bad_target(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = UNKNOWN\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 2, .message_substr = "Unknown target"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_missing_bang_or_question(void)
{
  const char* src = "#decorator(foo) {\n"
                      "    targets = STRUCT\n"
                      "    param value: uint32\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .message_substr = "(required) or '?' (optional)"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_bad_param_type(void)
{
  const char* src = "#decorator(foo) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: widget\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .message_substr = "type"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_unterminated_raw_block(void)
{
  const char* src = "#decorator(foo) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        if true then end\n";

  expected_diag_t expected[] = {
      {.line = 3, .col = 14, .message_substr = "]]"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_missing_raw_block(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = STRUCT\n" "    validate something\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .message_substr = "'[['"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_unknown_body_keyword(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = STRUCT\n" "    foobar = thing\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .col = 5, .message_substr = "Unknown decorator body item"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_param_missing_colon(void)
{
  const char* src = "#decorator(foo) {\n"
                      "    targets = STRUCT\n"
                      "    param value! uint32\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .message_substr = "':'"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_param_missing_type(void)
{
  const char* src = "#decorator(foo) {\n" "    targets = STRUCT\n" "    param value!:\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 4, .message_substr = "type"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_invalid_in_constraint(void)
{
  const char* src = "#decorator(foo) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32 in 1, 2, 3\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .message_substr = "'['"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_empty_body(void)
{
  const char* src = "#decorator(foo) {\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 12, .message_substr = "targets"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_multiple_errors(void)
{
  const char* src = "#decorator() {\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 12, .message_substr = "name"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_duplicate_same_file(void)
{
  const char* src = "#decorator(my_check) {\n"
                      "    targets = STRUCT\n"
                      "}\n"
                      "#decorator(my_check) {\n"
                      "    targets = MESSAGE\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 4, .col = 12, .message_substr = "is already defined"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_macro_error_duplicate_cross_schema(void)
{
  const char* sources[] = {
        "#decorator(shared) {\n"
        "    targets = STRUCT\n"
        "}\n",
        "#decorator(shared) {\n"
        "    targets = MESSAGE\n"
        "}\n",
    };
  const bebop_source_t bop_srcs[] = {
      {sources[0], strlen(sources[0]), "a.bop"},
      {sources[1], strlen(sources[1]), "b.bop"},
  };

  bebop_parse_result_t* result = NULL;
  bebop_parse_sources(ctx, bop_srcs, 2, &result);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);

  uint32_t diag_count = bebop_result_diagnostic_count(result);
  bool found = false;
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
    const char* msg = bebop_diagnostic_message(d);
    if (msg && strstr(msg, "defined in multiple schemas")) {
      found = true;

      TEST_ASSERT_EQUAL_UINT32(1, bebop_diagnostic_span(d).start_line);
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Expected cross-schema duplicate decorator diagnostic");
}

void test_macro_error_decorator_conflicts_with_def_cross_schema(void)
{
  const char* sources[] = {
        "struct Foo {\n"
        "    x: int32;\n"
        "}\n",
        "#decorator(Foo) {\n"
        "    targets = STRUCT\n"
        "}\n",
    };
  const bebop_source_t bop_srcs[] = {
      {sources[0], strlen(sources[0]), "types.bop"},
      {sources[1], strlen(sources[1]), "decorators.bop"},
  };

  bebop_parse_result_t* result = NULL;
  bebop_parse_sources(ctx, bop_srcs, 2, &result);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);

  uint32_t diag_count = bebop_result_diagnostic_count(result);
  bool found = false;
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
    const char* msg = bebop_diagnostic_message(d);
    if (msg && strstr(msg, "defined in multiple schemas")) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(
      found, "Expected cross-schema decorator-vs-definition conflict diagnostic"
  );
}

void test_macro_error_def_conflicts_with_decorator_cross_schema(void)
{
  const char* sources[] = {
        "#decorator(Bar) {\n"
        "    targets = MESSAGE\n"
        "}\n",
        "enum Bar {\n"
        "    A = 0;\n"
        "    B = 1;\n"
        "}\n",
    };
  const bebop_source_t bop_srcs[] = {
      {sources[0], strlen(sources[0]), "decorators.bop"},
      {sources[1], strlen(sources[1]), "types.bop"},
  };

  bebop_parse_result_t* result = NULL;
  bebop_parse_sources(ctx, bop_srcs, 2, &result);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);

  uint32_t diag_count = bebop_result_diagnostic_count(result);
  bool found = false;
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
    const char* msg = bebop_diagnostic_message(d);
    if (msg && strstr(msg, "defined in multiple schemas")) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(
      found, "Expected cross-schema definition-vs-decorator conflict diagnostic"
  );
}

void test_lua_wrap_no_params(void)
{
  const char* source = "print('hello')";

  const char* result =
      bebop__lua_wrap_function(&ctx->arena, (bebop__str_view_t) {source, strlen(source)}, NULL, 0);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_EQUAL_STRING("return function()\nprint('hello')\nend\n", result);
}

void test_lua_wrap_with_params(void)
{
  const char* source = "return self.name";
  const char* params[] = {"self", "target", "min", "max"};

  const char* result = bebop__lua_wrap_function(
      &ctx->arena, (bebop__str_view_t) {source, strlen(source)}, params, 4
  );
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_EQUAL_STRING(
      "return function(self, target, min, max)\nreturn self.name\nend\n", result
  );
}

void test_lua_wrap_multiline(void)
{
  const char* source = "if x > 0 then\n" "    return true\n" "end";
  const char* params[] = {"x"};

  const char* result = bebop__lua_wrap_function(
      &ctx->arena, (bebop__str_view_t) {source, strlen(source)}, params, 1
  );
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_NOT_NULL(strstr(result, "return function(x)\n"));
  TEST_ASSERT_NOT_NULL(strstr(result, "if x > 0 then\n"));
  TEST_ASSERT_NOT_NULL(strstr(result, "\nend\n"));
}

void test_lua_wrap_null_source(void)
{
  const char* params[] = {"self"};

  const char* result =
      bebop__lua_wrap_function(&ctx->arena, (bebop__str_view_t) {NULL, 0}, params, 1);
  TEST_ASSERT_NULL(result);

  result = bebop__lua_wrap_function(&ctx->arena, (bebop__str_view_t) {"x", 0}, params, 1);
  TEST_ASSERT_NULL(result);
}

void test_lua_wrap_decorator_validate(void)
{
  const char* src = "#decorator(range) {\n"
                      "    targets = FIELD\n"
                      "    param min!: float64\n"
                      "    param max?: float64\n"
                      "    validate [[\n"
                      "        if min > max then error(\"invalid\") end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  const char* val_src = get_validate_source(schema, def);
  TEST_ASSERT_NOT_NULL(val_src);

  const char* params[] = {"self", "target", "min", "max"};
  const char* wrapped = bebop__lua_wrap_function(
      &ctx->arena, (bebop__str_view_t) {val_src, def->decorator_def.validate_span.len}, params, 4
  );

  TEST_ASSERT_NOT_NULL(wrapped);
  TEST_ASSERT_NOT_NULL(strstr(wrapped, "return function(self, target, min, max)\n"));
  TEST_ASSERT_NOT_NULL(strstr(wrapped, "if min > max then error(\"invalid\") end"));
  TEST_ASSERT_NOT_NULL(strstr(wrapped, "\nend\n"));
}

void test_lua_wrap_decorator_export(void)
{
  const char* src = "#decorator(tag) {\n"
                      "    targets = FIELD\n"
                      "    param name!: string\n"
                      "    export [[\n"
                      "        return { tag_name = name }\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  const char* exp_src = get_export_source(schema, def);
  TEST_ASSERT_NOT_NULL(exp_src);

  const char* params[] = {"self", "target", "name"};
  const char* wrapped = bebop__lua_wrap_function(
      &ctx->arena, (bebop__str_view_t) {exp_src, def->decorator_def.export_span.len}, params, 3
  );

  TEST_ASSERT_NOT_NULL(wrapped);
  TEST_ASSERT_NOT_NULL(strstr(wrapped, "return function(self, target, name)\n"));
  TEST_ASSERT_NOT_NULL(strstr(wrapped, "return { tag_name = name }"));
  TEST_ASSERT_NOT_NULL(strstr(wrapped, "\nend\n"));
}

static bebop_decorator_t make_usage_no_args(bebop_str_t name, bebop_schema_t* schema)
{
  return (bebop_decorator_t) {
      .name = name,
      .span = {.off = 0, .len = 1, .start_line = 1, .start_col = 1},
      .args = NULL,
      .arg_count = 0,
      .next = NULL,
      .schema = schema,
  };
}

static bebop_decorator_t make_usage_uint32(
    bebop_str_t name, bebop_str_t param_name, int64_t value, bebop_schema_t* schema
)
{
  static bebop_decorator_arg_t arg;
  arg = (bebop_decorator_arg_t) {
      .name = param_name,
      .span = {0},
      .value = {.kind = BEBOP_LITERAL_INT, .int_val = value},
  };
  return (bebop_decorator_t) {
      .name = name,
      .span = {.off = 0, .len = 1, .start_line = 1, .start_col = 1},
      .args = &arg,
      .arg_count = 1,
      .next = NULL,
      .schema = schema,
  };
}

void test_lua_eval_state_lifecycle(void)
{
  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  TEST_ASSERT_NOT_NULL(state);
  bebop__lua_state_destroy(state);
}

void test_lua_eval_sandbox_safe(void)
{
  const char* src = "#decorator(test_safe) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        local s = string.format(\"%d\", 42)\n"
                      "        local n = math.floor(3.7)\n"
                      "        local t = {1, 2, 3}\n"
                      "        table.insert(t, 4)\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_sandbox_blocks_unsafe(void)
{
  const char* src = "#decorator(test_unsafe) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        io.open(\"/etc/passwd\", \"r\")\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);

  TEST_ASSERT_EQUAL(BEBOP_ERROR, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_constants(void)
{
  const char* src = "#decorator(test_consts) {\n"
                      "    targets = ALL\n"
                      "    validate [[\n"
                      "        if STRUCT ~= 2 then error(\"STRUCT wrong\") end\n"
                      "        if MESSAGE ~= 4 then error(\"MESSAGE wrong\") end\n"
                      "        if UNION ~= 8 then error(\"UNION wrong\") end\n"
                      "        if ENUM ~= 1 then error(\"ENUM wrong\") end\n"
                      "        if FIELD ~= 16 then error(\"FIELD wrong\") end\n"
                      "        if SERVICE ~= 32 then error(\"SERVICE wrong\") end\n"
                      "        if METHOD ~= 64 then error(\"METHOD wrong\") end\n"
                      "        if BRANCH ~= 128 then error(\"BRANCH wrong\") end\n"
                      "        if ALL ~= 255 then error(\"ALL wrong\") end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_error_emits_diagnostic(void)
{
  const char* src = "#decorator(test_err) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        error(\"something went wrong\")\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_ERROR, status);

  uint32_t diag_count = schema->diagnostic_count;
  TEST_ASSERT_TRUE(diag_count > 0);

  bool found = false;
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = &schema->diagnostics[i];
    if (d->code == BEBOP_DIAG_MACRO_VALIDATE_ERROR) {
      TEST_ASSERT_NOT_NULL(strstr(d->message, "something went wrong"));
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Expected MACRO_VALIDATE_ERROR diagnostic");

  bebop__lua_state_destroy(state);
}

void test_lua_eval_error_with_span(void)
{
  const char* src = "#decorator(test_span_err) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32\n"
                      "    validate [[\n"
                      "        error(\"targeted at param\", self.value.span)\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};

  bebop_str_t param_name = bebop_intern(BEBOP_INTERN(ctx), "value");
  bebop_decorator_arg_t arg = {
      .name = param_name,
      .span = {.off = 100, .len = 5, .start_line = 10, .start_col = 20},
      .value = {.kind = BEBOP_LITERAL_INT, .int_val = 42},
  };
  bebop_decorator_t usage = {
      .name = def->name,
      .span = {.off = 90, .len = 20, .start_line = 10, .start_col = 1},
      .args = &arg,
      .arg_count = 1,
      .schema = schema,
  };

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_ERROR, status);

  bool found = false;
  for (uint32_t i = 0; i < schema->diagnostic_count; i++) {
    const bebop_diagnostic_t* d = &schema->diagnostics[i];
    if (d->code == BEBOP_DIAG_MACRO_VALIDATE_ERROR && strstr(d->message, "targeted at param")) {
      TEST_ASSERT_EQUAL_UINT32(10, d->span.start_line);
      TEST_ASSERT_EQUAL_UINT32(20, d->span.start_col);
      TEST_ASSERT_EQUAL_UINT32(100, d->span.off);
      TEST_ASSERT_EQUAL_UINT32(5, d->span.len);
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Expected diagnostic at param span");

  bebop__lua_state_destroy(state);
}

void test_lua_eval_warn_with_span(void)
{
  const char* src = "#decorator(test_span_warn) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        warn(\"at usage site\", self.span)\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};

  bebop_decorator_t usage = {
      .name = def->name,
      .span = {.off = 50, .len = 12, .start_line = 7, .start_col = 5},
      .args = NULL,
      .arg_count = 0,
      .schema = schema,
  };

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bool found = false;
  for (uint32_t i = 0; i < schema->diagnostic_count; i++) {
    const bebop_diagnostic_t* d = &schema->diagnostics[i];
    if (d->code == BEBOP_DIAG_MACRO_VALIDATE_WARNING && strstr(d->message, "at usage site")) {
      TEST_ASSERT_EQUAL_UINT32(7, d->span.start_line);
      TEST_ASSERT_EQUAL_UINT32(5, d->span.start_col);
      TEST_ASSERT_EQUAL_UINT32(50, d->span.off);
      TEST_ASSERT_EQUAL_UINT32(12, d->span.len);
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Expected diagnostic at usage span");

  bebop__lua_state_destroy(state);
}

void test_lua_eval_warn_emits_diagnostic(void)
{
  const char* src = "#decorator(test_warn) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        warn(\"this is a warning\")\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bool found = false;
  for (uint32_t i = 0; i < schema->diagnostic_count; i++) {
    const bebop_diagnostic_t* d = &schema->diagnostics[i];
    if (d->code == BEBOP_DIAG_MACRO_VALIDATE_WARNING) {
      TEST_ASSERT_NOT_NULL(strstr(d->message, "this is a warning"));
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Expected MACRO_VALIDATE_WARNING diagnostic");

  bebop__lua_state_destroy(state);
}

void test_lua_eval_validate_pass(void)
{
  const char* src = "#decorator(noop) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        local x = 1 + 1\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_validate_fail(void)
{
  const char* src = "#decorator(check) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32\n"
                      "    validate [[\n"
                      "        if value <= 0 then\n"
                      "            error(\"value must be positive\")\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};

  bebop_str_t param_name = bebop_intern(BEBOP_INTERN(ctx), "value");
  bebop_decorator_t usage = make_usage_uint32(def->name, param_name, 0, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_ERROR, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_validate_with_params(void)
{
  const char* src = "#decorator(check) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32\n"
                      "    validate [[\n"
                      "        if value > 100 then\n"
                      "            error(\"too large\")\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};

  bebop_str_t param_name = bebop_intern(BEBOP_INTERN(ctx), "value");
  bebop_decorator_t usage = make_usage_uint32(def->name, param_name, 50, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_export_returns_table(void)
{
  const char* src = "#decorator(meta) {\n"
                      "    targets = STRUCT\n"
                      "    param value!: uint32\n"
                      "    export [[\n"
                      "        return { doubled = value * 2 }\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);

  bebop_str_t param_name = bebop_intern(BEBOP_INTERN(ctx), "value");
  bebop_decorator_t usage = make_usage_uint32(def->name, param_name, 21, schema);

  bebop_status_t status = bebop__lua_run_export(state, def, &usage);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_export_captures_data(void)
{
  const char* src = "#decorator(meta) {\n"
                      "    targets = STRUCT\n"
                      "    export [[\n"
                      "        return { opcode = 42, name = \"test\" }\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "@meta\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);

  bebop_def_t* struct_def = NULL;
  for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
    if (schema->sorted_defs[i]->kind == BEBOP_DEF_STRUCT) {
      struct_def = schema->sorted_defs[i];
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(struct_def);

  bebop_decorator_t* dec = struct_def->decorators;
  TEST_ASSERT_NOT_NULL(dec);
  TEST_ASSERT_NOT_NULL(dec->export_data);
  TEST_ASSERT_EQUAL_UINT32(2, dec->export_data->count);

  const bebop_literal_t* opcode = bebop_decorator_export(dec, "opcode");
  TEST_ASSERT_NOT_NULL(opcode);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_INT, bebop_literal_kind(opcode));
  TEST_ASSERT_EQUAL_INT64(42, bebop_literal_as_int(opcode));

  const bebop_literal_t* name = bebop_decorator_export(dec, "name");
  TEST_ASSERT_NOT_NULL(name);
  TEST_ASSERT_EQUAL(BEBOP_LITERAL_STRING, bebop_literal_kind(name));
  size_t len = 0;
  const char* name_val = bebop_literal_as_string(name, &len);
  TEST_ASSERT_EQUAL_STRING("test", name_val);

  TEST_ASSERT_NULL(bebop_decorator_export(dec, "missing"));

  TEST_ASSERT_NULL(bebop_decorator_export(NULL, "opcode"));
  TEST_ASSERT_NULL(bebop_decorator_export(dec, NULL));
}

void test_lua_eval_export_non_string_key(void)
{
  const char* src = "#decorator(bad_keys) {\n"
                      "    targets = STRUCT\n"
                      "    export [[\n"
                      "        return { [1] = \"whoops\" }\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "@bad_keys\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .col = 0, .message_substr = "Export table keys must be strings"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_lua_eval_export_non_literal_value(void)
{
  const char* src = "#decorator(bad_vals) {\n"
                      "    targets = STRUCT\n"
                      "    export [[\n"
                      "        return { nested = {} }\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "@bad_vals\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .col = 0, .message_substr = "must be a bool, number, or string"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_lua_eval_export_no_export_block(void)
{
  const char* src = "#decorator(no_export) {\n"
                      "    targets = STRUCT\n"
                      "}\n"
                      "\n"
                      "@no_export\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);

  bebop_def_t* struct_def = NULL;
  for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
    if (schema->sorted_defs[i]->kind == BEBOP_DEF_STRUCT) {
      struct_def = schema->sorted_defs[i];
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(struct_def);

  bebop_decorator_t* dec = struct_def->decorators;
  TEST_ASSERT_NOT_NULL(dec);
  TEST_ASSERT_NULL(dec->export_data);
  TEST_ASSERT_NULL(bebop_decorator_export(dec, "anything"));
}

void test_lua_eval_bit_library(void)
{
  const char* src = "#decorator(test_bit) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        if bit.band(0xFF, 0x0F) ~= 0x0F then error(\"band\") end\n"
                      "        if bit.bor(0xF0, 0x0F) ~= 0xFF then error(\"bor\") end\n"
                      "        if bit.bxor(0xFF, 0x0F) ~= 0xF0 then error(\"bxor\") end\n"
                      "        if bit.lshift(1, 4) ~= 16 then error(\"lshift\") end\n"
                      "        if bit.rshift(16, 4) ~= 1 then error(\"rshift\") end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_eval_helpers(void)
{
  const char* src = "#decorator(test_helpers) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        if not is_power_of_two(8) then error(\"8 is pot\") end\n"
                      "        if is_power_of_two(6) then error(\"6 is not pot\") end\n"
                      "        if not is_power_of_two(1) then error(\"1 is pot\") end\n"
                      "        if is_power_of_two(0) then error(\"0 is not pot\") end\n"
                      "        if not is_valid_identifier(\"hello\") then error(\"hello valid\") end\n"
                      "        if not is_valid_identifier(\"_foo123\") then error(\"_foo123 valid\") end\n"
                      "        if is_valid_identifier(\"123abc\") then error(\"123abc invalid\") end\n"
                      "        if is_valid_identifier(\"\") then error(\"empty invalid\") end\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* def = get_decorator_def(schema, 0);

  bebop_lua_state_t* state = bebop__lua_state_create(ctx);
  bebop__lua_compile_decorators(state, result);
  bebop_decorated_t target = {.kind = BEBOP_DECORATED_DEF};
  bebop_decorator_t usage = make_usage_no_args(def->name, schema);

  bebop_status_t status = bebop__lua_run_validate(state, def, &usage, target);
  TEST_ASSERT_EQUAL(BEBOP_OK, status);

  bebop__lua_state_destroy(state);
}

void test_lua_compile_syntax_error(void)
{
  const char* src = "#decorator(bad_lua) {\n"
                      "    targets = STRUCT\n"
                      "    validate [[\n"
                      "        local x = 1\n"
                      "        if x ==\n"
                      "    ]]\n"
                      "}\n";

  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(src, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_NOT_EQUAL(BEBOP_OK, status);

  const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
  TEST_ASSERT_NOT_NULL(schema);

  uint32_t diag_count = bebop_result_diagnostic_count(result);
  TEST_ASSERT_GREATER_THAN(0, diag_count);

  const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, 0);
  TEST_ASSERT_EQUAL(BEBOP_DIAG_ERROR, bebop_diagnostic_severity(diag));

  bebop_span_t diag_span = bebop_diagnostic_span(diag);
  TEST_ASSERT_GREATER_THAN(3, diag_span.start_line);

  const char* msg = bebop_diagnostic_message(diag);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_NULL(strstr(msg, "decorator:"));
}

void test_e2e_deprecated_on_struct_field(void);
void test_e2e_deprecated_on_message_field(void);
void test_e2e_deprecated_with_reason(void);
void test_e2e_deprecated_wrong_param_type(void);
void test_e2e_deprecated_unknown_param(void);
void test_e2e_flags_on_non_enum(void);
void test_e2e_flags_on_enum(void);
void test_e2e_unknown_decorator(void);
void test_e2e_lua_validate_rejects(void);
void test_e2e_lua_validate_accepts(void);
void test_e2e_target_mismatch_field(void);
void test_e2e_decorator_on_branch(void);
void test_e2e_target_mismatch_branch(void);
void test_e2e_lua_target_branch_variable(void);

void test_e2e_deprecated_on_struct_field(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "struct Foo {\n"
                      "    @deprecated(\"old\")\n"
                      "    x: int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 3, .col = 0, .message_substr = "cannot be applied to struct fields"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_deprecated_on_message_field(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "message Foo {\n"
                      "    @deprecated(\"old\")\n"
                      "    x(1): int32;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
}

void test_e2e_deprecated_with_reason(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "@deprecated(\"use NewFoo instead\")\n"
                      "message Foo {\n"
                      "    x(1): int32;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
}

void test_e2e_deprecated_wrong_param_type(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "@deprecated(123)\n"
                      "message Foo {\n"
                      "    x(1): int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 2, .col = 0, .message_substr = "expects string"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_deprecated_unknown_param(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "@deprecated(foo: \"bar\")\n"
                      "message Foo {\n"
                      "    x(1): int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 2, .col = 0, .message_substr = "Unknown parameter"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_flags_on_non_enum(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "@flags\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 2, .col = 0, .message_substr = "cannot be applied to this element"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_flags_on_enum(void)
{
  const char* src = "import \"bebop/decorators.bop\"\n"
                      "@flags\n"
                      "enum Perms : uint32 {\n"
                      "    None = 0;\n"
                      "    Read = 1;\n"
                      "    Write = 2;\n"
                      "    Execute = 4;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
}

void test_e2e_unknown_decorator(void)
{
  const char* src = "@nonexistent\n" "struct Foo {\n" "    x: int32;\n" "}\n";

  expected_diag_t expected[] = {
      {.line = 1, .col = 0, .message_substr = "Unknown decorator"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_lua_validate_rejects(void)
{
  const char* src = "#decorator(check_min) {\n"
                      "    targets = ALL\n"
                      "    param value!: uint32\n"
                      "    validate [[\n"
                      "        if value < 10 then\n"
                      "            error(\"value must be at least 10\")\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "@check_min(5)\n"
                      "struct Bad {\n"
                      "    x: int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 11, .col = 0, .message_substr = "value must be at least 10"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_lua_validate_accepts(void)
{
  const char* src = "#decorator(check_min) {\n"
                      "    targets = ALL\n"
                      "    param value!: uint32\n"
                      "    validate [[\n"
                      "        if value < 10 then\n"
                      "            error(\"value must be at least 10\")\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "@check_min(42)\n"
                      "struct Good {\n"
                      "    x: int32;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));
}

void test_e2e_target_mismatch_field(void)
{
  const char* src = "#decorator(struct_only) {\n"
                      "    targets = STRUCT\n"
                      "}\n"
                      "\n"
                      "message Foo {\n"
                      "    @struct_only\n"
                      "    x(1): int32;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 6, .col = 0, .message_substr = "cannot be applied to this element"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_decorator_on_branch(void)
{
  const char* src = "#decorator(branch_tag) {\n"
                      "    targets = BRANCH\n"
                      "    param name!: string\n"
                      "}\n"
                      "\n"
                      "struct A { x: int32; }\n"
                      "union U {\n"
                      "    @branch_tag(name: \"first\")\n"
                      "    a(1): A;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);
  TEST_ASSERT_EQUAL_UINT32(0, bebop_result_error_count(result));

  const bebop_def_t* u = bebop_result_find(result, "U");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT_EQUAL(1, bebop_def_branch_count(u));

  const bebop_union_branch_t* br = bebop_def_branch_at(u, 0);
  TEST_ASSERT_NOT_NULL(br);
  TEST_ASSERT_EQUAL(1, bebop_branch_decorator_count(br));

  const bebop_decorator_t* dec = bebop_branch_decorator_at(br, 0);
  TEST_ASSERT_NOT_NULL(dec);
  TEST_ASSERT_NOT_NULL(bebop_decorator_resolved(dec));
}

void test_e2e_target_mismatch_branch(void)
{
  const char* src = "#decorator(field_only) {\n"
                      "    targets = FIELD\n"
                      "}\n"
                      "\n"
                      "struct A { x: int32; }\n"
                      "union U {\n"
                      "    @field_only\n"
                      "    a(1): A;\n"
                      "}\n";

  expected_diag_t expected[] = {
      {.line = 7, .col = 0, .message_substr = "cannot be applied to this element"},
  };
  parse_expect_diagnostics(src, expected, 1);
}

void test_e2e_lua_target_branch_variable(void)
{
  const char* src = "#decorator(check_branch) {\n"
                      "    targets = BRANCH\n"
                      "    validate [[\n"
                      "        if target.kind ~= \"branch\" then\n"
                      "            error(\"expected branch, got \" .. tostring(target.kind))\n"
                      "        end\n"
                      "        if target.name ~= \"a\" then\n"
                      "            error(\"expected name 'a', got \" .. tostring(target.name))\n"
                      "        end\n"
                      "        if target.discriminator ~= 1 then\n"
                      "            error(\"expected discriminator 1, got \" .. tostring(target.discriminator))\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "struct A { x: int32; }\n"
                      "union U {\n"
                      "    @check_branch\n"
                      "    a(1): A;\n"
                      "}\n";

  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(src, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  if (bebop_result_error_count(result) > 0) {
    uint32_t diag_count = bebop_result_diagnostic_count(result);
    for (uint32_t i = 0; i < diag_count; i++) {
      const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
      fprintf(
          stderr,
          "[%s] %u:%u: %s\n",
          bebop_diag_severity_name(bebop_diagnostic_severity(d)),
          bebop_diagnostic_span(d).start_line,
          bebop_diagnostic_span(d).start_col,
          bebop_diagnostic_message(d)
      );
    }
    TEST_FAIL_MESSAGE("Lua branch target variable test failed - validator should pass");
  }
  (void)status;
}

void test_e2e_decorator_requires_import(void);
void test_e2e_decorator_visible_with_import(void);
void test_e2e_qualified_decorator_name(void);
void test_e2e_ambiguous_decorator_reference(void);
void test_e2e_toposort_decorator_before_user(void);
void test_e2e_public_api_decorator_metadata(void);
void test_e2e_lua_target_variable(void);

void test_e2e_decorator_requires_import(void)
{
  const char* sources[] = {
        "package validators;\n"
        "#decorator(range) {\n"
        "    targets = FIELD\n"
        "    param min!: float64\n"
        "}\n",
        "package app;\n"
        "message Foo {\n"
        "    @range(min: 0.0)\n"
        "    value(1): float64;\n"
        "}\n",
    };
  const bebop_source_t bop_srcs[] = {
      {sources[0], strlen(sources[0]), "validators.bop"},
      {sources[1], strlen(sources[1]), "app.bop"},
  };

  bebop_parse_result_t* result = NULL;
  bebop_parse_sources(ctx, bop_srcs, 2, &result);
  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE(bebop_result_error_count(result) > 0);

  uint32_t diag_count = bebop_result_diagnostic_count(result);
  bool found = false;
  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
    const char* msg = bebop_diagnostic_message(d);
    if (msg && (strstr(msg, "Unknown decorator") || strstr(msg, "unknown decorator"))) {
      found = true;
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found, "Expected unknown decorator error without import");
}

void test_e2e_decorator_visible_with_import(void)
{
  bebop_parse_result_t* result = parse_fixture(FIXTURE("dec_app.bop"));

  TEST_ASSERT_TRUE(bebop_result_schema_count(result) >= 2);

  const bebop_def_t* measurement = bebop_result_find(result, "app.Measurement");
  TEST_ASSERT_NOT_NULL(measurement);

  const bebop_field_t* field = bebop_def_field_at(measurement, 0);
  TEST_ASSERT_NOT_NULL(field);
  TEST_ASSERT_EQUAL(1, bebop_field_decorator_count(field));

  const bebop_decorator_t* dec = bebop_field_decorator_at(field, 0);
  TEST_ASSERT_NOT_NULL(dec);

  const bebop_def_t* resolved = bebop_decorator_resolved(dec);
  TEST_ASSERT_NOT_NULL(resolved);
  TEST_ASSERT_EQUAL(BEBOP_DEF_DECORATOR, bebop_def_kind(resolved));
  TEST_ASSERT_EQUAL_STRING("range", bebop_def_name(resolved));
}

void test_e2e_qualified_decorator_name(void)
{
  bebop_parse_result_t* result = parse_fixture(FIXTURE("dec_qualified.bop"));

  TEST_ASSERT_TRUE(bebop_result_schema_count(result) >= 2);

  const bebop_def_t* score = bebop_result_find(result, "app.Score");
  TEST_ASSERT_NOT_NULL(score);

  const bebop_field_t* field = bebop_def_field_at(score, 0);
  TEST_ASSERT_NOT_NULL(field);
  TEST_ASSERT_EQUAL(1, bebop_field_decorator_count(field));

  const bebop_decorator_t* dec = bebop_field_decorator_at(field, 0);
  TEST_ASSERT_NOT_NULL(dec);

  const bebop_def_t* resolved = bebop_decorator_resolved(dec);
  TEST_ASSERT_NOT_NULL(resolved);
  TEST_ASSERT_EQUAL(BEBOP_DEF_DECORATOR, bebop_def_kind(resolved));
  TEST_ASSERT_EQUAL_STRING("range", bebop_def_name(resolved));
}

void test_e2e_ambiguous_decorator_reference(void)
{
  bebop_parse_result_t* result = NULL;
  const char* path = BEBOP_TEST_FIXTURES_DIR "/should_fail/dec_ambig_user.bop";
  bebop_status_t status = bebop_parse(ctx, &path, 1, &result);
  (void)status;

  TEST_ASSERT_NOT_NULL(result);
  TEST_ASSERT_TRUE_MESSAGE(bebop_result_error_count(result) >= 1, "Expected at least one error");

  bool found_ambiguous = false;
  for (uint32_t i = 0; i < bebop_result_diagnostic_count(result); i++) {
    const bebop_diagnostic_t* diag = bebop_result_diagnostic_at(result, i);
    const char* msg = bebop_diagnostic_message(diag);
    if (strstr(msg, "multiple imported") != NULL) {
      found_ambiguous = true;

      const char* hint = bebop_diagnostic_hint(diag);
      TEST_ASSERT_NOT_NULL_MESSAGE(hint, "Ambiguous reference should have a hint");
      TEST_ASSERT_TRUE_MESSAGE(
          strstr(hint, "ambig_a.check") != NULL || strstr(hint, "ambig_b.check") != NULL,
          "Hint should suggest qualified names"
      );
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(found_ambiguous, "Should emit ambiguous reference error");
}

void test_e2e_toposort_decorator_before_user(void)
{
  const char* src = "message Foo {\n"
                      "    @tag(name: \"x\")\n"
                      "    x(1): int32;\n"
                      "}\n"
                      "\n"
                      "#decorator(tag) {\n"
                      "    targets = FIELD\n"
                      "    param name!: string\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);

  uint32_t def_count = bebop_result_definition_count(result);
  TEST_ASSERT_TRUE(def_count >= 2);

  int decorator_idx = -1;
  int user_idx = -1;
  for (uint32_t i = 0; i < def_count; i++) {
    const bebop_def_t* d = bebop_result_definition_at(result, i);
    if (d->kind == BEBOP_DEF_DECORATOR) {
      decorator_idx = (int)i;
    }
    if (d->kind == BEBOP_DEF_MESSAGE) {
      user_idx = (int)i;
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(decorator_idx >= 0, "Decorator def not found in result");
  TEST_ASSERT_TRUE_MESSAGE(user_idx >= 0, "Message def not found in result");
  TEST_ASSERT_TRUE_MESSAGE(
      decorator_idx < user_idx, "Decorator def should appear before its user in topological order"
  );
}

void test_e2e_public_api_decorator_metadata(void)
{
  const char* src = "/// Validates numeric range.\n"
                      "#decorator(range) {\n"
                      "    targets = FIELD\n"
                      "    multiple = false\n"
                      "    /// Minimum allowed value\n"
                      "    param min!: float64\n"
                      "    /// Maximum allowed value (optional)\n"
                      "    param max?: float64\n"
                      "}\n"
                      "\n"
                      "message Foo {\n"
                      "    @range(min: 0.0, max: 100.0)\n"
                      "    value(1): float64;\n"
                      "}\n";

  bebop_parse_result_t* result = parse_expect_success(src);

  bebop_schema_t* schema = get_schema(result);
  bebop_def_t* dec_def = get_decorator_def(schema, 0);
  TEST_ASSERT_NOT_NULL(dec_def);

  TEST_ASSERT_EQUAL_UINT32(BEBOP_TARGET_FIELD, bebop_def_decorator_targets(dec_def));

  TEST_ASSERT_FALSE(bebop_def_decorator_allow_multiple(dec_def));

  TEST_ASSERT_EQUAL_UINT32(2, bebop_def_decorator_param_count(dec_def));

  TEST_ASSERT_EQUAL_STRING("min", bebop_def_decorator_param_name(dec_def, 0));
  TEST_ASSERT_EQUAL_STRING("max", bebop_def_decorator_param_name(dec_def, 1));

  TEST_ASSERT_EQUAL(BEBOP_TYPE_FLOAT64, bebop_def_decorator_param_type(dec_def, 0));
  TEST_ASSERT_EQUAL(BEBOP_TYPE_FLOAT64, bebop_def_decorator_param_type(dec_def, 1));

  TEST_ASSERT_TRUE(bebop_def_decorator_param_required(dec_def, 0));
  TEST_ASSERT_FALSE(bebop_def_decorator_param_required(dec_def, 1));

  TEST_ASSERT_NOT_NULL(bebop_def_decorator_param_description(dec_def, 0));
  TEST_ASSERT_NOT_NULL(strstr(bebop_def_decorator_param_description(dec_def, 0), "Minimum"));
  TEST_ASSERT_NOT_NULL(bebop_def_decorator_param_description(dec_def, 1));
  TEST_ASSERT_NOT_NULL(strstr(bebop_def_decorator_param_description(dec_def, 1), "Maximum"));

  const bebop_def_t* foo = NULL;
  for (uint32_t i = 0; i < bebop_schema_definition_count(schema); i++) {
    const bebop_def_t* d = bebop_schema_definition_at(schema, i);
    if (d->kind == BEBOP_DEF_MESSAGE) {
      foo = d;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(foo);

  TEST_ASSERT_TRUE(bebop_def_field_count(foo) > 0);
  const bebop_field_t* field = bebop_def_field_at(foo, 0);
  TEST_ASSERT_NOT_NULL(field);
  TEST_ASSERT_EQUAL(1, bebop_field_decorator_count(field));

  const bebop_decorator_t* usage = bebop_field_decorator_at(field, 0);
  TEST_ASSERT_NOT_NULL(usage);

  const bebop_def_t* resolved = bebop_decorator_resolved(usage);
  TEST_ASSERT_NOT_NULL(resolved);
  TEST_ASSERT_EQUAL(BEBOP_DEF_DECORATOR, resolved->kind);
  TEST_ASSERT_EQUAL_STRING("range", bebop_def_name(resolved));
}

void test_e2e_lua_target_variable(void)
{
  const char* src = "#decorator(check_name) {\n"
                      "    targets = STRUCT | MESSAGE\n"
                      "    validate [[\n"
                      "        if target.kind ~= \"definition\" then\n"
                      "            error(\"expected definition, got \" .. tostring(target.kind))\n"
                      "        end\n"
                      "        if target.name ~= \"Foo\" then\n"
                      "            error(\"expected Foo, got \" .. tostring(target.name))\n"
                      "        end\n"
                      "    ]]\n"
                      "}\n"
                      "\n"
                      "@check_name\n"
                      "struct Foo {\n"
                      "    x: int32;\n"
                      "}\n";

  bebop_parse_result_t* result = NULL;
  bebop_status_t status = bebop_parse_source(ctx, BEBOP_SOURCE(src, "test.bop"), &result);
  TEST_ASSERT_NOT_NULL(result);

  if (bebop_result_error_count(result) > 0) {
    uint32_t diag_count = bebop_result_diagnostic_count(result);
    for (uint32_t i = 0; i < diag_count; i++) {
      const bebop_diagnostic_t* d = bebop_result_diagnostic_at(result, i);
      fprintf(
          stderr,
          "[%s] %u:%u: %s\n",
          bebop_diag_severity_name(bebop_diagnostic_severity(d)),
          bebop_diagnostic_span(d).start_line,
          bebop_diagnostic_span(d).start_col,
          bebop_diagnostic_message(d)
      );
    }
    TEST_FAIL_MESSAGE("Lua target variable test failed - validator should pass");
  }
  (void)status;
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_macro_minimal);
  RUN_TEST(test_macro_targets_single);
  RUN_TEST(test_macro_targets_combined);
  RUN_TEST(test_macro_targets_all);
  RUN_TEST(test_macro_targets_branch);

  RUN_TEST(test_macro_multiple_true);
  RUN_TEST(test_macro_multiple_false);

  RUN_TEST(test_macro_param_required);
  RUN_TEST(test_macro_param_optional);
  RUN_TEST(test_macro_param_default_value);
  RUN_TEST(test_macro_param_in_constraint);
  RUN_TEST(test_macro_param_multiple);

  RUN_TEST(test_macro_validate_block);
  RUN_TEST(test_macro_export_block);
  RUN_TEST(test_macro_validate_and_export);
  RUN_TEST(test_macro_no_validate_no_export);

  RUN_TEST(test_macro_description);
  RUN_TEST(test_macro_param_description);

  RUN_TEST(test_macro_multiple_decorators);
  RUN_TEST(test_macro_coexists_with_definitions);

  RUN_TEST(test_macro_error_missing_name);
  RUN_TEST(test_macro_error_missing_lparen);
  RUN_TEST(test_macro_error_missing_rparen);
  RUN_TEST(test_macro_error_missing_lbrace);
  RUN_TEST(test_macro_error_missing_targets);
  RUN_TEST(test_macro_error_bad_target);
  RUN_TEST(test_macro_error_missing_bang_or_question);
  RUN_TEST(test_macro_error_bad_param_type);
  RUN_TEST(test_macro_error_unterminated_raw_block);
  RUN_TEST(test_macro_error_missing_raw_block);
  RUN_TEST(test_macro_error_unknown_body_keyword);
  RUN_TEST(test_macro_error_param_missing_colon);
  RUN_TEST(test_macro_error_param_missing_type);
  RUN_TEST(test_macro_error_invalid_in_constraint);
  RUN_TEST(test_macro_error_empty_body);
  RUN_TEST(test_macro_error_multiple_errors);
  RUN_TEST(test_macro_error_duplicate_same_file);
  RUN_TEST(test_macro_error_duplicate_cross_schema);
  RUN_TEST(test_macro_error_decorator_conflicts_with_def_cross_schema);
  RUN_TEST(test_macro_error_def_conflicts_with_decorator_cross_schema);

  RUN_TEST(test_lua_wrap_no_params);
  RUN_TEST(test_lua_wrap_with_params);
  RUN_TEST(test_lua_wrap_multiline);
  RUN_TEST(test_lua_wrap_null_source);
  RUN_TEST(test_lua_wrap_decorator_validate);
  RUN_TEST(test_lua_wrap_decorator_export);

  RUN_TEST(test_lua_eval_state_lifecycle);
  RUN_TEST(test_lua_eval_sandbox_safe);
  RUN_TEST(test_lua_eval_sandbox_blocks_unsafe);
  RUN_TEST(test_lua_eval_constants);
  RUN_TEST(test_lua_eval_error_emits_diagnostic);
  RUN_TEST(test_lua_eval_error_with_span);
  RUN_TEST(test_lua_eval_warn_emits_diagnostic);
  RUN_TEST(test_lua_eval_warn_with_span);
  RUN_TEST(test_lua_eval_validate_pass);
  RUN_TEST(test_lua_eval_validate_fail);
  RUN_TEST(test_lua_eval_validate_with_params);
  RUN_TEST(test_lua_eval_export_returns_table);
  RUN_TEST(test_lua_eval_export_captures_data);
  RUN_TEST(test_lua_eval_export_non_string_key);
  RUN_TEST(test_lua_eval_export_non_literal_value);
  RUN_TEST(test_lua_eval_export_no_export_block);
  RUN_TEST(test_lua_eval_bit_library);
  RUN_TEST(test_lua_eval_helpers);
  RUN_TEST(test_lua_compile_syntax_error);

  RUN_TEST(test_e2e_deprecated_on_struct_field);
  RUN_TEST(test_e2e_deprecated_on_message_field);
  RUN_TEST(test_e2e_deprecated_with_reason);
  RUN_TEST(test_e2e_deprecated_wrong_param_type);
  RUN_TEST(test_e2e_deprecated_unknown_param);
  RUN_TEST(test_e2e_flags_on_non_enum);
  RUN_TEST(test_e2e_flags_on_enum);
  RUN_TEST(test_e2e_unknown_decorator);
  RUN_TEST(test_e2e_lua_validate_rejects);
  RUN_TEST(test_e2e_lua_validate_accepts);
  RUN_TEST(test_e2e_target_mismatch_field);
  RUN_TEST(test_e2e_decorator_on_branch);
  RUN_TEST(test_e2e_target_mismatch_branch);

  RUN_TEST(test_e2e_decorator_requires_import);
  RUN_TEST(test_e2e_decorator_visible_with_import);
  RUN_TEST(test_e2e_qualified_decorator_name);
  RUN_TEST(test_e2e_ambiguous_decorator_reference);
  RUN_TEST(test_e2e_toposort_decorator_before_user);
  RUN_TEST(test_e2e_public_api_decorator_metadata);
  RUN_TEST(test_e2e_lua_target_variable);
  RUN_TEST(test_e2e_lua_target_branch_variable);

  return UNITY_END();
}
