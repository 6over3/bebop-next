#include <bebop.h>
#include <bebop_wire.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef __GNUC__
#define GEN_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
#else
#define GEN_PRINTF(fmt, args)
#endif

#define GEN_PATH_SIZE 512
#define GEN_STACK_DEPTH 64
#define GEN_TYPE_SET_CAP 256

typedef struct {
  char* data;
  size_t len;
  size_t cap;
} gen_sb_t;

static void sb_init(gen_sb_t* sb)
{
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static void sb_free(gen_sb_t* sb)
{
  free(sb->data);
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static void sb_grow(gen_sb_t* sb, size_t needed)
{
  if (needed <= sb->cap) {
    return;
  }
  size_t new_cap = sb->cap ? sb->cap * 2 : 256;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  sb->data = realloc(sb->data, new_cap);
  sb->cap = new_cap;
}

static void sb_putc(gen_sb_t* sb, char c)
{
  sb_grow(sb, sb->len + 2);
  sb->data[sb->len++] = c;
  sb->data[sb->len] = '\0';
}

static void sb_puts(gen_sb_t* sb, const char* s)
{
  if (!s) {
    return;
  }
  size_t len = strlen(s);
  sb_grow(sb, sb->len + len + 1);
  memcpy(sb->data + sb->len, s, len);
  sb->len += len;
  sb->data[sb->len] = '\0';
}

static void sb_puts_screaming_raw(gen_sb_t* sb, const char* s)
{
  if (!s) {
    return;
  }
  bool prev_lower = false;
  bool prev_letter = false;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 'A' && c <= 'Z') {
      if (prev_lower) {
        sb_putc(sb, '_');
      }
      sb_putc(sb, (char)c);
      prev_lower = false;
      prev_letter = true;
    } else if (c >= 'a' && c <= 'z') {
      sb_putc(sb, (char)(c - 32));
      prev_lower = true;
      prev_letter = true;
    } else if (c >= '0' && c <= '9') {
      if (prev_letter) {
        sb_putc(sb, '_');
      }
      sb_putc(sb, (char)c);
      prev_lower = false;
      prev_letter = false;
    } else {
      sb_putc(sb, (char)c);
      prev_lower = false;
      prev_letter = false;
    }
  }
}

static void sb_puts_screaming(gen_sb_t* sb, const char* s)
{
  gen_sb_t tmp;
  sb_init(&tmp);
  sb_puts_screaming_raw(&tmp, s);

  const char* p = tmp.data;
  const char* prev_word = NULL;
  size_t prev_len = 0;
  bool first_word = true;

  while (p && *p) {
    while (*p == '_') {
      p++;
    }
    if (!*p) break;

    const char* word_start = p;
    while (*p && *p != '_') {
      p++;
    }
    size_t word_len = (size_t)(p - word_start);

    bool is_dup = prev_word && prev_len == word_len &&
                  memcmp(prev_word, word_start, word_len) == 0;
    if (!is_dup) {
      if (!first_word) {
        sb_putc(sb, '_');
      }
      for (size_t i = 0; i < word_len; i++) {
        sb_putc(sb, word_start[i]);
      }
      prev_word = word_start;
      prev_len = word_len;
      first_word = false;
    }
  }

  sb_free(&tmp);
}

static void screaming_name(char* buf, size_t buf_size, const char* name)
{
  gen_sb_t sb;
  sb_init(&sb);
  sb_puts_screaming(&sb, name);
  size_t copy_len = sb.len < buf_size - 1 ? sb.len : buf_size - 1;
  memcpy(buf, sb.data, copy_len);
  buf[copy_len] = '\0';
  sb_free(&sb);
}


static bool str_eq_nocase(const char* a, size_t a_len, const char* b, size_t b_len)
{
  if (a_len != b_len) {
    return false;
  }
  for (size_t i = 0; i < a_len; i++) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'a' && ca <= 'z') {
      ca -= 32;
    }
    if (cb >= 'a' && cb <= 'z') {
      cb -= 32;
    }
    if (ca != cb) {
      return false;
    }
  }
  return true;
}

static const char* find_last_word(const char* s, size_t len, size_t* word_len)
{
  if (len == 0) {
    *word_len = 0;
    return s;
  }
  size_t end = len;
  while (end > 0 && s[end - 1] == '_') {
    end--;
  }
  size_t start = end;
  while (start > 0 && s[start - 1] != '_') {
    start--;
  }
  *word_len = end - start;
  return s + start;
}

static const char* find_first_word(const char* s, size_t* word_len)
{
  while (*s == '_') {
    s++;
  }
  const char* start = s;
  while (*s && *s != '_') {
    s++;
  }
  *word_len = (size_t)(s - start);
  return start;
}

static void sb_enum_member(gen_sb_t* sb, const char* type_name, const char* member)
{
  gen_sb_t prefix;
  sb_init(&prefix);
  sb_puts_screaming(&prefix, type_name);

  gen_sb_t suffix;
  sb_init(&suffix);
  sb_puts_screaming(&suffix, member);

  size_t prefix_last_len = 0;
  const char* prefix_last =
      find_last_word(prefix.data, prefix.len, &prefix_last_len);

  size_t suffix_first_len = 0;
  const char* suffix_first = find_first_word(suffix.data, &suffix_first_len);

  sb_puts(sb, prefix.data);
  sb_putc(sb, '_');

  if (str_eq_nocase(prefix_last, prefix_last_len, suffix_first, suffix_first_len)
      && suffix_first_len > 0)
  {
    const char* skip = suffix_first + suffix_first_len;
    while (*skip == '_') {
      skip++;
    }
    sb_puts(sb, skip);
  } else {
    sb_puts(sb, suffix.data);
  }

  sb_free(&prefix);
  sb_free(&suffix);
}

static void sb_union_case(gen_sb_t* sb, const char* type_name, const char* branch)
{
  sb_puts(sb, "case ");
  sb_enum_member(sb, type_name, branch);
  sb_putc(sb, ':');
}

GEN_PRINTF(2, 3)

static void sb_printf(gen_sb_t* sb, const char* fmt, ...)
{
  va_list args, args2;
  va_start(args, fmt);
  va_copy(args2, args);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed > 0) {
    sb_grow(sb, sb->len + (size_t)needed + 1);
    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, args2);
    sb->len += (size_t)needed;
  }
  va_end(args2);
}

static void sb_indent(gen_sb_t* sb, int level)
{
  for (int i = 0; i < level; i++) {
    sb_puts(sb, "    ");
  }
}

static char* sb_steal(gen_sb_t* sb)
{
  if (!sb->data) {
    sb_grow(sb, 1);
    sb->data[0] = '\0';
  }
  char* result = sb->data;
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
  return result;
}

static void sb_escape_string(gen_sb_t* sb, const char* s)
{
  if (!s) {
    return;
  }
  for (; *s; s++) {
    switch (*s) {
      case '\\':
        sb_puts(sb, "\\\\");
        break;
      case '"':
        sb_puts(sb, "\\\"");
        break;
      case '\n':
        sb_puts(sb, "\\n");
        break;
      case '\r':
        sb_puts(sb, "\\r");
        break;
      case '\t':
        sb_puts(sb, "\\t");
        break;
      default:
        sb_putc(sb, *s);
        break;
    }
  }
}

typedef struct {
  const char* fqns[GEN_TYPE_SET_CAP];
  uint32_t hashes[GEN_TYPE_SET_CAP];
  uint16_t count;
} gen_type_set_t;

static uint32_t fnv1a(const char* s)
{
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h ? h : 1;
}

static void type_set_init(gen_type_set_t* set)
{
  memset((void*)set->fqns, 0, sizeof(set->fqns));
  memset(set->hashes, 0, sizeof(set->hashes));
  set->count = 0;
}

static bool type_set_has(gen_type_set_t* set, const char* name)
{
  uint32_t h = fnv1a(name);
  for (uint16_t i = 0; i < set->count; i++) {
    if (set->hashes[i] == h) {
      return true;
    }
  }
  return false;
}

static bool type_set_add(gen_type_set_t* set, const char* name)
{
  if (type_set_has(set, name)) {
    return false;
  }
  if (set->count >= GEN_TYPE_SET_CAP) {
    return false;
  }
  set->fqns[set->count] = name;
  set->hashes[set->count++] = fnv1a(name);
  return true;
}

typedef enum {
  GEN_STD_C99,
  GEN_STD_C11,
  GEN_STD_C23,
} gen_standard_t;

typedef enum {
  GEN_OUT_SPLIT,
  GEN_OUT_UNITY,
  GEN_OUT_SINGLE_HEADER,
} gen_output_mode_t;

typedef enum {
  GEN_EMIT_DECL,
  GEN_EMIT_IMPL,
} gen_emit_mode_t;

typedef struct {
  gen_standard_t c_standard;
  gen_output_mode_t output_mode;
  const char* prefix;
  bool no_reflection;
} gen_opts_t;

static gen_opts_t opts_parse(const bebop_plugin_request_t* req)
{
  gen_opts_t opts = {
      .c_standard = GEN_STD_C11,
      .output_mode = GEN_OUT_SPLIT,
      .prefix = "",
      .no_reflection = false,
  };

  uint32_t n = bebop_plugin_request_host_option_count(req);
  for (uint32_t i = 0; i < n; i++) {
    const char* key = bebop_plugin_request_host_option_key(req, i);
    const char* val = bebop_plugin_request_host_option_value(req, i);

    if (strcmp(key, "c_standard") == 0) {
      if (strcmp(val, "c99") == 0) {
        opts.c_standard = GEN_STD_C99;
      } else if (strcmp(val, "c23") == 0) {
        opts.c_standard = GEN_STD_C23;
      }
    } else if (strcmp(key, "output_mode") == 0) {
      if (strcmp(val, "unity") == 0) {
        opts.output_mode = GEN_OUT_UNITY;
      } else if (strcmp(val, "single_header") == 0) {
        opts.output_mode = GEN_OUT_SINGLE_HEADER;
      }
    } else if (strcmp(key, "prefix") == 0) {
      opts.prefix = val;
    } else if (strcmp(key, "no_reflection") == 0) {
      opts.no_reflection = strcmp(val, "true") == 0 || strcmp(val, "1") == 0;
    }
  }
  return opts;
}

static const char* C_KEYWORDS[] = {
    "auto",       "break",      "case",           "char",
    "const",      "continue",   "default",        "do",
    "double",     "else",       "enum",           "extern",
    "float",      "for",        "goto",           "if",
    "int",        "long",       "register",       "return",
    "short",      "signed",     "sizeof",         "static",
    "struct",     "switch",     "typedef",        "union",
    "unsigned",   "void",       "volatile",       "while",
    "inline",     "restrict",   "_Bool",          "_Complex",
    "_Imaginary", "_Alignas",   "_Alignof",       "_Atomic",
    "_Generic",   "_Noreturn",  "_Static_assert", "_Thread_local",
    "alignas",    "alignof",    "bool",           "constexpr",
    "false",      "nullptr",    "static_assert",  "thread_local",
    "true",       "typeof",     "typeof_unqual",  "_BitInt",
    "_Decimal32", "_Decimal64", "_Decimal128",    NULL};

static bool is_keyword(const char* name)
{
  for (const char** kw = C_KEYWORDS; *kw; kw++) {
    if (strcmp(name, *kw) == 0) {
      return true;
    }
  }
  return false;
}

typedef struct {
  bebop_type_kind_t kind;
  const char* ctype;
  const char* wire_get;
  const char* wire_set;
  const char* size_macro;
  uint32_t size;
} type_info_t;

static const type_info_t TYPE_MAP[] = {
    {BEBOP_TYPE_BOOL, "bool", "Bebop_Reader_GetBool", "Bebop_Writer_SetBool", "BEBOP_WIRE_SIZE_BOOL", 1},
    {BEBOP_TYPE_BYTE, "uint8_t", "Bebop_Reader_GetByte", "Bebop_Writer_SetByte", "BEBOP_WIRE_SIZE_BYTE", 1},
    {BEBOP_TYPE_INT8, "int8_t", "Bebop_Reader_GetI8", "Bebop_Writer_SetI8", "BEBOP_WIRE_SIZE_INT8", 1},
    {BEBOP_TYPE_INT16, "int16_t", "Bebop_Reader_GetI16", "Bebop_Writer_SetI16", "BEBOP_WIRE_SIZE_INT16", 2},
    {BEBOP_TYPE_UINT16, "uint16_t", "Bebop_Reader_GetU16", "Bebop_Writer_SetU16", "BEBOP_WIRE_SIZE_UINT16", 2},
    {BEBOP_TYPE_INT32, "int32_t", "Bebop_Reader_GetI32", "Bebop_Writer_SetI32", "BEBOP_WIRE_SIZE_INT32", 4},
    {BEBOP_TYPE_UINT32, "uint32_t", "Bebop_Reader_GetU32", "Bebop_Writer_SetU32", "BEBOP_WIRE_SIZE_UINT32", 4},
    {BEBOP_TYPE_INT64, "int64_t", "Bebop_Reader_GetI64", "Bebop_Writer_SetI64", "BEBOP_WIRE_SIZE_INT64", 8},
    {BEBOP_TYPE_UINT64, "uint64_t", "Bebop_Reader_GetU64", "Bebop_Writer_SetU64", "BEBOP_WIRE_SIZE_UINT64", 8},
    {BEBOP_TYPE_INT128, "Bebop_Int128", "Bebop_Reader_GetI128", "Bebop_Writer_SetI128", "BEBOP_WIRE_SIZE_INT128", 16},
    {BEBOP_TYPE_UINT128, "Bebop_UInt128", "Bebop_Reader_GetU128", "Bebop_Writer_SetU128", "BEBOP_WIRE_SIZE_UINT128", 16},
    {BEBOP_TYPE_FLOAT16, "Bebop_Float16", "Bebop_Reader_GetF16", "Bebop_Writer_SetF16", "BEBOP_WIRE_SIZE_FLOAT16", 2},
    {BEBOP_TYPE_FLOAT32, "float", "Bebop_Reader_GetF32", "Bebop_Writer_SetF32", "BEBOP_WIRE_SIZE_FLOAT32", 4},
    {BEBOP_TYPE_FLOAT64, "double", "Bebop_Reader_GetF64", "Bebop_Writer_SetF64", "BEBOP_WIRE_SIZE_FLOAT64", 8},
    {BEBOP_TYPE_BFLOAT16, "Bebop_BFloat16", "Bebop_Reader_GetBF16", "Bebop_Writer_SetBF16", "BEBOP_WIRE_SIZE_BFLOAT16", 2},
    {BEBOP_TYPE_STRING, "Bebop_Str", "Bebop_Reader_GetStr", "Bebop_Writer_SetStrView", NULL, 0},
    {BEBOP_TYPE_UUID, "Bebop_UUID", "Bebop_Reader_GetUUID", "Bebop_Writer_SetUUID", "BEBOP_WIRE_SIZE_UUID", 16},
    {BEBOP_TYPE_TIMESTAMP, "Bebop_Timestamp", "Bebop_Reader_GetTimestamp", "Bebop_Writer_SetTimestamp", "BEBOP_WIRE_SIZE_TIMESTAMP", 12},
    {BEBOP_TYPE_DURATION, "Bebop_Duration", "Bebop_Reader_GetDuration", "Bebop_Writer_SetDuration", "BEBOP_WIRE_SIZE_DURATION", 12},
    {0, NULL, NULL, NULL, NULL, 0}};

static const type_info_t* type_info(bebop_type_kind_t kind)
{
  for (const type_info_t* t = TYPE_MAP; t->ctype; t++) {
    if (t->kind == kind) {
      return t;
    }
  }
  return NULL;
}

static bool is_primitive(bebop_type_kind_t kind)
{
  const type_info_t* t = type_info(kind);
  return t && t->size > 0;
}

static uint32_t estimate_elem_size(const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  const type_info_t* ti = type_info(kind);
  if (ti && ti->size > 0) return ti->size;
  if (kind == BEBOP_TYPE_STRING) return 16;
  if (kind == BEBOP_TYPE_DEFINED) return 32;
  if (kind == BEBOP_TYPE_ARRAY || kind == BEBOP_TYPE_MAP) return 24;
  if (kind == BEBOP_TYPE_FIXED_ARRAY) {
    uint32_t inner_size = estimate_elem_size(bebop_descriptor_type_element(type));
    return inner_size * bebop_descriptor_type_fixed_size(type);
  }
  return 16;
}

static uint32_t calc_prefetch_dist(const bebop_descriptor_type_t* elem)
{
  uint32_t elem_size = estimate_elem_size(elem);
  uint32_t dist = 128 / (elem_size > 0 ? elem_size : 1);
  return dist < 1 ? 1 : (dist > 16 ? 16 : dist);
}

static uint32_t type_alignment(const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  switch (kind) {
    case BEBOP_TYPE_INT64:
    case BEBOP_TYPE_UINT64:
    case BEBOP_TYPE_FLOAT64:
    case BEBOP_TYPE_STRING:
    case BEBOP_TYPE_ARRAY:
    case BEBOP_TYPE_MAP:
    case BEBOP_TYPE_DEFINED:
      return 8;
    case BEBOP_TYPE_INT32:
    case BEBOP_TYPE_UINT32:
    case BEBOP_TYPE_FLOAT32:
      return 4;
    case BEBOP_TYPE_INT16:
    case BEBOP_TYPE_UINT16:
    case BEBOP_TYPE_FLOAT16:
    case BEBOP_TYPE_BFLOAT16:
      return 2;
    case BEBOP_TYPE_INT128:
    case BEBOP_TYPE_UINT128:
    case BEBOP_TYPE_UUID:
    case BEBOP_TYPE_TIMESTAMP:
    case BEBOP_TYPE_DURATION:
      return 8;
    case BEBOP_TYPE_FIXED_ARRAY:
      return type_alignment(bebop_descriptor_type_element(type));
    default:
      return 1;
  }
}

static uint32_t type_c_size(const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  const type_info_t* ti = type_info(kind);
  if (ti && ti->size > 0) return ti->size;
  if (kind == BEBOP_TYPE_STRING) return sizeof(void*) + sizeof(size_t);
  if (kind == BEBOP_TYPE_ARRAY) return sizeof(void*) + 2 * sizeof(size_t);
  if (kind == BEBOP_TYPE_MAP) return 6 * sizeof(void*);
  if (kind == BEBOP_TYPE_DEFINED) return sizeof(void*);
  if (kind == BEBOP_TYPE_FIXED_ARRAY) {
    return type_c_size(bebop_descriptor_type_element(type)) *
           bebop_descriptor_type_fixed_size(type);
  }
  return sizeof(void*);
}

static const char* bulk_set(bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
      return "Bebop_Writer_SetBoolArray";
    case BEBOP_TYPE_INT8:
      return "Bebop_Writer_SetI8Array";
    case BEBOP_TYPE_BYTE:
      return "Bebop_Writer_SetByteArray";
    case BEBOP_TYPE_INT16:
      return "Bebop_Writer_SetI16Array";
    case BEBOP_TYPE_UINT16:
      return "Bebop_Writer_SetU16Array";
    case BEBOP_TYPE_INT32:
      return "Bebop_Writer_SetI32Array";
    case BEBOP_TYPE_UINT32:
      return "Bebop_Writer_SetU32Array";
    case BEBOP_TYPE_INT64:
      return "Bebop_Writer_SetI64Array";
    case BEBOP_TYPE_UINT64:
      return "Bebop_Writer_SetU64Array";
    case BEBOP_TYPE_INT128:
      return "Bebop_Writer_SetI128Array";
    case BEBOP_TYPE_UINT128:
      return "Bebop_Writer_SetU128Array";
    case BEBOP_TYPE_FLOAT16:
      return "Bebop_Writer_SetF16Array";
    case BEBOP_TYPE_FLOAT32:
      return "Bebop_Writer_SetF32Array";
    case BEBOP_TYPE_FLOAT64:
      return "Bebop_Writer_SetF64Array";
    case BEBOP_TYPE_BFLOAT16:
      return "Bebop_Writer_SetBF16Array";
    case BEBOP_TYPE_UUID:
      return "Bebop_Writer_SetUUIDArray";
    case BEBOP_TYPE_TIMESTAMP:
      return "Bebop_Writer_SetTimestampArray";
    case BEBOP_TYPE_DURATION:
      return "Bebop_Writer_SetDurationArray";
    default:
      return NULL;
  }
}

