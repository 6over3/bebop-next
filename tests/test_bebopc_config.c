#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "bebopc_dir.h"
#include "cli/bebopc_config.h"
#include "unity.h"

static bebopc_ctx_t ctx;
static bebopc_config_t cfg;

void setUp(void)
{
  bebopc_ctx_init(&ctx);
  bebopc_config_init(&cfg);
}

void tearDown(void)
{
  bebopc_config_cleanup(&cfg);
  bebopc_ctx_cleanup(&ctx);
}

void test_config_init(void);
void test_config_cleanup(void);

void test_config_init(void)
{
  bebopc_config_t c;
  bebopc_config_init(&c);

  TEST_ASSERT_NULL(c.sources);
  TEST_ASSERT_EQUAL(0, c.source_count);
  TEST_ASSERT_NULL(c.exclude);
  TEST_ASSERT_EQUAL(0, c.exclude_count);
  TEST_ASSERT_NULL(c.plugins);
  TEST_ASSERT_EQUAL(0, c.plugin_count);
  TEST_ASSERT_NULL(c.options);
  TEST_ASSERT_EQUAL(0, c.option_count);
  TEST_ASSERT_EQUAL(CLI_COLOR_AUTO, c.color);
  TEST_ASSERT_EQUAL(CLI_FORMAT_TERMINAL, c.format);
  TEST_ASSERT_FALSE(c.verbose);
  TEST_ASSERT_FALSE(c.quiet);

  bebopc_config_cleanup(&c);
}

void test_config_cleanup(void)
{
  bebopc_config_t c;
  bebopc_ctx_t test_ctx;
  bebopc_ctx_init(&test_ctx);
  bebopc_config_init(&c);

  bebopc_config_add_source(&c, &test_ctx, "*.bop");
  bebopc_config_add_exclude(&c, &test_ctx, "vendor/*");
  bebopc_config_add_plugin(&c, &test_ctx, "c", "./out");
  bebopc_config_add_option(&c, &test_ctx, "key", "value");

  TEST_ASSERT_EQUAL(1, c.source_count);
  TEST_ASSERT_EQUAL(1, c.exclude_count);
  TEST_ASSERT_EQUAL(1, c.plugin_count);
  TEST_ASSERT_EQUAL(1, c.option_count);

  bebopc_config_cleanup(&c);
  bebopc_ctx_cleanup(&test_ctx);

  TEST_ASSERT_NULL(c.sources);
  TEST_ASSERT_EQUAL(0, c.source_count);
}

void test_add_source(void);
void test_add_multiple_sources(void);
void test_add_exclude(void);
void test_add_include(void);
void test_add_plugin(void);
void test_add_option(void);

void test_add_source(void)
{
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_source(&cfg, &ctx, "**/*.bop"));
  TEST_ASSERT_EQUAL(1, cfg.source_count);
  TEST_ASSERT_EQUAL_STRING("**/*.bop", cfg.sources[0]);
}

void test_add_multiple_sources(void)
{
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_source(&cfg, &ctx, "src/*.bop"));
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_source(&cfg, &ctx, "lib/*.bop"));
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_source(&cfg, &ctx, "test/*.bop"));

  TEST_ASSERT_EQUAL(3, cfg.source_count);
  TEST_ASSERT_EQUAL_STRING("src/*.bop", cfg.sources[0]);
  TEST_ASSERT_EQUAL_STRING("lib/*.bop", cfg.sources[1]);
  TEST_ASSERT_EQUAL_STRING("test/*.bop", cfg.sources[2]);
}

void test_add_exclude(void)
{
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_exclude(&cfg, &ctx, "**/vendor/**"));
  TEST_ASSERT_EQUAL(1, cfg.exclude_count);
  TEST_ASSERT_EQUAL_STRING("**/vendor/**", cfg.exclude[0]);
}

void test_add_include(void)
{
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_include(&cfg, &ctx, "/usr/local/share/bebop"));
  TEST_ASSERT_EQUAL(1, cfg.include_count);
  TEST_ASSERT_EQUAL_STRING("/usr/local/share/bebop", cfg.include[0]);
}

void test_add_plugin(void)
{
  TEST_ASSERT_EQUAL(
      BEBOPC_OK, bebopc_config_add_plugin(&cfg, &ctx, "typescript", "./generated/ts")
  );
  TEST_ASSERT_EQUAL(1, cfg.plugin_count);
  TEST_ASSERT_EQUAL_STRING("typescript", cfg.plugins[0].name);
  TEST_ASSERT_EQUAL_STRING("./generated/ts", cfg.plugins[0].out_dir);
}

void test_add_option(void)
{
  TEST_ASSERT_EQUAL(BEBOPC_OK, bebopc_config_add_option(&cfg, &ctx, "namespace", "MyApp.Models"));
  TEST_ASSERT_EQUAL(1, cfg.option_count);
  TEST_ASSERT_EQUAL_STRING("namespace", cfg.options[0].key);
  TEST_ASSERT_EQUAL_STRING("MyApp.Models", cfg.options[0].value);
}

void test_merge_cli_plugins(void);
void test_merge_cli_flags(void);

