#include "bebopc_cli.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "../bebopc_log.h"
#include "../bebopc_utils.h"

static bool _use_colors(void)
{
  return isatty(fileno(stdout)) != 0;
}

#define cli_markup(...) bebopc_markup_to(stdout, _use_colors(), __VA_ARGS__)
#define cli_markup_line(...) \
  do { \
    bebopc_markup_to(stdout, _use_colors(), __VA_ARGS__); \
    putchar('\n'); \
  } while (0)

static bool _add_file(cli_args_t* args, const char* path)
{
  uint32_t new_count = args->file_count + 1;
  char** new_files = realloc(args->files, new_count * sizeof(char*));
  if (!new_files) {
    return false;
  }
  args->files = new_files;
  args->files[args->file_count] = bebopc_strdup(path);
  if (!args->files[args->file_count]) {
    return false;
  }
  args->file_count = new_count;
  return true;
}

static cli_plugin_t* _find_or_add_plugin(cli_args_t* args, const char* name)
{
  for (uint32_t i = 0; i < args->plugin_count; i++) {
    if (bebopc_streq(args->plugins[i].name, name)) {
      return &args->plugins[i];
    }
  }

  uint32_t new_count = args->plugin_count + 1;
  cli_plugin_t* new_plugins = realloc(args->plugins, new_count * sizeof(cli_plugin_t));
  if (!new_plugins) {
    return NULL;
  }
  args->plugins = new_plugins;

  cli_plugin_t* p = &args->plugins[args->plugin_count];
  memset(p, 0, sizeof(*p));
  p->name = bebopc_strdup(name);
  if (!p->name) {
    return NULL;
  }

  args->plugin_count = new_count;
  return p;
}

static bool _set_plugin_path(cli_args_t* args, const char* spec)
{
  const char* eq = bebopc_strchr(spec, '=');
  if (!eq) {
    return false;
  }

  size_t name_len = (size_t)(eq - spec);
  char* name = bebopc_strndup(spec, name_len);
  if (!name) {
    return false;
  }

  cli_plugin_t* p = _find_or_add_plugin(args, name);
  free(name);
  if (!p) {
    return false;
  }

  free(p->path);
  p->path = bebopc_strdup(eq + 1);
  return p->path != NULL;
}

static bool _set_plugin_out(cli_args_t* args, const char* name, const char* dir)
{
  cli_plugin_t* p = _find_or_add_plugin(args, name);
  if (!p) {
    return false;
  }

  free(p->out_dir);
  p->out_dir = bebopc_strdup(dir);
  return p->out_dir != NULL;
}

static bool _add_exclude(cli_args_t* args, const char* pattern)
{
  uint32_t new_count = args->exclude_count + 1;
  char** new_excludes = realloc(args->excludes, new_count * sizeof(char*));
  if (!new_excludes) {
    return false;
  }
  args->excludes = new_excludes;
  args->excludes[args->exclude_count] = bebopc_strdup(pattern);
  if (!args->excludes[args->exclude_count]) {
    return false;
  }
  args->exclude_count = new_count;
  return true;
}

static bool _add_include(cli_args_t* args, const char* path)
{
  uint32_t new_count = args->include_count + 1;
  char** new_includes = realloc(args->includes, new_count * sizeof(char*));
  if (!new_includes) {
    return false;
  }
  args->includes = new_includes;
  args->includes[args->include_count] = bebopc_strdup(path);
  if (!args->includes[args->include_count]) {
    return false;
  }
  args->include_count = new_count;
  return true;
}

static bool _try_parse_plugin_out(const char* opt_name, const char* value, cli_args_t* args)
{
  size_t len = bebopc_strlen(opt_name);
  if (len < 5) {
    return false;
  }

  if (opt_name[len - 4] != '_' || opt_name[len - 3] != 'o' || opt_name[len - 2] != 'u'
      || opt_name[len - 1] != 't')
  {
    return false;
  }

  char* plugin_name = bebopc_strndup(opt_name, len - 4);
  if (!plugin_name) {
    return false;
  }

  bool ok = _set_plugin_out(args, plugin_name, value);
  free(plugin_name);
  return ok;
}