static const char* bulk_set_fixed(bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
      return "Bebop_Writer_SetFixedBoolArray";
    case BEBOP_TYPE_INT8:
      return "Bebop_Writer_SetFixedI8Array";
    case BEBOP_TYPE_BYTE:
      return "Bebop_Writer_SetFixedU8Array";
    case BEBOP_TYPE_INT16:
      return "Bebop_Writer_SetFixedI16Array";
    case BEBOP_TYPE_UINT16:
      return "Bebop_Writer_SetFixedU16Array";
    case BEBOP_TYPE_INT32:
      return "Bebop_Writer_SetFixedI32Array";
    case BEBOP_TYPE_UINT32:
      return "Bebop_Writer_SetFixedU32Array";
    case BEBOP_TYPE_INT64:
      return "Bebop_Writer_SetFixedI64Array";
    case BEBOP_TYPE_UINT64:
      return "Bebop_Writer_SetFixedU64Array";
    case BEBOP_TYPE_INT128:
      return "Bebop_Writer_SetFixedI128Array";
    case BEBOP_TYPE_UINT128:
      return "Bebop_Writer_SetFixedU128Array";
    case BEBOP_TYPE_FLOAT16:
      return "Bebop_Writer_SetFixedF16Array";
    case BEBOP_TYPE_FLOAT32:
      return "Bebop_Writer_SetFixedF32Array";
    case BEBOP_TYPE_FLOAT64:
      return "Bebop_Writer_SetFixedF64Array";
    case BEBOP_TYPE_BFLOAT16:
      return "Bebop_Writer_SetFixedBF16Array";
    case BEBOP_TYPE_UUID:
      return "Bebop_Writer_SetFixedUUIDArray";
    case BEBOP_TYPE_TIMESTAMP:
      return "Bebop_Writer_SetFixedTimestampArray";
    case BEBOP_TYPE_DURATION:
      return "Bebop_Writer_SetFixedDurationArray";
    default:
      return NULL;
  }
}

static const char* bulk_get_fixed(bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
      return "Bebop_Reader_GetFixedBoolArray";
    case BEBOP_TYPE_INT8:
      return "Bebop_Reader_GetFixedI8Array";
    case BEBOP_TYPE_BYTE:
      return "Bebop_Reader_GetFixedU8Array";
    case BEBOP_TYPE_INT16:
      return "Bebop_Reader_GetFixedI16Array";
    case BEBOP_TYPE_UINT16:
      return "Bebop_Reader_GetFixedU16Array";
    case BEBOP_TYPE_INT32:
      return "Bebop_Reader_GetFixedI32Array";
    case BEBOP_TYPE_UINT32:
      return "Bebop_Reader_GetFixedU32Array";
    case BEBOP_TYPE_INT64:
      return "Bebop_Reader_GetFixedI64Array";
    case BEBOP_TYPE_UINT64:
      return "Bebop_Reader_GetFixedU64Array";
    case BEBOP_TYPE_INT128:
      return "Bebop_Reader_GetFixedI128Array";
    case BEBOP_TYPE_UINT128:
      return "Bebop_Reader_GetFixedU128Array";
    case BEBOP_TYPE_FLOAT16:
      return "Bebop_Reader_GetFixedF16Array";
    case BEBOP_TYPE_FLOAT32:
      return "Bebop_Reader_GetFixedF32Array";
    case BEBOP_TYPE_FLOAT64:
      return "Bebop_Reader_GetFixedF64Array";
    case BEBOP_TYPE_BFLOAT16:
      return "Bebop_Reader_GetFixedBF16Array";
    case BEBOP_TYPE_UUID:
      return "Bebop_Reader_GetFixedUUIDArray";
    case BEBOP_TYPE_TIMESTAMP:
      return "Bebop_Reader_GetFixedTimestampArray";
    case BEBOP_TYPE_DURATION:
      return "Bebop_Reader_GetFixedDurationArray";
    default:
      return NULL;
  }
}

typedef struct {
  gen_sb_t out;
  gen_opts_t opts;
  gen_type_set_t array_types;
  gen_type_set_t map_types;
  gen_type_set_t message_fqns;
  const bebop_plugin_request_t* req;
  const bebop_descriptor_schema_t* schema;
  int indent;
  int loop_depth;
  bool is_mutable;
  bool in_step_fn;
  gen_emit_mode_t emit_mode;
  const char* error;
} gen_ctx_t;

static void ctx_init(gen_ctx_t* ctx, const bebop_plugin_request_t* req)
{
  sb_init(&ctx->out);
  ctx->opts = opts_parse(req);
  type_set_init(&ctx->array_types);
  type_set_init(&ctx->map_types);
  type_set_init(&ctx->message_fqns);
  ctx->req = req;
  ctx->schema = NULL;
  ctx->indent = 0;
  ctx->loop_depth = 0;
  ctx->is_mutable = false;
  ctx->in_step_fn = false;
  ctx->emit_mode = GEN_EMIT_IMPL;
  ctx->error = NULL;
}

static void ctx_free(gen_ctx_t* ctx)
{
  sb_free(&ctx->out);
}

GEN_PRINTF(2, 3)

static void emit(gen_ctx_t* ctx, const char* fmt, ...)
{
  sb_indent(&ctx->out, ctx->indent);
  va_list args, args2;
  va_start(args, fmt);
  va_copy(args2, args);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed > 0) {
    sb_grow(&ctx->out, ctx->out.len + (size_t)needed + 1);
    vsnprintf(ctx->out.data + ctx->out.len, (size_t)needed + 1, fmt, args2);
    ctx->out.len += (size_t)needed;
  }
  va_end(args2);
  sb_putc(&ctx->out, '\n');
}

static void emit_nl(gen_ctx_t* ctx)
{
  sb_putc(&ctx->out, '\n');
}

static void emit_doc(gen_ctx_t* ctx, const char* doc)
{
  if (doc && doc[0]) {
    emit(ctx, "/** %s */", doc);
  }
}

static bool emit_fn_start_ex(gen_ctx_t* ctx,
                             const char* attr,
                             const char* ret,
                             const char* name,
                             const char* params)
{
  if (ctx->emit_mode == GEN_EMIT_DECL) {
    if (attr && attr[0]) {
      emit(ctx, "%s %s %s(%s);", attr, ret, name, params);
    } else {
      emit(ctx, "%s %s(%s);", ret, name, params);
    }
    return false;
  }
  if (attr && attr[0]) {
    emit(ctx, "%s %s %s(%s) {", attr, ret, name, params);
  } else {
    emit(ctx, "%s %s(%s) {", ret, name, params);
  }
  return true;
}


static void name_from_fqn(gen_sb_t* out, const char* prefix, const char* fqn)
{
  bool cap_next = true;
  if (prefix && prefix[0]) {
    for (const char* p = prefix; *p; p++) {
      char c = *p;
      if (cap_next && c >= 'a' && c <= 'z') {
        c -= 32;
      }
      cap_next = (c == '_');
      sb_putc(out, c);
    }
  }
  for (const char* p = fqn; *p; p++) {
    if (*p == '.') {
      sb_putc(out, '_');
      cap_next = true;
    } else {
      char c = *p;
      if (cap_next && c >= 'a' && c <= 'z') {
        c -= 32;
      }
      cap_next = false;
      sb_putc(out, c);
    }
  }
}

static const char* type_name(gen_ctx_t* ctx, const char* fqn)
{
  static char bufs[4][GEN_PATH_SIZE];
  static int idx = 0;
  char* buf = bufs[idx++ & 3];
  char* p = buf;
  const char* prefix = ctx->opts.prefix;
  bool cap_next = true;
  if (prefix && prefix[0]) {
    while (*prefix && p < buf + GEN_PATH_SIZE - 1) {
      char c = *prefix++;
      if (cap_next && c >= 'a' && c <= 'z') {
        c -= 32;
      }
      cap_next = (c == '_');
      *p++ = c;
    }
  }
  while (*fqn && p < buf + GEN_PATH_SIZE - 1) {
    if (*fqn == '.') {
      *p++ = '_';
      cap_next = true;
    } else {
      char c = *fqn;
      if (cap_next && c >= 'a' && c <= 'z') {
        c -= 32;
      }
      cap_next = false;
      *p++ = c;
    }
    fqn++;
  }
  *p = '\0';
  return buf;
}

static void sb_puts_screaming_ident(gen_sb_t* out, const char* s)
{
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c >= 'a' && c <= 'z') {
      sb_putc(out, c - 32);
    } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      sb_putc(out, c);
    } else {
      sb_putc(out, '_');
    }
  }
}

static void include_guard(gen_sb_t* out, const char* package, const char* path)
{
  if (!path && !package) {
    sb_puts(out, "BEBOP_GENERATED_H_");
    return;
  }
  if (package && *package) {
    sb_puts_screaming_ident(out, package);
    sb_putc(out, '_');
  }
  if (path) {
    const char* filename = path;
    for (const char* p = path; *p; p++) {
      if (*p == '/' || *p == '\\') {
        filename = p + 1;
      }
    }
    sb_puts_screaming_ident(out, filename);
  }
  sb_puts(out, "_H_");
}

static const char* safe_field_name(const char* name)
{
  static char buf[GEN_PATH_SIZE];
  if (is_keyword(name)) {
    snprintf(buf, sizeof(buf), "%s_", name);
    return buf;
  }
  return name;
}

static const bebop_descriptor_def_t* find_def_in(
    const bebop_descriptor_def_t* def, const char* fqn)
{
  const char* def_fqn = bebop_descriptor_def_fqn(def);
  if (def_fqn && strcmp(def_fqn, fqn) == 0) {
    return def;
  }
  uint32_t nested = bebop_descriptor_def_nested_count(def);
  for (uint32_t i = 0; i < nested; i++) {
    const bebop_descriptor_def_t* found =
        find_def_in(bebop_descriptor_def_nested_at(def, i), fqn);
    if (found) {
      return found;
    }
  }
  return NULL;
}

static const bebop_descriptor_def_t* find_def(gen_ctx_t* ctx, const char* fqn)
{
  if (!ctx->req || !fqn) {
    return NULL;
  }
  uint32_t schema_count = bebop_plugin_request_schema_count(ctx->req);
  for (uint32_t s = 0; s < schema_count; s++) {
    const bebop_descriptor_schema_t* schema =
        bebop_plugin_request_schema_at(ctx->req, s);
    uint32_t n = bebop_descriptor_schema_def_count(schema);
    for (uint32_t i = 0; i < n; i++) {
      const bebop_descriptor_def_t* found =
          find_def_in(bebop_descriptor_schema_def_at(schema, i), fqn);
      if (found) {
        return found;
      }
    }
  }
  return NULL;
}

static bool type_is_enum(gen_ctx_t* ctx, const bebop_descriptor_type_t* type)
{
  if (bebop_descriptor_type_kind(type) != BEBOP_TYPE_DEFINED) {
    return false;
  }
  const bebop_descriptor_def_t* def =
      find_def(ctx, bebop_descriptor_type_fqn(type));
  return def && bebop_descriptor_def_kind(def) == BEBOP_DEF_ENUM;
}

static bool type_is_message_or_union(gen_ctx_t* ctx,
                                     const bebop_descriptor_type_t* type)
{
  if (bebop_descriptor_type_kind(type) != BEBOP_TYPE_DEFINED) {
    return false;
  }
  const bebop_descriptor_def_t* def =
      find_def(ctx, bebop_descriptor_type_fqn(type));
  if (!def) {
    return false;
  }
  bebop_def_kind_t k = bebop_descriptor_def_kind(def);
  return k == BEBOP_DEF_MESSAGE || k == BEBOP_DEF_UNION;
}

static bebop_type_kind_t enum_base_type(gen_ctx_t* ctx,
                                        const bebop_descriptor_type_t* type)
{
  const bebop_descriptor_def_t* def =
      find_def(ctx, bebop_descriptor_type_fqn(type));
  if (!def) {
    return BEBOP_TYPE_UINT32;
  }
  return bebop_descriptor_def_base_type(def);
}

static void emit_ctype(gen_ctx_t* ctx, const bebop_descriptor_type_t* type);

static void short_name(gen_sb_t* out,
                       gen_ctx_t* ctx,
                       const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t k = bebop_descriptor_type_kind(type);
  if (k == BEBOP_TYPE_DEFINED) {
    const char* fqn = bebop_descriptor_type_fqn(type);
    for (const char* p = fqn; *p; p++) {
      sb_putc(out, *p == '.' ? '_' : (char)tolower((unsigned char)*p));
    }
  } else if (k == BEBOP_TYPE_ARRAY) {
    short_name(out, ctx, bebop_descriptor_type_element(type));
    sb_puts(out, "_arr");
  } else if (k == BEBOP_TYPE_FIXED_ARRAY) {
    short_name(out, ctx, bebop_descriptor_type_element(type));
    sb_printf(out, "_fa%u", bebop_descriptor_type_fixed_size(type));
  } else if (k == BEBOP_TYPE_MAP) {
    short_name(out, ctx, bebop_descriptor_type_key(type));
    sb_putc(out, '_');
    short_name(out, ctx, bebop_descriptor_type_value(type));
    sb_puts(out, "_map");
  } else {
    const char* names[] = {
        [BEBOP_TYPE_STRING] = "str",    [BEBOP_TYPE_INT8] = "i8",
        [BEBOP_TYPE_BYTE] = "u8",       [BEBOP_TYPE_INT16] = "i16",
        [BEBOP_TYPE_UINT16] = "u16",    [BEBOP_TYPE_INT32] = "i32",
        [BEBOP_TYPE_UINT32] = "u32",    [BEBOP_TYPE_INT64] = "i64",
        [BEBOP_TYPE_UINT64] = "u64",    [BEBOP_TYPE_INT128] = "i128",
        [BEBOP_TYPE_UINT128] = "u128",  [BEBOP_TYPE_FLOAT16] = "f16",
        [BEBOP_TYPE_FLOAT32] = "f32",   [BEBOP_TYPE_FLOAT64] = "f64",
        [BEBOP_TYPE_BFLOAT16] = "bf16", [BEBOP_TYPE_BOOL] = "bool",
        [BEBOP_TYPE_UUID] = "uuid",     [BEBOP_TYPE_TIMESTAMP] = "ts",
        [BEBOP_TYPE_DURATION] = "dur",
    };
    if (k < sizeof(names) / sizeof(names[0]) && names[k]) {
      sb_puts(out, names[k]);
    } else {
      sb_puts(out, "x");
    }
  }
}

static void array_typedef_name(gen_sb_t* out,
                               gen_ctx_t* ctx,
                               const bebop_descriptor_type_t* elem)
{
  bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
  if (ek == BEBOP_TYPE_FIXED_ARRAY || ek == BEBOP_TYPE_MAP) {
    sb_puts(out, "Bebop_");
    gen_sb_t sn;
    sb_init(&sn);
    short_name(&sn, ctx, elem);
    bool cap_next = true;
    for (const char* p = sn.data; *p; p++) {
      if (*p == '_') {
        cap_next = true;
      } else if (cap_next) {
        sb_putc(out, (char)toupper((unsigned char)*p));
        cap_next = false;
      } else {
        sb_putc(out, *p);
      }
    }
    sb_free(&sn);
  } else {
    size_t old_len = ctx->out.len;
    emit_ctype(ctx, elem);
    size_t type_len = ctx->out.len - old_len;
    sb_grow(out, type_len + 1);
    memcpy(out->data + out->len, ctx->out.data + old_len, type_len);
    out->len += type_len;
    out->data[out->len] = '\0';
    ctx->out.len = old_len;
    ctx->out.data[old_len] = '\0';
  }
}

static const char* map_hash_fn(bebop_type_kind_t k)
{
  switch (k) {
    case BEBOP_TYPE_BOOL: return "Bebop_MapHash_Bool";
    case BEBOP_TYPE_BYTE: return "Bebop_MapHash_Byte";
    case BEBOP_TYPE_INT8: return "Bebop_MapHash_I8";
    case BEBOP_TYPE_INT16: return "Bebop_MapHash_I16";
    case BEBOP_TYPE_UINT16: return "Bebop_MapHash_U16";
    case BEBOP_TYPE_INT32: return "Bebop_MapHash_I32";
    case BEBOP_TYPE_UINT32: return "Bebop_MapHash_U32";
    case BEBOP_TYPE_INT64: return "Bebop_MapHash_I64";
    case BEBOP_TYPE_UINT64: return "Bebop_MapHash_U64";
    case BEBOP_TYPE_INT128: return "Bebop_MapHash_I128";
    case BEBOP_TYPE_UINT128: return "Bebop_MapHash_U128";
    case BEBOP_TYPE_STRING: return "Bebop_MapHash_Str";
    case BEBOP_TYPE_UUID: return "Bebop_MapHash_UUID";
    default: return NULL;
  }
}

static const char* map_eq_fn(bebop_type_kind_t k)
{
  switch (k) {
    case BEBOP_TYPE_BOOL: return "Bebop_MapEq_Bool";
    case BEBOP_TYPE_BYTE: return "Bebop_MapEq_Byte";
    case BEBOP_TYPE_INT8: return "Bebop_MapEq_I8";
    case BEBOP_TYPE_INT16: return "Bebop_MapEq_I16";
    case BEBOP_TYPE_UINT16: return "Bebop_MapEq_U16";
    case BEBOP_TYPE_INT32: return "Bebop_MapEq_I32";
    case BEBOP_TYPE_UINT32: return "Bebop_MapEq_U32";
    case BEBOP_TYPE_INT64: return "Bebop_MapEq_I64";
    case BEBOP_TYPE_UINT64: return "Bebop_MapEq_U64";
    case BEBOP_TYPE_INT128: return "Bebop_MapEq_I128";
    case BEBOP_TYPE_UINT128: return "Bebop_MapEq_U128";
    case BEBOP_TYPE_STRING: return "Bebop_MapEq_Str";
    case BEBOP_TYPE_UUID: return "Bebop_MapEq_UUID";
    default: return NULL;
  }
}

static void emit_array_type(gen_ctx_t* ctx, const bebop_descriptor_type_t* elem)
{
  bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
  // Unified array types - always use _Array (ArrayView is now an alias)
  const char* suffix = "_Array";

  const char* prim_names[] = {
      [BEBOP_TYPE_INT8] = "Bebop_I8",
      [BEBOP_TYPE_BYTE] = "Bebop_U8",
      [BEBOP_TYPE_INT16] = "Bebop_I16",
      [BEBOP_TYPE_UINT16] = "Bebop_U16",
      [BEBOP_TYPE_INT32] = "Bebop_I32",
      [BEBOP_TYPE_UINT32] = "Bebop_U32",
      [BEBOP_TYPE_INT64] = "Bebop_I64",
      [BEBOP_TYPE_UINT64] = "Bebop_U64",
      [BEBOP_TYPE_INT128] = "Bebop_I128",
      [BEBOP_TYPE_UINT128] = "Bebop_U128",
      [BEBOP_TYPE_FLOAT16] = "Bebop_F16",
      [BEBOP_TYPE_FLOAT32] = "Bebop_F32",
      [BEBOP_TYPE_FLOAT64] = "Bebop_F64",
      [BEBOP_TYPE_BFLOAT16] = "Bebop_BF16",
      [BEBOP_TYPE_BOOL] = "Bebop_Bool",
      [BEBOP_TYPE_UUID] = "Bebop_UUID",
      [BEBOP_TYPE_TIMESTAMP] = "Bebop_Timestamp",
      [BEBOP_TYPE_DURATION] = "Bebop_Duration",
  };

  if (ek < sizeof(prim_names) / sizeof(prim_names[0]) && prim_names[ek]) {
    sb_printf(&ctx->out, "%s%s", prim_names[ek], suffix);
    return;
  }
  if (ek == BEBOP_TYPE_STRING) {
    sb_puts(&ctx->out, "Bebop_Str_Array");
    return;
  }
  if (ek == BEBOP_TYPE_FIXED_ARRAY || ek == BEBOP_TYPE_MAP) {
    sb_puts(&ctx->out, "Bebop_");
    gen_sb_t sn;
    sb_init(&sn);
    short_name(&sn, ctx, elem);
    bool cap_next = true;
    for (const char* p = sn.data; *p; p++) {
      if (*p == '_') {
        cap_next = true;
      } else if (cap_next) {
        sb_putc(&ctx->out, (char)toupper((unsigned char)*p));
        cap_next = false;
      } else {
        sb_putc(&ctx->out, *p);
      }
    }
    sb_free(&sn);
    sb_puts(&ctx->out, suffix);
    return;
  }
  emit_ctype(ctx, elem);
  sb_puts(&ctx->out, suffix);
}

