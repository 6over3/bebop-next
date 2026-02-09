#include <string.h>

#include "cli/bebopc_cli.h"
#include "cli/bebopc_cli_defs.h"
#include "unity.h"

void setUp(void) {}

void tearDown(void) {}

void test_find_command_build(void);
void test_find_command_watch(void);
void test_find_command_unknown(void);
void test_find_command_null(void);

void test_find_command_build(void)
{
  TEST_ASSERT_EQUAL(CLI_CMD_BUILD, cli_find_command("build"));
}

void test_find_command_watch(void)
{
  TEST_ASSERT_EQUAL(CLI_CMD_WATCH, cli_find_command("watch"));
}

void test_find_command_unknown(void)
{
  TEST_ASSERT_EQUAL(CLI_CMD_NONE, cli_find_command("unknown"));
}

void test_find_command_null(void)
{
  TEST_ASSERT_EQUAL(CLI_CMD_NONE, cli_find_command(NULL));
}

void test_find_option_short_h(void);
void test_find_option_short_v(void);
void test_find_option_short_unknown(void);
void test_find_option_long_help(void);
void test_find_option_long_verbose(void);
void test_find_option_long_unknown(void);

void test_find_option_short_h(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_HELP, cli_find_option_short('h'));
}

void test_find_option_short_v(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_VERBOSE, cli_find_option_short('v'));
}

void test_find_option_short_unknown(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_COUNT, cli_find_option_short('z'));
}

void test_find_option_long_help(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_HELP, cli_find_option_long("help"));
}

void test_find_option_long_verbose(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_VERBOSE, cli_find_option_long("verbose"));
}

void test_find_option_long_unknown(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_COUNT, cli_find_option_long("unknown"));
}

void test_find_shell_bash(void);
void test_find_shell_zsh(void);
void test_find_shell_case_insensitive(void);
void test_find_shell_unknown(void);

void test_find_shell_bash(void)
{
  TEST_ASSERT_EQUAL(CLI_SHELL_BASH, cli_find_shell("bash"));
}

void test_find_shell_zsh(void)
{
  TEST_ASSERT_EQUAL(CLI_SHELL_ZSH, cli_find_shell("zsh"));
}

void test_find_shell_case_insensitive(void)
{
  TEST_ASSERT_EQUAL(CLI_SHELL_BASH, cli_find_shell("BASH"));
  TEST_ASSERT_EQUAL(CLI_SHELL_ZSH, cli_find_shell("Zsh"));
}

void test_find_shell_unknown(void)
{
  TEST_ASSERT_EQUAL(CLI_SHELL_UNKNOWN, cli_find_shell("unknown"));
}

void test_parse_build_command(void);
void test_parse_watch_command(void);
void test_parse_check_command(void);
void test_parse_completion_with_shell(void);
void test_parse_help_with_command(void);
void test_parse_unknown_command(void);

void test_parse_build_command(void)
{
  char* argv[] = {"bebopc", "build"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_BUILD, args.command);
  TEST_ASSERT_NULL(error);

  cli_args_cleanup(&args);
}

void test_parse_watch_command(void)
{
  char* argv[] = {"bebopc", "watch"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_WATCH, args.command);

  cli_args_cleanup(&args);
}

void test_parse_check_command(void)
{
  char* argv[] = {"bebopc", "check"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_CHECK, args.command);

  cli_args_cleanup(&args);
}

void test_parse_completion_with_shell(void)
{
  char* argv[] = {"bebopc", "completion", "bash"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_COMPLETION, args.command);
  TEST_ASSERT_EQUAL_STRING("bash", args.command_arg);

  cli_args_cleanup(&args);
}

void test_parse_help_with_command(void)
{
  char* argv[] = {"bebopc", "help", "build"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_HELP, args.command);
  TEST_ASSERT_EQUAL_STRING("build", args.command_arg);

  cli_args_cleanup(&args);
}

void test_parse_unknown_command(void)
{
  char* argv[] = {"bebopc", "unknown"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_ERR_INVALID_ARG, err);
  TEST_ASSERT_NOT_NULL(error);

  cli_args_cleanup(&args);
}

void test_parse_short_help(void);
void test_parse_long_help(void);
void test_parse_short_verbose(void);
void test_parse_long_verbose(void);
void test_parse_combined_short_options(void);
void test_parse_unknown_option(void);

