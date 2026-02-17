#ifndef BEBOPC_CLI_H
#define BEBOPC_CLI_H

#include <stdbool.h>
#include <stdint.h>

#include "../bebopc_error.h"
#include "bebopc_cli_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bebopc_config bebopc_config_t;

typedef enum {
  CLI_COLOR_AUTO = 0,
  CLI_COLOR_ALWAYS,
  CLI_COLOR_NEVER
} cli_color_mode_t;

typedef enum {
  CLI_FORMAT_TERMINAL = 0,
  CLI_FORMAT_JSON,
  CLI_FORMAT_MSBUILD,
  CLI_FORMAT_XCODE
} cli_format_t;

typedef struct {
  char* name;
  char* out_dir;
  char* path;
} cli_plugin_t;

typedef struct {
  char* key;
  char* value;
} cli_option_t;

typedef struct {
  cli_cmd_t command;
  const char* command_arg;

  char** files;
  uint32_t file_count;

  cli_plugin_t* plugins;
  uint32_t plugin_count;

  char** excludes;
  uint32_t exclude_count;

  char** includes;
  uint32_t include_count;

  cli_option_t* options;
  uint32_t option_count;

  char* config_path;

  cli_color_mode_t color;
  cli_format_t format;
  bool verbose;
  bool quiet;
  bool no_warn;
  bool trace;
  bool help;
  bool version;
  bool llm;

  bool no_emit;
  bool preserve_output;
  bool emit_source_info;

  bool fmt_check;
  bool fmt_diff;
} cli_args_t;

typedef struct {
  const char* program_name;
  const char* version;
  const char* description;
} cli_info_t;

bebopc_error_code_t cli_parse(cli_args_t* args,
                              int argc,
                              char** argv,
                              const char** error_msg);

void cli_args_cleanup(cli_args_t* args);

void cli_print_usage(const cli_info_t* info);

void cli_print_command_help(const cli_info_t* info, cli_cmd_t cmd);

void cli_print_version(const cli_info_t* info);

cli_cmd_t cli_find_command(const char* name);

cli_opt_t cli_find_option_short(char c);

cli_opt_t cli_find_option_long(const char* name);

const cli_opt_def_t* cli_get_option_def(cli_opt_t opt);

const cli_cmd_def_t* cli_get_command_def(cli_cmd_t cmd);

cli_shell_t cli_find_shell(const char* name);

const cli_shell_def_t* cli_get_shell_def(cli_shell_t shell);

#ifdef __cplusplus
}
#endif

#endif