static void emit_map_type(gen_ctx_t* ctx,
                          const bebop_descriptor_type_t* key,
                          const bebop_descriptor_type_t* val)
{
  (void)key;
  (void)val;
  sb_puts(&ctx->out, "Bebop_Map");
}

static void emit_ctype(gen_ctx_t* ctx, const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  switch (kind) {
    case BEBOP_TYPE_ARRAY:
      emit_array_type(ctx, bebop_descriptor_type_element(type));
      break;
    case BEBOP_TYPE_FIXED_ARRAY:
      emit_ctype(ctx, bebop_descriptor_type_element(type));
      break;
    case BEBOP_TYPE_MAP:
      emit_map_type(ctx,
                    bebop_descriptor_type_key(type),
                    bebop_descriptor_type_value(type));
      break;
    case BEBOP_TYPE_DEFINED:
      name_from_fqn(
          &ctx->out, ctx->opts.prefix, bebop_descriptor_type_fqn(type));
      break;
    default: {
      const type_info_t* t = type_info(kind);
      if (t) {
        sb_puts(&ctx->out, t->ctype);
      }
      break;
    }
  }
}

static void get_ctype_str(gen_ctx_t* ctx,
                          const bebop_descriptor_type_t* type,
                          char* buf,
                          size_t buf_size)
{
  size_t old_len = ctx->out.len;
  emit_ctype(ctx, type);
  size_t type_len = ctx->out.len - old_len;
  if (type_len >= buf_size) {
    type_len = buf_size - 1;
  }
  memcpy(buf, ctx->out.data + old_len, type_len);
  buf[type_len] = '\0';
  ctx->out.len = old_len;
  if (ctx->out.data) {
    ctx->out.data[old_len] = '\0';
  }
}

static void emit_map_forward(gen_ctx_t* ctx,
                             const bebop_descriptor_type_t* type)
{
  (void)ctx;
  (void)type;
}

static void emit_deferred_map_entries(gen_ctx_t* ctx)
{
  (void)ctx;
}

static void emit_array_typedef(gen_ctx_t* ctx,
                               const bebop_descriptor_type_t* type)
{
  const bebop_descriptor_type_t* elem = bebop_descriptor_type_element(type);
  bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);

  if (is_primitive(ek) || ek == BEBOP_TYPE_STRING) {
    return;
  }

  bool use_alloc = ctx->is_mutable;

  gen_sb_t elem_name;
  sb_init(&elem_name);
  short_name(&elem_name, ctx, elem);

  char dedup_key[GEN_PATH_SIZE];
  snprintf(dedup_key,
           sizeof(dedup_key),
           "%s_%s",
           elem_name.data,
           use_alloc ? "alloc" : "view");

  if (!type_set_add(&ctx->array_types, dedup_key)) {
    sb_free(&elem_name);
    return;
  }

  char guard[GEN_PATH_SIZE];
  snprintf(guard,
           sizeof(guard),
           "BEBOP_%s_ARRAY_%s_DEFINED_",
           elem_name.data,
           use_alloc ? "ALLOC" : "VIEW");
  for (char* p = guard; *p; p++) {
    if (*p >= 'a' && *p <= 'z') {
      *p -= 32;
    }
  }

  emit(ctx, "#ifndef %s", guard);
  emit(ctx, "#define %s", guard);

  gen_sb_t elem_ctype;
  sb_init(&elem_ctype);
  size_t old_len = ctx->out.len;
  emit_ctype(ctx, elem);
  size_t type_len = ctx->out.len - old_len;
  sb_grow(&elem_ctype, type_len + 1);
  memcpy(elem_ctype.data, ctx->out.data + old_len, type_len);
  elem_ctype.data[type_len] = '\0';
  elem_ctype.len = type_len;
  ctx->out.len = old_len;
  ctx->out.data[old_len] = '\0';

  gen_sb_t typedef_name;
  sb_init(&typedef_name);
  array_typedef_name(&typedef_name, ctx, elem);

  if (ek == BEBOP_TYPE_FIXED_ARRAY) {
    uint32_t fixed_size = bebop_descriptor_type_fixed_size(elem);
    emit(ctx, "typedef struct { %s (*data)[%u]; size_t length; size_t capacity; } %s_Array;",
         elem_ctype.data, fixed_size, typedef_name.data);
  } else {
    emit(ctx, "typedef struct { %s* data; size_t length; size_t capacity; } %s_Array;",
         elem_ctype.data, typedef_name.data);
  }
  emit(ctx, "#endif");

  sb_free(&elem_name);
  sb_free(&elem_ctype);
  sb_free(&typedef_name);
}

static void emit_container_types(gen_ctx_t* ctx,
                                 const bebop_descriptor_type_t* root_type)
{
  typedef struct {
    const bebop_descriptor_type_t* type;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {root_type, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];
    bebop_type_kind_t kind = bebop_descriptor_type_kind(f->type);

    if (!f->children_pushed) {
      f->children_pushed = true;
      if (kind == BEBOP_TYPE_ARRAY || kind == BEBOP_TYPE_FIXED_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(f->type);
        if (top < GEN_STACK_DEPTH) {
          stack[top++] = (frame_t) {elem, false};
        }
      } else if (kind == BEBOP_TYPE_MAP) {
        if (top < GEN_STACK_DEPTH - 1) {
          stack[top++] = (frame_t) {bebop_descriptor_type_key(f->type), false};
          stack[top++] =
              (frame_t) {bebop_descriptor_type_value(f->type), false};
        }
      }
      continue;
    }

    top--;
    if (kind == BEBOP_TYPE_ARRAY) {
      emit_array_typedef(ctx, f->type);
    } else if (kind == BEBOP_TYPE_MAP) {
      emit_map_forward(ctx, f->type);
    }
  }
}

static void emit_container_types_for_def(gen_ctx_t* ctx,
                                         const bebop_descriptor_def_t* def)
{
  bebop_def_kind_t kind = bebop_descriptor_def_kind(def);
  if (kind != BEBOP_DEF_STRUCT && kind != BEBOP_DEF_MESSAGE) {
    return;
  }

  ctx->is_mutable = bebop_descriptor_def_is_mutable(def);
  uint32_t fc = bebop_descriptor_def_field_count(def);
  for (uint32_t i = 0; i < fc; i++) {
    emit_container_types(
        ctx,
        bebop_descriptor_field_type(bebop_descriptor_def_field_at(def, i)));
  }
}

static bool field_is_deprecated(const bebop_descriptor_field_t* f)
{
  uint32_t n = bebop_descriptor_field_decorator_count(f);
  for (uint32_t i = 0; i < n; i++) {
    const bebop_descriptor_usage_t* u =
        bebop_descriptor_field_decorator_at(f, i);
    const char* fqn = bebop_descriptor_usage_fqn(u);
    if (fqn && strcmp(fqn, "deprecated") == 0) {
      return true;
    }
  }
  return false;
}

static const char* deprecated_msg(const bebop_descriptor_field_t* f)
{
  uint32_t n = bebop_descriptor_field_decorator_count(f);
  for (uint32_t i = 0; i < n; i++) {
    const bebop_descriptor_usage_t* u =
        bebop_descriptor_field_decorator_at(f, i);
    const char* fqn = bebop_descriptor_usage_fqn(u);
    if (fqn && strcmp(fqn, "deprecated") == 0) {
      if (bebop_descriptor_usage_arg_count(u) > 0) {
        const bebop_descriptor_literal_t* arg =
            bebop_descriptor_usage_arg_value(u, 0);
        if (arg && bebop_descriptor_literal_kind(arg) == BEBOP_LITERAL_STRING) {
          return bebop_descriptor_literal_as_string(arg);
        }
      }
      break;
    }
  }
  return NULL;
}

static const char* branch_name(const bebop_descriptor_branch_t* b)
{
  const char* bname = bebop_descriptor_branch_name(b);
  if (bname) {
    return bname;
  }
  const char* inline_fqn = bebop_descriptor_branch_inline_fqn(b);
  if (inline_fqn) {
    const char* dot = strrchr(inline_fqn, '.');
    return dot ? dot + 1 : inline_fqn;
  }
  return NULL;
}

static const char* branch_lower(const bebop_descriptor_branch_t* b)
{
  static char buf[256];
  const char* name = branch_name(b);
  if (!name) {
    return NULL;
  }
  char* p = buf;
  for (const char* s = name; *s && p < buf + 254; s++) {
    *p++ = (char)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s);
  }
  *p = '\0';
  if (is_keyword(buf)) {
    *p++ = '_';
    *p = '\0';
  }
  return buf;
}

static bool import_generates_code(gen_ctx_t* ctx, const char* import_path)
{
  if (!ctx->req || !import_path) {
    return false;
  }
  uint32_t schema_count = bebop_plugin_request_schema_count(ctx->req);
  for (uint32_t i = 0; i < schema_count; i++) {
    const bebop_descriptor_schema_t* s =
        bebop_plugin_request_schema_at(ctx->req, i);
    const char* spath = bebop_descriptor_schema_path(s);
    if (!spath) {
      continue;
    }
    size_t spath_len = strlen(spath);
    size_t import_len = strlen(import_path);
    if (spath_len >= import_len) {
      const char* suffix = spath + spath_len - import_len;
      if (strcmp(suffix, import_path) == 0) {
        uint32_t def_count = bebop_descriptor_schema_def_count(s);
        for (uint32_t j = 0; j < def_count; j++) {
          bebop_def_kind_t kind =
              bebop_descriptor_def_kind(bebop_descriptor_schema_def_at(s, j));
          if (kind == BEBOP_DEF_STRUCT || kind == BEBOP_DEF_MESSAGE
              || kind == BEBOP_DEF_UNION || kind == BEBOP_DEF_ENUM
              || kind == BEBOP_DEF_CONST)
          {
            return true;
          }
        }
        return false;
      }
    }
  }
  return true;
}

static void emit_generated_notice(gen_ctx_t* ctx,
                                  const bebop_descriptor_schema_t* schema)
{
  bebop_version_t ver = bebop_plugin_request_compiler_version(ctx->req);
  const char* source_path = bebop_descriptor_schema_path(schema);
  bebop_edition_t edition = bebop_descriptor_schema_edition(schema);

  sb_puts(&ctx->out, "// Code generated by bebopc-gen-c. DO NOT EDIT.\n");
  sb_printf(
      &ctx->out, "// source: %s\n", source_path ? source_path : "unknown");
  if (ver.suffix && ver.suffix[0]) {
    sb_printf(&ctx->out,
              "// bebopc %d.%d.%d-%s\n",
              ver.major,
              ver.minor,
              ver.patch,
              ver.suffix);
  } else {
    sb_printf(
        &ctx->out, "// bebopc %d.%d.%d\n", ver.major, ver.minor, ver.patch);
  }
  switch (edition) {
    case BEBOP_ED_2026:
      sb_puts(&ctx->out, "// edition 2026\n");
      break;
    default:
      ctx->error = "unsupported schema edition";
      return;
  }
  sb_puts(&ctx->out, "//\n");
  sb_puts(&ctx->out, "// This file requires the Bebop runtime library.\n");
  sb_puts(&ctx->out, "// https://github.com/6over3/bebop\n");
  sb_puts(&ctx->out, "//\n");
  sb_puts(&ctx->out,
          "// The Bebop compiler and runtime are licensed under the Apache "
          "License,\n");
  sb_puts(&ctx->out,
          "// Version 2.0. You may obtain a copy of the License at:\n");
  sb_puts(&ctx->out, "// https://www.apache.org/licenses/LICENSE-2.0\n");
  sb_puts(&ctx->out, "//\n");
  sb_puts(&ctx->out, "// SPDX-License-Identifier: Apache-2.0\n");
  sb_puts(&ctx->out, "// Copyright (c) 6OVER3 INSTITUTE\n\n");
}

static void emit_file_header(gen_ctx_t* ctx,
                             const bebop_descriptor_schema_t* schema)
{
  const char* path = bebop_descriptor_schema_path(schema);
  const char* package = bebop_descriptor_schema_package(schema);
  gen_sb_t guard;
  sb_init(&guard);
  include_guard(&guard, package, path);

  emit_generated_notice(ctx, schema);
  sb_printf(&ctx->out, "#ifndef %s\n", guard.data);
  sb_printf(&ctx->out, "#define %s\n\n", guard.data);
  sb_free(&guard);

  sb_puts(&ctx->out, "#include <bebop_wire.h>\n");

  uint32_t n = bebop_descriptor_schema_import_count(schema);
  const char* ext =
      (ctx->opts.output_mode == GEN_OUT_UNITY) ? ".bb.c" : ".bb.h";
  for (uint32_t i = 0; i < n; i++) {
    const char* imp = bebop_descriptor_schema_import_at(schema, i);
    if (!imp || !import_generates_code(ctx, imp)) {
      continue;
    }

    sb_puts(&ctx->out, "#include \"");
    for (const char* p = imp; *p; p++) {
      if (*p == '.' && strcmp(p, ".bop") == 0) {
        break;
      }
      sb_putc(&ctx->out, *p);
    }
    sb_puts(&ctx->out, ext);
    sb_puts(&ctx->out, "\"\n");
  }

  sb_puts(&ctx->out, "// @@bebop_insertion_point(includes)\n\n");
  sb_puts(&ctx->out, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
  sb_puts(&ctx->out, "// @@bebop_insertion_point(forward_declarations)\n\n");
}

static void emit_file_footer(gen_ctx_t* ctx)
{
  sb_puts(&ctx->out, "// @@bebop_insertion_point(declarations)\n\n");
  sb_puts(&ctx->out, "// @@bebop_insertion_point(definitions)\n\n");
  sb_puts(&ctx->out, "#ifdef __cplusplus\n}\n#endif\n\n");
  sb_puts(&ctx->out, "#endif\n");
  sb_puts(&ctx->out, "// @@bebop_insertion_point(eof)\n");
}

static void emit_single_header_impl_start(
    gen_ctx_t* ctx, const bebop_descriptor_schema_t* schema)
{
  const char* path = bebop_descriptor_schema_path(schema);
  const char* package = bebop_descriptor_schema_package(schema);
  gen_sb_t guard;
  sb_init(&guard);
  include_guard(&guard, package, path);
  if (guard.len > 2 && guard.data[guard.len - 1] == '_'
      && guard.data[guard.len - 2] == 'H')
  {
    guard.data[guard.len - 2] = '\0';
    guard.len -= 2;
  }
  sb_printf(&ctx->out, "#ifdef %s_IMPLEMENTATION\n\n", guard.data);
  sb_free(&guard);
}

static void emit_single_header_impl_end(gen_ctx_t* ctx)
{
  sb_puts(&ctx->out, "#endif /* IMPLEMENTATION */\n");
}

static void emit_source_header(gen_ctx_t* ctx,
                               const bebop_descriptor_schema_t* schema)
{
  const char* path = bebop_descriptor_schema_path(schema);
  emit_generated_notice(ctx, schema);
  sb_puts(&ctx->out, "#include \"");
  const char* filename = path;
  for (const char* p = path; *p; p++) {
    if (*p == '/' || *p == '\\') {
      filename = p + 1;
    }
  }
  for (const char* p = filename; *p; p++) {
    if (*p == '.' && strcmp(p, ".bop") == 0) {
      break;
    }
    sb_putc(&ctx->out, *p);
  }
  sb_puts(&ctx->out, ".bb.h\"\n\n");
}

static void gen_forward_decls(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
    if (kind == BEBOP_DEF_STRUCT || kind == BEBOP_DEF_MESSAGE ||
        kind == BEBOP_DEF_UNION) {
      const char* name = type_name(ctx, bebop_descriptor_def_fqn(d));
      emit(ctx, "typedef struct %s %s;", name, name);
    }
  }
}

static void gen_const(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);
  const char* doc = bebop_descriptor_def_documentation(def);
  const bebop_descriptor_literal_t* lit = bebop_descriptor_def_const_value(def);
  if (!lit) {
    return;
  }

  bebop_literal_kind_t lk = bebop_descriptor_literal_kind(lit);

  // Header: emit extern declarations
  if (ctx->emit_mode == GEN_EMIT_DECL) {
    emit_doc(ctx, doc);
    if (lk == BEBOP_LITERAL_INT) {
      emit(ctx, "extern const int64_t %s;", name);
    } else if (lk == BEBOP_LITERAL_FLOAT) {
      emit(ctx, "extern const double %s;", name);
    } else if (lk == BEBOP_LITERAL_BOOL) {
      emit(ctx, "extern const bool %s;", name);
    } else if (lk == BEBOP_LITERAL_STRING) {
      emit(ctx, "extern const char %s[];", name);
    } else if (lk == BEBOP_LITERAL_UUID) {
      emit(ctx, "extern const Bebop_UUID %s;", name);
    } else if (lk == BEBOP_LITERAL_BYTES) {
      emit(ctx, "extern const Bebop_Bytes %s;", name);
    } else if (lk == BEBOP_LITERAL_TIMESTAMP) {
      emit(ctx, "extern const Bebop_Timestamp %s;", name);
    } else if (lk == BEBOP_LITERAL_DURATION) {
      emit(ctx, "extern const Bebop_Duration %s;", name);
    }
    emit_nl(ctx);
    return;
  }

  // Impl: emit definitions
  if (lk == BEBOP_LITERAL_INT) {
    emit(ctx,
         "const int64_t %s = %lldLL;",
         name,
         (long long)bebop_descriptor_literal_as_int(lit));
  } else if (lk == BEBOP_LITERAL_FLOAT) {
    emit(ctx, "const double %s = %g;", name, bebop_descriptor_literal_as_float(lit));
  } else if (lk == BEBOP_LITERAL_BOOL) {
    emit(ctx,
         "const bool %s = %s;",
         name,
         bebop_descriptor_literal_as_bool(lit) ? "true" : "false");
  } else if (lk == BEBOP_LITERAL_STRING) {
    gen_sb_t escaped;
    sb_init(&escaped);
    sb_escape_string(&escaped, bebop_descriptor_literal_as_string(lit));
    emit(ctx,
         "const char %s[] = \"%s\";",
         name,
         escaped.data ? escaped.data : "");
    sb_free(&escaped);
  } else if (lk == BEBOP_LITERAL_UUID) {
    const uint8_t* b = bebop_descriptor_literal_as_uuid(lit);
    if (b) {
      emit(ctx,
                "const Bebop_UUID %s = {{0x%02x,0x%02x,0x%02x,0x%02x,"
                "0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,"
                "0x%02x,0x%02x,0x%02x,0x%02x}};",
                name, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9],
                b[10], b[11], b[12], b[13], b[14], b[15]);
    }
  } else if (lk == BEBOP_LITERAL_BYTES) {
    size_t len;
    const uint8_t* data = bebop_descriptor_literal_as_bytes(lit, &len);
    if (len == 0) {
      emit(ctx, "const Bebop_Bytes %s = {NULL, 0};", name);
    } else {
      emit(ctx, "static const uint8_t %s_data_[] = {", name);
      ctx->indent++;
      for (size_t i = 0; i < len; i++) {
        if (i % 12 == 0) {
          if (i > 0) sb_printf(&ctx->out, ",\n");
          sb_indent(&ctx->out, ctx->indent);
        } else {
          sb_printf(&ctx->out, ", ");
        }
        sb_printf(&ctx->out, "0x%02x", data[i]);
      }
      ctx->indent--;
      sb_printf(&ctx->out, "\n");
      emit(ctx, "};");
      emit(ctx,
           "const Bebop_Bytes %s = {%s_data_, sizeof(%s_data_)};",
           name, name, name);
    }
  } else if (lk == BEBOP_LITERAL_TIMESTAMP) {
    int64_t seconds;
    int32_t nanos, offset_ms;
    bebop_descriptor_literal_as_timestamp(lit, &seconds, &nanos, &offset_ms);
    emit(ctx,
         "const Bebop_Timestamp %s = {.seconds = %lldLL, .nanos = %d, .offset_ms = %d};",
         name, (long long)seconds, nanos, offset_ms);
  } else if (lk == BEBOP_LITERAL_DURATION) {
    int64_t seconds;
    int32_t nanos;
    bebop_descriptor_literal_as_duration(lit, &seconds, &nanos);
    emit(ctx,
         "const Bebop_Duration %s = {.seconds = %lldLL, .nanos = %d};",
         name, (long long)seconds, nanos);
  }
  emit_nl(ctx);
}

