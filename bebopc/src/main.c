#include <bebop.h>
#include <beboplsp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bebopc.h"
#include "bebopc_diag.h"
#include "bebopc_dir.h"
#include "bebopc_utils.h"
#include "cli/bebopc_cli.h"
#include "cli/bebopc_cli_completion.h"
#include "cli/bebopc_compile.h"
#include "cli/bebopc_config.h"
#include "cli/bebopc_runner.h"
#include "watcher/bebop_fsw.h"

#ifndef BEBOP_VERSION_STRING
#define BEBOP_VERSION_STRING "0.0.0-dev"
#endif

typedef struct main_ctx main_ctx_t;
static main_ctx_t* g_main_ctx;

#define BEBOPC_LOG_CTX (&g_main_ctx->log)
#include "bebopc_log.h"

bebopc_log_ctx_t* g_log_ctx;

struct main_ctx {
  bebopc_ctx_t base;
  bebopc_config_t cfg;
  diag_ctx_t diag;
  bebopc_log_ctx_t log;
  cli_args_t args;
};

static const cli_info_t cli_info = {
    .program_name = "bebopc",
    .version = BEBOP_VERSION_STRING,
    .description = "Bebop schema compiler and code generation tool"
};

static volatile sig_atomic_t g_interrupted = 0;

static void signal_handler(int sig)
{
  (void)sig;
  g_interrupted = 1;
}

static void* host_alloc(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
  (void)old_size;
  (void)ctx;
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new_size);
}

static bebop_file_result_t host_file_read(const char* path, void* ctx)
{
  (void)ctx;

  bool is_stdin = strcmp(path, "-") == 0;
  FILE* f = is_stdin ? stdin : fopen(path, "rb");
  if (!f) {
    return (bebop_file_result_t) {.error = "File not found"};
  }

  if (!is_stdin && fseek(f, 0, SEEK_END) == 0) {
    long len = ftell(f);
    if (len >= 0) {
      fseek(f, 0, SEEK_SET);
      char* content = malloc((size_t)len + 1);
      if (!content) {
        fclose(f);
        return (bebop_file_result_t) {.error = "Out of memory"};
      }
      size_t n = fread(content, 1, (size_t)len, f);
      fclose(f);
      content[n] = '\0';
      return (bebop_file_result_t) {.content = content, .content_len = n};
    }
    fseek(f, 0, SEEK_SET);
  }

  size_t capacity = 4096, size = 0;
  char* content = malloc(capacity);
  if (!content) {
    if (!is_stdin) {
      fclose(f);
    }
    return (bebop_file_result_t) {.error = "Out of memory"};
  }

  size_t n;
  while ((n = fread(content + size, 1, capacity - size, f)) > 0) {
    size += n;
    if (size == capacity) {
      capacity *= 2;
      char* grown = realloc(content, capacity);
      if (!grown) {
        free(content);
        if (!is_stdin) {
          fclose(f);
        }
        return (bebop_file_result_t) {.error = "Out of memory"};
      }
      content = grown;
    }
  }
  if (!is_stdin) {
    fclose(f);
  }
  content[size] = '\0';
  return (bebop_file_result_t) {.content = content, .content_len = size};
}

static bool host_file_exists(const char* path, void* ctx)
{
  (void)ctx;
  return bebopc_file_is_file(path);
}

static void ctx_init(main_ctx_t* ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  bebopc_error_init(&ctx->base.errors);
  bebopc_config_init(&ctx->cfg);

  ctx->base.host.allocator.alloc = host_alloc;
  ctx->base.host.file_reader.read = host_file_read;
  ctx->base.host.file_reader.exists = host_file_exists;
  ctx->base.cfg = &ctx->cfg;

  bebopc_console_init();
  diag_init(&ctx->diag);
  bebopc_log_ctx_init(&ctx->log, bebopc_color_supported());

  g_main_ctx = ctx;
  g_log_ctx = &ctx->log;
}

static void ctx_cleanup(main_ctx_t* ctx)
{
  cli_args_cleanup(&ctx->args);
  bebopc_config_cleanup(&ctx->cfg);
  bebopc_error_cleanup(&ctx->base.errors);
  g_main_ctx = NULL;
  g_log_ctx = NULL;
}

