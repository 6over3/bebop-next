#include "bebopc_glob.h"

#include <stdlib.h>

#include "bebopc_dir.h"
#include "bebopc_error.h"
#include "bebopc_utils.h"

typedef enum {
  BEBOPC_GLOB_PATTERN_INCLUDE,
  BEBOPC_GLOB_PATTERN_EXCLUDE
} bebopc_glob_pattern_type_t;

typedef struct {
  char* pattern;
  bebopc_glob_pattern_type_t type;
} bebopc_glob_pattern_t;

struct bebopc_glob {
  bebopc_glob_config_t config;
  bebopc_glob_pattern_t* patterns;
  size_t pattern_count;
  size_t pattern_capacity;
};

static bebopc_error_code_t _glob_pattern_add(
    bebopc_glob_t* glob, const char* pattern, bebopc_glob_pattern_type_t type
)
{
  if (!glob || !pattern) {
    return BEBOPC_ERR_INVALID_ARG;
  }

  if (glob->pattern_count >= glob->pattern_capacity) {
    size_t new_capacity = glob->pattern_capacity == 0 ? 8 : glob->pattern_capacity * 2;
    bebopc_glob_pattern_t* new_patterns =
        realloc(glob->patterns, new_capacity * sizeof(bebopc_glob_pattern_t));
    if (!new_patterns) {
      return BEBOPC_ERR_OUT_OF_MEMORY;
    }
    glob->patterns = new_patterns;
    glob->pattern_capacity = new_capacity;
  }

  char* pattern_copy = bebopc_strdup(pattern);
  if (!pattern_copy) {
    return BEBOPC_ERR_OUT_OF_MEMORY;
  }

  glob->patterns[glob->pattern_count].pattern = pattern_copy;
  glob->patterns[glob->pattern_count].type = type;
  glob->pattern_count++;

  return BEBOPC_OK;
}

static int _glob_result_push(bebopc_glob_result_t* result, const char* path, const char* stem)
{
  if (!result) {
    return -1;
  }

  if (result->count >= result->capacity) {
    size_t new_capacity = result->capacity == 0 ? 16 : result->capacity * 2;
    bebopc_glob_match_t* new_matches =
        realloc(result->matches, new_capacity * sizeof(bebopc_glob_match_t));
    if (!new_matches) {
      return -1;
    }
    result->matches = new_matches;
    result->capacity = new_capacity;
  }

  char* path_copy = bebopc_strdup(path);
  char* stem_copy = stem ? bebopc_strdup(stem) : NULL;
  if (!path_copy) {
    return -1;
  }

  result->matches[result->count].path = path_copy;
  result->matches[result->count].stem = stem_copy;
  result->count++;

  return 0;
}

static inline bool _glob_char_eq(char a, char b, bool case_sensitive)
{
  return case_sensitive ? (a == b) : BEBOPC_CHAR_IEQ(a, b);
}

static bool _glob_match_segment_n(
    const char* pattern, size_t plen, const char* name, size_t nlen, bool case_sensitive
)
{
  const char* p = pattern;
  const char* p_end = pattern + plen;
  const char* n = name;
  const char* n_end = name + nlen;
  const char* star_p = NULL;
  const char* star_n = NULL;

  while (n < n_end) {
    if (p < p_end && *p == '*') {
      star_p = p++;
      star_n = n;
    } else if (p < p_end && (*p == '?' || _glob_char_eq(*p, *n, case_sensitive))) {
      p++;
      n++;
    } else if (star_p) {
      p = star_p + 1;
      n = ++star_n;
    } else {
      return false;
    }
  }

  while (p < p_end && *p == '*') {
    p++;
  }

  return p == p_end;
}

static bool _glob_match_segment(const char* pattern, const char* name, bool case_sensitive)
{
  return _glob_match_segment_n(pattern, strlen(pattern), name, strlen(name), case_sensitive);
}

static bool _glob_is_double_star(const char* segment, size_t len)
{
  return len == 2 && segment[0] == '*' && segment[1] == '*';
}