static bool _add_option(cli_args_t* args, const char* spec)
{
  if (!spec) {
    return false;
  }
  const char* eq = bebopc_strchr(spec, '=');
  if (!eq) {
    return false;
  }

  uint32_t new_count = args->option_count + 1;
  cli_option_t* new_opts = realloc(args->options, new_count * sizeof(cli_option_t));
  if (!new_opts) {
    return false;
  }
  args->options = new_opts;

  size_t key_len = (size_t)(eq - spec);
  args->options[args->option_count].key = bebopc_strndup(spec, key_len);
  args->options[args->option_count].value = bebopc_strdup(eq + 1);

  if (!args->options[args->option_count].key || !args->options[args->option_count].value) {
    free(args->options[args->option_count].key);
    free(args->options[args->option_count].value);
    return false;
  }

  args->option_count = new_count;
  return true;
}

static cli_color_mode_t _parse_color_mode(const char* str)
{
  if (bebopc_strcaseeq(str, "auto")) {
    return CLI_COLOR_AUTO;
  }
  if (bebopc_strcaseeq(str, "always")) {
    return CLI_COLOR_ALWAYS;
  }
  if (bebopc_strcaseeq(str, "never")) {
    return CLI_COLOR_NEVER;
  }
  return CLI_COLOR_AUTO;
}

static cli_format_t _parse_format(const char* str)
{
  if (bebopc_strcaseeq(str, "terminal")) {
    return CLI_FORMAT_TERMINAL;
  }
  if (bebopc_strcaseeq(str, "json")) {
    return CLI_FORMAT_JSON;
  }
  if (bebopc_strcaseeq(str, "msbuild")) {
    return CLI_FORMAT_MSBUILD;
  }
  if (bebopc_strcaseeq(str, "xcode")) {
    return CLI_FORMAT_XCODE;
  }
  return CLI_FORMAT_TERMINAL;
}

cli_cmd_t cli_find_command(const char* name)
{
  if (!name) {
    return CLI_CMD_NONE;
  }
  for (int i = 0; i < CLI_CMD_COUNT; i++) {
    if (bebopc_streq(name, cli_commands[i].name)) {
      return (cli_cmd_t)i;
    }
  }
  return CLI_CMD_NONE;
}

cli_opt_t cli_find_option_short(char c)
{
  if (c == 0) {
    return CLI_OPT_COUNT;
  }
  for (int i = 0; i < CLI_OPT_COUNT; i++) {
    if (cli_options[i].short_name == c) {
      return (cli_opt_t)i;
    }
  }
  return CLI_OPT_COUNT;
}

cli_opt_t cli_find_option_long(const char* name)
{
  if (!name) {
    return CLI_OPT_COUNT;
  }
  for (int i = 0; i < CLI_OPT_COUNT; i++) {
    if (bebopc_streq(name, cli_options[i].long_name)) {
      return (cli_opt_t)i;
    }
  }
  return CLI_OPT_COUNT;
}

cli_shell_t cli_find_shell(const char* name)
{
  if (!name) {
    return CLI_SHELL_UNKNOWN;
  }
  for (int i = 0; i < CLI_SHELL_COUNT; i++) {
    if (bebopc_strcaseeq(name, cli_shells[i].name)) {
      return (cli_shell_t)i;
    }
  }
  return CLI_SHELL_UNKNOWN;
}

const cli_opt_def_t* cli_get_option_def(cli_opt_t opt)
{
  if (opt < 0 || opt >= CLI_OPT_COUNT) {
    return NULL;
  }
  return &cli_options[opt];
}

const cli_cmd_def_t* cli_get_command_def(cli_cmd_t cmd)
{
  if (cmd < 0 || cmd >= CLI_CMD_COUNT) {
    return NULL;
  }
  return &cli_commands[cmd];
}

const cli_shell_def_t* cli_get_shell_def(cli_shell_t shell)
{
  if (shell < 0 || shell >= CLI_SHELL_COUNT) {
    return NULL;
  }
  return &cli_shells[shell];
}