static int cmd_build(main_ctx_t* ctx)
{
  int ret = 1;
  const char** files = NULL;
  uint32_t file_count = 0;
  bebopc_compile_result_t result = {0};
  bebopc_runner_t runner = {0};
  bool runner_initialized = false;

  if (ctx->cfg.plugin_count == 0) {
    log_error("no output specified");
    log_info("Use --<plugin>_out=<dir> (e.g. --c_out=./generated)");
    goto cleanup;
  }

  files = bebopc_collect_files(&ctx->base, ctx->args.files, ctx->args.file_count, &file_count);
  if (!files || file_count == 0) {
    log_error("no input files matched");
    if (ctx->cfg.source_count > 0) {
      const char* root = ctx->cfg.project_root ? ctx->cfg.project_root : ".";
      log_info("Patterns searched from: %s", root);
      for (uint32_t i = 0; i < ctx->cfg.source_count; i++) {
        log_info("  - %s", ctx->cfg.sources[i]);
      }
    }
    goto cleanup;
  }

  if (ctx->args.verbose) {
    log_info("Compiling %u file(s)...", file_count);
  }

  bebopc_error_code_t err = bebopc_compile(&ctx->base, files, file_count, &result);
  bebopc_render_diagnostics(&ctx->base, result.result);

  uint32_t error_count = result.error_count;
  uint32_t warning_count = result.warning_count;

  if (err != BEBOPC_OK && err != BEBOPC_ERR_PARSE) {
    const bebopc_error_t* last = bebopc_error_last(&ctx->base.errors);
    log_error("%s", last && last->message ? last->message : bebopc_error_code_str(err));
    goto cleanup;
  }

  if (error_count > 0) {
    goto cleanup;
  }

  if (!result.desc) {
    if (!ctx->args.quiet) {
      log_info("No definitions to generate");
    }
    ret = 0;
    goto cleanup;
  }

  err = bebopc_runner_init(&runner, &ctx->base, result.beb_ctx, result.desc, files, file_count);
  if (err != BEBOPC_OK) {
    log_error("failed to initialize runner");
    goto cleanup;
  }
  runner_initialized = true;
  result.beb_ctx = NULL;
  result.desc = NULL;
  files = NULL;
  file_count = 0;

  if (ctx->args.verbose) {
    log_info("Running %u plugin(s)...", ctx->cfg.plugin_count);
  }

  err = bebopc_runner_generate(&runner);
  if (err != BEBOPC_OK) {
    const bebopc_error_t* last = bebopc_error_last(&ctx->base.errors);
    log_error("%s", last && last->message ? last->message : "code generation failed");
    goto cleanup;
  }

  if (!ctx->args.quiet) {
    if (warning_count > 0) {
      log_info("Build completed with %u warning(s)", warning_count);
    } else {
      log_info("Build completed");
    }
  }
  ret = 0;

cleanup:
  if (runner_initialized) {
    bebopc_runner_cleanup(&runner);
  }
  bebopc_compile_cleanup(&result);
  bebopc_files_free(files, file_count);
  return ret;
}