static size_t _glob_split_path(const char* path, const char*** segments, size_t** lengths)
{
  if (!path || !*path) {
    *segments = NULL;
    *lengths = NULL;
    return 0;
  }

  size_t count = 1;
  for (const char* p = path; *p; p++) {
    if (*p == '/' || *p == '\\') {
      count++;
    }
  }

  *segments = malloc(count * sizeof(char*));
  *lengths = malloc(count * sizeof(size_t));
  if (!*segments || !*lengths) {
    free(*segments);
    free(*lengths);
    *segments = NULL;
    *lengths = NULL;
    return 0;
  }

  size_t idx = 0;
  const char* start = path;
  for (const char* p = path;; p++) {
    if (*p == '/' || *p == '\\' || *p == '\0') {
      (*segments)[idx] = start;
      (*lengths)[idx] = (size_t)(p - start);
      idx++;
      if (*p == '\0') {
        break;
      }
      start = p + 1;
    }
  }

  size_t write_idx = 0;
  for (size_t i = 0; i < idx; i++) {
    if ((*lengths)[i] > 0) {
      (*segments)[write_idx] = (*segments)[i];
      (*lengths)[write_idx] = (*lengths)[i];
      write_idx++;
    }
  }

  return write_idx;
}

static bool _glob_match_segments(
    const char** pattern_segs,
    size_t* pattern_lens,
    size_t pattern_count,
    const char** path_segs,
    size_t* path_lens,
    size_t path_count,
    size_t pi,
    size_t si,
    bool case_sensitive,
    uint8_t* memo
)
{
  // Memoize on the entry (pi, si): each '**' forks the recursion two ways,
  // which is exponential on patterns like **/a/**/a/** without it.
  const size_t memo_idx = pi * (path_count + 1) + si;
  if (memo && memo[memo_idx]) {
    return memo[memo_idx] == 1;
  }
  const size_t entry_idx = memo_idx;
  bool matched = false;

  while (pi < pattern_count && si < path_count) {
    if (_glob_is_double_star(pattern_segs[pi], pattern_lens[pi])) {
      matched = _glob_match_segments(
                    pattern_segs,
                    pattern_lens,
                    pattern_count,
                    path_segs,
                    path_lens,
                    path_count,
                    pi + 1,
                    si,
                    case_sensitive,
                    memo
                )
          || _glob_match_segments(
                    pattern_segs,
                    pattern_lens,
                    pattern_count,
                    path_segs,
                    path_lens,
                    path_count,
                    pi,
                    si + 1,
                    case_sensitive,
                    memo
                );
      goto done;
    }

    if (!_glob_match_segment_n(
            pattern_segs[pi], pattern_lens[pi], path_segs[si], path_lens[si], case_sensitive
        ))
    {
      goto done;
    }

    pi++;
    si++;
  }

  while (pi < pattern_count && _glob_is_double_star(pattern_segs[pi], pattern_lens[pi])) {
    pi++;
  }

  matched = pi == pattern_count && si == path_count;

done:
  if (memo) {
    memo[entry_idx] = matched ? 1 : 2;
  }
  return matched;
}

#define BEBOPC_GLOB_MAX_DEPTH 64

static void _glob_dir_traverse(
    bebopc_glob_t* glob,
    bebopc_ctx_t* ctx,
    bebopc_glob_result_t* result,
    const char* base_dir,
    const char* rel_path,
    int depth
)
{
  if (depth > BEBOPC_GLOB_MAX_DEPTH) {
    return;
  }

  char* full_path;
  if (rel_path && *rel_path) {
    full_path = bebopc_path_join(base_dir, rel_path);
  } else {
    full_path = bebopc_strdup(base_dir);
  }
  if (!full_path) {
    return;
  }

  bebopc_dir_t dir;
  if (bebopc_dir_open(&dir, ctx, full_path) != BEBOPC_OK) {
    free(full_path);
    return;
  }

  bebopc_file_t file;
  while (dir.has_next) {
    if (bebopc_dir_readfile(&dir, &file) == BEBOPC_OK) {
      if (bebopc_strcmp(file.name, BEBOPC_STRING(".")) == 0
          || bebopc_strcmp(file.name, BEBOPC_STRING("..")) == 0)
      {
        bebopc_dir_next(&dir);
        continue;
      }

      char* entry_rel_path;
      if (rel_path && *rel_path) {
        entry_rel_path = bebopc_path_join(rel_path, (const char*)file.name);
      } else {
        entry_rel_path = bebopc_strdup((const char*)file.name);
      }
      if (!entry_rel_path) {
        bebopc_dir_next(&dir);
        continue;
      }

      bool included = false;
      bool excluded = false;

      if (glob->config.preserve_order) {
        for (size_t i = 0; i < glob->pattern_count; i++) {
          bool matches = bebopc_glob_match(glob, glob->patterns[i].pattern, entry_rel_path);
          if (matches) {
            if (glob->patterns[i].type == BEBOPC_GLOB_PATTERN_INCLUDE) {
              included = true;
              excluded = false;
            } else {
              excluded = true;
              included = false;
            }
          }
        }
      } else {
        for (size_t i = 0; i < glob->pattern_count; i++) {
          if (glob->patterns[i].type == BEBOPC_GLOB_PATTERN_INCLUDE) {
            if (bebopc_glob_match(glob, glob->patterns[i].pattern, entry_rel_path)) {
              included = true;
              break;
            }
          }
        }
        if (included) {
          for (size_t i = 0; i < glob->pattern_count; i++) {
            if (glob->patterns[i].type == BEBOPC_GLOB_PATTERN_EXCLUDE) {
              if (bebopc_glob_match(glob, glob->patterns[i].pattern, entry_rel_path)) {
                excluded = true;
                break;
              }
            }
          }
        }
      }

      if (included && !excluded && !file.is_dir) {
        _glob_result_push(result, entry_rel_path, NULL);
      }

      if (file.is_dir) {
        _glob_dir_traverse(glob, ctx, result, base_dir, entry_rel_path, depth + 1);
      }

      free(entry_rel_path);
    }
    bebopc_dir_next(&dir);
  }

  bebopc_dir_close(&dir);
  free(full_path);
}