static void gen_enum(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* name = type_name(ctx, bebop_descriptor_def_fqn(def));
  const char* doc = bebop_descriptor_def_documentation(def);
  bebop_type_kind_t base = bebop_descriptor_def_base_type(def);
  bool is_flags = bebop_descriptor_def_is_flags(def);
  uint32_t member_count = bebop_descriptor_def_member_count(def);

  emit_doc(ctx, doc);
  emit(ctx, "typedef enum {");

  bool is_signed = (base == BEBOP_TYPE_INT8 || base == BEBOP_TYPE_INT16
                    || base == BEBOP_TYPE_INT32 || base == BEBOP_TYPE_INT64);

  ctx->indent++;
  for (uint32_t i = 0; i < member_count; i++) {
    const bebop_descriptor_member_t* m = bebop_descriptor_def_member_at(def, i);
    const char* mname = bebop_descriptor_member_name(m);
    const char* mdoc = bebop_descriptor_member_documentation(m);
    uint64_t mval = bebop_descriptor_member_value(m);

    if (mdoc && mdoc[0]) {
      emit(ctx, "/** %s */", mdoc);
    }
    sb_indent(&ctx->out, ctx->indent);
    sb_enum_member(&ctx->out, name, mname);
    if (is_flags) {
      sb_printf(&ctx->out, " = 0x%llxu,\n", (unsigned long long)mval);
    } else if (is_signed) {
      sb_printf(&ctx->out, " = %lld,\n", (long long)(int64_t)mval);
    } else {
      sb_printf(&ctx->out, " = %llu,\n", (unsigned long long)mval);
    }
  }
  ctx->indent--;

  emit(ctx, "} %s;", name);
  emit_nl(ctx);
}

static void emit_fixed_array_dims(gen_ctx_t* ctx,
                                  const bebop_descriptor_type_t* type)
{
  while (bebop_descriptor_type_kind(type) == BEBOP_TYPE_FIXED_ARRAY) {
    sb_printf(&ctx->out, "[%u]", bebop_descriptor_type_fixed_size(type));
    type = bebop_descriptor_type_element(type);
  }
}

static void emit_field_decl(gen_ctx_t* ctx,
                            const bebop_descriptor_field_t* f,
                            bool is_message,
                            bool is_mut,
                            const char* struct_name)
{
  BEBOP_WIRE_UNUSED(struct_name);
  const char* fname = bebop_descriptor_field_name(f);
  const char* fdoc = bebop_descriptor_field_documentation(f);
  const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
  bebop_type_kind_t fkind = bebop_descriptor_type_kind(ftype);

  if (fdoc && fdoc[0]) {
    emit(ctx, "/** %s */", fdoc);
  }

  sb_indent(&ctx->out, ctx->indent);

  if (field_is_deprecated(f)) {
    const char* msg = deprecated_msg(f);
    if (msg) {
      sb_printf(&ctx->out, "BEBOP_WIRE_DEPRECATED_MSG(\"%s\") ", msg);
    } else {
      sb_puts(&ctx->out, "BEBOP_WIRE_DEPRECATED ");
    }
  }

  if (is_message) {
    if (fkind == BEBOP_TYPE_FIXED_ARRAY) {
      sb_puts(&ctx->out, "BEBOP_WIRE_OPT_FA(");
      emit_ctype(ctx, ftype);
      const bebop_descriptor_type_t* t = ftype;
      while (bebop_descriptor_type_kind(t) == BEBOP_TYPE_FIXED_ARRAY) {
        sb_printf(&ctx->out, ", [%u]", bebop_descriptor_type_fixed_size(t));
        t = bebop_descriptor_type_element(t);
      }
      sb_puts(&ctx->out, ") ");
    } else if (type_is_message_or_union(ctx, ftype)) {
      sb_puts(&ctx->out, "BEBOP_WIRE_OPT(");
      emit_ctype(ctx, ftype);
      sb_puts(&ctx->out, " *) ");
    } else {
      sb_puts(&ctx->out, "BEBOP_WIRE_OPT(");
      emit_ctype(ctx, ftype);
      sb_puts(&ctx->out, ") ");
    }
  } else {
    if (!is_mut) {
      sb_puts(&ctx->out, "const ");
    }
    emit_ctype(ctx, ftype);
    sb_putc(&ctx->out, ' ');
  }

  sb_puts(&ctx->out, safe_field_name(fname));

  if (!is_message && fkind == BEBOP_TYPE_FIXED_ARRAY) {
    emit_fixed_array_dims(ctx, ftype);
  }
  sb_puts(&ctx->out, ";\n");
}

typedef struct {
  uint32_t index;
  uint32_t align;
  uint32_t size;
} field_sort_entry_t;

static int field_sort_cmp(const void* a, const void* b)
{
  const field_sort_entry_t* fa = (const field_sort_entry_t*)a;
  const field_sort_entry_t* fb = (const field_sort_entry_t*)b;
  if (fa->align != fb->align) return (fb->align > fa->align) ? 1 : -1;
  if (fa->size != fb->size) return (fb->size > fa->size) ? 1 : -1;
  return (int)fa->index - (int)fb->index;
}

static void gen_struct(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* name = type_name(ctx, bebop_descriptor_def_fqn(def));
  const char* doc = bebop_descriptor_def_documentation(def);
  uint32_t field_count = bebop_descriptor_def_field_count(def);
  bool is_mut = bebop_descriptor_def_is_mutable(def);

  emit_doc(ctx, doc);
  emit(ctx, "struct %s {", name);
  ctx->indent++;

  if (field_count == 0) {
    emit(ctx, "BEBOP_WIRE_EMPTY_STRUCT;");
  } else {
    field_sort_entry_t* sorted = malloc(field_count * sizeof(field_sort_entry_t));
    for (uint32_t i = 0; i < field_count; i++) {
      const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
      const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
      sorted[i].index = i;
      sorted[i].align = type_alignment(ftype);
      sorted[i].size = type_c_size(ftype);
    }
    qsort(sorted, field_count, sizeof(field_sort_entry_t), field_sort_cmp);
    for (uint32_t i = 0; i < field_count; i++) {
      emit_field_decl(
          ctx, bebop_descriptor_def_field_at(def, sorted[i].index), false, is_mut, name);
    }
    free(sorted);
  }
  emit(ctx, "// @@bebop_insertion_point(struct_scope:%s)", name);
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_message(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  char name[GEN_PATH_SIZE];
  snprintf(
      name, sizeof(name), "%s", type_name(ctx, bebop_descriptor_def_fqn(def)));
  const char* doc = bebop_descriptor_def_documentation(def);
  uint32_t field_count = bebop_descriptor_def_field_count(def);

  if (field_count > 0) {
    emit(ctx, "typedef enum {");
    ctx->indent++;
    for (uint32_t i = 0; i < field_count; i++) {
      const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
      const char* fname = bebop_descriptor_field_name(f);
      uint32_t idx = bebop_descriptor_field_index(f);
      sb_indent(&ctx->out, ctx->indent);
      sb_enum_member(&ctx->out, name, fname);
      sb_printf(&ctx->out, "_TAG = %u,\n", idx);
    }
    ctx->indent--;
    emit(ctx, "} %s_Tag;", name);
    emit_nl(ctx);
  }

  emit_doc(ctx, doc);
  emit(ctx, "struct %s {", name);
  ctx->indent++;

  if (field_count == 0) {
    emit(ctx, "BEBOP_WIRE_EMPTY_STRUCT;");
  } else {
    for (uint32_t i = 0; i < field_count; i++) {
      emit_field_decl(
          ctx, bebop_descriptor_def_field_at(def, i), true, true, name);
    }
  }
  emit(ctx, "// @@bebop_insertion_point(struct_scope:%s)", name);
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_union(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  char name[GEN_PATH_SIZE];
  snprintf(
      name, sizeof(name), "%s", type_name(ctx, bebop_descriptor_def_fqn(def)));
  const char* doc = bebop_descriptor_def_documentation(def);
  uint32_t branch_count = bebop_descriptor_def_branch_count(def);

  emit_doc(ctx, doc);
  emit(ctx, "typedef enum {");
  ctx->indent++;
  sb_indent(&ctx->out, ctx->indent);
  sb_enum_member(&ctx->out, name, "None");
  sb_puts(&ctx->out, " = 0,\n");

  for (uint32_t i = 0; i < branch_count; i++) {
    const bebop_descriptor_branch_t* b = bebop_descriptor_def_branch_at(def, i);
    const char* bname = branch_name(b);
    if (bname) {
      sb_indent(&ctx->out, ctx->indent);
      sb_enum_member(&ctx->out, name, bname);
      sb_printf(&ctx->out,
                " = %u,\n",
                (unsigned)bebop_descriptor_branch_discriminator(b));
    }
  }
  ctx->indent--;
  emit(ctx, "} %s_Disc;", name);
  emit_nl(ctx);

  emit(ctx, "struct %s {", name);
  ctx->indent++;
  emit(ctx, "%s_Disc discriminator;", name);
  emit(ctx, "union {");
  ctx->indent++;

  for (uint32_t i = 0; i < branch_count; i++) {
    const bebop_descriptor_branch_t* b = bebop_descriptor_def_branch_at(def, i);
    const char* bname = branch_name(b);
    if (!bname) {
      continue;
    }

    const char* type_fqn = bebop_descriptor_branch_type_ref_fqn(b);
    if (!type_fqn) {
      type_fqn = bebop_descriptor_branch_inline_fqn(b);
    }

    sb_indent(&ctx->out, ctx->indent);
    if (type_fqn) {
      name_from_fqn(&ctx->out, ctx->opts.prefix, type_fqn);
    }
    sb_putc(&ctx->out, ' ');
    sb_puts(&ctx->out, branch_lower(b));
    sb_puts(&ctx->out, ";\n");
  }
  ctx->indent--;
  emit(ctx, "};");
  emit(ctx, "// @@bebop_insertion_point(struct_scope:%s)", name);
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_typedef(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
    switch (kind) {
      case BEBOP_DEF_CONST:
        gen_const(ctx, d);
        break;
      case BEBOP_DEF_ENUM:
        break;
      case BEBOP_DEF_STRUCT:
        gen_struct(ctx, d);
        break;
      case BEBOP_DEF_MESSAGE:
        gen_message(ctx, d);
        break;
      case BEBOP_DEF_UNION:
        gen_union(ctx, d);
        break;
      default:
        break;
    }
  }
}

static void gen_enums_only(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
    if (kind == BEBOP_DEF_ENUM) {
      gen_enum(ctx, d);
    }
  }
}

static const char* refl_type_kind(bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
      return "BEBOP_REFLECTION_TYPE_BOOL";
    case BEBOP_TYPE_BYTE:
      return "BEBOP_REFLECTION_TYPE_BYTE";
    case BEBOP_TYPE_INT8:
      return "BEBOP_REFLECTION_TYPE_INT8";
    case BEBOP_TYPE_INT16:
      return "BEBOP_REFLECTION_TYPE_INT16";
    case BEBOP_TYPE_UINT16:
      return "BEBOP_REFLECTION_TYPE_UINT16";
    case BEBOP_TYPE_INT32:
      return "BEBOP_REFLECTION_TYPE_INT32";
    case BEBOP_TYPE_UINT32:
      return "BEBOP_REFLECTION_TYPE_UINT32";
    case BEBOP_TYPE_INT64:
      return "BEBOP_REFLECTION_TYPE_INT64";
    case BEBOP_TYPE_UINT64:
      return "BEBOP_REFLECTION_TYPE_UINT64";
    case BEBOP_TYPE_INT128:
      return "BEBOP_REFLECTION_TYPE_INT128";
    case BEBOP_TYPE_UINT128:
      return "BEBOP_REFLECTION_TYPE_UINT128";
    case BEBOP_TYPE_FLOAT16:
      return "BEBOP_REFLECTION_TYPE_FLOAT16";
    case BEBOP_TYPE_FLOAT32:
      return "BEBOP_REFLECTION_TYPE_FLOAT32";
    case BEBOP_TYPE_FLOAT64:
      return "BEBOP_REFLECTION_TYPE_FLOAT64";
    case BEBOP_TYPE_BFLOAT16:
      return "BEBOP_REFLECTION_TYPE_BFLOAT16";
    case BEBOP_TYPE_STRING:
      return "BEBOP_REFLECTION_TYPE_STRING";
    case BEBOP_TYPE_UUID:
      return "BEBOP_REFLECTION_TYPE_UUID";
    case BEBOP_TYPE_TIMESTAMP:
      return "BEBOP_REFLECTION_TYPE_TIMESTAMP";
    case BEBOP_TYPE_DURATION:
      return "BEBOP_REFLECTION_TYPE_DURATION";
    case BEBOP_TYPE_ARRAY:
      return "BEBOP_REFLECTION_TYPE_ARRAY";
    case BEBOP_TYPE_FIXED_ARRAY:
      return "BEBOP_REFLECTION_TYPE_FIXED_ARRAY";
    case BEBOP_TYPE_MAP:
      return "BEBOP_REFLECTION_TYPE_MAP";
    case BEBOP_TYPE_DEFINED:
      return "BEBOP_REFLECTION_TYPE_DEFINED";
    default:
      return "0";
  }
}

static const char* refl_scalar_type_ref(bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
      return "&BebopReflection_Type_Bool";
    case BEBOP_TYPE_BYTE:
      return "&BebopReflection_Type_Byte";
    case BEBOP_TYPE_INT8:
      return "&BebopReflection_Type_Int8";
    case BEBOP_TYPE_INT16:
      return "&BebopReflection_Type_Int16";
    case BEBOP_TYPE_UINT16:
      return "&BebopReflection_Type_UInt16";
    case BEBOP_TYPE_INT32:
      return "&BebopReflection_Type_Int32";
    case BEBOP_TYPE_UINT32:
      return "&BebopReflection_Type_UInt32";
    case BEBOP_TYPE_INT64:
      return "&BebopReflection_Type_Int64";
    case BEBOP_TYPE_UINT64:
      return "&BebopReflection_Type_UInt64";
    case BEBOP_TYPE_INT128:
      return "&BebopReflection_Type_Int128";
    case BEBOP_TYPE_UINT128:
      return "&BebopReflection_Type_UInt128";
    case BEBOP_TYPE_FLOAT16:
      return "&BebopReflection_Type_Float16";
    case BEBOP_TYPE_FLOAT32:
      return "&BebopReflection_Type_Float32";
    case BEBOP_TYPE_FLOAT64:
      return "&BebopReflection_Type_Float64";
    case BEBOP_TYPE_BFLOAT16:
      return "&BebopReflection_Type_BFloat16";
    case BEBOP_TYPE_STRING:
      return "&BebopReflection_Type_String";
    case BEBOP_TYPE_UUID:
      return "&BebopReflection_Type_UUID";
    case BEBOP_TYPE_TIMESTAMP:
      return "&BebopReflection_Type_Timestamp";
    case BEBOP_TYPE_DURATION:
      return "&BebopReflection_Type_Duration";
    default:
      return "NULL";
  }
}

static void refl_type_name(gen_sb_t* sb, gen_ctx_t* ctx,
                           const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  switch (kind) {
    case BEBOP_TYPE_ARRAY:
    case BEBOP_TYPE_FIXED_ARRAY: {
      sb_puts(sb, "arr_");
      if (kind == BEBOP_TYPE_FIXED_ARRAY) {
        sb_printf(sb, "%u_", bebop_descriptor_type_fixed_size(type));
      }
      refl_type_name(sb, ctx, bebop_descriptor_type_element(type));
      break;
    }
    case BEBOP_TYPE_MAP: {
      sb_puts(sb, "map_");
      refl_type_name(sb, ctx, bebop_descriptor_type_key(type));
      sb_puts(sb, "_");
      refl_type_name(sb, ctx, bebop_descriptor_type_value(type));
      break;
    }
    case BEBOP_TYPE_DEFINED: {
      const char* fqn = bebop_descriptor_type_fqn(type);
      for (const char* p = fqn; *p; p++) {
        if (*p == '.') {
          sb_putc(sb, '_');
        } else {
          sb_putc(sb, *p);
        }
      }
      break;
    }
    default: {
      const type_info_t* ti = type_info(kind);
      if (ti) {
        for (const char* p = ti->ctype; *p; p++) {
          char c = *p;
          if (c == ' ' || c == '*') continue;
          sb_putc(sb, c);
        }
      }
      break;
    }
  }
}

static void gen_refl_type_descriptor(gen_ctx_t* ctx,
                                     const bebop_descriptor_type_t* type,
                                     const char* prefix,
                                     gen_type_set_t* emitted);

static void gen_refl_type_descriptors_for_type(gen_ctx_t* ctx,
                                               const bebop_descriptor_type_t* type,
                                               const char* prefix,
                                               gen_type_set_t* emitted)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  if (kind == BEBOP_TYPE_ARRAY || kind == BEBOP_TYPE_FIXED_ARRAY) {
    gen_refl_type_descriptors_for_type(ctx,
                                       bebop_descriptor_type_element(type),
                                       prefix, emitted);
    gen_refl_type_descriptor(ctx, type, prefix, emitted);
  } else if (kind == BEBOP_TYPE_MAP) {
    gen_refl_type_descriptors_for_type(ctx, bebop_descriptor_type_key(type),
                                       prefix, emitted);
    gen_refl_type_descriptors_for_type(ctx, bebop_descriptor_type_value(type),
                                       prefix, emitted);
    gen_refl_type_descriptor(ctx, type, prefix, emitted);
  } else if (kind == BEBOP_TYPE_DEFINED) {
    gen_refl_type_descriptor(ctx, type, prefix, emitted);
  }
}

