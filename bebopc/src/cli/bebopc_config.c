#include "bebopc_config.h"

#include <cyaml.h>
#include <stdio.h>
#include <stdlib.h>

#include "../bebopc_dir.h"
#include "../bebopc_utils.h"

#define CONFIG_FILENAME "bebop.yml"

void bebopc_config_init(bebopc_config_t* cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->color = CLI_COLOR_AUTO;
  cfg->format = CLI_FORMAT_TERMINAL;
}

static void free_string_array(char** arr, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++) {
    free(arr[i]);
  }
  free(arr);
}

static void free_options(bebopc_kv_t* opts, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++) {
    free(opts[i].key);
    free(opts[i].value);
  }
  free(opts);
}

static void free_plugin(bebopc_plugin_t* p)
{
  free(p->name);
  free(p->out_dir);
  free(p->path);
  free_options(p->options, p->option_count);
}

void bebopc_config_cleanup(bebopc_config_t* cfg)
{
  free_string_array(cfg->sources, cfg->source_count);
  free_string_array(cfg->exclude, cfg->exclude_count);
  free_string_array(cfg->include, cfg->include_count);

  for (uint32_t i = 0; i < cfg->plugin_count; i++) {
    free_plugin(&cfg->plugins[i]);
  }
  free(cfg->plugins);

  free_options(cfg->options, cfg->option_count);

  free_string_array(cfg->watch.exclude_directories, cfg->watch.exclude_directory_count);
  free_string_array(cfg->watch.exclude_files, cfg->watch.exclude_file_count);

  free(cfg->config_path);
  free(cfg->project_root);

  memset(cfg, 0, sizeof(*cfg));
}