static int cmd_check(main_ctx_t* ctx)
{
  int ret = 1;
  const char** files = NULL;
  uint32_t file_count = 0;
  bebopc_compile_result_t result = {0};

  files = bebopc_collect_files(&ctx->base, ctx->args.files, ctx->args.file_count, &file_count);
  if (!files || file_count == 0) {
    log_error("no input files matched");
    if (ctx->cfg.source_count > 0) {
      const char* root = ctx->cfg.project_root ? ctx->cfg.project_root : ".";
      log_info("Patterns searched from: %s", root);
      for (uint32_t i = 0; i < ctx->cfg.source_count; i++) {
        log_info("  - %s", ctx->cfg.sources[i]);
      }
    }
    goto cleanup;
  }

  if (ctx->args.verbose) {
    log_info("Checking %u file(s)...", file_count);
  }

  bebopc_error_code_t err = bebopc_compile(&ctx->base, files, file_count, &result);
  bebopc_render_diagnostics(&ctx->base, result.result);

  uint32_t error_count = result.error_count;
  uint32_t warning_count = result.warning_count;
  uint32_t parsed_count = result.file_count;

  if (err != BEBOPC_OK && err != BEBOPC_ERR_PARSE) {
    const bebopc_error_t* last = bebopc_error_last(&ctx->base.errors);
    log_error("%s", last && last->message ? last->message : bebopc_error_code_str(err));
    goto cleanup;
  }

  if (error_count == 0) {
    if (!ctx->args.quiet) {
      if (warning_count > 0) {
        log_info("Check passed with %u warning(s)", warning_count);
      } else {
        log_info("Check passed (%u file(s))", parsed_count);
      }
    }
    ret = 0;
  }

cleanup:
  bebopc_compile_cleanup(&result);
  bebopc_files_free(files, file_count);
  return ret;
}

#define WATCH_MAX_CHANGED 64

typedef struct {
  main_ctx_t* ctx;
  bool needs_rebuild;
  uint64_t last_event_time;
  bool check_only;
  char* changed_files[WATCH_MAX_CHANGED];
  uint32_t changed_count;
} watch_state_t;

static void watch_log_event(
    main_ctx_t* ctx, const char* color, const char* status, const char* path
)
{
  (void)ctx;
  time_t now = time(NULL);
  struct tm* tm = localtime(&now);
  char ts[16];
  strftime(ts, sizeof(ts), "%H:%M:%S", tm);
  markup_line(
      "[white][[/][grey]%s[/][white]][/] [%s]%s[/]%s%s",
      ts,
      color,
      status,
      path ? ": " : "",
      path ? path : ""
  );
}

static void watch_add_changed(watch_state_t* state, const char* dir, const char* name)
{
  if (state->changed_count >= WATCH_MAX_CHANGED) {
    return;
  }

  char* path = bebopc_path_join(dir, name);
  if (!path) {
    return;
  }

  for (uint32_t i = 0; i < state->changed_count; i++) {
    if (strcmp(state->changed_files[i], path) == 0) {
      free(path);
      return;
    }
  }

  state->changed_files[state->changed_count++] = path;
}

static void watch_clear_changed(watch_state_t* state)
{
  for (uint32_t i = 0; i < state->changed_count; i++) {
    free(state->changed_files[i]);
  }
  state->changed_count = 0;
}

static void watch_callback(const bebop_fsw_event_t* event, void* userdata)
{
  watch_state_t* state = userdata;

  if (event->action == BEBOP_FSW_ACTION_ADD || event->action == BEBOP_FSW_ACTION_MODIFY
      || event->action == BEBOP_FSW_ACTION_DELETE)
  {
    watch_add_changed(state, event->dir, event->name);
    state->needs_rebuild = true;
    state->last_event_time = bebopc_monotonic_ms();
  }
}

static int watch_check_files(main_ctx_t* ctx, const char** files, uint32_t file_count)
{
  int ret = 1;
  bebopc_compile_result_t result = {0};

  if (!files || file_count == 0) {
    return 0;
  }

  bebopc_error_code_t err = bebopc_compile(&ctx->base, files, file_count, &result);
  bebopc_render_diagnostics(&ctx->base, result.result);

  uint32_t error_count = result.error_count;

  if (err != BEBOPC_OK && err != BEBOPC_ERR_PARSE) {
    const bebopc_error_t* last = bebopc_error_last(&ctx->base.errors);
    log_error("%s", last && last->message ? last->message : bebopc_error_code_str(err));
    goto cleanup;
  }

  ret = (error_count == 0) ? 0 : 1;

cleanup:
  bebopc_compile_cleanup(&result);
  return ret;
}

static int watch_compile(watch_state_t* state)
{
  main_ctx_t* ctx = state->ctx;
  bebopc_error_cleanup(&ctx->base.errors);
  bebopc_error_init(&ctx->base.errors);

  if (state->check_only && state->changed_count > 0) {
    return watch_check_files(ctx, (const char**)(void*)state->changed_files, state->changed_count);
  }

  return state->check_only ? cmd_check(ctx) : cmd_build(ctx);
}