static void gen_refl_type_descriptor(gen_ctx_t* ctx,
                                     const bebop_descriptor_type_t* type,
                                     const char* prefix,
                                     gen_type_set_t* emitted)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);

  gen_sb_t name;
  sb_init(&name);
  sb_puts(&name, prefix);
  sb_puts(&name, "__type_");
  refl_type_name(&name, ctx, type);

  if (!type_set_add(emitted, name.data)) {
    sb_free(&name);
    return;
  }

  if (kind == BEBOP_TYPE_ARRAY) {
    gen_sb_t elem_name;
    sb_init(&elem_name);
    const bebop_descriptor_type_t* elem = bebop_descriptor_type_element(type);
    bebop_type_kind_t elem_kind = bebop_descriptor_type_kind(elem);
    if (elem_kind <= BEBOP_TYPE_DURATION) {
      sb_puts(&elem_name, refl_scalar_type_ref(elem_kind));
    } else {
      sb_puts(&elem_name, "&");
      sb_puts(&elem_name, prefix);
      sb_puts(&elem_name, "__type_");
      refl_type_name(&elem_name, ctx, elem);
    }

    emit(ctx,
         "static const BebopReflection_TypeDescriptor %s = "
         "{BEBOP_REFLECTION_TYPE_ARRAY, %s, NULL, NULL, 0, NULL};",
         name.data, elem_name.data);
    sb_free(&elem_name);
  } else if (kind == BEBOP_TYPE_FIXED_ARRAY) {
    gen_sb_t elem_name;
    sb_init(&elem_name);
    const bebop_descriptor_type_t* elem = bebop_descriptor_type_element(type);
    bebop_type_kind_t elem_kind = bebop_descriptor_type_kind(elem);
    if (elem_kind <= BEBOP_TYPE_DURATION) {
      sb_puts(&elem_name, refl_scalar_type_ref(elem_kind));
    } else {
      sb_puts(&elem_name, "&");
      sb_puts(&elem_name, prefix);
      sb_puts(&elem_name, "__type_");
      refl_type_name(&elem_name, ctx, elem);
    }

    emit(ctx,
         "static const BebopReflection_TypeDescriptor %s = "
         "{BEBOP_REFLECTION_TYPE_FIXED_ARRAY, %s, NULL, NULL, %u, NULL};",
         name.data, elem_name.data, bebop_descriptor_type_fixed_size(type));
    sb_free(&elem_name);
  } else if (kind == BEBOP_TYPE_MAP) {
    gen_sb_t key_name, val_name;
    sb_init(&key_name);
    sb_init(&val_name);

    const bebop_descriptor_type_t* key_type = bebop_descriptor_type_key(type);
    const bebop_descriptor_type_t* val_type = bebop_descriptor_type_value(type);
    bebop_type_kind_t key_kind = bebop_descriptor_type_kind(key_type);
    bebop_type_kind_t val_kind = bebop_descriptor_type_kind(val_type);

    if (key_kind <= BEBOP_TYPE_DURATION) {
      sb_puts(&key_name, refl_scalar_type_ref(key_kind));
    } else {
      sb_puts(&key_name, "&");
      sb_puts(&key_name, prefix);
      sb_puts(&key_name, "__type_");
      refl_type_name(&key_name, ctx, key_type);
    }

    if (val_kind <= BEBOP_TYPE_DURATION) {
      sb_puts(&val_name, refl_scalar_type_ref(val_kind));
    } else {
      sb_puts(&val_name, "&");
      sb_puts(&val_name, prefix);
      sb_puts(&val_name, "__type_");
      refl_type_name(&val_name, ctx, val_type);
    }

    emit(ctx,
         "static const BebopReflection_TypeDescriptor %s = "
         "{BEBOP_REFLECTION_TYPE_MAP, NULL, %s, %s, 0, NULL};",
         name.data, key_name.data, val_name.data);
    sb_free(&key_name);
    sb_free(&val_name);
  } else if (kind == BEBOP_TYPE_DEFINED) {
    emit(ctx,
         "static const BebopReflection_TypeDescriptor %s = "
         "{BEBOP_REFLECTION_TYPE_DEFINED, NULL, NULL, NULL, 0, \"%s\"};",
         name.data, bebop_descriptor_type_fqn(type));
  }

  sb_free(&name);
}

static void gen_refl_field_type_ref(gen_sb_t* sb, gen_ctx_t* ctx,
                                    const bebop_descriptor_type_t* type,
                                    const char* prefix)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  if (kind <= BEBOP_TYPE_DURATION) {
    sb_puts(sb, refl_scalar_type_ref(kind));
  } else {
    sb_puts(sb, "&");
    sb_puts(sb, prefix);
    sb_puts(sb, "__type_");
    refl_type_name(sb, ctx, type);
  }
}

static void gen_refl_enum(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);
  const char* short_name = bebop_descriptor_def_name(def);
  const char* pkg = bebop_descriptor_schema_package(ctx->schema);
  bebop_type_kind_t base = bebop_descriptor_def_base_type(def);
  bool is_flags = bebop_descriptor_def_is_flags(def);
  uint32_t member_count = bebop_descriptor_def_member_count(def);

  if (member_count > 0) {
    emit(ctx, "static const BebopReflection_EnumMemberDescriptor %s__refl_members[%u] = {",
         name, member_count);
    ctx->indent++;
    for (uint32_t i = 0; i < member_count; i++) {
      const bebop_descriptor_member_t* m = bebop_descriptor_def_member_at(def, i);
      const char* mname = bebop_descriptor_member_name(m);
      int64_t mval = (int64_t)bebop_descriptor_member_value(m);
      emit(ctx, "{\"%s\", %lldLL},", mname, (long long)mval);
    }
    ctx->indent--;
    emit(ctx, "};");
  }

  emit(ctx, "const BebopReflection_DefinitionDescriptor %s__refl_descriptor = {", name);
  ctx->indent++;
  emit(ctx, ".magic = BEBOP_REFLECTION_MAGIC,");
  emit(ctx, ".kind = BEBOP_REFLECTION_DEF_ENUM,");
  emit(ctx, ".name = \"%s\",", short_name);
  emit(ctx, ".fqn = \"%s\",", fqn);
  emit(ctx, ".package = \"%s\",", pkg ? pkg : "");
  emit(ctx, ".enum_def = {");
  ctx->indent++;
  emit(ctx, ".base_type = %s,", refl_type_kind(base));
  emit(ctx, ".n_members = %u,", member_count);
  if (member_count > 0) {
    emit(ctx, ".members = %s__refl_members,", name);
  } else {
    emit(ctx, ".members = NULL,");
  }
  emit(ctx, ".is_flags = %s,", is_flags ? "true" : "false");
  ctx->indent--;
  emit(ctx, "},");
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_refl_struct_or_message(gen_ctx_t* ctx,
                                       const bebop_descriptor_def_t* def,
                                       bool is_message)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);
  const char* short_name = bebop_descriptor_def_name(def);
  const char* pkg = bebop_descriptor_schema_package(ctx->schema);
  uint32_t field_count = bebop_descriptor_def_field_count(def);
  bool is_mut = bebop_descriptor_def_is_mutable(def);
  uint32_t fixed_size = is_message ? 0 : bebop_descriptor_def_fixed_size(def);

  gen_type_set_t emitted;
  type_set_init(&emitted);
  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
    const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
    gen_refl_type_descriptors_for_type(ctx, ftype, name, &emitted);
  }

  if (field_count > 0) {
    emit(ctx, "static const BebopReflection_FieldDescriptor %s__refl_fields[%u] = {",
         name, field_count);
    ctx->indent++;
    for (uint32_t i = 0; i < field_count; i++) {
      const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
      const char* fname = bebop_descriptor_field_name(f);
      const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
      uint32_t index = bebop_descriptor_field_index(f);

      gen_sb_t type_ref;
      sb_init(&type_ref);
      gen_refl_field_type_ref(&type_ref, ctx, ftype, name);

      emit(ctx, "{\"%s\", %s, %u, offsetof(%s, %s)},", fname, type_ref.data, index, name, safe_field_name(fname));
      sb_free(&type_ref);
    }
    ctx->indent--;
    emit(ctx, "};");
  }

  emit(ctx, "const BebopReflection_DefinitionDescriptor %s__refl_descriptor = {", name);
  ctx->indent++;
  emit(ctx, ".magic = BEBOP_REFLECTION_MAGIC,");
  emit(ctx, ".kind = %s,",
       is_message ? "BEBOP_REFLECTION_DEF_MESSAGE" : "BEBOP_REFLECTION_DEF_STRUCT");
  emit(ctx, ".name = \"%s\",", short_name);
  emit(ctx, ".fqn = \"%s\",", fqn);
  emit(ctx, ".package = \"%s\",", pkg ? pkg : "");
  if (is_message) {
    emit(ctx, ".message_def = {");
    ctx->indent++;
    emit(ctx, ".n_fields = %u,", field_count);
    if (field_count > 0) {
      emit(ctx, ".fields = %s__refl_fields,", name);
    } else {
      emit(ctx, ".fields = NULL,");
    }
    emit(ctx, ".sizeof_type = sizeof(%s),", name);
    ctx->indent--;
    emit(ctx, "},");
  } else {
    emit(ctx, ".struct_def = {");
    ctx->indent++;
    emit(ctx, ".n_fields = %u,", field_count);
    if (field_count > 0) {
      emit(ctx, ".fields = %s__refl_fields,", name);
    } else {
      emit(ctx, ".fields = NULL,");
    }
    emit(ctx, ".sizeof_type = sizeof(%s),", name);
    emit(ctx, ".fixed_size = %u,", fixed_size);
    emit(ctx, ".is_mutable = %s,", is_mut ? "true" : "false");
    ctx->indent--;
    emit(ctx, "},");
  }
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_refl_union(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);
  const char* short_name = bebop_descriptor_def_name(def);
  const char* pkg = bebop_descriptor_schema_package(ctx->schema);
  uint32_t branch_count = bebop_descriptor_def_branch_count(def);

  if (branch_count > 0) {
    emit(ctx, "static const BebopReflection_UnionBranchDescriptor %s__refl_branches[%u] = {",
         name, branch_count);
    ctx->indent++;
    for (uint32_t i = 0; i < branch_count; i++) {
      const bebop_descriptor_branch_t* br = bebop_descriptor_def_branch_at(def, i);
      uint8_t disc = bebop_descriptor_branch_discriminator(br);
      const char* bname = bebop_descriptor_branch_name(br);
      const char* inline_fqn = bebop_descriptor_branch_inline_fqn(br);
      const char* ref_fqn = bebop_descriptor_branch_type_ref_fqn(br);
      const char* type_fqn = inline_fqn ? inline_fqn : ref_fqn;

      emit(ctx, "{%u, \"%s\", \"%s\", offsetof(%s, %s)},", disc, bname ? bname : "", type_fqn ? type_fqn : "", name, branch_lower(br));
    }
    ctx->indent--;
    emit(ctx, "};");
  }

  emit(ctx, "const BebopReflection_DefinitionDescriptor %s__refl_descriptor = {", name);
  ctx->indent++;
  emit(ctx, ".magic = BEBOP_REFLECTION_MAGIC,");
  emit(ctx, ".kind = BEBOP_REFLECTION_DEF_UNION,");
  emit(ctx, ".name = \"%s\",", short_name);
  emit(ctx, ".fqn = \"%s\",", fqn);
  emit(ctx, ".package = \"%s\",", pkg ? pkg : "");
  emit(ctx, ".union_def = {");
  ctx->indent++;
  emit(ctx, ".n_branches = %u,", branch_count);
  if (branch_count > 0) {
    emit(ctx, ".branches = %s__refl_branches,", name);
  } else {
    emit(ctx, ".branches = NULL,");
  }
  emit(ctx, ".sizeof_type = sizeof(%s),", name);
  ctx->indent--;
  emit(ctx, "},");
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_refl_service(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);
  const char* short_name = bebop_descriptor_def_name(def);
  const char* pkg = bebop_descriptor_schema_package(ctx->schema);
  uint32_t method_count = bebop_descriptor_def_method_count(def);

  gen_type_set_t emitted;
  type_set_init(&emitted);
  for (uint32_t i = 0; i < method_count; i++) {
    const bebop_descriptor_method_t* m = bebop_descriptor_def_method_at(def, i);
    const bebop_descriptor_type_t* req = bebop_descriptor_method_request(m);
    const bebop_descriptor_type_t* resp = bebop_descriptor_method_response(m);
    if (req) gen_refl_type_descriptors_for_type(ctx, req, name, &emitted);
    if (resp) gen_refl_type_descriptors_for_type(ctx, resp, name, &emitted);
  }

  if (method_count > 0) {
    emit(ctx, "static const BebopReflection_MethodDescriptor %s__refl_methods[%u] = {",
         name, method_count);
    ctx->indent++;
    for (uint32_t i = 0; i < method_count; i++) {
      const bebop_descriptor_method_t* m = bebop_descriptor_def_method_at(def, i);
      const char* mname = bebop_descriptor_method_name(m);
      uint32_t mid = bebop_descriptor_method_id(m);
      bebop_method_type_t mtype = bebop_descriptor_method_type(m);
      const bebop_descriptor_type_t* req = bebop_descriptor_method_request(m);
      const bebop_descriptor_type_t* resp = bebop_descriptor_method_response(m);

      const char* mtype_str;
      switch (mtype) {
        case BEBOP_METHOD_SERVER_STREAM:
          mtype_str = "BEBOP_REFLECTION_METHOD_SERVER_STREAM";
          break;
        case BEBOP_METHOD_CLIENT_STREAM:
          mtype_str = "BEBOP_REFLECTION_METHOD_CLIENT_STREAM";
          break;
        case BEBOP_METHOD_DUPLEX_STREAM:
          mtype_str = "BEBOP_REFLECTION_METHOD_DUPLEX_STREAM";
          break;
        default:
          mtype_str = "BEBOP_REFLECTION_METHOD_UNARY";
          break;
      }

      gen_sb_t req_ref, resp_ref;
      sb_init(&req_ref);
      sb_init(&resp_ref);
      if (req) {
        gen_refl_field_type_ref(&req_ref, ctx, req, name);
      } else {
        sb_puts(&req_ref, "NULL");
      }
      if (resp) {
        gen_refl_field_type_ref(&resp_ref, ctx, resp, name);
      } else {
        sb_puts(&resp_ref, "NULL");
      }

      emit(ctx, "{\"%s\", %s, %s, %u, %s},",
           mname, req_ref.data, resp_ref.data, mid, mtype_str);
      sb_free(&req_ref);
      sb_free(&resp_ref);
    }
    ctx->indent--;
    emit(ctx, "};");
  }

  emit(ctx, "const BebopReflection_DefinitionDescriptor %s__refl_descriptor = {", name);
  ctx->indent++;
  emit(ctx, ".magic = BEBOP_REFLECTION_MAGIC,");
  emit(ctx, ".kind = BEBOP_REFLECTION_DEF_SERVICE,");
  emit(ctx, ".name = \"%s\",", short_name);
  emit(ctx, ".fqn = \"%s\",", fqn);
  emit(ctx, ".package = \"%s\",", pkg ? pkg : "");
  emit(ctx, ".service_def = {");
  ctx->indent++;
  emit(ctx, ".n_methods = %u,", method_count);
  if (method_count > 0) {
    emit(ctx, ".methods = %s__refl_methods,", name);
  } else {
    emit(ctx, ".methods = NULL,");
  }
  ctx->indent--;
  emit(ctx, "},");
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_reflection(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
    switch (kind) {
      case BEBOP_DEF_ENUM:
        gen_refl_enum(ctx, d);
        break;
      case BEBOP_DEF_STRUCT:
        gen_refl_struct_or_message(ctx, d, false);
        break;
      case BEBOP_DEF_MESSAGE:
        gen_refl_struct_or_message(ctx, d, true);
        break;
      case BEBOP_DEF_UNION:
        gen_refl_union(ctx, d);
        break;
      case BEBOP_DEF_SERVICE:
        gen_refl_service(ctx, d);
        break;
      default:
        break;
    }
  }
}

static void gen_reflection_decl(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
    if (kind == BEBOP_DEF_CONST || kind == BEBOP_DEF_DECORATOR) {
      continue;
    }
    const char* name = type_name(ctx, bebop_descriptor_def_fqn(d));
    emit(ctx, "extern const BebopReflection_DefinitionDescriptor %s__refl_descriptor;", name);
    if (kind == BEBOP_DEF_STRUCT || kind == BEBOP_DEF_MESSAGE || kind == BEBOP_DEF_UNION) {
      emit(ctx, "extern const Bebop_TypeInfo %s__type_info;", name);
    }
  }
}

static void gen_type_info(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  if (ctx->emit_mode == GEN_EMIT_DECL) {
    return;
  }

  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);

  emit(ctx, "const Bebop_TypeInfo %s__type_info = {", name);
  ctx->indent++;
  emit(ctx, ".type_fqn = \"%s\",", fqn);
  emit(ctx, ".prefix = NULL,");
  emit(ctx, ".size_fn = (Bebop_SizeFn)%s_EncodedSize,", name);
  emit(ctx, ".encode_fn = (Bebop_EncodeFn)%s_Encode,", name);
  emit(ctx, ".decode_fn = (Bebop_DecodeFn)%s_Decode,", name);
  ctx->indent--;
  emit(ctx, "};");
  emit_nl(ctx);
}

static void gen_container_typedef(gen_ctx_t* ctx,
                                  const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    emit_container_types_for_def(ctx, d);
  }
}

typedef struct {
  const bebop_descriptor_type_t* type;
  char access[GEN_PATH_SIZE];
  int state;
  int loop_var;
  bool is_ptr;
} size_work_t;

static void gen_size_type_ex(gen_ctx_t* ctx,
                             const bebop_descriptor_type_t* type,
                             const char* access,
                             bool is_ptr)
{
  size_work_t stack[GEN_STACK_DEPTH];
  int top = 0;

  stack[top++] = (size_work_t) {type, "", 0, ctx->loop_depth, is_ptr};
  snprintf(stack[0].access, GEN_PATH_SIZE, "%s", access);

  while (top > 0) {
    size_work_t* w = &stack[top - 1];
    bebop_type_kind_t kind = bebop_descriptor_type_kind(w->type);

    if (w->state == 0) {
      const type_info_t* ti = type_info(kind);
      if (ti && ti->size_macro) {
        emit(ctx, "size += %s;", ti->size_macro);
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_STRING) {
        emit(ctx,
             "size += BEBOP_WIRE_SIZE_LEN + %s.length + BEBOP_WIRE_SIZE_NUL;",
             w->access);
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_DEFINED) {
        if (type_is_enum(ctx, w->type)) {
          bebop_type_kind_t base = enum_base_type(ctx, w->type);
          const type_info_t* bti = type_info(base);
          emit(ctx, "size += %s;", bti ? bti->size_macro : "4");
        } else if (w->is_ptr) {
          emit(ctx,
               "size += %s_EncodedSize(%s);",
               type_name(ctx, bebop_descriptor_type_fqn(w->type)),
               w->access);
        } else {
          emit(ctx,
               "size += %s_EncodedSize(&%s);",
               type_name(ctx, bebop_descriptor_type_fqn(w->type)),
               w->access);
        }
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(w->type);
        bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
        emit(ctx, "size += BEBOP_WIRE_SIZE_LEN;");
        const type_info_t* eti = type_info(ek);
        if (eti && eti->size_macro) {
          emit(ctx, "size += %s.length * %s;", w->access, eti->size_macro);
          top--;
          continue;
        }
        if (ek == BEBOP_TYPE_STRING) {
          emit(ctx,
               "for (size_t _i%d = 0; _i%d < %s.length; _i%d++) {",
               w->loop_var,
               w->loop_var,
               w->access,
               w->loop_var);
          ctx->indent++;
          emit(ctx,
                        "size += BEBOP_WIRE_SIZE_LEN + %s.data[_i%d].length + "
                        "BEBOP_WIRE_SIZE_NUL;",
                        w->access, w->loop_var);
          ctx->indent--;
          emit(ctx, "}");
          top--;
          continue;
        }
        emit(ctx,
             "for (size_t _i%d = 0; _i%d < %s.length; _i%d++) {",
             w->loop_var,
             w->loop_var,
             w->access,
             w->loop_var);
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH) {
          size_work_t child = {elem, "", 0, w->loop_var + 1, false};
          snprintf(child.access,
                   GEN_PATH_SIZE,
                   "%s.data[_i%d]",
                   w->access,
                   w->loop_var);
          stack[top++] = child;
        }
        continue;
      }
      if (kind == BEBOP_TYPE_FIXED_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(w->type);
        bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
        uint32_t count = bebop_descriptor_type_fixed_size(w->type);
        const type_info_t* eti = type_info(ek);
        if (eti && eti->size_macro) {
          emit(ctx, "size += %u * %s;", count, eti->size_macro);
          top--;
          continue;
        }
        emit(ctx,
             "for (size_t _i%d = 0; _i%d < BEBOP_ARRAY_COUNT(%s); _i%d++) {",
             w->loop_var,
             w->loop_var,
             w->access,
             w->loop_var);
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH) {
          size_work_t child = {elem, "", 0, w->loop_var + 1, false};
          snprintf(
              child.access, GEN_PATH_SIZE, "%s[_i%d]", w->access, w->loop_var);
          stack[top++] = child;
        }
        continue;
      }
      if (kind == BEBOP_TYPE_MAP) {
        char key_type[256], val_type[256];
        get_ctype_str(ctx, bebop_descriptor_type_key(w->type), key_type, sizeof(key_type));
        get_ctype_str(ctx, bebop_descriptor_type_value(w->type), val_type, sizeof(val_type));

        emit(ctx, "size += BEBOP_WIRE_SIZE_LEN;");
        emit(ctx, "{");
        ctx->indent++;
        emit(ctx, "Bebop_MapIter _mit%d;", w->loop_var);
        emit(ctx, "Bebop_MapIter_Init(&_mit%d, &%s);", w->loop_var, w->access);
        emit(ctx, "void *_mk%d, *_mv%d;", w->loop_var, w->loop_var);
        emit(ctx,
             "while (Bebop_MapIter_Next(&_mit%d, &_mk%d, &_mv%d)) {",
             w->loop_var, w->loop_var, w->loop_var);
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH - 1) {
          const bebop_descriptor_type_t* vtype = bebop_descriptor_type_value(w->type);
          bebop_type_kind_t vkind = bebop_descriptor_type_kind(vtype);
          bool val_is_ptr = (vkind == BEBOP_TYPE_FIXED_ARRAY);
          size_work_t val_work = {vtype, "", 0, w->loop_var + 1, val_is_ptr};
          if (val_is_ptr) {
            snprintf(val_work.access, GEN_PATH_SIZE,
                     "((%s*)_mv%d)",
                     val_type, w->loop_var);
          } else {
            snprintf(val_work.access, GEN_PATH_SIZE,
                     "(*(%s*)_mv%d)",
                     val_type, w->loop_var);
          }
          size_work_t key_work = {bebop_descriptor_type_key(w->type),
                                  "", 0, w->loop_var + 1, false};
          snprintf(key_work.access, GEN_PATH_SIZE,
                   "(*(%s*)_mk%d)",
                   key_type, w->loop_var);
          stack[top++] = val_work;
          stack[top++] = key_work;
        }
        continue;
      }
      top--;
    } else {
      bebop_type_kind_t kind2 = bebop_descriptor_type_kind(w->type);
      ctx->indent--;
      emit(ctx, "}");
      if (kind2 == BEBOP_TYPE_MAP) {
        ctx->indent--;
        emit(ctx, "}");
      }
      top--;
    }
  }
}