bebopc_error_code_t cli_parse(cli_args_t* args, int argc, char** argv, const char** error_msg)
{
  memset(args, 0, sizeof(*args));
  args->command = CLI_CMD_NONE;
  args->color = CLI_COLOR_AUTO;
  args->format = CLI_FORMAT_TERMINAL;

  static char error_buf[256];
  *error_msg = NULL;

  int i = 1;
  bool parsing_options = true;

  while (i < argc) {
    const char* arg = argv[i];

    if (parsing_options && bebopc_streq(arg, "--")) {
      parsing_options = false;
      i++;
      continue;
    }

    if (parsing_options && arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
      const char* opt_name = arg + 2;
      const char* eq = bebopc_strchr(opt_name, '=');
      char name_buf[64];
      const char* value = NULL;

      if (eq) {
        size_t len = (size_t)(eq - opt_name);
        if (len >= sizeof(name_buf)) {
          snprintf(error_buf, sizeof(error_buf), "unknown option: %s", arg);
          *error_msg = error_buf;
          return BEBOPC_ERR_INVALID_ARG;
        }
        memcpy(name_buf, opt_name, len);
        name_buf[len] = '\0';
        opt_name = name_buf;
        value = eq + 1;
      }

      if (_try_parse_plugin_out(opt_name, value, args)) {
        if (!value) {
          snprintf(error_buf, sizeof(error_buf), "option --%s requires a value", opt_name);
          *error_msg = error_buf;
          return BEBOPC_ERR_INVALID_ARG;
        }
        i++;
        continue;
      }

      cli_opt_t opt = cli_find_option_long(opt_name);
      if (opt == CLI_OPT_COUNT) {
        snprintf(error_buf, sizeof(error_buf), "unknown option: --%s", opt_name);
        *error_msg = error_buf;
        return BEBOPC_ERR_INVALID_ARG;
      }

      const cli_opt_def_t* def = &cli_options[opt];
      if (def->has_value && !value) {
        if (i + 1 >= argc) {
          snprintf(error_buf, sizeof(error_buf), "option --%s requires a value", opt_name);
          *error_msg = error_buf;
          return BEBOPC_ERR_INVALID_ARG;
        }
        value = argv[++i];
      }

      switch (opt) {
        case CLI_OPT_HELP:
          args->help = true;
          break;
        case CLI_OPT_VERSION:
          args->version = true;
          break;
        case CLI_OPT_LLM:
          args->llm = true;
          break;
        case CLI_OPT_VERBOSE:
          args->verbose = true;
          break;
        case CLI_OPT_QUIET:
          args->quiet = true;
          break;
        case CLI_OPT_NO_WARN:
          args->no_warn = true;
          break;
        case CLI_OPT_TRACE:
          args->trace = true;
          break;
        case CLI_OPT_NO_EMIT:
          args->no_emit = true;
          break;
        case CLI_OPT_PRESERVE_OUTPUT:
          args->preserve_output = true;
          break;
        case CLI_OPT_EMIT_SOURCE_INFO:
          args->emit_source_info = true;
          break;
        case CLI_OPT_CONFIG:
          free(args->config_path);
          args->config_path = bebopc_strdup(value);
          break;
        case CLI_OPT_COLOR:
          args->color = _parse_color_mode(value);
          break;
        case CLI_OPT_FORMAT:
          args->format = _parse_format(value);
          break;
        case CLI_OPT_PLUGIN:
          if (!_set_plugin_path(args, value)) {
            snprintf(
                error_buf, sizeof(error_buf), "invalid plugin format: %s (expected NAME=PATH)", value
            );
            *error_msg = error_buf;
            return BEBOPC_ERR_INVALID_ARG;
          }
          break;
        case CLI_OPT_EXCLUDE:
          if (!_add_exclude(args, value)) {
            return BEBOPC_ERR_OUT_OF_MEMORY;
          }
          break;
        case CLI_OPT_INCLUDE:
          if (!_add_include(args, value)) {
            return BEBOPC_ERR_OUT_OF_MEMORY;
          }
          break;
        case CLI_OPT_OPTION:
          if (!_add_option(args, value)) {
            snprintf(
                error_buf, sizeof(error_buf), "invalid option format: %s (expected KEY=VALUE)", value
            );
            *error_msg = error_buf;
            return BEBOPC_ERR_INVALID_ARG;
          }
          break;
        case CLI_OPT_FMT_CHECK:
          args->fmt_check = true;
          break;
        case CLI_OPT_FMT_DIFF:
          args->fmt_diff = true;
          break;
        case CLI_OPT_FMT_WRITE:
          break;
        default:
          break;
      }
      i++;
      continue;
    }

    if (parsing_options && arg[0] == '-' && arg[1] != '\0' && arg[1] != '-') {
      size_t len = bebopc_strlen(arg);
      for (size_t j = 1; j < len; j++) {
        char c = arg[j];
        cli_opt_t opt = cli_find_option_short(c);
        if (opt == CLI_OPT_COUNT) {
          snprintf(error_buf, sizeof(error_buf), "unknown option: -%c", c);
          *error_msg = error_buf;
          return BEBOPC_ERR_INVALID_ARG;
        }

        const cli_opt_def_t* def = &cli_options[opt];
        const char* value = NULL;

        if (def->has_value) {
          if (arg[j + 1] != '\0') {
            value = &arg[j + 1];
            j = len;
          } else if (i + 1 < argc) {
            value = argv[++i];
          } else {
            snprintf(error_buf, sizeof(error_buf), "option -%c requires a value", c);
            *error_msg = error_buf;
            return BEBOPC_ERR_INVALID_ARG;
          }
        }

        switch (opt) {
          case CLI_OPT_HELP:
            args->help = true;
            break;
          case CLI_OPT_VERSION:
            args->version = true;
            break;
          case CLI_OPT_VERBOSE:
            args->verbose = true;
            break;
          case CLI_OPT_QUIET:
            args->quiet = true;
            break;
          case CLI_OPT_CONFIG:
            free(args->config_path);
            args->config_path = bebopc_strdup(value);
            break;
          case CLI_OPT_EXCLUDE:
            if (!_add_exclude(args, value)) {
              return BEBOPC_ERR_OUT_OF_MEMORY;
            }
            break;
          case CLI_OPT_INCLUDE:
            if (!_add_include(args, value)) {
              return BEBOPC_ERR_OUT_OF_MEMORY;
            }
            break;
          case CLI_OPT_OPTION:
            if (!_add_option(args, value)) {
              snprintf(
                  error_buf,
                  sizeof(error_buf),
                  "invalid option format: %s (expected KEY=VALUE)",
                  value
              );
              *error_msg = error_buf;
              return BEBOPC_ERR_INVALID_ARG;
            }
            break;
          case CLI_OPT_FMT_WRITE:
            break;
          default:
            break;
        }
      }
      i++;
      continue;
    }

    if (args->command == CLI_CMD_NONE) {
      args->command = cli_find_command(arg);
      if (args->command == CLI_CMD_NONE) {
        if (bebopc_strchr(arg, '/') || bebopc_strchr(arg, '\\') || bebopc_strchr(arg, '.')) {
          args->command = CLI_CMD_BUILD;
          if (!_add_file(args, arg)) {
            return BEBOPC_ERR_OUT_OF_MEMORY;
          }
        } else {
          snprintf(error_buf, sizeof(error_buf), "unknown command: %s", arg);
          *error_msg = error_buf;
          return BEBOPC_ERR_INVALID_ARG;
        }
      } else if (args->command == CLI_CMD_COMPLETION || args->command == CLI_CMD_HELP) {
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          args->command_arg = argv[++i];
        }
      }
    } else {
      if (!_add_file(args, arg)) {
        return BEBOPC_ERR_OUT_OF_MEMORY;
      }
    }
    i++;
  }

  if (args->verbose && args->quiet) {
    *error_msg = "--verbose and --quiet are mutually exclusive";
    return BEBOPC_ERR_INVALID_ARG;
  }

  return BEBOPC_OK;
}

