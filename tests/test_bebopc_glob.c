#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bebopc_error.h"
#include "bebopc_glob.h"
#include "unity.h"

static bebopc_ctx_t ctx;

void setUp(void)
{
  bebopc_ctx_init(&ctx);
}

void tearDown(void)
{
  bebopc_ctx_cleanup(&ctx);
}

void test_glob_match_exact(void);
void test_glob_match_wildcard_star(void);
void test_glob_match_wildcard_question(void);
void test_glob_match_double_star(void);
void test_glob_match_case_insensitive(void);
void test_glob_match_extension(void);

void test_glob_match_exact(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "file.txt", "file.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "file.txt", "other.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "path/to/file.txt", "path/to/file.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "path/to/file.txt", "path/to/other.txt"));

  bebopc_glob_free(glob);
}

void test_glob_match_wildcard_star(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*.txt", "file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*.txt", "readme.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "*.txt", "file.md"));

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "file.*", "file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "file.*", "file.md"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "file.*", "other.txt"));

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*test*", "mytest.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*test*", "testfile"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*test*", "test"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "*test*", "file.txt"));

  bebopc_glob_free(glob);
}

void test_glob_match_wildcard_question(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "file?.txt", "file1.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "file?.txt", "fileA.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "file?.txt", "file12.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "file?.txt", "file.txt"));

  bebopc_glob_free(glob);
}

void test_glob_match_double_star(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "**/*", "file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "**/*", "dir/file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "**/*", "a/b/c/file.txt"));

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "**/*.txt", "file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "**/*.txt", "dir/file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "**/*.txt", "a/b/c/file.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "**/*.txt", "file.md"));

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "src/**/*", "src/file.c"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "src/**/*", "src/sub/file.c"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "src/**/*", "other/file.c"));

  bebopc_glob_free(glob);
}

void test_glob_match_case_insensitive(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*.TXT", "file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*.txt", "FILE.TXT"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "README.*", "readme.md"));

  bebopc_glob_free(glob);

  bebopc_glob_config_t config = {
      .case_sensitive = true, .preserve_order = false, .follow_symlinks = false
  };
  glob = bebopc_glob_new(config);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "*.TXT", "file.txt"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, "*.txt", "file.txt"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, "README.*", "readme.md"));

  bebopc_glob_free(glob);
}

void test_glob_match_extension(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match(glob, ".*", ".gitignore"));
  TEST_ASSERT_TRUE(bebopc_glob_match(glob, ".*", ".hidden"));
  TEST_ASSERT_FALSE(bebopc_glob_match(glob, ".*", "visible"));

  bebopc_glob_free(glob);
}

void test_glob_segment_star(void);
void test_glob_segment_question(void);

void test_glob_segment_star(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match_segment(glob, "*", "anything"));
  TEST_ASSERT_TRUE(bebopc_glob_match_segment(glob, "*.c", "file.c"));
  TEST_ASSERT_TRUE(bebopc_glob_match_segment(glob, "test_*", "test_foo"));
  TEST_ASSERT_TRUE(bebopc_glob_match_segment(glob, "*_test", "foo_test"));

  bebopc_glob_free(glob);
}

void test_glob_segment_question(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_TRUE(bebopc_glob_match_segment(glob, "?", "a"));
  TEST_ASSERT_FALSE(bebopc_glob_match_segment(glob, "?", "ab"));
  TEST_ASSERT_TRUE(bebopc_glob_match_segment(glob, "a?c", "abc"));
  TEST_ASSERT_FALSE(bebopc_glob_match_segment(glob, "a?c", "ac"));

  bebopc_glob_free(glob);
}

static bool _ends_with(const char* str, const char* suffix)
{
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len) {
    return false;
  }
  return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static bool _contains(const char* str, const char* sub)
{
  return strstr(str, sub) != NULL;
}

void test_glob_fixture_all_bop_files(void);
void test_glob_fixture_valid_only(void);
void test_glob_fixture_exclude_should_fail(void);
void test_glob_fixture_json_files(void);
void test_glob_fixture_specific_file(void);
void test_glob_paths_fixture(void);

void test_glob_fixture_all_bop_files(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "**/*.bop"));

  bebopc_glob_result_t* result = bebopc_glob_execute(glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_TRUE(result->count > 0);

  for (size_t i = 0; i < result->count; i++) {
    TEST_ASSERT_TRUE(_ends_with(result->matches[i].path, ".bop"));
  }

  bebopc_glob_result_free(result);
  bebopc_glob_free(glob);
}