static void gen_size_type(gen_ctx_t* ctx,
                          const bebop_descriptor_type_t* type,
                          const char* access)
{
  gen_size_type_ex(ctx, type, access, false);
}

static void gen_size_struct(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* name = type_name(ctx, bebop_descriptor_def_fqn(def));
  uint32_t field_count = bebop_descriptor_def_field_count(def);
  uint32_t fixed_size = bebop_descriptor_def_fixed_size(def);

  char macro_name[GEN_PATH_SIZE];
  screaming_name(macro_name, sizeof(macro_name), name);

  if (ctx->emit_mode == GEN_EMIT_DECL) {
    emit(ctx, "#define %s_MIN_SIZE %u", macro_name, fixed_size > 0 ? fixed_size : 0);
    emit(ctx, "#define %s_FIXED_SIZE %u", macro_name, fixed_size);
    emit_nl(ctx);
  }

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_EncodedSize", name);
  snprintf(sig, sizeof(sig), "const %s *v", name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_PURE", "size_t", fn_name, sig)) {
    return;
  }

  ctx->indent++;

  if (fixed_size > 0) {
    emit(ctx, "BEBOP_WIRE_UNUSED(v);");
    emit(ctx, "return %s_FIXED_SIZE;", macro_name);
  } else if (field_count == 0) {
    emit(ctx, "BEBOP_WIRE_UNUSED(v);");
    emit(ctx, "return 0;");
  } else {
    emit(ctx, "size_t size = 0;");
    ctx->loop_depth = 0;
    for (uint32_t i = 0; i < field_count; i++) {
      const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
      const char* fname = bebop_descriptor_field_name(f);
      char access[GEN_PATH_SIZE];
      snprintf(access, sizeof(access), "v->%s", safe_field_name(fname));
      gen_size_type(ctx, bebop_descriptor_field_type(f), access);
    }
    emit(ctx, "return size;");
  }

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_size_message(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  const char* name = type_name(ctx, fqn);
  uint32_t field_count = bebop_descriptor_def_field_count(def);

  char macro_name[GEN_PATH_SIZE];
  screaming_name(macro_name, sizeof(macro_name), name);

  if (ctx->emit_mode == GEN_EMIT_DECL) {
    emit(ctx,
         "#define %s_MIN_SIZE (BEBOP_WIRE_SIZE_LEN + BEBOP_WIRE_SIZE_BYTE)",
         macro_name);
    emit(ctx, "#define %s_FIXED_SIZE 0", macro_name);
    emit_nl(ctx);
  }

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_EncodedSize", name);
  snprintf(sig, sizeof(sig), "const %s *v", name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_PURE", "size_t", fn_name, sig)) {
    return;
  }

  ctx->indent++;

  bool has_fields = false;
  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
    if (!field_is_deprecated(f)) {
      has_fields = true;
      break;
    }
  }

  if (!has_fields) {
    emit(ctx, "BEBOP_WIRE_UNUSED(v);");
  }

  emit(ctx, "size_t size = %s_MIN_SIZE;", macro_name);

  ctx->loop_depth = 0;
  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
    if (field_is_deprecated(f)) {
      continue;
    }
    const char* fname = bebop_descriptor_field_name(f);
    const char* safe = safe_field_name(fname);

    emit(ctx, "if (BEBOP_WIRE_IS_SOME(v->%s)) {", safe);
    ctx->indent++;
    emit(ctx, "size += BEBOP_WIRE_SIZE_BYTE;");
    char access[GEN_PATH_SIZE];
    snprintf(access, sizeof(access), "BEBOP_WIRE_UNWRAP(v->%s)", safe);
    const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
    bool is_ptr = type_is_message_or_union(ctx, ftype);
    gen_size_type_ex(ctx, ftype, access, is_ptr);
    ctx->indent--;
    emit(ctx, "}");
  }

  emit(ctx, "return size;");
  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_size_union(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  char name[GEN_PATH_SIZE];
  snprintf(name, sizeof(name), "%s", type_name(ctx, fqn));
  uint32_t branch_count = bebop_descriptor_def_branch_count(def);

  char macro_name[GEN_PATH_SIZE];
  screaming_name(macro_name, sizeof(macro_name), name);

  if (ctx->emit_mode == GEN_EMIT_DECL) {
    emit(ctx,
         "#define %s_MIN_SIZE (BEBOP_WIRE_SIZE_LEN + BEBOP_WIRE_SIZE_BYTE)",
         macro_name);
    emit(ctx, "#define %s_FIXED_SIZE 0", macro_name);
    emit_nl(ctx);
  }

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_EncodedSize", name);
  snprintf(sig, sizeof(sig), "const %s *v", name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_PURE", "size_t", fn_name, sig)) {
    return;
  }

  ctx->indent++;
  emit(ctx, "size_t size = %s_MIN_SIZE;", macro_name);

  emit(ctx, "switch (v->discriminator) {");
  for (uint32_t i = 0; i < branch_count; i++) {
    const bebop_descriptor_branch_t* b = bebop_descriptor_def_branch_at(def, i);
    const char* bname = branch_name(b);
    if (!bname) {
      continue;
    }
    const char* type_fqn = bebop_descriptor_branch_type_ref_fqn(b);
    if (!type_fqn) {
      type_fqn = bebop_descriptor_branch_inline_fqn(b);
    }

    sb_indent(&ctx->out, ctx->indent);
    sb_union_case(&ctx->out, name, bname);
    sb_putc(&ctx->out, '\n');
    ctx->indent++;
    emit(ctx,
         "size += %s_EncodedSize(&v->%s);",
         type_name(ctx, type_fqn),
         branch_lower(b));
    emit(ctx, "break;");
    ctx->indent--;
  }
  emit(ctx, "default: break;");
  emit(ctx, "}");

  emit(ctx, "return size;");
  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_encode_type_ex(gen_ctx_t* ctx,
                               const bebop_descriptor_type_t* type,
                               const char* access,
                               bool is_ptr)
{
  size_work_t stack[GEN_STACK_DEPTH];
  int top = 0;
  const char* ret = ctx->in_step_fn ? "return -(int)r" : "return r";

  stack[top++] = (size_work_t) {type, "", 0, ctx->loop_depth, is_ptr};
  snprintf(stack[0].access, GEN_PATH_SIZE, "%s", access);

  while (top > 0) {
    size_work_t* w = &stack[top - 1];
    bebop_type_kind_t kind = bebop_descriptor_type_kind(w->type);

    if (w->state == 0) {
      const type_info_t* ti = type_info(kind);
      if ((ti && ti->size > 0) || kind == BEBOP_TYPE_STRING) {
        emit(ctx,
             "if (BEBOP_WIRE_UNLIKELY((r = %s(w, %s)) != BEBOP_WIRE_OK)) %s;",
             ti->wire_set,
             w->access,
             ret);
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_DEFINED) {
        if (type_is_enum(ctx, w->type)) {
          bebop_type_kind_t base = enum_base_type(ctx, w->type);
          const type_info_t* bti = type_info(base);
          if (bti) {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(w, (%s)%s)) != BEBOP_WIRE_OK)) %s;",
                 bti->wire_set,
                 bti->ctype,
                 w->access,
                 ret);
          }
        } else if (w->is_ptr) {
          emit(ctx,
               "if (BEBOP_WIRE_UNLIKELY((r = %s_Encode(w, %s)) != BEBOP_WIRE_OK)) %s;",
               type_name(ctx, bebop_descriptor_type_fqn(w->type)),
               w->access,
               ret);
        } else {
          emit(ctx,
               "if (BEBOP_WIRE_UNLIKELY((r = %s_Encode(w, &%s)) != BEBOP_WIRE_OK)) %s;",
               type_name(ctx, bebop_descriptor_type_fqn(w->type)),
               w->access,
               ret);
        }
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(w->type);
        bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
        const char* bulk = bulk_set(ek);
        if (bulk) {
          emit(ctx,
               "if (BEBOP_WIRE_UNLIKELY((r = %s(w, %s.data, %s.length)) != BEBOP_WIRE_OK)) %s;",
               bulk,
               w->access,
               w->access,
               ret);
          top--;
          continue;
        }
        emit(ctx,
             "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetU32(w, (uint32_t)%s.length)) != "
             "BEBOP_WIRE_OK)) %s;",
             w->access,
             ret);
        emit(ctx,
             "for (size_t _i%d = 0; _i%d < %s.length; _i%d++) {",
             w->loop_var,
             w->loop_var,
             w->access,
             w->loop_var);
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH) {
          size_work_t child = {elem, "", 0, w->loop_var + 1, false};
          snprintf(child.access,
                   GEN_PATH_SIZE,
                   "%s.data[_i%d]",
                   w->access,
                   w->loop_var);
          stack[top++] = child;
        }
        continue;
      }
      if (kind == BEBOP_TYPE_FIXED_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(w->type);
        bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
        uint32_t fa_size = bebop_descriptor_type_fixed_size(w->type);
        const char* bulk = bulk_set_fixed(ek);
        if (bulk) {
          if (w->is_ptr) {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(w, %s, %u)) != BEBOP_WIRE_OK)) %s;",
                 bulk, w->access, fa_size, ret);
          } else {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(w, %s, BEBOP_ARRAY_COUNT(%s))) != BEBOP_WIRE_OK)) "
                 "%s;",
                 bulk, w->access, w->access, ret);
          }
          top--;
          continue;
        }
        if (w->is_ptr) {
          emit(ctx, "for (size_t _i%d = 0; _i%d < %u; _i%d++) {",
               w->loop_var, w->loop_var, fa_size, w->loop_var);
        } else {
          emit(ctx,
               "for (size_t _i%d = 0; _i%d < BEBOP_ARRAY_COUNT(%s); _i%d++) {",
               w->loop_var, w->loop_var, w->access, w->loop_var);
        }
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH) {
          size_work_t child = {elem, "", 0, w->loop_var + 1, false};
          snprintf(
              child.access, GEN_PATH_SIZE, "%s[_i%d]", w->access, w->loop_var);
          stack[top++] = child;
        }
        continue;
      }
      if (kind == BEBOP_TYPE_MAP) {
        char key_type[256], val_type[256];
        get_ctype_str(ctx, bebop_descriptor_type_key(w->type), key_type, sizeof(key_type));
        get_ctype_str(ctx, bebop_descriptor_type_value(w->type), val_type, sizeof(val_type));

        emit(ctx,
             "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetU32(w, (uint32_t)%s.length)) != "
             "BEBOP_WIRE_OK)) %s;",
             w->access, ret);
        emit(ctx, "{");
        ctx->indent++;
        emit(ctx, "Bebop_MapIter _mit%d;", w->loop_var);
        emit(ctx, "Bebop_MapIter_Init(&_mit%d, &%s);", w->loop_var, w->access);
        emit(ctx, "void *_mk%d, *_mv%d;", w->loop_var, w->loop_var);
        emit(ctx,
             "while (Bebop_MapIter_Next(&_mit%d, &_mk%d, &_mv%d)) {",
             w->loop_var, w->loop_var, w->loop_var);
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH - 1) {
          const bebop_descriptor_type_t* vtype = bebop_descriptor_type_value(w->type);
          bebop_type_kind_t vkind = bebop_descriptor_type_kind(vtype);
          bool val_is_ptr = (vkind == BEBOP_TYPE_FIXED_ARRAY);
          size_work_t val_work = {vtype, "", 0, w->loop_var + 1, val_is_ptr};
          if (val_is_ptr) {
            snprintf(val_work.access, GEN_PATH_SIZE,
                     "((%s*)_mv%d)",
                     val_type, w->loop_var);
          } else {
            snprintf(val_work.access, GEN_PATH_SIZE,
                     "(*(%s*)_mv%d)",
                     val_type, w->loop_var);
          }
          size_work_t key_work = {bebop_descriptor_type_key(w->type),
                                  "", 0, w->loop_var + 1, false};
          snprintf(key_work.access, GEN_PATH_SIZE,
                   "(*(%s*)_mk%d)",
                   key_type, w->loop_var);
          stack[top++] = val_work;
          stack[top++] = key_work;
        }
        continue;
      }
      top--;
    } else {
      bebop_type_kind_t kind2 = bebop_descriptor_type_kind(w->type);
      ctx->indent--;
      emit(ctx, "}");
      if (kind2 == BEBOP_TYPE_MAP) {
        ctx->indent--;
        emit(ctx, "}");
      }
      top--;
    }
  }
}

static void gen_encode_type(gen_ctx_t* ctx,
                            const bebop_descriptor_type_t* type,
                            const char* access)
{
  gen_encode_type_ex(ctx, type, access, false);
}