void cli_args_cleanup(cli_args_t* args)
{
  for (uint32_t i = 0; i < args->file_count; i++) {
    free(args->files[i]);
  }
  free(args->files);

  for (uint32_t i = 0; i < args->plugin_count; i++) {
    free(args->plugins[i].name);
    free(args->plugins[i].out_dir);
    free(args->plugins[i].path);
  }
  free(args->plugins);

  for (uint32_t i = 0; i < args->exclude_count; i++) {
    free(args->excludes[i]);
  }
  free(args->excludes);

  for (uint32_t i = 0; i < args->include_count; i++) {
    free(args->includes[i]);
  }
  free(args->includes);

  for (uint32_t i = 0; i < args->option_count; i++) {
    free(args->options[i].key);
    free(args->options[i].value);
  }
  free(args->options);

  free(args->config_path);

  memset(args, 0, sizeof(*args));
}

static void _print_option(const cli_opt_def_t* def)
{
  if (def->short_name) {
    cli_markup("  [cyan]-%c[/], ", def->short_name);
  } else {
    cli_markup("      ");
  }

  cli_markup("[cyan]--%s[/]", def->long_name);
  if (def->value_name) {
    cli_markup(" [dim]<%s>[/]", def->value_name);
  }

  int pad = 20 - (int)bebopc_strlen(def->long_name);
  if (def->value_name) {
    pad -= (int)bebopc_strlen(def->value_name) + 3;
  }
  if (pad < 1) {
    pad = 1;
  }
  cli_markup("%*s", pad, "");
  cli_markup_line("[white]%s[/]", def->description);
}

