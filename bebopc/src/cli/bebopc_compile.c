#include "bebopc_compile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bebopc_diag.h"
#include "../bebopc_dir.h"
#include "../bebopc_glob.h"
#include "bebopc_config.h"

#define ADD_CONST(x) ((const char**)(void*)(x))

const char** bebopc_collect_files(
    bebopc_ctx_t* ctx, char** cli_files, uint32_t cli_file_count, uint32_t* out_count
)
{
  const char** files = NULL;
  bebopc_glob_t* glob = NULL;

  *out_count = 0;

  const bebopc_config_t* cfg = ctx->cfg;
  if (!cfg) {
    goto done;
  }

  if (cli_file_count > 0) {
    bool has_globs = false;
    for (uint32_t i = 0; i < cli_file_count; i++) {
      if (bebopc_glob_is_pattern(cli_files[i])) {
        has_globs = true;
        break;
      }
    }

    if (!has_globs) {
      files = malloc(cli_file_count * sizeof(char*));
      if (!files) {
        goto done;
      }
      for (uint32_t i = 0; i < cli_file_count; i++) {
        files[i] = bebopc_strdup(cli_files[i]);
        if (!files[i]) {
          bebopc_files_free(files, i);
          files = NULL;
          goto done;
        }
      }
      *out_count = cli_file_count;
      goto done;
    }

    glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
    if (!glob) {
      goto done;
    }
    for (uint32_t i = 0; i < cli_file_count; i++) {
      bebopc_glob_include(glob, cli_files[i]);
    }
    for (uint32_t i = 0; i < cfg->exclude_count; i++) {
      bebopc_glob_exclude(glob, cfg->exclude[i]);
    }
    size_t count = 0;
    files = ADD_CONST(bebopc_glob_paths(glob, ctx, ".", &count));
    *out_count = (uint32_t)count;
    goto done;
  }

  if (cfg->source_count == 0) {
    goto done;
  }

  glob = bebopc_glob_new(BEBOPC_GLOB_CONFIG_DEFAULT);
  if (!glob) {
    goto done;
  }

  for (uint32_t i = 0; i < cfg->source_count; i++) {
    bebopc_glob_include(glob, cfg->sources[i]);
  }

  for (uint32_t i = 0; i < cfg->exclude_count; i++) {
    bebopc_glob_exclude(glob, cfg->exclude[i]);
  }

  const char* root = cfg->project_root ? cfg->project_root : ".";
  size_t count = 0;
  files = ADD_CONST(bebopc_glob_paths(glob, ctx, root, &count));
  *out_count = (uint32_t)count;

done:
  if (glob) {
    bebopc_glob_free(glob);
  }
  return files;
}

typedef struct {
  char* path;
  char* content;
  size_t content_len;
  diag_source_t diag_src;
} source_entry_t;

typedef struct {
  source_entry_t* entries;
  uint32_t count;
  uint32_t capacity;
} source_cache_t;

static void cache_init(source_cache_t* cache)
{
  cache->entries = NULL;
  cache->count = 0;
  cache->capacity = 0;
}

static void cache_cleanup(source_cache_t* cache)
{
  for (uint32_t i = 0; i < cache->count; i++) {
    free(cache->entries[i].path);
    free(cache->entries[i].content);
    diag_source_cleanup(&cache->entries[i].diag_src);
  }
  free(cache->entries);
}

static diag_source_t* cache_get(source_cache_t* cache, bebopc_ctx_t* ctx, const char* path)
{
  for (uint32_t i = 0; i < cache->count; i++) {
    if (strcmp(cache->entries[i].path, path) == 0) {
      return &cache->entries[i].diag_src;
    }
  }

  if (cache->count >= cache->capacity) {
    uint32_t new_cap = cache->capacity ? cache->capacity * 2 : 8;
    source_entry_t* new_entries = realloc(cache->entries, new_cap * sizeof(source_entry_t));
    if (!new_entries) {
      return NULL;
    }
    cache->entries = new_entries;
    cache->capacity = new_cap;
  }

  size_t content_len = 0;
  char* content = bebopc_file_read(ctx, path, &content_len);
  if (!content) {
    return NULL;
  }

  source_entry_t* entry = &cache->entries[cache->count++];
  entry->path = bebopc_strdup(path);
  entry->content = content;
  entry->content_len = content_len;
  diag_source_init(&entry->diag_src, entry->path, entry->content, (uint32_t)entry->content_len);

  return &entry->diag_src;
}

static diag_severity_t convert_severity(bebop_diag_severity_t sev)
{
  switch (sev) {
    case BEBOP_DIAG_ERROR:
      return DIAG_ERROR;
    case BEBOP_DIAG_WARNING:
      return DIAG_WARNING;
    default:
      return DIAG_INFO;
  }
}