static void watch_show_status(main_ctx_t* ctx, bool check_only)
{
  const char* root = ctx->cfg.project_root ? ctx->cfg.project_root : ".";

  bebopc_table_t* t = bebopc_table_new();
  bebopc_table_set_border(t, BEBOPC_TABLE_BORDER_ROUNDED);
  bebopc_table_set_border_color(t, "grey");
  bebopc_table_add_column(t, "[white]Status[/]");
  bebopc_table_add_column(t, "[white]Value[/]");

  bebopc_table_add_row(t, "[green]Watching[/]", root, NULL);
  bebopc_table_add_row(
      t,
      check_only ? "[yellow]Mode[/]" : "[cyan]Mode[/]",
      check_only ? "check only (no plugins)" : "build",
      NULL
  );

  if (ctx->cfg.source_count > 0) {
    bebopc_table_add_row(t, "[dim]Sources[/]", ctx->cfg.sources[0], NULL);
  }
  for (uint32_t i = 1; i < ctx->cfg.source_count && i < 4; i++) {
    bebopc_table_add_row(t, "", ctx->cfg.sources[i], NULL);
  }

  if (!check_only) {
    for (uint32_t i = 0; i < ctx->cfg.plugin_count && i < 4; i++) {
      bebopc_table_add_row(t, i == 0 ? "[dim]Generators[/]" : "", ctx->cfg.plugins[i].name, NULL);
    }
  }

  bebopc_table_render(t, &ctx->log);
  bebopc_table_free(t);

  markup_line("");
  markup_line("[dim]Press Ctrl+C to stop[/]");
  markup_line("");
}