static uint32_t _cmd_to_flag(cli_cmd_t cmd)
{
  switch (cmd) {
    case CLI_CMD_BUILD:
      return CLI_OPT_FLAG_BUILD;
    case CLI_CMD_CHECK:
      return CLI_OPT_FLAG_CHECK;
    case CLI_CMD_WATCH:
      return CLI_OPT_FLAG_WATCH;
    case CLI_CMD_FMT:
      return CLI_OPT_FLAG_FMT;
    default:
      return 0;
  }
}

static void _print_options_for_cmd(cli_cmd_t cmd)
{
  uint32_t flag = _cmd_to_flag(cmd);
  for (int i = 0; i < CLI_OPT_COUNT; i++) {
    if (cli_options[i].flags & flag) {
      _print_option(&cli_options[i]);
    }
  }
}

static void _print_root_options(void)
{
  for (int i = 0; i < CLI_OPT_COUNT; i++) {
    if (cli_options[i].flags & CLI_OPT_FLAG_ROOT) {
      _print_option(&cli_options[i]);
    }
  }
}

void cli_print_usage(const cli_info_t* info)
{
  cli_markup_line(
      "[bold]Usage:[/] [green]%s[/] [cyan]<command>[/] [dim][options] " "[files...][/]",
      info->program_name
  );
  cli_markup_line("");
  cli_markup_line("%s", info->description);
  cli_markup_line("");

  cli_markup_line("[bold][yellow]Commands:[/]");
  for (int i = 0; i < CLI_CMD_COUNT; i++) {
    cli_markup_line(
        "  [green]%-12s[/]  [white]%s[/]", cli_commands[i].name, cli_commands[i].description
    );
  }

  cli_markup_line("");
  cli_markup_line("[bold][yellow]Global Options:[/]");
  _print_root_options();

  cli_markup_line("");
  cli_markup_line(
      "[dim]Run '%s help <command>' for more information on a command.[/]", info->program_name
  );
}

void cli_print_command_help(const cli_info_t* info, cli_cmd_t cmd)
{
  const cli_cmd_def_t* def = cli_get_command_def(cmd);
  if (!def) {
    cli_print_usage(info);
    return;
  }

  cli_markup("[bold]Usage:[/] [green]%s[/] [cyan]%s[/]", info->program_name, def->name);
  if (def->show_options) {
    cli_markup(" [dim][options][/]");
  }
  if (def->args_pattern && def->args_pattern[0]) {
    cli_markup(" [dim]%s[/]", def->args_pattern);
  }
  cli_markup_line("");
  cli_markup_line("");

  cli_markup_line("[white]%s.[/]", def->description);

  if (def->extended_help) {
    cli_markup_line("[dim]%s[/]", def->extended_help);
  }
  cli_markup_line("");

  if (cmd == CLI_CMD_COMPLETION) {
    cli_markup_line("[bold][yellow]Supported shells:[/]");
    for (int i = 0; i < CLI_SHELL_COUNT; i++) {
      cli_markup_line(
          "  [green]%-12s[/]  [white]%s[/]", cli_shells[i].name, cli_shells[i].description
      );
    }
    cli_markup_line("");
  }

  if (def->show_options) {
    cli_markup_line("[bold][yellow]Options:[/]");
    _print_options_for_cmd(cmd);
  }
}

void cli_print_version(const cli_info_t* info)
{
  cli_markup_line("[bold][green]%s[/] [cyan]%s[/]", info->program_name, info->version);
}