uint32_t bebopc_render_diagnostics(bebopc_ctx_t* ctx, bebop_parse_result_t* result)
{
  uint32_t error_count = 0;
  diag_label_t* labels = NULL;
  source_cache_t cache;
  diag_buf_t buf;
  diag_ctx_t diag_ctx;

  if (!result) {
    return 0;
  }

  uint32_t diag_count = bebop_result_diagnostic_count(result);
  if (diag_count == 0) {
    return 0;
  }

  diag_init(&diag_ctx);
  cache_init(&cache);
  diag_buf_init(&buf);

  const bebopc_config_t* cfg = ctx->cfg;
  if (cfg) {
    switch (cfg->color) {
      case CLI_COLOR_ALWAYS:
        diag_set_color(&diag_ctx, true);
        break;
      case CLI_COLOR_NEVER:
        diag_set_color(&diag_ctx, false);
        break;
      default:
        break;
    }
    switch (cfg->format) {
      case CLI_FORMAT_JSON:
        diag_set_format(&diag_ctx, DIAG_FMT_JSON);
        break;
      case CLI_FORMAT_MSBUILD:
        diag_set_format(&diag_ctx, DIAG_FMT_MSBUILD);
        break;
      case CLI_FORMAT_XCODE:
        diag_set_format(&diag_ctx, DIAG_FMT_XCODE);
        break;
      default:
        diag_set_format(&diag_ctx, DIAG_FMT_TERMINAL);
        break;
    }
  }

  for (uint32_t i = 0; i < diag_count; i++) {
    const bebop_diagnostic_t* bd = bebop_result_diagnostic_at(result, i);
    if (!bd) {
      continue;
    }

    bebop_diag_severity_t sev = bebop_diagnostic_severity(bd);
    if (sev == BEBOP_DIAG_ERROR) {
      error_count++;
    }

    const char* path = bebop_diagnostic_path(bd);
    diag_source_t* src = path ? cache_get(&cache, ctx, path) : NULL;

    uint32_t bebop_label_count = bebop_diagnostic_label_count(bd);
    bebop_span_t primary_span = bebop_diagnostic_span(bd);

    uint32_t total_labels = src ? (1 + bebop_label_count) : 0;
    labels = NULL;

    if (total_labels > 0) {
      labels = malloc(total_labels * sizeof(diag_label_t));
      if (labels) {
        labels[0] = (diag_label_t) {.start = primary_span.off,
                                    .end = primary_span.off + primary_span.len,
                                    .message = NULL,
                                    .priority = 0};

        for (uint32_t li = 0; li < bebop_label_count; li++) {
          bebop_span_t lspan = bebop_diagnostic_label_span(bd, li);
          labels[1 + li] = (diag_label_t) {.start = lspan.off,
                                           .end = lspan.off + lspan.len,
                                           .message = bebop_diagnostic_label_message(bd, li),
                                           .priority = 1};
        }
      } else {
        total_labels = 0;
      }
    }

    char code_buf[32];
    snprintf(code_buf, sizeof(code_buf), "BOP%04u", bebop_diagnostic_code(bd));

    diag_t diag = {
        .severity = convert_severity(sev),
        .code = code_buf,
        .message = bebop_diagnostic_message(bd),
        .note = bebop_diagnostic_hint(bd),
        .source = src,
        .labels = labels,
        .label_count = total_labels
    };

    diag_render(&diag_ctx, &buf, &diag);
    free(labels);
    labels = NULL;

    if (buf.len > 0) {
      fwrite(buf.data, 1, buf.len, stderr);
      buf.len = 0;
    }
  }

  diag_buf_cleanup(&buf);
  cache_cleanup(&cache);

  return error_count;
}

bebopc_error_code_t bebopc_compile(
    bebopc_ctx_t* ctx, const char** files, uint32_t file_count, bebopc_compile_result_t* out
)
{
  bebopc_error_code_t err = BEBOPC_OK;
  bebop_status_t status;

  memset(out, 0, sizeof(*out));

  if (!ctx || !files || file_count == 0) {
    err = BEBOPC_ERR_INVALID_ARG;
    goto cleanup;
  }

  out->beb_ctx = bebop_context_create(&ctx->host);
  if (!out->beb_ctx) {
    err = BEBOPC_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }

  status = bebop_parse(out->beb_ctx, files, file_count, &out->result);

  out->file_count = file_count;
  out->error_count = out->result ? bebop_result_error_count(out->result) : 0;
  out->warning_count = out->result ? bebop_result_warning_count(out->result) : 0;

  if (status == BEBOP_FATAL) {
    return BEBOPC_ERR_PARSE;
  }

  if (out->error_count == 0 && out->result) {
    bebop_desc_flags_t flags = BEBOP_DESC_FLAG_NONE;
    if (ctx->cfg && ctx->cfg->emit_source_info) {
      flags |= BEBOP_DESC_FLAG_SOURCE_INFO;
    }
    status = bebop_descriptor_build(out->result, flags, &out->desc);
    if (status != BEBOP_OK) {
      err = BEBOPC_ERR_INTERNAL;
      goto cleanup;
    }
  }

  return BEBOPC_OK;

cleanup:
  bebopc_compile_cleanup(out);
  return err;
}

void bebopc_compile_cleanup(bebopc_compile_result_t* result)
{
  if (!result) {
    return;
  }

  if (result->desc) {
    bebop_descriptor_free(result->desc);
  }

  if (result->beb_ctx) {
    bebop_context_destroy(result->beb_ctx);
  }

  memset(result, 0, sizeof(*result));
}