void test_merge_cli_plugins(void)
{
  bebopc_config_add_plugin(&cfg, &ctx, "c", "./c_out");

  cli_args_t args = {0};
  cli_plugin_t plugin = {.name = "ts", .out_dir = "./ts_out", .path = NULL};
  args.plugins = &plugin;
  args.plugin_count = 1;

  bebopc_error_code_t err = bebopc_config_merge_cli(&cfg, &ctx, &args);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);

  TEST_ASSERT_EQUAL(2, cfg.plugin_count);
  TEST_ASSERT_EQUAL_STRING("c", cfg.plugins[0].name);
  TEST_ASSERT_EQUAL_STRING("ts", cfg.plugins[1].name);
}

void test_merge_cli_flags(void)
{
  cfg.verbose = false;
  cfg.quiet = false;

  cli_args_t args = {0};
  args.verbose = true;
  args.quiet = true;
  args.color = CLI_COLOR_ALWAYS;
  args.format = CLI_FORMAT_JSON;

  bebopc_error_code_t err = bebopc_config_merge_cli(&cfg, &ctx, &args);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);

  TEST_ASSERT_TRUE(cfg.verbose);
  TEST_ASSERT_TRUE(cfg.quiet);
  TEST_ASSERT_EQUAL(CLI_COLOR_ALWAYS, cfg.color);
  TEST_ASSERT_EQUAL(CLI_FORMAT_JSON, cfg.format);
}

void test_load_no_config(void);
void test_load_missing_explicit_config(void);
void test_load_yaml_config(void);

void test_load_no_config(void)
{
  char* orig_cwd = bebopc_getcwd();

#ifdef _WIN32
  const char* tmpdir = getenv("TEMP");
  if (!tmpdir) {
    tmpdir = getenv("TMP");
  }
  if (!tmpdir) {
    tmpdir = "C:\\Windows\\Temp";
  }
#else
  const char* tmpdir = "/tmp";
#endif
  int rc = chdir(tmpdir);
  TEST_ASSERT_EQUAL(0, rc);

  bebopc_error_code_t err = bebopc_config_load(&cfg, &ctx, NULL);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_NULL(cfg.config_path);

  if (orig_cwd) {
    int rc2 = chdir(orig_cwd);
    TEST_ASSERT_EQUAL(0, rc2);
    free(orig_cwd);
  }
}

void test_load_missing_explicit_config(void)
{
  bebopc_error_code_t err = bebopc_config_load(&cfg, &ctx, "/nonexistent/bebop.yml");
  TEST_ASSERT_EQUAL(BEBOPC_ERR_NOT_FOUND, err);
}

void test_load_yaml_config(void)
{
  const char* config_path = BEBOP_TEST_FIXTURES_DIR "/bebop.yml";
  bebopc_error_code_t err = bebopc_config_load(&cfg, &ctx, config_path);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_NOT_NULL(cfg.config_path);

  TEST_ASSERT_EQUAL(2, cfg.source_count);
  TEST_ASSERT_EQUAL_STRING("schemas/**/*.bop", cfg.sources[0]);
  TEST_ASSERT_EQUAL_STRING("shared/*.bop", cfg.sources[1]);

  TEST_ASSERT_EQUAL(1, cfg.exclude_count);
  TEST_ASSERT_EQUAL_STRING("**/vendor/**", cfg.exclude[0]);

  TEST_ASSERT_EQUAL(1, cfg.include_count);
  TEST_ASSERT_EQUAL_STRING("vendor/bebop-std", cfg.include[0]);

  TEST_ASSERT_EQUAL(2, cfg.plugin_count);
  TEST_ASSERT_EQUAL_STRING("c", cfg.plugins[0].name);
  TEST_ASSERT_EQUAL_STRING("./generated/c", cfg.plugins[0].out_dir);
  TEST_ASSERT_EQUAL_STRING("typescript", cfg.plugins[1].name);
  TEST_ASSERT_EQUAL_STRING("./generated/ts", cfg.plugins[1].out_dir);

  TEST_ASSERT_EQUAL(2, cfg.option_count);
  TEST_ASSERT_EQUAL_STRING("namespace", cfg.options[0].key);
  TEST_ASSERT_EQUAL_STRING("MyApp.Models", cfg.options[0].value);
  TEST_ASSERT_EQUAL_STRING("emit_source_info", cfg.options[1].key);
  TEST_ASSERT_EQUAL_STRING("true", cfg.options[1].value);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_config_init);
  RUN_TEST(test_config_cleanup);

  RUN_TEST(test_add_source);
  RUN_TEST(test_add_multiple_sources);
  RUN_TEST(test_add_exclude);
  RUN_TEST(test_add_include);
  RUN_TEST(test_add_plugin);
  RUN_TEST(test_add_option);

  RUN_TEST(test_merge_cli_plugins);
  RUN_TEST(test_merge_cli_flags);

  RUN_TEST(test_load_no_config);
  RUN_TEST(test_load_missing_explicit_config);
  RUN_TEST(test_load_yaml_config);

  return UNITY_END();
}