static void gen_encode_struct(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* name = type_name(ctx, bebop_descriptor_def_fqn(def));
  uint32_t field_count = bebop_descriptor_def_field_count(def);

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_Encode", name);
  snprintf(sig, sizeof(sig), "Bebop_Writer *w, const %s *v", name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_HOT", "Bebop_WireResult", fn_name, sig)) {
    return;
  }

  ctx->indent++;

  if (field_count == 0) {
    emit(ctx, "BEBOP_WIRE_UNUSED(w); BEBOP_WIRE_UNUSED(v);");
    emit(ctx, "return BEBOP_WIRE_OK;");
  } else {
    emit(ctx, "// @@bebop_insertion_point(encode_start:%s)", name);
    emit(ctx, "Bebop_WireResult r;");
    ctx->loop_depth = 0;
    for (uint32_t i = 0; i < field_count; i++) {
      const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
      const char* fname = bebop_descriptor_field_name(f);
      char access[GEN_PATH_SIZE];
      snprintf(access, sizeof(access), "v->%s", safe_field_name(fname));
      gen_encode_type(ctx, bebop_descriptor_field_type(f), access);
    }
    emit(ctx, "// @@bebop_insertion_point(encode_end:%s)", name);
    emit(ctx, "return BEBOP_WIRE_OK;");
  }

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_encode_message(gen_ctx_t* ctx,
                               const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  char name[GEN_PATH_SIZE];
  snprintf(name, sizeof(name), "%s", type_name(ctx, fqn));
  uint32_t field_count = bebop_descriptor_def_field_count(def);

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_Encode", name);
  snprintf(sig, sizeof(sig), "Bebop_Writer *w, const %s *v", name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_HOT", "Bebop_WireResult", fn_name, sig)) {
    return;
  }

  ctx->indent++;
  if (field_count == 0) {
    emit(ctx, "BEBOP_WIRE_UNUSED(v);");
  }
  emit(ctx, "// @@bebop_insertion_point(encode_start:%s)", name);
  emit(ctx, "Bebop_WireResult r;");
  emit(ctx, "size_t len_pos;");
  emit(ctx,
       "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetLen(w, &len_pos)) != BEBOP_WIRE_OK)) return r;");
  emit(ctx, "size_t start = Bebop_Writer_Len(w);");
  emit_nl(ctx);

  ctx->loop_depth = 0;
  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
    if (field_is_deprecated(f)) {
      continue;
    }
    const char* fname = bebop_descriptor_field_name(f);
    const char* safe = safe_field_name(fname);

    emit(ctx, "if (BEBOP_WIRE_IS_SOME(v->%s)) {", safe);
    ctx->indent++;
    sb_indent(&ctx->out, ctx->indent);
    sb_puts(&ctx->out, "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetByte(w, ");
    sb_puts_screaming(&ctx->out, name);
    sb_putc(&ctx->out, '_');
    sb_puts_screaming(&ctx->out, fname);
    sb_puts(&ctx->out, "_TAG)) != BEBOP_WIRE_OK)) return r;\n");
    char access[GEN_PATH_SIZE];
    snprintf(access, sizeof(access), "BEBOP_WIRE_UNWRAP(v->%s)", safe);
    const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
    bool is_ptr = type_is_message_or_union(ctx, ftype);
    gen_encode_type_ex(ctx, ftype, access, is_ptr);
    ctx->indent--;
    emit(ctx, "}");
  }

  emit(ctx, "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetByte(w, 0)) != BEBOP_WIRE_OK)) return r;");
  emit_nl(ctx);
  emit(ctx, "// @@bebop_insertion_point(encode_end:%s)", name);
  emit(ctx, "return Bebop_Writer_FillLen(w, len_pos, "
              "(uint32_t)(Bebop_Writer_Len(w) - start));");

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_encode_union(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  char name[GEN_PATH_SIZE];
  snprintf(name, sizeof(name), "%s", type_name(ctx, fqn));
  uint32_t branch_count = bebop_descriptor_def_branch_count(def);

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_Encode", name);
  snprintf(sig, sizeof(sig), "Bebop_Writer *w, const %s *v", name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_HOT", "Bebop_WireResult", fn_name, sig)) {
    return;
  }

  ctx->indent++;
  emit(ctx, "// @@bebop_insertion_point(encode_start:%s)", name);
  emit(ctx, "Bebop_WireResult r;");
  emit(ctx, "size_t len_pos;");
  emit(ctx,
       "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetLen(w, &len_pos)) != BEBOP_WIRE_OK)) return r;");
  emit(ctx, "size_t start = Bebop_Writer_Len(w);");
  emit_nl(ctx);
  emit(ctx, "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Writer_SetByte(w, (uint8_t)v->discriminator)) != "
              "BEBOP_WIRE_OK)) return r;");
  emit_nl(ctx);

  emit(ctx, "switch (v->discriminator) {");
  for (uint32_t i = 0; i < branch_count; i++) {
    const bebop_descriptor_branch_t* b = bebop_descriptor_def_branch_at(def, i);
    const char* bname = branch_name(b);
    if (!bname) {
      continue;
    }
    const char* type_fqn = bebop_descriptor_branch_type_ref_fqn(b);
    if (!type_fqn) {
      type_fqn = bebop_descriptor_branch_inline_fqn(b);
    }

    sb_indent(&ctx->out, ctx->indent);
    sb_union_case(&ctx->out, name, bname);
    sb_putc(&ctx->out, '\n');
    ctx->indent++;
    emit(ctx,
         "if (BEBOP_WIRE_UNLIKELY((r = %s_Encode(w, &v->%s)) != BEBOP_WIRE_OK)) return r;",
         type_name(ctx, type_fqn),
         branch_lower(b));
    emit(ctx, "break;");
    ctx->indent--;
  }
  emit(ctx, "// @@bebop_insertion_point(encode_switch:%s)", name);
  emit(ctx, "default: return BEBOP_WIRE_ERR_INVALID;");
  emit(ctx, "}");
  emit_nl(ctx);
  emit(ctx, "// @@bebop_insertion_point(encode_end:%s)", name);
  emit(ctx, "return Bebop_Writer_FillLen(w, len_pos, "
              "(uint32_t)(Bebop_Writer_Len(w) - start));");

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void emit_decode_ptr(gen_ctx_t* ctx,
                            const type_info_t* ti,
                            const char* access)
{
  const char* ret = ctx->in_step_fn ? "return -(int)r" : "return r";
  if (ctx->is_mutable) {
    emit(ctx,
         "if (BEBOP_WIRE_UNLIKELY((r = %s(rd, &%s)) != BEBOP_WIRE_OK)) %s;",
         ti->wire_get,
         access,
         ret);
  } else {
    emit(ctx,
         "if (BEBOP_WIRE_UNLIKELY((r = %s(rd, BEBOP_WIRE_MUTPTR(%s, &%s))) != BEBOP_WIRE_OK)) %s;",
         ti->wire_get,
         ti->ctype,
         access,
         ret);
  }
}

GEN_PRINTF(4, 5)

static void emit_const_assign(gen_ctx_t* ctx,
                              const char* cast_type,
                              const char* access,
                              const char* fmt,
                              ...)
{
  sb_indent(&ctx->out, ctx->indent);
  if (!ctx->is_mutable) {
    sb_printf(&ctx->out, "*BEBOP_WIRE_MUTPTR(%s, &%s) = ", cast_type, access);
  } else {
    sb_printf(&ctx->out, "%s = ", access);
  }
  va_list args, args2;
  va_start(args, fmt);
  va_copy(args2, args);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed > 0) {
    sb_grow(&ctx->out, ctx->out.len + (size_t)needed + 1);
    vsnprintf(ctx->out.data + ctx->out.len, (size_t)needed + 1, fmt, args2);
    ctx->out.len += (size_t)needed;
  }
  va_end(args2);
  sb_puts(&ctx->out, ";\n");
}

static bool type_needs_ctx(const bebop_descriptor_type_t* type)
{
  bebop_type_kind_t kind = bebop_descriptor_type_kind(type);
  if (kind == BEBOP_TYPE_MAP) {
    return true;
  }
  if (kind == BEBOP_TYPE_ARRAY) {
    const bebop_descriptor_type_t* elem = bebop_descriptor_type_element(type);
    bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
    return !is_primitive(ek) || ek == BEBOP_TYPE_STRING;
  }
  return false;
}

static bool def_needs_ctx(const bebop_descriptor_def_t* def)
{
  bebop_def_kind_t kind = bebop_descriptor_def_kind(def);
  if (kind != BEBOP_DEF_STRUCT && kind != BEBOP_DEF_MESSAGE) {
    return false;
  }
  uint32_t n = bebop_descriptor_def_field_count(def);
  for (uint32_t i = 0; i < n; i++) {
    if (type_needs_ctx(
            bebop_descriptor_field_type(bebop_descriptor_def_field_at(def, i))))
    {
      return true;
    }
  }
  return false;
}

static void gen_decode_type_ex(gen_ctx_t* ctx,
                               const bebop_descriptor_type_t* type,
                               const char* target,
                               bool is_ptr)
{
  size_work_t stack[GEN_STACK_DEPTH];
  int top = 0;
  const char* ret = ctx->in_step_fn ? "return -(int)r" : "return r";
  const char* ret_oom = ctx->in_step_fn ? "return -(int)BEBOP_WIRE_ERR_OOM"
                                        : "return BEBOP_WIRE_ERR_OOM";

  stack[top++] = (size_work_t) {type, "", 0, ctx->loop_depth, is_ptr};
  snprintf(stack[0].access, GEN_PATH_SIZE, "%s", target);

  while (top > 0) {
    size_work_t* w = &stack[top - 1];
    bebop_type_kind_t kind = bebop_descriptor_type_kind(w->type);

    if (w->state == 0) {
      const type_info_t* ti = type_info(kind);
      if ((ti && ti->size > 0) || kind == BEBOP_TYPE_STRING) {
        emit_decode_ptr(ctx, ti, w->access);
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_DEFINED) {
        if (type_is_enum(ctx, w->type)) {
          bebop_type_kind_t base = enum_base_type(ctx, w->type);
          const type_info_t* bti = type_info(base);
          if (bti) {
            const char* enum_type =
                type_name(ctx, bebop_descriptor_type_fqn(w->type));
            emit(ctx, "{");
            ctx->indent++;
            emit(ctx, "%s _tmp;", bti->ctype);
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(rd, &_tmp)) != BEBOP_WIRE_OK)) %s;",
                 bti->wire_get,
                 ret);
            emit_const_assign(ctx, enum_type, w->access, "(%s)_tmp", enum_type);
            ctx->indent--;
            emit(ctx, "}");
          }
        } else {
          const char* def_type =
              type_name(ctx, bebop_descriptor_type_fqn(w->type));
          if (w->is_ptr) {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s_Decode(ctx, rd, %s)) != BEBOP_WIRE_OK)) %s;",
                 def_type,
                 w->access,
                 ret);
          } else if (ctx->is_mutable) {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s_Decode(ctx, rd, &%s)) != BEBOP_WIRE_OK)) %s;",
                 def_type,
                 w->access,
                 ret);
          } else {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s_Decode(ctx, rd, BEBOP_WIRE_MUTPTR(%s, &%s))) != "
                 "BEBOP_WIRE_OK)) %s;",
                 def_type,
                 def_type,
                 w->access,
                 ret);
          }
        }
        top--;
        continue;
      }
      if (kind == BEBOP_TYPE_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(w->type);
        bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);

        char arr_type[256];
        get_ctype_str(ctx, w->type, arr_type, sizeof(arr_type));

        emit(ctx, "{");
        ctx->indent++;
        emit(ctx, "uint32_t _len;");
        emit(ctx,
             "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Reader_GetU32(rd, &_len)) != BEBOP_WIRE_OK)) %s;",
             ret);
        if (ctx->is_mutable) {
          emit(ctx, "%s.length = _len;", w->access);
        } else {
          emit(ctx,
               "BEBOP_WIRE_MUTPTR(%s, &%s)->length = _len;",
               arr_type,
               w->access);
        }

        const type_info_t* eti = type_info(ek);
        if (eti && eti->size > 0 && ek != BEBOP_TYPE_STRING) {
          // Zero-copy: point directly into read buffer, capacity=0 marks as view
          if (ctx->is_mutable) {
            emit(ctx,
                            "%s.data = BEBOP_WIRE_CASTPTR(%s *, Bebop_Reader_Ptr(rd));",
                            w->access, eti->ctype);
            emit(ctx, "%s.capacity = 0;", w->access);
          } else {
            emit(ctx,
                            "BEBOP_WIRE_MUTPTR(%s, &%s)->data = "
                            "BEBOP_WIRE_CASTPTR(%s *, Bebop_Reader_Ptr(rd));",
                            arr_type, w->access, eti->ctype);
            emit(ctx,
                            "BEBOP_WIRE_MUTPTR(%s, &%s)->capacity = 0;",
                            arr_type, w->access);
          }
          emit(ctx, "Bebop_Reader_Skip(rd, _len * %s);", eti->size_macro);
          ctx->indent--;
          emit(ctx, "}");
          top--;
          continue;
        }

        if (ek == BEBOP_TYPE_FIXED_ARRAY) {
          const bebop_descriptor_type_t* inner = elem;
          gen_sb_t dims;
          sb_init(&dims);
          while (bebop_descriptor_type_kind(inner) == BEBOP_TYPE_FIXED_ARRAY) {
            sb_printf(&dims, "[%u]", bebop_descriptor_type_fixed_size(inner));
            inner = bebop_descriptor_type_element(inner);
          }
          char inner_type[256];
          get_ctype_str(ctx, inner, inner_type, sizeof(inner_type));

          emit(
              ctx,
              "%s (*_d%d)%s = Bebop_WireCtx_Alloc(ctx, _len * sizeof(*_d%d));",
              inner_type,
              w->loop_var,
              dims.data ? dims.data : "",
              w->loop_var);
          sb_free(&dims);
          emit(ctx, "if (BEBOP_WIRE_UNLIKELY(!_d%d && _len > 0)) %s;", w->loop_var, ret_oom);
          uint32_t pf_dist = calc_prefetch_dist(elem);
          emit(ctx,
               "for (size_t _i%d = 0; _i%d < _len; _i%d++) {",
               w->loop_var,
               w->loop_var,
               w->loop_var);
          ctx->indent++;
          emit(ctx,
               "if (_i%d + %u < _len) BEBOP_WIRE_PREFETCH_W(&_d%d[_i%d + %u]);",
               w->loop_var,
               pf_dist,
               w->loop_var,
               w->loop_var,
               pf_dist);

          w->state = 1;
          if (top < GEN_STACK_DEPTH) {
            size_work_t child = {elem, "", 0, w->loop_var + 1, false};
            snprintf(child.access,
                     GEN_PATH_SIZE,
                     "_d%d[_i%d]",
                     w->loop_var,
                     w->loop_var);
            stack[top++] = child;
          }
          continue;
        }

        char elem_type[256];
        get_ctype_str(ctx, elem, elem_type, sizeof(elem_type));

        emit(ctx,
             "%s *_d%d = Bebop_WireCtx_Alloc(ctx, _len * sizeof(*_d%d));",
             elem_type,
             w->loop_var,
             w->loop_var);
        emit(ctx, "if (BEBOP_WIRE_UNLIKELY(!_d%d && _len > 0)) %s;", w->loop_var, ret_oom);
        uint32_t pf_dist2 = calc_prefetch_dist(elem);
        emit(ctx,
             "for (size_t _i%d = 0; _i%d < _len; _i%d++) {",
             w->loop_var,
             w->loop_var,
             w->loop_var);
        ctx->indent++;
        emit(ctx,
             "if (_i%d + %u < _len) BEBOP_WIRE_PREFETCH_W(&_d%d[_i%d + %u]);",
             w->loop_var,
             pf_dist2,
             w->loop_var,
             w->loop_var,
             pf_dist2);

        w->state = 1;
        if (top < GEN_STACK_DEPTH) {
          size_work_t child = {elem, "", 0, w->loop_var + 1, false};
          snprintf(child.access,
                   GEN_PATH_SIZE,
                   "_d%d[_i%d]",
                   w->loop_var,
                   w->loop_var);
          stack[top++] = child;
        }
        continue;
      }
      if (kind == BEBOP_TYPE_FIXED_ARRAY) {
        const bebop_descriptor_type_t* elem =
            bebop_descriptor_type_element(w->type);
        bebop_type_kind_t ek = bebop_descriptor_type_kind(elem);
        uint32_t fa_size = bebop_descriptor_type_fixed_size(w->type);
        const char* bulk = bulk_get_fixed(ek);
        if (bulk) {
          const type_info_t* eti = type_info(ek);
          if (w->is_ptr) {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(rd, (%s *)%s, %u)) != BEBOP_WIRE_OK)) %s;",
                 bulk, eti ? eti->ctype : "void", w->access, fa_size, ret);
          } else if (ctx->is_mutable) {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(rd, %s, BEBOP_ARRAY_COUNT(%s))) != "
                 "BEBOP_WIRE_OK)) %s;",
                 bulk, w->access, w->access, ret);
          } else {
            emit(ctx,
                 "if (BEBOP_WIRE_UNLIKELY((r = %s(rd, BEBOP_WIRE_MUTPTR(%s, %s), BEBOP_ARRAY_COUNT(%s))) != "
                 "BEBOP_WIRE_OK)) %s;",
                 bulk, eti ? eti->ctype : "void", w->access, w->access, ret);
          }
          top--;
          continue;
        }
        if (w->is_ptr) {
          emit(ctx, "for (size_t _i%d = 0; _i%d < %u; _i%d++) {",
               w->loop_var, w->loop_var, fa_size, w->loop_var);
        } else {
          emit(ctx,
               "for (size_t _i%d = 0; _i%d < BEBOP_ARRAY_COUNT(%s); _i%d++) {",
               w->loop_var, w->loop_var, w->access, w->loop_var);
        }
        ctx->indent++;
        w->state = 1;
        if (top < GEN_STACK_DEPTH) {
          size_work_t child = {elem, "", 0, w->loop_var + 1, false};
          snprintf(
              child.access, GEN_PATH_SIZE, "%s[_i%d]", w->access, w->loop_var);
          stack[top++] = child;
        }
        continue;
      }
      if (kind == BEBOP_TYPE_MAP) {
        const bebop_descriptor_type_t* key_type_t = bebop_descriptor_type_key(w->type);
        bebop_type_kind_t key_kind = bebop_descriptor_type_kind(key_type_t);
        const char* hash_fn = map_hash_fn(key_kind);
        const char* eq_fn = map_eq_fn(key_kind);
        char key_type[256], val_type[256];
        get_ctype_str(ctx, key_type_t, key_type, sizeof(key_type));
        get_ctype_str(ctx, bebop_descriptor_type_value(w->type), val_type, sizeof(val_type));

        emit(ctx, "{");
        ctx->indent++;
        emit(ctx, "uint32_t _len;");
        emit(ctx,
             "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Reader_GetU32(rd, &_len)) != BEBOP_WIRE_OK)) %s;",
             ret);
        if (ctx->is_mutable) {
          emit(ctx, "Bebop_Map_Init(&%s, ctx, %s, %s);", w->access, hash_fn, eq_fn);
        } else {
          emit(ctx, "Bebop_Map_Init(BEBOP_WIRE_MUTPTR(Bebop_Map, &%s), ctx, %s, %s);",
               w->access, hash_fn, eq_fn);
        }
        emit(ctx,
             "for (size_t _i%d = 0; _i%d < _len; _i%d++) {",
             w->loop_var, w->loop_var, w->loop_var);
        ctx->indent++;
        emit(ctx, "%s* _k%d = Bebop_WireCtx_Alloc(ctx, sizeof(%s));",
             key_type, w->loop_var, key_type);
        const bebop_descriptor_type_t* vtype = bebop_descriptor_type_value(w->type);
        bebop_type_kind_t vkind = bebop_descriptor_type_kind(vtype);
        if (vkind == BEBOP_TYPE_FIXED_ARRAY) {
          uint32_t fa_size = bebop_descriptor_type_fixed_size(vtype);
          emit(ctx, "%s* _v%d = Bebop_WireCtx_Alloc(ctx, sizeof(%s) * %u);",
               val_type, w->loop_var, val_type, fa_size);
        } else {
          emit(ctx, "%s* _v%d = Bebop_WireCtx_Alloc(ctx, sizeof(%s));",
               val_type, w->loop_var, val_type);
        }
        emit(ctx, "if (BEBOP_WIRE_UNLIKELY(!_k%d || !_v%d)) %s;", w->loop_var, w->loop_var, ret_oom);
        w->state = 1;
        if (top < GEN_STACK_DEPTH - 1) {
          bool val_is_ptr = (vkind == BEBOP_TYPE_FIXED_ARRAY);
          size_work_t val_work = {vtype, "", 0, w->loop_var + 1, val_is_ptr};
          size_work_t key_work = {bebop_descriptor_type_key(w->type),
                                  "", 0, w->loop_var + 1, false};
          if (val_is_ptr) {
            snprintf(val_work.access, GEN_PATH_SIZE, "_v%d", w->loop_var);
          } else {
            snprintf(val_work.access, GEN_PATH_SIZE, "(*_v%d)", w->loop_var);
          }
          snprintf(key_work.access, GEN_PATH_SIZE, "(*_k%d)", w->loop_var);
          stack[top++] = val_work;
          stack[top++] = key_work;
        }
        continue;
      }
      top--;
    } else if (w->state == 1) {
      bebop_type_kind_t kind2 = bebop_descriptor_type_kind(w->type);
      if (kind2 == BEBOP_TYPE_MAP) {
        if (ctx->is_mutable) {
          emit(ctx, "Bebop_Map_Put(&%s, _k%d, _v%d);", w->access, w->loop_var, w->loop_var);
        } else {
          emit(ctx, "Bebop_Map_Put(BEBOP_WIRE_MUTPTR(Bebop_Map, &%s), _k%d, _v%d);",
               w->access, w->loop_var, w->loop_var);
        }
      }
      ctx->indent--;
      emit(ctx, "}");
      if (kind2 == BEBOP_TYPE_ARRAY) {
        if (ctx->is_mutable) {
          emit(ctx, "%s.data = _d%d;", w->access, w->loop_var);
          emit(ctx, "%s.capacity = 0;", w->access);
        } else {
          char arr_type[256];
          get_ctype_str(ctx, w->type, arr_type, sizeof(arr_type));
          emit(ctx,
               "BEBOP_WIRE_MUTPTR(%s, &%s)->data = _d%d;",
               arr_type,
               w->access,
               w->loop_var);
          emit(ctx,
               "BEBOP_WIRE_MUTPTR(%s, &%s)->capacity = 0;",
               arr_type,
               w->access);
        }
        ctx->indent--;
        emit(ctx, "}");
      } else if (kind2 == BEBOP_TYPE_MAP) {
        ctx->indent--;
        emit(ctx, "}");
      }
      top--;
    }
  }
}

static void gen_decode_type(gen_ctx_t* ctx,
                            const bebop_descriptor_type_t* type,
                            const char* target)
{
  gen_decode_type_ex(ctx, type, target, false);
}