bebopc_glob_t* bebopc_glob_new(bebopc_glob_config_t config)
{
  bebopc_glob_t* glob = calloc(1, sizeof(bebopc_glob_t));
  if (!glob) {
    return NULL;
  }
  glob->config = config;
  return glob;
}

void bebopc_glob_free(bebopc_glob_t* glob)
{
  if (!glob) {
    return;
  }

  for (size_t i = 0; i < glob->pattern_count; i++) {
    free(glob->patterns[i].pattern);
  }
  free(glob->patterns);
  free(glob);
}

bebopc_error_code_t bebopc_glob_include(bebopc_glob_t* glob, const char* pattern)
{
  return _glob_pattern_add(glob, pattern, BEBOPC_GLOB_PATTERN_INCLUDE);
}

bebopc_error_code_t bebopc_glob_exclude(bebopc_glob_t* glob, const char* pattern)
{
  return _glob_pattern_add(glob, pattern, BEBOPC_GLOB_PATTERN_EXCLUDE);
}

bebopc_error_code_t bebopc_glob_include_many(
    bebopc_glob_t* glob, const char** patterns, size_t count
)
{
  for (size_t i = 0; i < count; i++) {
    bebopc_error_code_t err = _glob_pattern_add(glob, patterns[i], BEBOPC_GLOB_PATTERN_INCLUDE);
    if (err != BEBOPC_OK) {
      return err;
    }
  }
  return BEBOPC_OK;
}

bebopc_error_code_t bebopc_glob_exclude_many(
    bebopc_glob_t* glob, const char** patterns, size_t count
)
{
  for (size_t i = 0; i < count; i++) {
    bebopc_error_code_t err = _glob_pattern_add(glob, patterns[i], BEBOPC_GLOB_PATTERN_EXCLUDE);
    if (err != BEBOPC_OK) {
      return err;
    }
  }
  return BEBOPC_OK;
}

bebopc_glob_result_t* bebopc_glob_execute(
    bebopc_glob_t* glob, bebopc_ctx_t* ctx, const char* directory
)
{
  if (!glob) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "glob matcher is NULL");
    }
    return NULL;
  }

  if (!directory) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "directory is NULL");
    }
    return NULL;
  }

  bebopc_glob_result_t* result = calloc(1, sizeof(bebopc_glob_result_t));
  if (!result) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate glob result");
    }
    return NULL;
  }

  _glob_dir_traverse(glob, ctx, result, directory, "", 0);

  return result;
}