static int cmd_watch(main_ctx_t* ctx)
{
  int ret = 1;
  bebop_fsw_t* fsw = NULL;
  const char** excludes = NULL;
  bool check_only = ctx->cfg.watch.no_emit || (ctx->cfg.plugin_count == 0);
  watch_state_t state = {.ctx = ctx, .check_only = check_only};

  if (ctx->cfg.source_count == 0) {
    log_error("no source patterns configured");
    goto cleanup;
  }

  fsw = bebop_fsw_create(watch_callback, &state);
  if (!fsw) {
    log_error("failed to create file watcher");
    goto cleanup;
  }

  const char* root = ctx->cfg.project_root ? ctx->cfg.project_root : ".";

  uint32_t total_excludes = ctx->cfg.exclude_count + ctx->cfg.watch.exclude_directory_count
      + ctx->cfg.watch.exclude_file_count;
  if (total_excludes > 0) {
    excludes = malloc(total_excludes * sizeof(char*));
    if (excludes) {
      uint32_t idx = 0;
      for (uint32_t i = 0; i < ctx->cfg.exclude_count; i++) {
        excludes[idx++] = ctx->cfg.exclude[i];
      }
      for (uint32_t i = 0; i < ctx->cfg.watch.exclude_directory_count; i++) {
        excludes[idx++] = ctx->cfg.watch.exclude_directories[i];
      }
      for (uint32_t i = 0; i < ctx->cfg.watch.exclude_file_count; i++) {
        excludes[idx++] = ctx->cfg.watch.exclude_files[i];
      }
    }
  }

  bebop_fsw_filter_t filter = {
      .include = (const char**)(void*)ctx->cfg.sources,
      .include_count = ctx->cfg.source_count,
      .exclude = excludes,
      .exclude_count = total_excludes
  };

  bebop_fsw_options_t opts = {.recursive = true, .filter = &filter};

  bebop_fsw_result_t res = bebop_fsw_add_watch(fsw, root, &opts, NULL);
  if (res != BEBOP_FSW_OK) {
    log_error("failed to watch '%s': %s", root, bebop_fsw_strerror(res));
    goto cleanup;
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  if (!ctx->cfg.watch.preserve_output) {
    fprintf(stderr, "\033[2J\033[H");
  }

  watch_show_status(ctx, state.check_only);

  uint32_t file_count = 0;
  const char** files = bebopc_collect_files(&ctx->base, NULL, 0, &file_count);
  if (!files || file_count == 0) {
    log_error("no input files matched source patterns");
    log_info("Patterns searched from: %s", root);
    for (uint32_t i = 0; i < ctx->cfg.source_count; i++) {
      log_info("  - %s", ctx->cfg.sources[i]);
    }
    goto cleanup;
  }

  if (!state.check_only) {
    bebopc_files_free(files, file_count);
    watch_compile(&state);
  } else {
    log_info("Watching %u file(s)", file_count);
    bebopc_files_free(files, file_count);
  }

  const uint32_t debounce_ms = 100;

  while (!g_interrupted) {
    int n = bebop_fsw_poll(fsw, 200);
    if (n < 0 && n != BEBOP_FSW_ERR_CLOSED) {
      log_error("watch error: %s", bebop_fsw_strerror((bebop_fsw_result_t)n));
      goto cleanup;
    }

    if (state.needs_rebuild) {
      uint64_t now = bebopc_monotonic_ms();
      if (now - state.last_event_time >= debounce_ms) {
        state.needs_rebuild = false;
        if (!ctx->cfg.watch.preserve_output) {
          fprintf(stderr, "\033[2J\033[H");
        }
        watch_show_status(ctx, state.check_only);

        for (uint32_t i = 0; i < state.changed_count; i++) {
          const char* path = state.changed_files[i];
          const char* name = strrchr(path, '/');
          watch_log_event(ctx, "blue", "Changed", name ? name + 1 : path);
        }

        int result = watch_compile(&state);
        watch_clear_changed(&state);

        if (result == 0) {
          watch_log_event(
              ctx, "green", state.check_only ? "Check passed" : "Build succeeded", NULL
          );
        } else {
          watch_log_event(ctx, "red", state.check_only ? "Check failed" : "Build failed", NULL);
        }
      }
    }
  }

  markup_line("");
  watch_log_event(ctx, "yellow", "Stopped", NULL);
  ret = 0;

cleanup:
  watch_clear_changed(&state);
  free(excludes);
  bebop_fsw_destroy(fsw);
  return ret;
}

static const char* INIT_TEMPLATE = "# Bebop compiler configuration\n"
                                   "\n"
                                   "sources:\n"
                                   "  - \"**/*.bop\"\n"
                                   "\n"
                                   "exclude:\n"
                                   "  - \"**/node_modules/**\"\n"
                                   "\n"
                                   "plugins:\n"
                                   "  # typescript: ./generated\n"
                                   "  # csharp: ./generated\n"
                                   "  # rust: ./generated\n";

static int cmd_init(main_ctx_t* ctx)
{
  (void)ctx;

  const char* filename = "bebop.yml";

  if (bebopc_file_is_file(filename)) {
    log_error("'%s' already exists", filename);
    return 1;
  }

  FILE* f = fopen(filename, "w");
  if (!f) {
    log_error("failed to create '%s'", filename);
    return 1;
  }

  size_t len = strlen(INIT_TEMPLATE);
  size_t written = fwrite(INIT_TEMPLATE, 1, len, f);
  fclose(f);

  if (written != len) {
    log_error("failed to write '%s'", filename);
    return 1;
  }

  log_info("Created %s", filename);
  return 0;
}

static int cmd_fmt(main_ctx_t* ctx)
{
  int ret = 0;
  const char** files = NULL;
  uint32_t file_count = 0;
  bool any_unformatted = false;

  files = bebopc_collect_files(&ctx->base, ctx->args.files, ctx->args.file_count, &file_count);
  if (!files || file_count == 0) {
    log_error("no input files");
    return 1;
  }

  for (uint32_t i = 0; i < file_count; i++) {
    const char* path = files[i];
    bebop_file_result_t file = host_file_read(path, NULL);
    if (file.error) {
      log_error("%s: %s", path, file.error);
      ret = 1;
      continue;
    }

    bebop_host_t host = ctx->base.host;
    bebop_context_t* beb = bebop_context_create(&host);
    if (!beb) {
      free((void*)file.content);
      log_error("out of memory");
      ret = 1;
      continue;
    }

    bebop_source_t source = {.source = file.content, .len = file.content_len, .path = path};
    bebop_parse_result_t* result = NULL;
    bebop_status_t status = bebop_parse_source(beb, &source, &result);

    if (status == BEBOP_FATAL || !result) {
      bebop_context_destroy(beb);
      free((void*)file.content);
      log_error("%s: parse failed", path);
      ret = 1;
      continue;
    }

    if (bebop_result_error_count(result) > 0) {
      bebopc_render_diagnostics(&ctx->base, result);
      bebop_context_destroy(beb);
      free((void*)file.content);
      ret = 1;
      continue;
    }

    const bebop_schema_t* schema = bebop_result_schema_at(result, 0);
    if (!schema) {
      bebop_context_destroy(beb);
      free((void*)file.content);
      log_error("%s: no schema", path);
      ret = 1;
      continue;
    }

    size_t formatted_len = 0;
    const char* formatted = bebop_emit_schema(schema, &formatted_len);
    if (!formatted) {
      bebop_context_destroy(beb);
      free((void*)file.content);
      log_error("%s: failed to format", path);
      ret = 1;
      continue;
    }

    bool is_formatted = (formatted_len == file.content_len)
        && (memcmp(formatted, file.content, formatted_len) == 0);

    if (ctx->args.fmt_check) {
      if (!is_formatted) {
        log_error("%s: not formatted", path);
        any_unformatted = true;
      } else if (ctx->args.verbose) {
        log_info("%s: ok", path);
      }
    } else if (ctx->args.fmt_diff) {
      if (!is_formatted) {
        markup_line("[bold]--- %s (original)[/]", path);
        markup_line("[bold]+++ %s (formatted)[/]", path);
        markup_line("[cyan]@@ file @@[/]");
        markup_line("[red]-%s[/]", "[original content]");
        markup_line("[green]+%s[/]", "[formatted content]");
        markup_line("");
      }
    } else {
      if (!is_formatted) {
        FILE* f = fopen(path, "wb");
        if (!f) {
          log_error("%s: failed to write", path);
          ret = 1;
        } else {
          fwrite(formatted, 1, formatted_len, f);
          fclose(f);
          if (!ctx->args.quiet) {
            log_info("Formatted %s", path);
          }
        }
      } else if (ctx->args.verbose) {
        log_info("%s: already formatted", path);
      }
    }

    bebop_context_destroy(beb);
    free((void*)file.content);
  }

  bebopc_files_free(files, file_count);

  if (ctx->args.fmt_check && any_unformatted) {
    return 1;
  }

  return ret;
}

static int cmd_lsp(main_ctx_t* ctx)
{
  beboplsp_includes_t includes = {
      .paths = (const char**)(void*)ctx->cfg.include, .count = ctx->cfg.include_count
  };

  beboplsp_server_t* server = beboplsp_server_create(&includes);
  if (!server) {
    log_error("failed to create LSP server");
    return 1;
  }

  beboplsp_server_run(server);
  beboplsp_server_destroy(server);
  return 0;
}

static int cmd_completion(main_ctx_t* ctx)
{
  const char* shell_name = ctx->args.command_arg;
  if (!shell_name) {
    log_error("missing shell argument");
    log_info("Supported shells: bash, zsh, fish, powershell");
    return 1;
  }

  cli_shell_t shell = cli_find_shell(shell_name);
  if (shell == CLI_SHELL_UNKNOWN) {
    log_error("unknown shell '%s'", shell_name);
    return 1;
  }

  cli_generate_completion(shell, stdout);
  return 0;
}

static int cmd_help(main_ctx_t* ctx)
{
  if (ctx->args.command_arg) {
    cli_cmd_t cmd = cli_find_command(ctx->args.command_arg);
    if (cmd == CLI_CMD_NONE) {
      log_error("unknown command '%s'", ctx->args.command_arg);
      return 1;
    }
    cli_print_command_help(&cli_info, cmd);
  } else {
    cli_print_usage(&cli_info);
  }
  return 0;
}

static int cmd_version(main_ctx_t* ctx)
{
  (void)ctx;
  cli_print_version(&cli_info);
  return 0;
}

int main(int argc, char** argv)
{
#ifndef _WIN32
  // A plugin that exits or closes stdin mid-request would otherwise kill the
  // compiler with no diagnostic; write() reports EPIPE instead.
  signal(SIGPIPE, SIG_IGN);
#endif

  main_ctx_t ctx;
  ctx_init(&ctx);

  const char* error_msg = NULL;
  bebopc_error_code_t err = cli_parse(&ctx.args, argc, argv, &error_msg);
  if (err != BEBOPC_OK) {
    log_error("%s", error_msg ? error_msg : "invalid arguments");
    ctx_cleanup(&ctx);
    return 1;
  }

  if (ctx.args.help) {
    if (ctx.args.command != CLI_CMD_NONE) {
      cli_print_command_help(&cli_info, ctx.args.command);
    } else {
      cli_print_usage(&cli_info);
    }
    ctx_cleanup(&ctx);
    return 0;
  }

  if (ctx.args.version) {
    cli_print_version(&cli_info);
    ctx_cleanup(&ctx);
    return 0;
  }

  if (ctx.args.llm) {
    markup_line("[bold][cyan]# bebopc %s[/]", cli_info.version);
    markup_line("");

    if (ctx.args.command == CLI_CMD_NONE) {
      markup_line("%s", cli_info.description);
      markup_line("");

      markup_line("[bold][yellow]## Usage[/]");
      markup_line(
          "  [green]%s[/] [cyan]<command>[/] [dim][options] [files...][/]", cli_info.program_name
      );
      markup_line("");

      markup_line("[bold][yellow]## Commands[/]");
      for (int i = 0; i < CLI_CMD_COUNT; i++) {
        const cli_cmd_def_t* cmd = cli_get_command_def((cli_cmd_t)i);
        if (cmd->args_pattern && cmd->args_pattern[0]) {
          markup_line("  [green]%s[/] [dim]%s[/]", cmd->name, cmd->args_pattern);
        } else {
          markup_line("  [green]%s[/]", cmd->name);
        }
        markup_line("    [white]%s[/]", cmd->description);
        markup_line("");
      }

      markup_line("[bold][yellow]## Global Options[/]");
      for (int i = 0; i < CLI_OPT_COUNT; i++) {
        const cli_opt_def_t* opt = cli_get_option_def((cli_opt_t)i);
        if (!(opt->flags & CLI_OPT_FLAG_ROOT)) {
          continue;
        }
        if (opt->short_name) {
          markup("[cyan]  -%c[/], [cyan]--%s[/]", opt->short_name, opt->long_name);
        } else {
          markup("[cyan]  --%s[/]", opt->long_name);
        }
        markup_line("  [white]%s[/]", opt->description);
      }
      markup_line("");
    } else {
      const cli_cmd_def_t* cmd = cli_get_command_def(ctx.args.command);
      markup_line("[bold][yellow]## %s[/]", cmd->name);
      markup_line("[white]%s[/]", cmd->description);
      markup_line("");

      markup_line("[bold][yellow]## Usage[/]");
      markup("[green]  %s %s[/]", cli_info.program_name, cmd->name);
      if (cmd->show_options) {
        markup(" [dim][options][/]");
      }
      if (cmd->args_pattern && cmd->args_pattern[0]) {
        markup(" [dim]%s[/]", cmd->args_pattern);
      }
      markup_line("");

      if (cmd->extended_help) {
        markup_line("");
        markup_line("[dim]%s[/]", cmd->extended_help);
      }

      if (cmd->show_options) {
        markup_line("");
        markup_line("[bold][yellow]## Options[/]");
        uint32_t cmd_flag = 0;
        switch (ctx.args.command) {
          case CLI_CMD_BUILD:
            cmd_flag = CLI_OPT_FLAG_BUILD;
            break;
          case CLI_CMD_CHECK:
            cmd_flag = CLI_OPT_FLAG_CHECK;
            break;
          case CLI_CMD_WATCH:
            cmd_flag = CLI_OPT_FLAG_WATCH;
            break;
          default:
            break;
        }
        for (int i = 0; i < CLI_OPT_COUNT; i++) {
          const cli_opt_def_t* opt = cli_get_option_def((cli_opt_t)i);
          if (!(opt->flags & cmd_flag)) {
            continue;
          }
          if (opt->short_name) {
            markup("[cyan]  -%c[/], [cyan]--%s[/]", opt->short_name, opt->long_name);
          } else {
            markup("[cyan]  --%s[/]", opt->long_name);
          }
          if (opt->value_name) {
            markup(" [dim]<%s>[/]", opt->value_name);
          }
          markup_line("  [white]%s[/]", opt->description);
        }
      }
      markup_line("");
    }

    markup_line("[bold][yellow]## Documentation[/]");
    markup_line("[white]Schema syntax, types, and detailed documentation:[/]");
    markup_line("  [underline][blue]https://bebop.sh/llms.txt[/]       " "[dim](concise)[/]");
    markup_line("  [underline][blue]https://bebop.sh/llms-full.txt[/]  [dim](full)[/]");
    ctx_cleanup(&ctx);
    return 0;
  }

  if (ctx.args.command == CLI_CMD_NONE) {
    cli_print_usage(&cli_info);
    ctx_cleanup(&ctx);
    return 0;
  }

  err = bebopc_config_load(&ctx.cfg, &ctx.base, ctx.args.config_path);
  if (err != BEBOPC_OK && ctx.args.config_path) {
    const bebopc_error_t* last = bebopc_error_last(&ctx.base.errors);
    if (last && last->message) {
      log_error("%s", last->message);
    }
    ctx_cleanup(&ctx);
    return 1;
  }

  err = bebopc_config_merge_cli(&ctx.cfg, &ctx.base, &ctx.args);
  if (err != BEBOPC_OK) {
    log_error("failed to merge config");
    ctx_cleanup(&ctx);
    return 1;
  }

  char* exe = bebopc_exe_path();
  if (exe) {
    char* exe_dir = bebopc_path_dirname(exe);
    free(exe);
    if (exe_dir) {
      // Development layout: <exe_dir>/include/ (contains bebop/*.bop)
      char* builtin = bebopc_path_join(exe_dir, "include");
      if (builtin && bebopc_file_is_dir(builtin)) {
        bebopc_config_add_include(&ctx.cfg, &ctx.base, builtin);
      }
      free(builtin);

      // Installed layout: <prefix>/share/ (exe is in <prefix>/bin/, contains bebop/*.bop)
      char* prefix = bebopc_path_dirname(exe_dir);
      if (prefix) {
        char* share = bebopc_path_join(prefix, "share");
        if (share && bebopc_file_is_dir(share)) {
          bebopc_config_add_include(&ctx.cfg, &ctx.base, share);
        }
        free(share);
        free(prefix);
      }
      free(exe_dir);
    }
  }

  ctx.base.host.includes.paths = (const char**)(void*)ctx.cfg.include;
  ctx.base.host.includes.count = ctx.cfg.include_count;

  int result;
  switch (ctx.args.command) {
    case CLI_CMD_BUILD:
      result = cmd_build(&ctx);
      break;
    case CLI_CMD_WATCH:
      result = cmd_watch(&ctx);
      break;
    case CLI_CMD_INIT:
      result = cmd_init(&ctx);
      break;
    case CLI_CMD_CHECK:
      result = cmd_check(&ctx);
      break;
    case CLI_CMD_FMT:
      result = cmd_fmt(&ctx);
      break;
    case CLI_CMD_LSP:
      result = cmd_lsp(&ctx);
      break;
    case CLI_CMD_COMPLETION:
      result = cmd_completion(&ctx);
      break;
    case CLI_CMD_HELP:
      result = cmd_help(&ctx);
      break;
    case CLI_CMD_VERSION:
      result = cmd_version(&ctx);
      break;
    default:
      log_error("unknown command");
      result = 1;
      break;
  }

  ctx_cleanup(&ctx);
  return result;
}