void test_parse_short_help(void)
{
  char* argv[] = {"bebopc", "-h"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_TRUE(args.help);

  cli_args_cleanup(&args);
}

void test_parse_long_help(void)
{
  char* argv[] = {"bebopc", "--help"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_TRUE(args.help);

  cli_args_cleanup(&args);
}

void test_parse_short_verbose(void)
{
  char* argv[] = {"bebopc", "build", "-v"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_BUILD, args.command);
  TEST_ASSERT_TRUE(args.verbose);

  cli_args_cleanup(&args);
}

void test_parse_long_verbose(void)
{
  char* argv[] = {"bebopc", "build", "--verbose"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_TRUE(args.verbose);

  cli_args_cleanup(&args);
}

void test_parse_combined_short_options(void)
{
  char* argv[] = {"bebopc", "build", "-vq"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_ERR_INVALID_ARG, err);
  TEST_ASSERT_NOT_NULL(error);
}

void test_parse_unknown_option(void)
{
  char* argv[] = {"bebopc", "build", "--unknown"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_ERR_INVALID_ARG, err);
  TEST_ASSERT_NOT_NULL(error);

  cli_args_cleanup(&args);
}

void test_parse_config_short(void);
void test_parse_config_long(void);
void test_parse_config_equals(void);
void test_parse_plugin_out(void);
void test_parse_multiple_plugins(void);
void test_parse_plugin_path(void);
void test_parse_exclude(void);
void test_parse_option_key_value(void);
void test_parse_missing_value(void);

void test_parse_config_short(void)
{
  char* argv[] = {"bebopc", "build", "-c", "bebop.yml"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL_STRING("bebop.yml", args.config_path);

  cli_args_cleanup(&args);
}

void test_parse_config_long(void)
{
  char* argv[] = {"bebopc", "build", "--config", "bebop.yml"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL_STRING("bebop.yml", args.config_path);

  cli_args_cleanup(&args);
}

void test_parse_config_equals(void)
{
  char* argv[] = {"bebopc", "build", "--config=bebop.yml"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL_STRING("bebop.yml", args.config_path);

  cli_args_cleanup(&args);
}

void test_parse_plugin_out(void)
{
  char* argv[] = {"bebopc", "build", "--c_out=./out"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(1, args.plugin_count);
  TEST_ASSERT_EQUAL_STRING("c", args.plugins[0].name);
  TEST_ASSERT_EQUAL_STRING("./out", args.plugins[0].out_dir);

  cli_args_cleanup(&args);
}

void test_parse_multiple_plugins(void)
{
  char* argv[] = {"bebopc", "build", "--c_out=./c_out", "--ts_out=./ts_out"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(2, args.plugin_count);
  TEST_ASSERT_EQUAL_STRING("c", args.plugins[0].name);
  TEST_ASSERT_EQUAL_STRING("./c_out", args.plugins[0].out_dir);
  TEST_ASSERT_EQUAL_STRING("ts", args.plugins[1].name);
  TEST_ASSERT_EQUAL_STRING("./ts_out", args.plugins[1].out_dir);

  cli_args_cleanup(&args);
}

void test_parse_plugin_path(void)
{
  char* argv[] = {"bebopc", "build", "--plugin=custom=/usr/bin/plugin", "--custom_out=./out"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(1, args.plugin_count);
  TEST_ASSERT_EQUAL_STRING("custom", args.plugins[0].name);
  TEST_ASSERT_EQUAL_STRING("./out", args.plugins[0].out_dir);
  TEST_ASSERT_EQUAL_STRING("/usr/bin/plugin", args.plugins[0].path);

  cli_args_cleanup(&args);
}

void test_parse_exclude(void)
{
  char* argv[] = {"bebopc", "build", "-x", "**/vendor/**"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(1, args.exclude_count);
  TEST_ASSERT_EQUAL_STRING("**/vendor/**", args.excludes[0]);

  cli_args_cleanup(&args);
}

void test_parse_option_key_value(void)
{
  char* argv[] = {"bebopc", "build", "-D", "namespace=MyApp"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(1, args.option_count);
  TEST_ASSERT_EQUAL_STRING("namespace", args.options[0].key);
  TEST_ASSERT_EQUAL_STRING("MyApp", args.options[0].value);

  cli_args_cleanup(&args);
}

void test_parse_missing_value(void)
{
  char* argv[] = {"bebopc", "build", "-c"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_ERR_INVALID_ARG, err);
  TEST_ASSERT_NOT_NULL(error);

  cli_args_cleanup(&args);
}

void test_parse_single_file(void);
void test_parse_multiple_files(void);
void test_parse_file_as_command(void);
void test_parse_double_dash_files(void);

void test_parse_single_file(void)
{
  char* argv[] = {"bebopc", "build", "schema.bop"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 3, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_BUILD, args.command);
  TEST_ASSERT_EQUAL(1, args.file_count);
  TEST_ASSERT_EQUAL_STRING("schema.bop", args.files[0]);

  cli_args_cleanup(&args);
}

void test_parse_multiple_files(void)
{
  char* argv[] = {"bebopc", "build", "a.bop", "b.bop", "c.bop"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 5, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(3, args.file_count);
  TEST_ASSERT_EQUAL_STRING("a.bop", args.files[0]);
  TEST_ASSERT_EQUAL_STRING("b.bop", args.files[1]);
  TEST_ASSERT_EQUAL_STRING("c.bop", args.files[2]);

  cli_args_cleanup(&args);
}

void test_parse_file_as_command(void)
{
  char* argv[] = {"bebopc", "schema.bop"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 2, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(CLI_CMD_BUILD, args.command);
  TEST_ASSERT_EQUAL(1, args.file_count);
  TEST_ASSERT_EQUAL_STRING("schema.bop", args.files[0]);

  cli_args_cleanup(&args);
}

void test_parse_double_dash_files(void)
{
  char* argv[] = {"bebopc", "build", "--", "-weird-file.bop"};
  cli_args_t args;
  const char* error = NULL;

  bebopc_error_code_t err = cli_parse(&args, 4, argv, &error);
  TEST_ASSERT_EQUAL(BEBOPC_OK, err);
  TEST_ASSERT_EQUAL(1, args.file_count);
  TEST_ASSERT_EQUAL_STRING("-weird-file.bop", args.files[0]);

  cli_args_cleanup(&args);
}

void test_options_table_count(void);
void test_commands_table_count(void);
void test_shells_table_count(void);
void test_command_def_has_extended_help(void);

void test_options_table_count(void)
{
  TEST_ASSERT_EQUAL(CLI_OPT_COUNT, sizeof(cli_options) / sizeof(cli_options[0]));
}

void test_commands_table_count(void)
{
  TEST_ASSERT_EQUAL(CLI_CMD_COUNT, sizeof(cli_commands) / sizeof(cli_commands[0]));
}

void test_shells_table_count(void)
{
  TEST_ASSERT_EQUAL(CLI_SHELL_COUNT, sizeof(cli_shells) / sizeof(cli_shells[0]));
}

void test_command_def_has_extended_help(void)
{
  const cli_cmd_def_t* build = cli_get_command_def(CLI_CMD_BUILD);
  TEST_ASSERT_NOT_NULL(build);
  TEST_ASSERT_NOT_NULL(build->extended_help);

  const cli_cmd_def_t* version = cli_get_command_def(CLI_CMD_VERSION);
  TEST_ASSERT_NOT_NULL(version);
  TEST_ASSERT_NULL(version->extended_help);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_find_command_build);
  RUN_TEST(test_find_command_watch);
  RUN_TEST(test_find_command_unknown);
  RUN_TEST(test_find_command_null);

  RUN_TEST(test_find_option_short_h);
  RUN_TEST(test_find_option_short_v);
  RUN_TEST(test_find_option_short_unknown);
  RUN_TEST(test_find_option_long_help);
  RUN_TEST(test_find_option_long_verbose);
  RUN_TEST(test_find_option_long_unknown);

  RUN_TEST(test_find_shell_bash);
  RUN_TEST(test_find_shell_zsh);
  RUN_TEST(test_find_shell_case_insensitive);
  RUN_TEST(test_find_shell_unknown);

  RUN_TEST(test_parse_build_command);
  RUN_TEST(test_parse_watch_command);
  RUN_TEST(test_parse_check_command);
  RUN_TEST(test_parse_completion_with_shell);
  RUN_TEST(test_parse_help_with_command);
  RUN_TEST(test_parse_unknown_command);

  RUN_TEST(test_parse_short_help);
  RUN_TEST(test_parse_long_help);
  RUN_TEST(test_parse_short_verbose);
  RUN_TEST(test_parse_long_verbose);
  RUN_TEST(test_parse_combined_short_options);
  RUN_TEST(test_parse_unknown_option);

  RUN_TEST(test_parse_config_short);
  RUN_TEST(test_parse_config_long);
  RUN_TEST(test_parse_config_equals);
  RUN_TEST(test_parse_plugin_out);
  RUN_TEST(test_parse_multiple_plugins);
  RUN_TEST(test_parse_plugin_path);
  RUN_TEST(test_parse_exclude);
  RUN_TEST(test_parse_option_key_value);
  RUN_TEST(test_parse_missing_value);

  RUN_TEST(test_parse_single_file);
  RUN_TEST(test_parse_multiple_files);
  RUN_TEST(test_parse_file_as_command);
  RUN_TEST(test_parse_double_dash_files);

  RUN_TEST(test_options_table_count);
  RUN_TEST(test_commands_table_count);
  RUN_TEST(test_shells_table_count);
  RUN_TEST(test_command_def_has_extended_help);

  return UNITY_END();
}