bebopc_error_code_t bebopc_config_add_source(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const char* pattern
)
{
  char** new_arr = realloc(cfg->sources, (cfg->source_count + 1) * sizeof(char*));
  if (!new_arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate source pattern");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->sources = new_arr;
  cfg->sources[cfg->source_count] = bebopc_strdup(pattern);
  if (!cfg->sources[cfg->source_count]) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to duplicate source pattern");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->source_count++;
  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_config_add_exclude(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const char* pattern
)
{
  char** new_arr = realloc(cfg->exclude, (cfg->exclude_count + 1) * sizeof(char*));
  if (!new_arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate exclude pattern");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->exclude = new_arr;
  cfg->exclude[cfg->exclude_count] = bebopc_strdup(pattern);
  if (!cfg->exclude[cfg->exclude_count]) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to duplicate exclude pattern");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->exclude_count++;
  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_config_add_include(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const char* path
)
{
  char** new_arr = realloc(cfg->include, (cfg->include_count + 1) * sizeof(char*));
  if (!new_arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate include path");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->include = new_arr;
  cfg->include[cfg->include_count] = bebopc_strdup(path);
  if (!cfg->include[cfg->include_count]) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to duplicate include path");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->include_count++;
  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_config_add_plugin(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const char* name, const char* out_dir
)
{
  bebopc_plugin_t* new_arr =
      realloc(cfg->plugins, (cfg->plugin_count + 1) * sizeof(bebopc_plugin_t));
  if (!new_arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate plugin");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->plugins = new_arr;
  memset(&cfg->plugins[cfg->plugin_count], 0, sizeof(bebopc_plugin_t));

  cfg->plugins[cfg->plugin_count].name = bebopc_strdup(name);
  cfg->plugins[cfg->plugin_count].out_dir = bebopc_strdup(out_dir);

  if (!cfg->plugins[cfg->plugin_count].name || !cfg->plugins[cfg->plugin_count].out_dir) {
    free(cfg->plugins[cfg->plugin_count].name);
    free(cfg->plugins[cfg->plugin_count].out_dir);
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to duplicate plugin fields");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->plugin_count++;
  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_config_add_option(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const char* key, const char* value
)
{
  bebopc_kv_t* new_arr = realloc(cfg->options, (cfg->option_count + 1) * sizeof(bebopc_kv_t));
  if (!new_arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate option");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->options = new_arr;
  cfg->options[cfg->option_count].key = bebopc_strdup(key);
  cfg->options[cfg->option_count].value = bebopc_strdup(value);

  if (!cfg->options[cfg->option_count].key || !cfg->options[cfg->option_count].value) {
    free(cfg->options[cfg->option_count].key);
    free(cfg->options[cfg->option_count].value);
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to duplicate option fields");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }
  cfg->option_count++;
  return BEBOPC_OK;
}

char* bebopc_config_find(const char* start_dir)
{
  if (!start_dir) {
    return NULL;
  }

  char* current = bebopc_path_realpath(start_dir);
  if (!current) {
    return NULL;
  }

  while (current && current[0] != '\0') {
    char* config_path = bebopc_path_join(current, CONFIG_FILENAME);
    if (config_path && bebopc_file_is_file(config_path)) {
      free(current);
      return config_path;
    }
    free(config_path);

    char* parent = bebopc_path_dirname(current);
    if (!parent || bebopc_streq(parent, current)) {
      free(parent);
      break;
    }
    free(current);
    current = parent;
  }

  free(current);
  return NULL;
}

static bebopc_error_code_t ypath_strings(
    bebopc_ctx_t* ctx, cyaml_doc_t* doc, const char* path, char*** out, uint32_t* out_count
)
{
  *out = NULL;
  *out_count = 0;

  cyaml_path_result_t r = cyaml_path_query(doc, NULL, path);
  if (r.error) {
    return BEBOPC_OK;
  }
  if (r.count == 0) {
    cyaml_path_result_free(&r);
    return BEBOPC_OK;
  }

  char** arr = calloc(r.count, sizeof(char*));
  if (!arr) {
    cyaml_path_result_free(&r);
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate array for %s", path);
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }

  uint32_t count = 0;
  for (uint32_t i = 0; i < r.count; i++) {
    if (!cyaml_is_scalar(r.nodes[i])) {
      continue;
    }
    arr[count] = cyaml_scalar_str(doc, r.nodes[i]);
    if (!arr[count]) {
      for (uint32_t j = 0; j < count; j++) {
        free(arr[j]);
      }
      free(arr);
      cyaml_path_result_free(&r);
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to copy string from %s", path);
      return BEBOPC_ERR_OUT_OF_MEMORY;
    }
    count++;
  }

  cyaml_path_result_free(&r);
  *out = arr;
  *out_count = count;
  return BEBOPC_OK;
}

static bebopc_error_code_t ypath_options(
    bebopc_ctx_t* ctx, cyaml_doc_t* doc, const char* path, bebopc_kv_t** out, uint32_t* out_count
)
{
  *out = NULL;
  *out_count = 0;

  cyaml_node_t* map = cyaml_path_first(doc, NULL, path);
  if (!map || !cyaml_is_map(map)) {
    return BEBOPC_OK;
  }

  uint32_t len = cyaml_map_len(map);
  if (len == 0) {
    return BEBOPC_OK;
  }

  bebopc_kv_t* arr = calloc(len, sizeof(bebopc_kv_t));
  if (!arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate options for %s", path);
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }

  uint32_t count = 0;
  for (uint32_t i = 0; i < len; i++) {
    cyaml_pair_t* pair = cyaml_map_at(map, i);
    if (!pair || !cyaml_is_scalar(pair->key) || !cyaml_is_scalar(pair->val)) {
      continue;
    }

    arr[count].key = cyaml_scalar_str(doc, pair->key);
    arr[count].value = cyaml_scalar_str(doc, pair->val);
    if (!arr[count].key || !arr[count].value) {
      free(arr[count].key);
      free(arr[count].value);
      for (uint32_t j = 0; j < count; j++) {
        free(arr[j].key);
        free(arr[j].value);
      }
      free(arr);
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to copy option from %s", path);
      return BEBOPC_ERR_OUT_OF_MEMORY;
    }
    count++;
  }

  *out = arr;
  *out_count = count;
  return BEBOPC_OK;
}

static bool ypath_bool(cyaml_doc_t* doc, const char* path, bool def)
{
  cyaml_node_t* node = cyaml_path_first(doc, NULL, path);
  if (!node || !cyaml_is_scalar(node)) {
    return def;
  }
  bool val = def;
  cyaml_as_bool(doc, node, &val);
  return val;
}

static bebopc_error_code_t parse_plugins(
    bebopc_ctx_t* ctx, cyaml_doc_t* doc, bebopc_plugin_t** out, uint32_t* out_count
)
{
  bebopc_error_code_t err = BEBOPC_OK;
  *out = NULL;
  *out_count = 0;

  cyaml_node_t* map = cyaml_path_first(doc, NULL, "/plugins");
  if (!map || !cyaml_is_map(map)) {
    return BEBOPC_OK;
  }

  uint32_t len = cyaml_map_len(map);
  if (len == 0) {
    return BEBOPC_OK;
  }

  bebopc_plugin_t* arr = calloc(len, sizeof(bebopc_plugin_t));
  if (!arr) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate plugins");
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }

  uint32_t count = 0;
  for (uint32_t i = 0; i < len; i++) {
    cyaml_pair_t* pair = cyaml_map_at(map, i);
    if (!pair || !cyaml_is_scalar(pair->key)) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_PARSE, "plugin key must be a string");
      err = BEBOPC_ERR_PARSE;
      goto cleanup;
    }

    arr[count].name = cyaml_scalar_str(doc, pair->key);
    if (!arr[count].name) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to copy plugin name");
      err = BEBOPC_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }

    if (cyaml_is_scalar(pair->val)) {
      arr[count].out_dir = cyaml_scalar_str(doc, pair->val);
    } else if (cyaml_is_map(pair->val)) {
      cyaml_node_t* out_node = cyaml_get(doc, pair->val, "out");
      if (out_node && cyaml_is_scalar(out_node)) {
        arr[count].out_dir = cyaml_scalar_str(doc, out_node);
      }

      cyaml_node_t* opts = cyaml_get(doc, pair->val, "options");
      if (opts && cyaml_is_map(opts)) {
        uint32_t opt_len = cyaml_map_len(opts);
        if (opt_len > 0) {
          arr[count].options = calloc(opt_len, sizeof(bebopc_kv_t));
          if (!arr[count].options) {
            BEBOPC_ERROR(
                &ctx->errors,
                BEBOPC_ERR_OUT_OF_MEMORY,
                "failed to allocate options for '%s'",
                arr[count].name
            );
            err = BEBOPC_ERR_OUT_OF_MEMORY;
            goto cleanup;
          }
          for (uint32_t j = 0; j < opt_len; j++) {
            cyaml_pair_t* op = cyaml_map_at(opts, j);
            if (!op || !cyaml_is_scalar(op->key) || !cyaml_is_scalar(op->val)) {
              continue;
            }
            arr[count].options[arr[count].option_count].key = cyaml_scalar_str(doc, op->key);
            arr[count].options[arr[count].option_count].value = cyaml_scalar_str(doc, op->val);
            if (!arr[count].options[arr[count].option_count].key
                || !arr[count].options[arr[count].option_count].value)
            {
              free(arr[count].options[arr[count].option_count].key);
              free(arr[count].options[arr[count].option_count].value);
              err = BEBOPC_ERR_OUT_OF_MEMORY;
              goto cleanup;
            }
            arr[count].option_count++;
          }
        }
      }
    }

    if (!arr[count].out_dir) {
      BEBOPC_ERROR(
          &ctx->errors, BEBOPC_ERR_PARSE, "plugin '%s' requires output directory", arr[count].name
      );
      err = BEBOPC_ERR_PARSE;
      goto cleanup;
    }
    count++;
  }

  *out = arr;
  *out_count = count;
  return BEBOPC_OK;

cleanup:
  for (uint32_t i = 0; i <= count && i < len; i++) {
    free_plugin(&arr[i]);
  }
  free(arr);
  return err;
}

bebopc_error_code_t bebopc_config_load(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const char* explicit_path
)
{
  bebopc_error_code_t err = BEBOPC_OK;
  char* config_path = NULL;
  char* file_content = NULL;
  cyaml_doc_t* doc = NULL;

  if (explicit_path) {
    config_path = bebopc_strdup(explicit_path);
    if (!config_path) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to copy config path");
      return BEBOPC_ERR_OUT_OF_MEMORY;
    }
  } else {
    char* cwd = bebopc_getcwd();
    if (cwd) {
      config_path = bebopc_config_find(cwd);
      free(cwd);
    }
  }

  if (!config_path) {
    cfg->project_root = bebopc_getcwd();
    return BEBOPC_OK;
  }

  if (!bebopc_file_is_file(config_path)) {
    if (explicit_path) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_NOT_FOUND, "config file not found: %s", explicit_path);
      err = BEBOPC_ERR_NOT_FOUND;
    }
    goto cleanup;
  }

  size_t file_size = 0;
  file_content = bebopc_file_read(ctx, config_path, &file_size);
  if (!file_content) {
    err = BEBOPC_ERR_IO;
    goto cleanup;
  }

  cyaml_error_t yaml_err = {0};
  doc = cyaml_parse(file_content, file_size, NULL, &yaml_err);
  if (!doc) {
    BEBOPC_ERROR(
        &ctx->errors,
        BEBOPC_ERR_PARSE,
        "%s:%u:%u: %s",
        config_path,
        yaml_err.span.start_line,
        yaml_err.span.start_col,
        yaml_err.msg
    );
    err = BEBOPC_ERR_PARSE;
    goto cleanup;
  }

  cyaml_node_t* root = cyaml_root(doc);
  if (!cyaml_is_map(root)) {
    BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_PARSE, "%s: root must be a YAML map", config_path);
    err = BEBOPC_ERR_PARSE;
    goto cleanup;
  }

  err = ypath_strings(ctx, doc, "/sources/*", &cfg->sources, &cfg->source_count);
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  err = ypath_strings(ctx, doc, "/exclude/*", &cfg->exclude, &cfg->exclude_count);
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  err = ypath_strings(ctx, doc, "/include/*", &cfg->include, &cfg->include_count);
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  err = parse_plugins(ctx, doc, &cfg->plugins, &cfg->plugin_count);
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  err = ypath_options(ctx, doc, "/options", &cfg->options, &cfg->option_count);
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  err = ypath_strings(
      ctx,
      doc,
      "/watch/exclude_directories/*",
      &cfg->watch.exclude_directories,
      &cfg->watch.exclude_directory_count
  );
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  err = ypath_strings(
      ctx, doc, "/watch/exclude_files/*", &cfg->watch.exclude_files, &cfg->watch.exclude_file_count
  );
  if (err != BEBOPC_OK) {
    goto cleanup;
  }

  cfg->watch.preserve_output = ypath_bool(doc, "/watch/preserve_output", false);
  cfg->watch.no_emit = ypath_bool(doc, "/watch/no_emit", false);

  cfg->config_path = config_path;
  cfg->project_root = bebopc_path_dirname(config_path);
  config_path = NULL;

cleanup:
  if (doc) {
    cyaml_free(doc);
  }
  free(file_content);
  free(config_path);
  return err;
}

bebopc_error_code_t bebopc_config_merge_cli(
    bebopc_config_t* cfg, bebopc_ctx_t* ctx, const cli_args_t* args
)
{
  bebopc_error_code_t err;

  for (uint32_t i = 0; i < args->exclude_count; i++) {
    err = bebopc_config_add_exclude(cfg, ctx, args->excludes[i]);
    if (err != BEBOPC_OK) {
      return err;
    }
  }

  for (uint32_t i = 0; i < args->include_count; i++) {
    err = bebopc_config_add_include(cfg, ctx, args->includes[i]);
    if (err != BEBOPC_OK) {
      return err;
    }
  }

  for (uint32_t i = 0; i < args->plugin_count; i++) {
    const cli_plugin_t* p = &args->plugins[i];
    if (!p->out_dir) {
      continue;
    }

    err = bebopc_config_add_plugin(cfg, ctx, p->name, p->out_dir);
    if (err != BEBOPC_OK) {
      return err;
    }

    if (p->path) {
      cfg->plugins[cfg->plugin_count - 1].path = bebopc_strdup(p->path);
    }
  }

  for (uint32_t i = 0; i < args->option_count; i++) {
    err = bebopc_config_add_option(cfg, ctx, args->options[i].key, args->options[i].value);
    if (err != BEBOPC_OK) {
      return err;
    }
  }

  cfg->verbose = args->verbose;
  cfg->quiet = args->quiet;
  cfg->no_warn = args->no_warn;
  cfg->trace = args->trace;
  cfg->color = args->color;
  cfg->format = args->format;

  if (args->no_emit) {
    cfg->watch.no_emit = true;
  }
  if (args->preserve_output) {
    cfg->watch.preserve_output = true;
  }
  if (args->emit_source_info) {
    cfg->emit_source_info = true;
  }

  return BEBOPC_OK;
}