static void gen_decode_struct(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* name = type_name(ctx, bebop_descriptor_def_fqn(def));
  uint32_t field_count = bebop_descriptor_def_field_count(def);
  bool needs_ctx = def_needs_ctx(def);
  ctx->is_mutable = bebop_descriptor_def_is_mutable(def);

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_Decode", name);
  snprintf(sig,
           sizeof(sig),
           "Bebop_WireCtx *ctx, Bebop_Reader *rd, %s *v",
           name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_HOT", "Bebop_WireResult", fn_name, sig)) {
    return;
  }

  ctx->indent++;

  if (field_count == 0) {
    emit(
        ctx,
        "BEBOP_WIRE_UNUSED(ctx); BEBOP_WIRE_UNUSED(rd); BEBOP_WIRE_UNUSED(v);");
    emit(ctx, "return BEBOP_WIRE_OK;");
  } else {
    if (!needs_ctx) {
      emit(ctx, "BEBOP_WIRE_UNUSED(ctx);");
    }
    emit(ctx, "// @@bebop_insertion_point(decode_start:%s)", name);
    emit(ctx, "BEBOP_WIRE_PREFETCH_R(Bebop_Reader_Ptr(rd) + 64);");
    emit(ctx, "Bebop_WireResult r;");
    ctx->loop_depth = 0;

    for (uint32_t i = 0; i < field_count; i++) {
      const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
      const char* fname = bebop_descriptor_field_name(f);
      char target[GEN_PATH_SIZE];
      snprintf(target, sizeof(target), "v->%s", safe_field_name(fname));
      gen_decode_type(ctx, bebop_descriptor_field_type(f), target);
    }

    emit(ctx, "// @@bebop_insertion_point(decode_end:%s)", name);
    emit(ctx, "return BEBOP_WIRE_OK;");
  }

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_decode_message(gen_ctx_t* ctx,
                               const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  char name[GEN_PATH_SIZE];
  snprintf(name, sizeof(name), "%s", type_name(ctx, fqn));
  uint32_t field_count = bebop_descriptor_def_field_count(def);
  bool needs_ctx = def_needs_ctx(def);

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_Decode", name);
  snprintf(sig,
           sizeof(sig),
           "Bebop_WireCtx *ctx, Bebop_Reader *rd, %s *v",
           name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_HOT", "Bebop_WireResult", fn_name, sig)) {
    return;
  }

  ctx->indent++;
  if (!needs_ctx) {
    emit(ctx, "BEBOP_WIRE_UNUSED(ctx);");
  }
  if (field_count == 0) {
    emit(ctx, "BEBOP_WIRE_UNUSED(v);");
  }
  emit(ctx, "// @@bebop_insertion_point(decode_start:%s)", name);
  emit(ctx, "Bebop_WireResult r;");
  emit(ctx, "uint32_t msg_len;");
  emit(
      ctx,
      "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Reader_GetLen(rd, &msg_len)) != BEBOP_WIRE_OK)) return r;");
  emit(ctx, "const uint8_t *end = Bebop_Reader_Ptr(rd) + msg_len;");
  emit_nl(ctx);

  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
    emit(ctx,
         "BEBOP_WIRE_SET_NONE(v->%s);",
         safe_field_name(bebop_descriptor_field_name(f)));
  }
  emit_nl(ctx);

  emit(ctx, "while (Bebop_Reader_Ptr(rd) < end) {");
  ctx->indent++;
  emit(ctx, "uint8_t tag;");
  emit(ctx,
       "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Reader_GetByte(rd, &tag)) != BEBOP_WIRE_OK)) return r;");
  emit(ctx, "if (tag == 0) break;");
  emit_nl(ctx);

  emit(ctx, "switch (tag) {");
  ctx->loop_depth = 0;
  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_descriptor_field_t* f = bebop_descriptor_def_field_at(def, i);
    const char* fname = bebop_descriptor_field_name(f);
    const char* safe = safe_field_name(fname);

    sb_indent(&ctx->out, ctx->indent);
    sb_puts(&ctx->out, "case ");
    sb_puts_screaming(&ctx->out, name);
    sb_putc(&ctx->out, '_');
    sb_puts_screaming(&ctx->out, fname);
    sb_puts(&ctx->out, "_TAG:\n");
    ctx->indent++;
    emit(ctx, "v->%s.has_value = true;", safe);
    const bebop_descriptor_type_t* ftype = bebop_descriptor_field_type(f);
    if (type_is_message_or_union(ctx, ftype)) {
      const char* ftype_name = type_name(ctx, bebop_descriptor_type_fqn(ftype));
      emit(ctx,
           "v->%s.value = Bebop_WireCtx_Alloc(ctx, sizeof(%s));",
           safe,
           ftype_name);
      emit(ctx, "if (BEBOP_WIRE_UNLIKELY(!v->%s.value)) return BEBOP_WIRE_ERR_OOM;", safe);
      char target[GEN_PATH_SIZE];
      snprintf(target, sizeof(target), "v->%s.value", safe);
      gen_decode_type_ex(ctx, ftype, target, true);
    } else {
      char target[GEN_PATH_SIZE];
      snprintf(target, sizeof(target), "v->%s.value", safe);
      gen_decode_type(ctx, ftype, target);
    }
    emit(ctx, "break;");
    ctx->indent--;
  }
  emit(ctx, "// @@bebop_insertion_point(decode_switch:%s)", name);
  emit(ctx, "default:");
  ctx->indent++;
  emit(ctx, "Bebop_Reader_Seek(rd, end);");
  emit(ctx, "goto done;");
  ctx->indent--;
  emit(ctx, "}");
  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
  emit(ctx, "done:");
  emit(ctx, "// @@bebop_insertion_point(decode_end:%s)", name);
  emit(ctx, "return BEBOP_WIRE_OK;");

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_decode_union(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  const char* fqn = bebop_descriptor_def_fqn(def);
  char name[GEN_PATH_SIZE];
  snprintf(name, sizeof(name), "%s", type_name(ctx, fqn));
  uint32_t branch_count = bebop_descriptor_def_branch_count(def);

  char fn_name[GEN_PATH_SIZE], sig[GEN_PATH_SIZE];
  snprintf(fn_name, sizeof(fn_name), "%s_Decode", name);
  snprintf(sig,
           sizeof(sig),
           "Bebop_WireCtx *ctx, Bebop_Reader *rd, %s *v",
           name);
  if (!emit_fn_start_ex(ctx, "BEBOP_WIRE_HOT", "Bebop_WireResult", fn_name, sig)) {
    return;
  }

  ctx->indent++;
  emit(ctx, "// @@bebop_insertion_point(decode_start:%s)", name);
  emit(ctx, "Bebop_WireResult r;");
  emit(ctx, "uint32_t union_len;");
  emit(ctx, "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Reader_GetLen(rd, &union_len)) != BEBOP_WIRE_OK)) "
              "return r;");
  emit(ctx, "const uint8_t *end = Bebop_Reader_Ptr(rd) + union_len;");
  emit_nl(ctx);
  emit(ctx, "uint8_t disc;");
  emit(ctx,
       "if (BEBOP_WIRE_UNLIKELY((r = Bebop_Reader_GetByte(rd, &disc)) != BEBOP_WIRE_OK)) return r;");
  emit(ctx, "v->discriminator = (%s_Disc)disc;", name);
  emit_nl(ctx);

  emit(ctx, "switch (v->discriminator) {");
  for (uint32_t i = 0; i < branch_count; i++) {
    const bebop_descriptor_branch_t* b = bebop_descriptor_def_branch_at(def, i);
    const char* bname = branch_name(b);
    if (!bname) {
      continue;
    }
    const char* type_fqn = bebop_descriptor_branch_type_ref_fqn(b);
    if (!type_fqn) {
      type_fqn = bebop_descriptor_branch_inline_fqn(b);
    }

    sb_indent(&ctx->out, ctx->indent);
    sb_union_case(&ctx->out, name, bname);
    sb_putc(&ctx->out, '\n');
    ctx->indent++;
    emit(ctx,
         "if (BEBOP_WIRE_UNLIKELY((r = %s_Decode(ctx, rd, &v->%s)) != BEBOP_WIRE_OK)) return r;",
         type_name(ctx, type_fqn),
         branch_lower(b));
    emit(ctx, "break;");
    ctx->indent--;
  }
  emit(ctx, "// @@bebop_insertion_point(decode_switch:%s)", name);
  emit(ctx, "default:");
  ctx->indent++;
  emit(ctx, "Bebop_Reader_Seek(rd, end);");
  emit(ctx, "break;");
  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
  emit(ctx, "// @@bebop_insertion_point(decode_end:%s)", name);
  emit(ctx, "return BEBOP_WIRE_OK;");

  ctx->indent--;
  emit(ctx, "}");
  emit_nl(ctx);
}

static void gen_functions(gen_ctx_t* ctx, const bebop_descriptor_def_t* def)
{
  typedef struct {
    const bebop_descriptor_def_t* def;
    bool children_pushed;
  } frame_t;

  frame_t stack[GEN_STACK_DEPTH];
  int top = 0;
  stack[top++] = (frame_t) {def, false};

  while (top > 0) {
    frame_t* f = &stack[top - 1];

    if (!f->children_pushed) {
      f->children_pushed = true;
      uint32_t nested = bebop_descriptor_def_nested_count(f->def);
      for (uint32_t i = nested; i > 0; i--) {
        if (top < GEN_STACK_DEPTH) {
          stack[top++] =
              (frame_t) {bebop_descriptor_def_nested_at(f->def, i - 1), false};
        }
      }
      continue;
    }

    top--;
    const bebop_descriptor_def_t* d = f->def;
    bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
    switch (kind) {
      case BEBOP_DEF_STRUCT:
        gen_size_struct(ctx, d);
        gen_encode_struct(ctx, d);
        gen_decode_struct(ctx, d);
        gen_type_info(ctx, d);
        break;
      case BEBOP_DEF_MESSAGE:
        gen_size_message(ctx, d);
        gen_encode_message(ctx, d);
        gen_decode_message(ctx, d);
        gen_type_info(ctx, d);
        break;
      case BEBOP_DEF_UNION:
        gen_size_union(ctx, d);
        gen_encode_union(ctx, d);
        gen_decode_union(ctx, d);
        gen_type_info(ctx, d);
        break;
      default:
        break;
    }
  }
}

static uint8_t* read_stdin(size_t* out_len)
{
  size_t cap = 4096, len = 0;
  uint8_t* buf = malloc(cap);
  if (!buf) {
    return NULL;
  }

  while (1) {
    if (len + 1024 > cap) {
      cap *= 2;
      uint8_t* new_buf = realloc(buf, cap);
      if (!new_buf) {
        free(buf);
        return NULL;
      }
      buf = new_buf;
    }
    size_t n = fread(buf + len, 1, cap - len, stdin);
    if (n == 0) {
      break;
    }
    len += n;
  }
  *out_len = len;
  return buf;
}

static bool write_stdout(const uint8_t* data, size_t len)
{
  return fwrite(data, 1, len, stdout) == len;
}

static void* plugin_alloc(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
  (void)old_size;
  (void)ctx;
  if (new_size == 0) { free(ptr); return NULL; }
  return realloc(ptr, new_size);
}

static bebop_file_result_t plugin_file_read(const char* path, void* ctx)
{
  (void)ctx;
  (void)path;
  return (bebop_file_result_t) {.error = "file reading not supported"};
}

static bool plugin_file_exists(const char* path, void* ctx)
{
  (void)ctx;
  (void)path;
  return false;
}

static int send_error(bebop_context_t* ctx,
                      bebop_host_allocator_t* alloc,
                      const char* msg)
{
  bebop_plugin_response_builder_t* b =
      bebop_plugin_response_builder_create(alloc);
  if (!b) {
    return 1;
  }
  bebop_plugin_response_builder_set_error(b, msg);
  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  if (!resp) {
    return 1;
  }
  const uint8_t* buf = NULL;
  size_t len = 0;
  bebop_status_t status = bebop_plugin_response_encode(ctx, resp, &buf, &len);
  bebop_plugin_response_free(resp);
  if (status != BEBOP_OK) {
    return 1;
  }
  write_stdout(buf, len);
  return 1;
}

static const char* get_basename(const char* path)
{
  if (!path) {
    return NULL;
  }
  const char* slash = strrchr(path, '/');
#ifdef _WIN32
  const char* backslash = strrchr(path, '\\');
  if (backslash && (!slash || backslash > slash)) {
    slash = backslash;
  }
#endif
  return slash ? slash + 1 : path;
}

static char* make_output_name(bebop_host_allocator_t* alloc,
                              const char* path,
                              const char* ext)
{
  if (!path) {
    return NULL;
  }
  const char* basename = get_basename(path);
  size_t len = strlen(basename);
  const char* dot = strrchr(basename, '.');
  size_t base_len = dot ? (size_t)(dot - basename) : len;
  size_t ext_len = strlen(ext);
  size_t out_len = base_len + 1 + ext_len + 1;
  char* out = alloc->alloc(NULL, 0, out_len, alloc->ctx);
  if (!out) {
    return NULL;
  }
  memcpy(out, basename, base_len);
  out[base_len] = '.';
  memcpy(out + base_len + 1, ext, ext_len);
  out[base_len + 1 + ext_len] = '\0';
  return out;
}

static bool path_ends_with(const char* path, const char* suffix)
{
  size_t path_len = strlen(path);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > path_len) {
    return false;
  }
  return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool should_generate(const bebop_plugin_request_t* req,
                            const char* schema_path)
{
  if (!schema_path) {
    return false;
  }
  uint32_t file_count = bebop_plugin_request_file_count(req);
  if (file_count == 0) {
    return true;
  }
  for (uint32_t i = 0; i < file_count; i++) {
    const char* file = bebop_plugin_request_file_at(req, i);
    if (!file) {
      continue;
    }
    if (strcmp(schema_path, file) == 0 || path_ends_with(schema_path, file)
        || path_ends_with(file, schema_path))
    {
      return true;
    }
  }
  return false;
}

static void add_file(bebop_plugin_response_builder_t* b,
                     bebop_host_allocator_t* alloc,
                     char* name,
                     char* content)
{
  bebop_plugin_response_builder_add_file(b, name ? name : "output.h", content);
  if (name && alloc->alloc) {
    alloc->alloc(name, strlen(name) + 1, 0, alloc->ctx);
  }
  if (content && alloc->alloc) {
    alloc->alloc(content, strlen(content) + 1, 0, alloc->ctx);
  }
}

static int generate(bebop_context_t* bctx,
                    bebop_host_allocator_t* alloc,
                    const bebop_plugin_request_t* req)
{
  uint32_t schema_count = bebop_plugin_request_schema_count(req);
  gen_ctx_t ctx;
  ctx_init(&ctx, req);

  bebop_plugin_response_builder_t* b =
      bebop_plugin_response_builder_create(alloc);
  if (!b) {
    return send_error(bctx, alloc, "out of memory");
  }

  for (uint32_t i = 0; i < schema_count; i++) {
    const bebop_descriptor_schema_t* schema =
        bebop_plugin_request_schema_at(req, i);
    const char* path = bebop_descriptor_schema_path(schema);
    if (!should_generate(req, path)) {
      continue;
    }

    ctx.schema = schema;
    type_set_init(&ctx.array_types);
    type_set_init(&ctx.map_types);
    sb_free(&ctx.out);
    sb_init(&ctx.out);
    ctx.indent = 0;

    uint32_t def_count = bebop_descriptor_schema_def_count(schema);

    switch (ctx.opts.output_mode) {
      case GEN_OUT_UNITY: {
        emit_file_header(&ctx, schema);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_forward_decls(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        emit_nl(&ctx);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_enums_only(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        for (uint32_t j = 0; j < def_count; j++) {
          gen_container_typedef(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        for (uint32_t j = 0; j < def_count; j++) {
          gen_typedef(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        emit_deferred_map_entries(&ctx);
        ctx.emit_mode = GEN_EMIT_DECL;
        for (uint32_t j = 0; j < def_count; j++) {
          gen_functions(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        if (!ctx.opts.no_reflection) {
          emit_nl(&ctx);
          for (uint32_t j = 0; j < def_count; j++) {
            gen_reflection_decl(&ctx, bebop_descriptor_schema_def_at(schema, j));
          }
        }
        ctx.emit_mode = GEN_EMIT_IMPL;
        for (uint32_t j = 0; j < def_count; j++) {
          gen_functions(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        if (!ctx.opts.no_reflection) {
          emit_nl(&ctx);
          for (uint32_t j = 0; j < def_count; j++) {
            gen_reflection(&ctx, bebop_descriptor_schema_def_at(schema, j));
          }
        }
        emit_file_footer(&ctx);
        add_file(b,
                 alloc,
                 make_output_name(alloc, path, "bb.c"),
                 sb_steal(&ctx.out));
        break;
      }
      case GEN_OUT_SINGLE_HEADER: {
        emit_file_header(&ctx, schema);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_forward_decls(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        emit_nl(&ctx);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_enums_only(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        for (uint32_t j = 0; j < def_count; j++) {
          gen_container_typedef(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        for (uint32_t j = 0; j < def_count; j++) {
          gen_typedef(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        emit_deferred_map_entries(&ctx);
        ctx.emit_mode = GEN_EMIT_DECL;
        for (uint32_t j = 0; j < def_count; j++) {
          gen_functions(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        if (!ctx.opts.no_reflection) {
          emit_nl(&ctx);
          for (uint32_t j = 0; j < def_count; j++) {
            gen_reflection_decl(&ctx, bebop_descriptor_schema_def_at(schema, j));
          }
        }
        emit_single_header_impl_start(&ctx, schema);
        ctx.emit_mode = GEN_EMIT_IMPL;
        for (uint32_t j = 0; j < def_count; j++) {
          gen_functions(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        if (!ctx.opts.no_reflection) {
          emit_nl(&ctx);
          for (uint32_t j = 0; j < def_count; j++) {
            gen_reflection(&ctx, bebop_descriptor_schema_def_at(schema, j));
          }
        }
        emit_single_header_impl_end(&ctx);
        emit_file_footer(&ctx);
        add_file(b,
                 alloc,
                 make_output_name(alloc, path, "bb.h"),
                 sb_steal(&ctx.out));
        break;
      }
      case GEN_OUT_SPLIT:
      default: {
        ctx.emit_mode = GEN_EMIT_DECL;
        emit_file_header(&ctx, schema);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_forward_decls(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        emit_nl(&ctx);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_enums_only(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        for (uint32_t j = 0; j < def_count; j++) {
          gen_container_typedef(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        for (uint32_t j = 0; j < def_count; j++) {
          gen_typedef(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        emit_deferred_map_entries(&ctx);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_functions(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        if (!ctx.opts.no_reflection) {
          emit_nl(&ctx);
          for (uint32_t j = 0; j < def_count; j++) {
            gen_reflection_decl(&ctx, bebop_descriptor_schema_def_at(schema, j));
          }
        }
        emit_file_footer(&ctx);
        add_file(b,
                 alloc,
                 make_output_name(alloc, path, "bb.h"),
                 sb_steal(&ctx.out));

        sb_init(&ctx.out);
        ctx.emit_mode = GEN_EMIT_IMPL;
        emit_source_header(&ctx, schema);
        for (uint32_t j = 0; j < def_count; j++) {
          gen_functions(&ctx, bebop_descriptor_schema_def_at(schema, j));
        }
        if (!ctx.opts.no_reflection) {
          emit_nl(&ctx);
          for (uint32_t j = 0; j < def_count; j++) {
            gen_reflection(&ctx, bebop_descriptor_schema_def_at(schema, j));
          }
        }
        add_file(b,
                 alloc,
                 make_output_name(alloc, path, "bb.c"),
                 sb_steal(&ctx.out));
        break;
      }
    }
  }

  if (ctx.error) {
    const char* err = ctx.error;
    ctx_free(&ctx);
    bebop_plugin_response_builder_free(b);
    return send_error(bctx, alloc, err);
  }

  ctx_free(&ctx);

  bebop_plugin_response_t* resp = bebop_plugin_response_builder_finish(b);
  if (!resp) {
    return send_error(bctx, alloc, "failed to build response");
  }

  const uint8_t* buf = NULL;
  size_t len = 0;
  bebop_status_t status = bebop_plugin_response_encode(bctx, resp, &buf, &len);
  bebop_plugin_response_free(resp);
  if (status != BEBOP_OK) {
    return 1;
  }

  write_stdout(buf, len);
  return 0;
}

int main(void)
{
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  bebop_host_allocator_t alloc = {.alloc = plugin_alloc};
  bebop_file_reader_t reader = {.read = plugin_file_read,
                                .exists = plugin_file_exists};
  bebop_host_t host = {.allocator = alloc, .file_reader = reader};

  bebop_context_t* ctx = bebop_context_create(&host);
  if (!ctx) {
    fprintf(stderr, "[bebopc-gen-c] failed to create context\n");
    return 1;
  }

  size_t req_len = 0;
  uint8_t* req_buf = read_stdin(&req_len);
  if (!req_buf || req_len == 0) {
    fprintf(stderr, "[bebopc-gen-c] failed to read request\n");
    bebop_context_destroy(ctx);
    return 1;
  }

  bebop_plugin_request_t* req = NULL;
  bebop_status_t status =
      bebop_plugin_request_decode(ctx, req_buf, req_len, &req);

  if (status != BEBOP_OK || !req) {
    fprintf(stderr, "[bebopc-gen-c] failed to decode request\n");
    free(req_buf);
    bebop_context_destroy(ctx);
    return 1;
  }

  int result = generate(ctx, &alloc, req);

  bebop_plugin_request_free(req);
  free(req_buf);
  bebop_context_destroy(ctx);

  return result;
}