char** bebopc_glob_paths(
    bebopc_glob_t* glob, bebopc_ctx_t* ctx, const char* directory, size_t* out_count
)
{
  if (!out_count) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_INVALID_ARG, "out_count is NULL");
    }
    return NULL;
  }

  bebopc_glob_result_t* result = bebopc_glob_execute(glob, ctx, directory);
  if (!result) {
    *out_count = 0;
    return NULL;
  }

  if (result->count == 0) {
    bebopc_glob_result_free(result);
    *out_count = 0;
    return NULL;
  }

  char** paths = malloc(result->count * sizeof(char*));
  if (!paths) {
    if (ctx) {
      BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to allocate paths array");
    }
    bebopc_glob_result_free(result);
    *out_count = 0;
    return NULL;
  }

  for (size_t i = 0; i < result->count; i++) {
    paths[i] = bebopc_path_join(directory, result->matches[i].path);
    if (!paths[i]) {
      for (size_t j = 0; j < i; j++) {
        free(paths[j]);
      }
      free(paths);
      if (ctx) {
        BEBOPC_ERROR(&ctx->errors, BEBOPC_ERR_OUT_OF_MEMORY, "failed to join path");
      }
      bebopc_glob_result_free(result);
      *out_count = 0;
      return NULL;
    }
  }

  *out_count = result->count;
  bebopc_glob_result_free(result);
  return paths;
}

void bebopc_glob_paths_free(char** paths, size_t count)
{
  if (!paths) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    free(paths[i]);
  }
  free(paths);
}

void bebopc_glob_result_free(bebopc_glob_result_t* result)
{
  if (!result) {
    return;
  }

  for (size_t i = 0; i < result->count; i++) {
    free(result->matches[i].path);
    free(result->matches[i].stem);
  }
  free(result->matches);
  free(result);
}

bool bebopc_glob_match(bebopc_glob_t* glob, const char* pattern, const char* path)
{
  if (!pattern || !path) {
    return false;
  }

  bool case_sensitive = glob ? glob->config.case_sensitive : false;

  const char** pattern_segs = NULL;
  size_t* pattern_lens = NULL;
  size_t pattern_count = _glob_split_path(pattern, &pattern_segs, &pattern_lens);

  const char** path_segs = NULL;
  size_t* path_lens = NULL;
  size_t path_count = _glob_split_path(path, &path_segs, &path_lens);

  uint8_t* memo = calloc((pattern_count + 1) * (path_count + 1), 1);

  bool result = _glob_match_segments(
      pattern_segs,
      pattern_lens,
      pattern_count,
      path_segs,
      path_lens,
      path_count,
      0,
      0,
      case_sensitive,
      memo
  );

  free(memo);
  free(pattern_segs);
  free(pattern_lens);
  free(path_segs);
  free(path_lens);

  return result;
}

bool bebopc_glob_match_segment(bebopc_glob_t* glob, const char* pattern, const char* name)
{
  bool case_sensitive = glob ? glob->config.case_sensitive : false;
  return _glob_match_segment(pattern, name, case_sensitive);
}

bool bebopc_glob_filter(bebopc_glob_t* glob, const char* path)
{
  if (!glob || !path) {
    return false;
  }
  if (glob->pattern_count == 0) {
    return false;
  }

  bool included = false;
  bool excluded = false;

  if (glob->config.preserve_order) {
    for (size_t i = 0; i < glob->pattern_count; i++) {
      bool matches = bebopc_glob_match(glob, glob->patterns[i].pattern, path);
      if (matches) {
        if (glob->patterns[i].type == BEBOPC_GLOB_PATTERN_INCLUDE) {
          included = true;
          excluded = false;
        } else {
          excluded = true;
          included = false;
        }
      }
    }
  } else {
    for (size_t i = 0; i < glob->pattern_count; i++) {
      if (glob->patterns[i].type == BEBOPC_GLOB_PATTERN_INCLUDE) {
        if (bebopc_glob_match(glob, glob->patterns[i].pattern, path)) {
          included = true;
          break;
        }
      }
    }
    if (included) {
      for (size_t i = 0; i < glob->pattern_count; i++) {
        if (glob->patterns[i].type == BEBOPC_GLOB_PATTERN_EXCLUDE) {
          if (bebopc_glob_match(glob, glob->patterns[i].pattern, path)) {
            excluded = true;
            break;
          }
        }
      }
    }
  }

  return included && !excluded;
}

bool bebopc_glob_is_pattern(const char* str)
{
  if (!str) {
    return false;
  }

  for (size_t i = 0; str[i] != '\0'; i++) {
    char c = str[i];

    if (c == '\\' && str[i + 1] != '\0') {
      i++;
      continue;
    }

    if (c == '*' || c == '?') {
      return true;
    }

    if (c == '[') {
      size_t j = i + 1;
      if (str[j] == '!' || str[j] == '^') {
        j++;
      }
      while (str[j] != '\0' && str[j] != ']') {
        if (str[j] == '\\' && str[j + 1] != '\0') {
          j++;
        }
        j++;
      }
      if (str[j] == ']') {
        return true;
      }
    }
  }

  return false;
}