void test_glob_fixture_valid_only(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "valid/*.bop"));

  bebopc_glob_result_t* result = bebopc_glob_execute(glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_TRUE(result->count > 0);

  for (size_t i = 0; i < result->count; i++) {
#ifdef _WIN32
    TEST_ASSERT_TRUE(_contains(result->matches[i].path, "valid\\"));
#else
    TEST_ASSERT_TRUE(_contains(result->matches[i].path, "valid/"));
#endif
    TEST_ASSERT_TRUE(_ends_with(result->matches[i].path, ".bop"));
  }

  bebopc_glob_result_free(result);
  bebopc_glob_free(glob);
}

void test_glob_fixture_exclude_should_fail(void)
{
  bebopc_glob_t* valid_glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(valid_glob, "valid/*.bop"));
  bebopc_glob_result_t* valid_result =
      bebopc_glob_execute(valid_glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  size_t valid_count = valid_result->count;
  bebopc_glob_result_free(valid_result);
  bebopc_glob_free(valid_glob);

  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "**/*.bop"));
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_exclude(glob, "should_fail/**/*"));

  bebopc_glob_result_t* result = bebopc_glob_execute(glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_EQUAL(valid_count, result->count);

  for (size_t i = 0; i < result->count; i++) {
    TEST_ASSERT_FALSE(_contains(result->matches[i].path, "should_fail/"));
  }

  bebopc_glob_result_free(result);
  bebopc_glob_free(glob);
}

void test_glob_fixture_json_files(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "**/*.json"));

  bebopc_glob_result_t* result = bebopc_glob_execute(glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_EQUAL(2, result->count);

  for (size_t i = 0; i < result->count; i++) {
    TEST_ASSERT_TRUE(_ends_with(result->matches[i].path, ".json"));
  }

  bebopc_glob_result_free(result);
  bebopc_glob_free(glob);
}

void test_glob_fixture_specific_file(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "**/enum*.bop"));

  bebopc_glob_result_t* result = bebopc_glob_execute(glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  TEST_ASSERT_NOT_NULL(result);

  TEST_ASSERT_EQUAL(3, result->count);

  for (size_t i = 0; i < result->count; i++) {
    TEST_ASSERT_TRUE(_contains(result->matches[i].path, "enum"));
  }

  bebopc_glob_result_free(result);
  bebopc_glob_free(glob);
}

void test_glob_paths_fixture(void)
{
  bebopc_glob_t* count_glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(count_glob, "valid/*.bop"));
  bebopc_glob_result_t* count_result =
      bebopc_glob_execute(count_glob, &ctx, BEBOP_TEST_FIXTURES_DIR);
  size_t expected_count = count_result->count;
  bebopc_glob_result_free(count_result);
  bebopc_glob_free(count_glob);

  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "valid/*.bop"));

  size_t count = 0;
  char** paths = bebopc_glob_paths(glob, &ctx, BEBOP_TEST_FIXTURES_DIR, &count);

  TEST_ASSERT_EQUAL(expected_count, count);
  TEST_ASSERT_NOT_NULL(paths);

  for (size_t i = 0; i < count; i++) {
    TEST_ASSERT_NOT_NULL(paths[i]);
    TEST_ASSERT_TRUE(_contains(paths[i], "fixtures"));
    TEST_ASSERT_TRUE(_ends_with(paths[i], ".bop"));
  }

  bebopc_glob_paths_free(paths, count);
  bebopc_glob_free(glob);
}

void test_glob_include_exclude(void);
void test_glob_preserve_order(void);

void test_glob_include_exclude(void)
{
  bebopc_glob_t* glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "**/*.txt"));
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_exclude(glob, "**/secret/*"));

  bebopc_glob_free(glob);
}

void test_glob_preserve_order(void)
{
  bebopc_glob_config_t config = {
      .case_sensitive = false, .preserve_order = true, .follow_symlinks = false
  };
  bebopc_glob_t* glob = bebopc_glob_new(config);
  TEST_ASSERT_NOT_NULL(glob);

  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "**/*"));
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_exclude(glob, "logs/**/*"));
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_glob_include(glob, "logs/important/**/*"));

  bebopc_glob_free(glob);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_glob_match_exact);
  RUN_TEST(test_glob_match_wildcard_star);
  RUN_TEST(test_glob_match_wildcard_question);
  RUN_TEST(test_glob_match_double_star);
  RUN_TEST(test_glob_match_case_insensitive);
  RUN_TEST(test_glob_match_extension);

  RUN_TEST(test_glob_segment_star);
  RUN_TEST(test_glob_segment_question);

  RUN_TEST(test_glob_fixture_all_bop_files);
  RUN_TEST(test_glob_fixture_valid_only);
  RUN_TEST(test_glob_fixture_exclude_should_fail);
  RUN_TEST(test_glob_fixture_json_files);
  RUN_TEST(test_glob_fixture_specific_file);
  RUN_TEST(test_glob_paths_fixture);

  RUN_TEST(test_glob_include_exclude);
  RUN_TEST(test_glob_preserve_order);

  return UNITY_END();
}
