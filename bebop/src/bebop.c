#if defined(__linux__) || defined(__ANDROID__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#endif

#if defined(_MSC_VER) && !defined(__cplusplus) && !defined(BEBOP__MAX_ALIGN_T_DEFINED)
#define BEBOP__MAX_ALIGN_T_DEFINED
typedef struct {
  long long __max_align_ll;
  long double __max_align_ld;
} max_align_t;
#endif

#if defined(_MSC_VER)
#define BEBOP_MAX_ALIGN __alignof(max_align_t)
#else
#define BEBOP_MAX_ALIGN _Alignof(max_align_t)
#endif

#include "bebop.h"

#ifdef __APPLE__
#include <xlocale.h>
#else
#include <locale.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127 4309)
#endif

#include "cwisstable.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "bebop_tables.h"

#define BEBOP_INTERN(ctx) (&(ctx)->intern)
#define BEBOP_ARENA(ctx) (&(ctx)->arena)
#define BEBOP_STR(ctx, handle) bebop_str_get(BEBOP_INTERN(ctx), (handle))
#define BEBOP_STR_LEN(ctx, handle) bebop_str_len(BEBOP_INTERN(ctx), (handle))
#define BEBOP_STREQ(str, len, lit) \
  ((len) == (sizeof(lit) - 1) && memcmp((str), (lit), sizeof(lit) - 1) == 0)
#define BEBOP_HAS_PREFIX(str, lit) (memcmp((str), (lit), sizeof(lit) - 1) == 0)
#define bebop_streqn(nul_str, buf, buflen) \
  (strlen(nul_str) == (buflen) && memcmp((nul_str), (buf), (buflen)) == 0)
#define bebop_streqni(nul_str, buf, buflen) \
  (strlen(nul_str) == (buflen) && bebop__memicmp((nul_str), (buf), (buflen)) == 0)
#define BEBOP_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BEBOP_UNUSED(x) ((void)(x))
#define BEBOP_MAX_DIAGNOSTICS 100

#if defined(__GNUC__) || defined(__clang__)
#define BEBOP_LIKELY(x) __builtin_expect(!!(x), 1)
#define BEBOP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BEBOP_MAYBE_UNUSED __attribute__((unused))
#else
#define BEBOP_LIKELY(x) (x)
#define BEBOP_UNLIKELY(x) (x)
#define BEBOP_MAYBE_UNUSED
#endif

#define BEBOP_DISCARD_CONST(type, ptr) ((type)(uintptr_t)(ptr))

#define BEBOP_ARRAY_PUSH(arena, arr, count, cap, T) \
  (((count) >= (cap)) \
       ? ((cap) = ((cap) == 0)           ? 16 \
              : ((cap) > UINT32_MAX / 2) ? 0 \
                                         : (cap) * 2, \
          ((cap) == 0) ? NULL \
                       : ((arr) = (T*)bebop_arena_realloc( \
                              (arena), (arr), ((cap) / 2) * sizeof(T), (cap) * sizeof(T) \
                          ), \
                          (arr) ? &(arr)[(count)++] : NULL)) \
       : &(arr)[(count)++])

#define BEBOP_DIAG_FMT(schema, sev, code, span, ...) \
  do { \
    char bebop__msg[512]; \
    snprintf(bebop__msg, sizeof(bebop__msg), __VA_ARGS__); \
    bebop__schema_add_diagnostic( \
        (schema), (bebop__diag_loc_t) {(sev), (code), (span)}, bebop__msg, NULL \
    ); \
  } while (0)

#define BEBOP_ERROR_FMT(schema, code, span, ...) \
  BEBOP_DIAG_FMT((schema), BEBOP_DIAG_ERROR, (code), (span), __VA_ARGS__)

#define BEBOP_WARNING_FMT(schema, code, span, ...) \
  BEBOP_DIAG_FMT((schema), BEBOP_DIAG_WARNING, (code), (span), __VA_ARGS__)

#define BEBOP_DIAG_HINT_FMT(schema, sev, code, span, hint, ...) \
  do { \
    char bebop__msg[512]; \
    snprintf(bebop__msg, sizeof(bebop__msg), __VA_ARGS__); \
    bebop__schema_add_diagnostic( \
        (schema), (bebop__diag_loc_t) {(sev), (code), (span)}, bebop__msg, (hint) \
    ); \
  } while (0)

#define BEBOP_ERROR_HINT_FMT(schema, code, span, hint, ...) \
  BEBOP_DIAG_HINT_FMT((schema), BEBOP_DIAG_ERROR, (code), (span), (hint), __VA_ARGS__)

#define BEBOP_DIAG_ADD_LABEL(schema, span, message) \
  bebop__schema_diag_add_label((schema), (span), (message))

typedef struct {
  const char* file;
  int line;
  const char* func;
} bebop__src_loc_t;

#define bebop__SRC_LOC ((bebop__src_loc_t) {__FILE__, __LINE__, __func__})

typedef struct {
  const char* data;
  size_t len;
} bebop__str_view_t;

typedef struct {
  bebop_diag_severity_t severity;
  uint32_t code;
  bebop_span_t span;
} bebop__diag_loc_t;

#define BEBOP_ADD_WOULD_OVERFLOW_SIZE(a, b) ((b) > SIZE_MAX - (a))
#define BEBOP_ADD_WOULD_OVERFLOW_U32(a, b) ((b) > UINT32_MAX - (a))
#define BEBOP_MUL_WOULD_OVERFLOW_U32(a, b) ((b) != 0 && (a) > UINT32_MAX / (b))

#define BEBOP_DOUBLE_CAPACITY_U32(cap) \
  (((cap) == 0) ? 16 : (((cap) > UINT32_MAX / 2) ? 0 : (cap) * 2))

#define BEBOP_MAX_FIELD_INDEX 255
#define BEBOP_MAX_DISCRIMINATOR 255
#define BEBOP_MIN_DISCRIMINATOR 1
#define BEBOP_MAX_SERVICE_METHODS 255

#define BEBOP_DIAG_MSG_SIZE 512
#define BEBOP_SMALL_BUF_SIZE 128
#define BEBOP_MEDIUM_BUF_SIZE 256
#define BEBOP_MAX_TYPE_DEPTH 64
#define BEBOP_MAX_DECORATOR_CHAIN_LENGTH 1000
#define BEBOP_MAX_FIXED_ARRAY_SIZE 65535

#define BEBOP_SEMA_INITIAL_SCOPE_CAPACITY 16
#define BEBOP_INITIAL_ARRAY_CAPACITY 16
#define BEBOP_INITIAL_SMALL_CAPACITY 4

#if defined(_MSC_VER)
#define BEBOP_BUILTIN_UNREACHABLE() __assume(0)
#else
#define BEBOP_BUILTIN_UNREACHABLE() __builtin_unreachable()
#endif

#if defined(BEBOP_DISABLE_ASSERTS) || defined(NDEBUG)

#define BEBOP_ASSERT(cond) ((void)0)
#define BEBOP_ASSERT_MSG(cond, msg) ((void)0)
#define BEBOP_UNREACHABLE() BEBOP_BUILTIN_UNREACHABLE()

#else

#define BEBOP_ASSERT(cond) \
  do { \
    if (!(cond)) { \
      bebop__assert_fail(#cond, bebop__SRC_LOC); \
    } \
  } while (0)

#define BEBOP_ASSERT_MSG(cond, msg) \
  do { \
    if (!(cond)) { \
      bebop__assert_fail_msg(#cond, msg, bebop__SRC_LOC); \
    } \
  } while (0)

#define BEBOP_UNREACHABLE() \
  do { \
    bebop__assert_fail("unreachable", bebop__SRC_LOC); \
    BEBOP_BUILTIN_UNREACHABLE(); \
  } while (0)

_Noreturn static inline void bebop__assert_fail(const char* cond, const bebop__src_loc_t loc)
{
  fprintf(stderr, "bebop: assertion failed: %s\n", cond);
  fprintf(stderr, "  at %s:%d in %s()\n", loc.file, loc.line, loc.func);
  abort();
}

_Noreturn static inline void bebop__assert_fail_msg(
    const char* cond, const char* msg, const bebop__src_loc_t loc
)
{
  fprintf(stderr, "bebop: assertion failed: %s\n", cond);
  fprintf(stderr, "  message: %s\n", msg);
  fprintf(stderr, "  at %s:%d in %s()\n", loc.file, loc.line, loc.func);
  abort();
}

#endif

#define BEBOP_ARENA_CHUNK_SIZE (64 * 1024)
#define BEBOP_ARENA_MIN_ALIGN BEBOP_MAX_ALIGN

typedef struct bebop__chunk bebop__chunk_t;

typedef struct {
  bebop__chunk_t* head;
  bebop__chunk_t* current;
  bebop_host_allocator_t alloc;
} bebop_arena_t;

bool bebop_arena_init(bebop_arena_t* arena, const bebop_host_allocator_t* alloc, size_t initial);

void bebop_arena_destroy(bebop_arena_t* arena);

void bebop_arena_reset(bebop_arena_t* arena);

void* bebop_arena_alloc(bebop_arena_t* arena, size_t size, size_t align);

void* bebop_arena_dup(bebop_arena_t* arena, const void* src, size_t size);
char* bebop_arena_strdup(bebop_arena_t* arena, const char* str);

char* bebop_arena_strndup(bebop_arena_t* arena, const char* str, size_t len);

void* bebop_arena_malloc(bebop_arena_t* arena, size_t size);

void* bebop_arena_realloc(bebop_arena_t* arena, const void* ptr, size_t old_size, size_t new_size);

void bebop_arena_free(const bebop_arena_t* arena, const void* ptr);

#define bebop_arena_new(arena, T, n) ((T*)bebop_arena_alloc((arena), sizeof(T) * (n), _Alignof(T)))

#define bebop_arena_new1(arena, T) bebop_arena_new((arena), T, 1)

static inline void* bebop__cwiss_alloc(const size_t size, const size_t align, void* ctx)
{
  return bebop_arena_alloc(ctx, size, align);
}

static inline void bebop__cwiss_free(void* ptr, const size_t size, const size_t align, void* ctx)
{
  (void)ptr;
  (void)size;
  (void)align;
  (void)ctx;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4127 4098)
#endif

CWISS_DECLARE_FLAT_MAP_POLICY(
    bebop_defmap_kPolicy,
    uint32_t,
    void*,
    (alloc_alloc, bebop__cwiss_alloc),
    (alloc_free, bebop__cwiss_free)
);
CWISS_DECLARE_HASHMAP_WITH(bebop_defmap, uint32_t, void*, bebop_defmap_kPolicy);

CWISS_DECLARE_FLAT_MAP_POLICY(
    bebop_internmap_kPolicy,
    uint64_t,
    uint32_t,
    (alloc_alloc, bebop__cwiss_alloc),
    (alloc_free, bebop__cwiss_free)
);
CWISS_DECLARE_HASHMAP_WITH(bebop_internmap, uint64_t, uint32_t, bebop_internmap_kPolicy);

CWISS_DECLARE_FLAT_MAP_POLICY(
    bebop_idxmap_kPolicy,
    uint32_t,
    uint32_t,
    (alloc_alloc, bebop__cwiss_alloc),
    (alloc_free, bebop__cwiss_free)
);
CWISS_DECLARE_HASHMAP_WITH(bebop_idxmap, uint32_t, uint32_t, bebop_idxmap_kPolicy);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct {
  uint32_t idx;
} bebop_str_t;

#define BEBOP_STR_NULL ((bebop_str_t) {0})

typedef struct {
  bebop_str_t* items;
  uint32_t count;
  uint32_t capacity;
} bebop__dep_list_t;

typedef struct {
  char** strings;
  uint64_t* hashes;
  uint32_t* lengths;
  uint32_t count;
  uint32_t capacity;
  bebop_arena_t* arena;
  bebop_internmap lookup;
} bebop_intern_t;

bool bebop_intern_init(bebop_intern_t* intern, bebop_arena_t* arena, uint32_t capacity);

bebop_str_t bebop_intern(bebop_intern_t* intern, const char* str);

bebop_str_t bebop_intern_n(bebop_intern_t* intern, const char* str, size_t len);
const char* bebop_str_get(const bebop_intern_t* intern, bebop_str_t handle);
size_t bebop_str_len(const bebop_intern_t* intern, bebop_str_t handle);

#define bebop_str_is_null(handle) ((handle).idx == 0)
#define bebop_str_eq(a, b) ((a).idx == (b).idx)

uint64_t bebop_hash_fnv1a(const char* str, size_t len);

typedef int32_t bebop_codepoint_t;

#define BEBOP_CP_INVALID (-1)
#define BEBOP_CP_BOM 0xFEFF

extern const int8_t bebop_utf8_width_table[32];

int bebop_utf8_decode(const char* s, size_t len, bebop_codepoint_t* cp);
int bebop_utf8_encode(bebop_codepoint_t cp, char* dst);
bool bebop_utf8_valid(const char* s, size_t len);
int bebop_unescape_char(const char* s, size_t len, char* out, int* out_len);

#define BEBOP_IS_ASCII(c) (((unsigned char)(c)) < 0x80)
#define BEBOP_IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define BEBOP_IS_HEX(c) \
  (BEBOP_IS_DIGIT(c) || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f'))
#define BEBOP_HEX_VALUE(c) \
  (((c) >= '0' && (c) <= '9')       ? (c) - '0' \
       : ((c) >= 'A' && (c) <= 'F') ? (c) - 'A' + 10 \
       : ((c) >= 'a' && (c) <= 'f') ? (c) - 'a' + 10 \
                                    : -1)
#define BEBOP_IS_ALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define BEBOP_IS_IDENT_START(c) (BEBOP_IS_ALPHA(c) || (c) == '_')
#define BEBOP_IS_IDENT_CHAR(c) (BEBOP_IS_IDENT_START(c) || BEBOP_IS_DIGIT(c))
#define BEBOP_TOLOWER(c) (((c) >= 'A' && (c) <= 'Z') ? (c) + 32 : (c))

#define BEBOP_IS_WHITESPACE(c) ((c) == ' ' || (c) == '\t')
#define BEBOP_IS_NEWLINE(c) ((c) == '\n' || (c) == '\r')
#define BEBOP_IS_BLANK(c) (BEBOP_IS_WHITESPACE(c) || BEBOP_IS_NEWLINE(c))

static inline int bebop__memicmp(const char* a, const char* b, const size_t n)
{
  for (size_t i = 0; i < n; i++) {
    const int d = BEBOP_TOLOWER((unsigned char)a[i]) - BEBOP_TOLOWER((unsigned char)b[i]);
    if (d != 0) {
      return d;
    }
  }
  return 0;
}

static inline int bebop__strcmp(const char* a, const char* b)
{
  if (a == b) {
    return 0;
  }
  if (!a) {
    return -1;
  }
  if (!b) {
    return 1;
  }
  const size_t la = strlen(a), lb = strlen(b);
  const size_t min = la < lb ? la : lb;
  const int cmp = memcmp(a, b, min);
  if (cmp != 0) {
    return cmp;
  }
  return (la > lb) - (la < lb);
}

static inline bool bebop__streq(const char* a, const char* b)
{
  if (a == b) {
    return true;
  }
  if (!a || !b) {
    return false;
  }
  const size_t la = strlen(a), lb = strlen(b);
  if (la != lb) {
    return false;
  }
  return memcmp(a, b, la) == 0;
}

static inline char* bebop__join_dotted(
    bebop_arena_t* arena, const bebop__str_view_t prefix, const bebop__str_view_t name
)
{
  const size_t total = prefix.len + 1 + name.len;
  char* buf = bebop_arena_alloc(arena, total + 1, 1);
  if (!buf) {
    return NULL;
  }
  memcpy(buf, prefix.data, prefix.len);
  buf[prefix.len] = '.';
  memcpy(buf + prefix.len + 1, name.data, name.len);
  buf[total] = '\0';
  return buf;
}

#define bebop__HASH_SEED 0x5AFE5EED
#define bebop__HASH_C1 0xcc9e2d51
#define bebop__HASH_C2 0x1b873593
#define bebop__HASH_N 0xe6546b64

uint32_t bebop_util_hash_method_id(const char* input, size_t length);

bool bebop_util_parse_int(const char* str, size_t len, int64_t* out);
bool bebop_util_parse_uint(const char* str, size_t len, uint64_t* out);
bool bebop_util_parse_float(const char* str, size_t len, double* out);
bool bebop_util_parse_uuid(const char* str, size_t len, uint8_t out[16]);
bool bebop_util_parse_timestamp(
    const char* str, size_t len, int64_t* out_seconds, int32_t* out_nanos, int32_t* out_offset_ms
);
bool bebop_util_parse_duration(
    const char* str, size_t len, int64_t* out_seconds, int32_t* out_nanos
);
bool bebop_util_scan_int(const char* str, size_t len);
bool bebop_util_scan_float(const char* str, size_t len);
uint32_t bebop_util_levenshtein(
    const char* a, size_t a_len, const char* b, size_t b_len, uint32_t max_dist
);
const char* bebop_util_fuzzy_match(
    const char* input,
    size_t input_len,
    const char* const* candidates,
    size_t candidate_count,
    uint32_t max_distance
);

struct bebop_context {
  bebop_arena_t arena;
  bebop_intern_t intern;
  bebop_host_t host;
  bebop_error_t last_error;
  char* error_message;
};

void bebop__context_set_error(bebop_context_t* ctx, bebop_error_t error, const char* message);

enum {
#define X(name, code, sev, msg) BEBOP_DIAG_##name = code,
  BEBOP_DIAGNOSTIC_CODES(X)
#undef X
};

typedef struct {
  bebop_span_t span;
  const char* message;
} bebop_diag_label_t;

struct bebop_diagnostic {
  bebop_diag_severity_t severity;
  uint32_t code;
  bebop_span_t span;
  const char* path;
  const char* message;
  const char* hint;
  bebop_diag_label_t* labels;
  uint32_t label_count;
};

struct bebop_literal {
  bebop_literal_kind_t kind;
  bebop_span_t span;
  bool has_env_var;
  bebop_str_t raw_value;
  bebop_context_t* ctx;

  union {
    bool bool_val;
    int64_t int_val;
    double float_val;
    bebop_str_t string_val;
    uint8_t uuid_val[16];

    struct {
      int64_t seconds;
      int32_t nanos;
      int32_t offset_ms;
    } timestamp_val;

    struct {
      int64_t seconds;
      int32_t nanos;
    } duration_val;

    struct {
      uint8_t* data;
      size_t len;
    } bytes_val;
  };
};

struct bebop_decorator_arg {
  bebop_str_t name;
  bebop_span_t span;
  bebop_literal_t value;
  bebop_decorator_t* decorator;
};

struct bebop_export_entry {
  bebop_str_t key;
  bebop_literal_t value;
};

struct bebop_export_data {
  bebop_export_entry_t* entries;
  uint32_t count;
};

struct bebop_decorator {
  bebop_str_t name;
  bebop_span_t span;
  bebop_decorator_arg_t* args;
  uint32_t arg_count;
  bebop_decorator_t* next;
  bebop_schema_t* schema;
  bebop_def_t* resolved;
  bebop_export_data_t* export_data;
};

typedef enum {
  BEBOP_DECORATED_DEF,
  BEBOP_DECORATED_FIELD,
  BEBOP_DECORATED_METHOD,
  BEBOP_DECORATED_BRANCH,
  BEBOP_DECORATED_ENUM_MEMBER,
} bebop_decorated_kind_t;

typedef struct {
  bebop_decorated_kind_t kind;

  union {
    bebop_def_t* def;
    bebop_field_t* field;
    bebop_method_t* method;
    bebop_union_branch_t* branch;
    bebop_enum_member_t* enum_member;
  };
} bebop_decorated_t;

typedef struct {
  bebop_decorator_target_t flag;
  bebop_decorated_t target;
} bebop__decor_target_t;

#define BEBOP_LUA_NOREF (-2)

struct bebop_macro_decorator_def;

typedef struct bebop_macro_param_def {
  bebop_str_t name;
  bebop_str_t description;
  bebop_type_kind_t type;
  bool required;
  bebop_literal_t* default_value;
  bebop_literal_t* allowed_values;
  uint32_t allowed_value_count;
  bebop_span_t span;
} bebop_macro_param_def_t;

struct bebop_type {
  bebop_type_kind_t kind;
  bebop_span_t span;

  union {
    struct {
      bebop_type_t* element;
    } array;

    struct {
      bebop_type_t* element;
      uint32_t size;
    } fixed_array;

    struct {
      bebop_type_t* key;
      bebop_type_t* value;
    } map;

    struct {
      bebop_str_t name;
      bebop_def_t* resolved;
    } defined;
  };
};

struct bebop_field {
  bebop_str_t name;
  bebop_span_t span;
  bebop_span_t name_span;
  bebop_str_t documentation;
  bebop_decorator_t* decorators;
  bebop_type_t* type;
  uint32_t index;
  bebop_def_t* parent;
};

struct bebop_enum_member {
  bebop_str_t name;
  bebop_span_t span;
  bebop_span_t name_span;
  bebop_span_t value_span;
  bebop_str_t value_expr;
  bebop_str_t documentation;
  bebop_decorator_t* decorators;
  uint64_t value;
  bebop_def_t* parent;
};

struct bebop_union_branch {
  uint8_t discriminator;
  bebop_span_t span;
  bebop_str_t documentation;
  bebop_decorator_t* decorators;
  bebop_def_t* def;
  bebop_type_t* type_ref;
  bebop_str_t name;
  bebop_span_t name_span;
  bebop_def_t* parent;
};

struct bebop_method {
  bebop_str_t name;
  bebop_span_t span;
  bebop_span_t name_span;
  bebop_str_t documentation;
  bebop_decorator_t* decorators;
  bebop_type_t* request_type;
  bebop_type_t* response_type;
  bebop_method_type_t method_type;
  uint32_t id;
  bebop_def_t* parent;
};

struct bebop_def {
  bebop_def_kind_t kind;
  bebop_str_t name;
  bebop_str_t fqn;
  bebop_span_t span;
  bebop_span_t name_span;
  bebop_str_t documentation;
  bebop_decorator_t* decorators;
  bebop_def_t* next;
  struct bebop_schema* schema;
  bebop_def_t* parent;

  bebop_visibility_t visibility;

  bebop_def_t* nested_defs;
  bebop_def_t* nested_defs_tail;
  uint32_t nested_def_count;

  bebop_def_t** dependents;
  uint32_t dependents_count;
  uint32_t dependents_capacity;
  bebop_span_t* references;
  uint32_t references_count;
  uint32_t references_capacity;

  union {
    struct {
      bebop_type_kind_t base_type;
      bebop_enum_member_t* members;
      uint32_t member_count;
    } enum_def;

    struct {
      bebop_field_t* fields;
      uint32_t field_count;
      bool is_mutable;
      uint32_t fixed_size;
    } struct_def;

    struct {
      bebop_field_t* fields;
      uint32_t field_count;
    } message_def;

    struct {
      bebop_union_branch_t* branches;
      uint32_t branch_count;
    } union_def;

    struct {
      bebop_method_t* methods;
      uint32_t method_count;
      bebop_type_t** mixins;
      uint32_t mixin_count;
    } service_def;

    struct {
      bebop_type_t* type;
      bebop_literal_t value;
    } const_def;

    struct {
      bebop_decorator_target_t targets;
      bool allow_multiple;
      bebop_macro_param_def_t* params;
      uint32_t param_count;
      bebop_span_t validate_span;
      bebop_span_t export_span;
      int validate_ref;
      int export_ref;
    } decorator_def;
  };
};

typedef enum {
  BEBOP_TRIVIA_WHITESPACE,
  BEBOP_TRIVIA_NEWLINE,
  BEBOP_TRIVIA_LINE_COMMENT,
  BEBOP_TRIVIA_BLOCK_COMMENT,
  BEBOP_TRIVIA_DOC_COMMENT,
} bebop_trivia_kind_t;

typedef struct {
  bebop_trivia_kind_t kind;
  bebop_span_t span;
} bebop_trivia_t;

typedef struct {
  bebop_trivia_t* items;
  uint32_t count;
} bebop_trivia_list_t;

typedef struct {
  bebop_token_kind_t kind;
  bebop_span_t span;
  bebop_str_t lexeme;
  bebop_trivia_list_t leading;
  bebop_trivia_list_t trailing;
} bebop_token_t;

typedef struct {
  bebop_token_t* tokens;
  uint32_t count;
} bebop_token_stream_t;

bebop_token_stream_t bebop_scan(bebop_context_t* ctx, const char* source, size_t len);

bebop_token_stream_t bebop__scan_with_schema(
    bebop_context_t* ctx, const char* source, size_t len, bebop_schema_t* schema
);
const char* bebop_trivia_kind_name(bebop_trivia_kind_t kind);

typedef struct {
  bebop_str_t path;
  bebop_span_t span;
  const char* resolved_path;
  bebop_schema_t* schema;
} bebop_import_t;

typedef enum {
  BEBOP_SCHEMA_PARSED = 0,
  BEBOP_SCHEMA_VALIDATED,
} bebop_schema_state_t;

struct bebop_schema {
  bebop_context_t* ctx;
  struct bebop_parse_result* result;

  const char* path;
  const char* source;
  size_t source_len;

  bebop_token_stream_t tokens;

  bebop_edition_t edition;

  bebop_str_t package;
  bebop_span_t package_span;

  bebop_def_t* definitions;
  bebop_def_t* definitions_tail;
  uint32_t definition_count;

  bebop_defmap def_table;

  bebop_def_t** sorted_defs;
  uint32_t sorted_defs_count;
  uint32_t sorted_defs_capacity;

  bebop_import_t* imports;
  uint32_t import_count;
  uint32_t import_capacity;

  bebop_diagnostic_t* diagnostics;
  uint32_t diagnostic_count;
  uint32_t diagnostic_capacity;
  uint32_t error_count;
  uint32_t warning_count;

  bebop_schema_state_t state;
};

bebop_schema_t* bebop__schema_create(
    bebop_context_t* ctx, const char* path, const char* source, size_t len
);
void bebop__schema_add_def(bebop_schema_t* schema, bebop_def_t* def);
void bebop__schema_add_import(bebop_schema_t* schema, bebop_str_t path, bebop_span_t span);
void bebop__schema_add_diagnostic(
    bebop_schema_t* schema, bebop__diag_loc_t loc, const char* message, const char* hint
);
void bebop__schema_diag_add_label(bebop_schema_t* schema, bebop_span_t span, const char* message);
bebop_def_t* bebop__schema_find_def(bebop_schema_t* schema, bebop_str_t name);

bool bebop__schema_has_visibility(bebop_schema_t* source, bebop_schema_t* target);

const char* bebop__lua_wrap_function(
    bebop_arena_t* arena,
    bebop__str_view_t source,
    const char* const* params,
    uint32_t param_count
);

typedef struct bebop_lua_state bebop_lua_state_t;

bebop_lua_state_t* bebop__lua_state_create(bebop_context_t* ctx);

void bebop__lua_state_destroy(bebop_lua_state_t* state);

void bebop__lua_compile_decorators(bebop_lua_state_t* state, bebop_parse_result_t* result);

bebop_status_t bebop__lua_run_validate(
    bebop_lua_state_t* state,
    bebop_def_t* decorator_def,
    const bebop_decorator_t* usage,
    bebop_decorated_t target
);

bebop_status_t bebop__lua_run_export(
    bebop_lua_state_t* state, bebop_def_t* decorator_def, bebop_decorator_t* usage
);

void bebop__schema_register_def(bebop_schema_t* schema, bebop_def_t* def);

void bebop__def_add_nested(bebop_def_t* parent, bebop_def_t* nested);

bebop_def_t* bebop__def_find_nested(bebop_def_t* parent, bebop_str_t name);

bool bebop__def_is_accessible(const bebop_def_t* def);

bebop_str_t bebop__def_compute_fqn(bebop_def_t* def);

struct bebop_parse_result {
  bebop_context_t* ctx;

  bebop_schema_t** schemas;
  uint32_t schema_count;
  uint32_t schema_capacity;

  bebop_def_t** all_defs;
  uint32_t all_def_count;

  bebop_defmap def_table;

  uint32_t total_error_count;
  uint32_t total_warning_count;
};

bebop_parse_result_t* bebop__result_create(bebop_context_t* ctx);
void bebop__result_add_schema(bebop_parse_result_t* result, bebop_schema_t* schema);

bebop_def_t* bebop__result_find_def(bebop_parse_result_t* result, const char* name);

bebop_def_t* bebop__result_resolve_type(
    bebop_parse_result_t* result,
    bebop_schema_t* schema,
    bebop_def_t* context_def,
    const char* name,
    bebop_def_t** ambiguous_with
);

typedef struct bebop_sema bebop_sema_t;

typedef enum {
  BEBOP_PARSER_OK = 0,
  BEBOP_PARSER_HAD_ERROR = 1 << 0,
  BEBOP_PARSER_PANIC_MODE = 1 << 1,
  BEBOP_PARSER_FATAL = 1 << 2,
} bebop_parser_flags_t;

typedef enum {
  BEBOP_PREAMBLE_START = 0,
  BEBOP_PREAMBLE_EDITION,
  BEBOP_PREAMBLE_PACKAGE,
  BEBOP_PREAMBLE_IMPORTS,
  BEBOP_PREAMBLE_DONE,
} bebop_preamble_state_t;

typedef struct {
  bebop_context_t* ctx;
  bebop_schema_t* schema;
  bebop_token_stream_t stream;
  uint32_t current;
  const char* source;
  size_t source_len;
  bebop_parser_flags_t flags;
  bebop_sema_t* sema;
  bebop_preamble_state_t preamble_state;
} bebop_parser_t;

bebop_schema_t* bebop__parse_tokens(
    bebop_context_t* ctx, bebop_token_stream_t stream, const bebop_source_t* source
);
void bebop__parse_tokens_into(
    bebop_context_t* ctx, bebop_token_stream_t stream, bebop_schema_t* schema
);

typedef struct bebop_sema {
  bebop_context_t* ctx;
  bebop_schema_t* schema;

  bebop_def_t* current_def;

  bebop_defmap name_scope;

  uint8_t seen_indices[256];
  bebop_span_t seen_index_spans[256];
} bebop_sema_t;

bool bebop_sema_init(bebop_sema_t* sema, bebop_context_t* ctx, bebop_schema_t* schema);

void bebop_sema_enter_def(bebop_sema_t* sema, bebop_def_t* def);
void bebop_sema_exit_def(bebop_sema_t* sema);

bool bebop_sema_check_duplicate_name(bebop_sema_t* sema, bebop_str_t name, bebop_span_t span);
bool bebop_sema_check_field_index(bebop_sema_t* sema, uint32_t index, bebop_span_t span);
bool bebop_sema_check_map_key_type(
    const bebop_sema_t* sema, bebop_type_t* key_type, bebop_span_t span
);
bool bebop_sema_check_self_reference(
    const bebop_sema_t* sema, bebop_type_t* type, bebop_span_t span
);
bool bebop_sema_check_service_type(const bebop_sema_t* sema, bebop_type_t* type, bebop_span_t span);
bool bebop_sema_check_enum_member(
    const bebop_sema_t* sema, const bebop_enum_member_t* member, const bebop_def_t* enum_def
);
bool bebop_sema_check_enum_complete(const bebop_sema_t* sema, const bebop_def_t* enum_def);

bebop_status_t bebop_validate(bebop_parse_result_t* result);

const char* bebop_status_message(const bebop_status_t status)
{
  switch (status) {
    case BEBOP_OK:
      return "ok";
    case BEBOP_OK_WITH_WARNINGS:
      return "ok with warnings";
    case BEBOP_ERROR:
      return "error";
    case BEBOP_FATAL:
      return "fatal error";
  }
  return "unknown status";
}
#ifndef CLANGD_TU_bebop_arena
#include "bebop_arena.c"
#endif
#ifndef CLANGD_TU_bebop_str
#include "bebop_str.c"
#endif
#ifndef CLANGD_TU_bebop_utils
#include "bebop_utils.c"
#endif

bebop_version_t bebop_version(void)
{
  return (bebop_version_t) {.major = BEBOP_VERSION_MAJOR,
                            .minor = BEBOP_VERSION_MINOR,
                            .patch = BEBOP_VERSION_PATCH,
                            .suffix = BEBOP_VERSION_SUFFIX};
}

const char* bebop_version_string(void)
{
  return BEBOP_VERSION_STRING;
}

bebop_context_t* bebop_context_create(const bebop_host_t* host)
{
  BEBOP_ASSERT(host != NULL && "Host configuration is required");
  BEBOP_ASSERT(host->allocator.alloc != NULL && "Allocator.alloc is required");
  BEBOP_ASSERT(host->file_reader.read != NULL && "File reader is required");
  BEBOP_ASSERT(host->file_reader.exists != NULL && "File exists checker is required");

  bebop_context_t* ctx =
      host->allocator.alloc(NULL, 0, sizeof(bebop_context_t), host->allocator.ctx);
  if (!ctx) {
    return NULL;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->host = *host;

  if (!bebop_arena_init(&ctx->arena, &ctx->host.allocator, 0)) {
    ctx->host.allocator.alloc(ctx, sizeof(bebop_context_t), 0, ctx->host.allocator.ctx);
    return NULL;
  }

  if (!bebop_intern_init(&ctx->intern, &ctx->arena, 0)) {
    bebop_arena_destroy(&ctx->arena);
    ctx->host.allocator.alloc(ctx, sizeof(bebop_context_t), 0, ctx->host.allocator.ctx);
    return NULL;
  }

  return ctx;
}

void bebop_context_destroy(bebop_context_t* ctx)
{
  if (!ctx) {
    return;
  }

  const bebop_host_allocator_t alloc = ctx->host.allocator;

  bebop_arena_destroy(&ctx->arena);

  alloc.alloc(ctx, sizeof(bebop_context_t), 0, alloc.ctx);
}

bebop_error_t bebop_context_last_error(const bebop_context_t* ctx)
{
  if (!ctx) {
    return BEBOP_ERR_NONE;
  }
  return ctx->last_error;
}

const char* bebop_context_error_message(const bebop_context_t* ctx)
{
  if (!ctx) {
    return NULL;
  }
  return ctx->error_message;
}

void bebop_context_clear_error(bebop_context_t* ctx)
{
  if (!ctx) {
    return;
  }
  ctx->last_error = BEBOP_ERR_NONE;
  ctx->error_message = NULL;
}

const char* bebop_context_get_option(const bebop_context_t* ctx, const char* key)
{
  if (!ctx || !key) {
    return NULL;
  }
  for (uint32_t i = 0; i < ctx->host.options.count; i++) {
    if (bebop__strcmp(ctx->host.options.entries[i].key, key) == 0) {
      return ctx->host.options.entries[i].value;
    }
  }
  return NULL;
}

void bebop__context_set_error(bebop_context_t* ctx, const bebop_error_t error, const char* message)
{
  BEBOP_ASSERT(ctx != NULL);

  ctx->last_error = error;
  if (message) {
    ctx->error_message = bebop_arena_strdup(&ctx->arena, message);
  } else {
    ctx->error_message = NULL;
  }
}

#ifndef CLANGD_TU_bebop_schema
#include "bebop_schema.c"
#endif
#ifndef CLANGD_TU_bebop_scanner
#include "bebop_scanner.c"
#endif
#ifndef CLANGD_TU_bebop_sema
#include "bebop_sema.c"
#endif
#ifndef CLANGD_TU_bebop_result
#include "bebop_result.c"
#endif
#ifndef CLANGD_TU_bebop_parser
#include "bebop_parser.c"
#endif
#ifndef CLANGD_TU_bebop_macro
#include "bebop_macro.c"
#endif
#ifndef CLANGD_TU_bebop_emit
#include "bebop_emit.c"
#endif
#ifndef CLANGD_TU_bebop_validate
#include "bebop_validate.c"
#endif
#ifndef CLANGD_TU_bebop_wire
#include "bebop_wire.c"
#endif
#ifndef CLANGD_TU_bebop_descriptor
#include "bebop_descriptor.c"
#endif
#include "generated/descriptor.bb.c"
#include "generated/plugin.bb.c"
#ifndef CLANGD_TU_bebop_plugin
#include "bebop_plugin.c"
#endif

typedef struct {
  const char* path;
  const char* source;
  size_t len;
} bebop__work_item_t;

typedef struct {
  bebop_context_t* ctx;
  bebop_parse_result_t* result;
  bebop__work_item_t* items;
  size_t count;
  size_t capacity;
  size_t head;
} bebop__work_list_t;

static bool bebop__normalize_path(char* path, size_t size, int* escape_count)
{
  if (!path || size == 0 || path[0] == '\0') {
    return false;
  }

  bool is_absolute = (path[0] == '/');
  char* parts[PATH_MAX / 2];
  int part_count = 0;
  int escapes = 0;
  char* p = path;

  while (*p && part_count < PATH_MAX / 2) {
    while (*p == '/') {
      p++;
    }
    if (*p == '\0') {
      break;
    }

    char* start = p;
    while (*p && *p != '/') {
      p++;
    }

    const size_t len = (size_t)(p - start);

    if (len == 1 && start[0] == '.') {
      if (*p == '/') {
        p++;
      }
      continue;
    }
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      if (part_count > 0) {
        part_count--;
      } else {
        escapes++;
      }
      if (*p == '/') {
        p++;
      }
      continue;
    }

    parts[part_count++] = start;
    if (*p == '/') {
      *p = '\0';
      p++;
    }
  }

  if (escape_count) {
    *escape_count = escapes;
  }

  char* out = path;
  if (is_absolute) {
    *out++ = '/';
  }

  for (int i = 0; i < part_count; i++) {
    if (i > 0) {
      *out++ = '/';
    }
    const size_t len = strlen(parts[i]);
    memmove(out, parts[i], len);
    out += len;
  }

  if (out == path) {
    *out++ = is_absolute ? '/' : '.';
  }
  *out = '\0';

  return true;
}

static bool bebop__paths_equal(const char* a, const char* b)
{
  if (bebop__streq(a, b)) {
    return true;
  }
  if (!a || !b) {
    return false;
  }

  char norm_a[PATH_MAX], norm_b[PATH_MAX];
  snprintf(norm_a, sizeof(norm_a), "%s", a);
  snprintf(norm_b, sizeof(norm_b), "%s", b);

  int esc_a = 0, esc_b = 0;
  bebop__normalize_path(norm_a, sizeof(norm_a), &esc_a);
  bebop__normalize_path(norm_b, sizeof(norm_b), &esc_b);

  return bebop__streq(norm_a, norm_b);
}

static bool bebop__path_seen(const bebop__work_list_t* wl, const char* path)
{
  for (uint32_t i = 0; i < wl->result->schema_count; i++) {
    if (wl->result->schemas[i]->path && bebop__paths_equal(wl->result->schemas[i]->path, path)) {
      return true;
    }
  }
  for (size_t i = wl->head; i < wl->count; i++) {
    if (bebop__paths_equal(wl->items[i].path, path)) {
      return true;
    }
  }
  return false;
}

static bool bebop__work_list_grow(bebop__work_list_t* wl)
{
  if (wl->count < wl->capacity) {
    return true;
  }

  const size_t new_capacity = wl->capacity * 2;
  bebop__work_item_t* new_items =
      bebop_arena_new(BEBOP_ARENA(wl->ctx), bebop__work_item_t, new_capacity);
  if (!new_items) {
    return false;
  }

  memcpy(new_items, wl->items, sizeof(bebop__work_item_t) * wl->count);
  wl->items = new_items;
  wl->capacity = new_capacity;
  return true;
}

static bool bebop__work_list_add(
    bebop__work_list_t* wl, const char* path, const char* source, const size_t len
)
{
  if (!bebop__work_list_grow(wl)) {
    return false;
  }

  wl->items[wl->count++] = (bebop__work_item_t) {
      .path = path,
      .source = source,
      .len = len,
  };
  return true;
}

static bool bebop__process_imports(bebop__work_list_t* wl, bebop_schema_t* schema)
{
  bebop_context_t* ctx = wl->ctx;
  const bebop_file_reader_t* fr = &ctx->host.file_reader;
  const bebop_includes_t* inc = &ctx->host.includes;
  const bebop_host_alloc_fn host_alloc = ctx->host.allocator.alloc;
  void* alloc_ctx = ctx->host.allocator.ctx;

  for (uint32_t i = 0; i < schema->import_count; i++) {
    bebop_import_t* imp = &schema->imports[i];
    const char* import_path = BEBOP_STR(ctx, imp->path);
    if (!import_path) {
      continue;
    }

    // Reject paths with traversal components (. or ..)
    bool has_traversal = false;
    const char* s = import_path;
    while (*s) {
      // Check for . or .. at start of path or after /
      if (s == import_path || *(s - 1) == '/') {
        if (s[0] == '.' && (s[1] == '/' || s[1] == '\0')) {
          has_traversal = true;
          break;
        }
        if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == '\0')) {
          has_traversal = true;
          break;
        }
      }
      s++;
    }
    if (has_traversal) {
      char msg[512];
      snprintf(msg, sizeof(msg), "Import '%s' contains path traversal", import_path);
      bebop__schema_add_diagnostic(
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_IMPORT_NOT_FOUND, imp->span},
          msg,
          "imports cannot contain '.' or '..' components"
      );
      continue;
    }

    const char* resolved = NULL;
    char path_buf[PATH_MAX];

    for (uint32_t p = 0; p < inc->count; p++) {
      snprintf(path_buf, sizeof(path_buf), "%s/%s", inc->paths[p], import_path);
      int escape_count = 0;
      bebop__normalize_path(path_buf, sizeof(path_buf), &escape_count);

      if (escape_count > 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Import '%s' contains path traversal", import_path);
        bebop__schema_add_diagnostic(
            schema,
            (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_IMPORT_NOT_FOUND, imp->span},
            msg,
            "import paths cannot use '..' components"
        );
        break;
      }

      if (fr->exists(path_buf, fr->ctx)) {
        resolved = path_buf;
        break;
      }
    }

    if (!resolved) {
      char msg[512];
      snprintf(msg, sizeof(msg), "Cannot resolve import '%s'", import_path);

      char hint[1024];
      int hint_len = 0;
      if (inc->count > 0) {
        hint_len = snprintf(hint, sizeof(hint), "searched in: ");
        for (uint32_t p = 0; p < inc->count && hint_len < (int)sizeof(hint) - 1; p++) {
          hint_len += snprintf(
              hint + hint_len,
              sizeof(hint) - (size_t)hint_len,
              "%s%s",
              p > 0 ? ", " : "",
              inc->paths[p]
          );
        }
      } else {
        snprintf(
            hint, sizeof(hint), "no include paths configured (use -I or 'include:' in config)"
        );
      }
      bebop__schema_add_diagnostic(
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_IMPORT_NOT_FOUND, imp->span},
          msg,
          hint
      );
      continue;
    }

    imp->resolved_path = bebop_arena_strdup(BEBOP_ARENA(ctx), resolved);

    if (bebop__path_seen(wl, resolved)) {
      continue;
    }

    const bebop_file_result_t file = fr->read(resolved, fr->ctx);
    if (file.error) {
      char msg[512];
      snprintf(msg, sizeof(msg), "Cannot read import '%.200s': %.200s", resolved, file.error);
      bebop__schema_add_diagnostic(
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_IMPORT_NOT_FOUND, imp->span},
          msg,
          NULL
      );
      continue;
    }

    const char* path_copy = imp->resolved_path;
    const char* source_copy = bebop_arena_strndup(BEBOP_ARENA(ctx), file.content, file.content_len);

    host_alloc(file.content, file.content_len, 0, alloc_ctx);

    if (!path_copy || !source_copy) {
      bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to copy import data");
      return false;
    }

    if (!bebop__work_list_add(wl, path_copy, source_copy, file.content_len)) {
      bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to add import to work list");
      return false;
    }
  }
  return true;
}

static bebop_status_t bebop__parse_impl(bebop__work_list_t* wl)
{
  bebop_context_t* ctx = wl->ctx;
  bebop_parse_result_t* result = wl->result;
  while (wl->head < wl->count) {
    const bebop__work_item_t item = wl->items[wl->head++];

    if (bebop__path_seen(wl, item.path)) {
      continue;
    }

    bebop_schema_t* schema = bebop__schema_create(ctx, item.path, item.source, item.len);
    if (!schema) {
      bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate schema");
      return BEBOP_FATAL;
    }

    const bebop_token_stream_t stream = bebop__scan_with_schema(ctx, item.source, item.len, schema);

    if (bebop_context_last_error(ctx) != BEBOP_ERR_NONE) {
      return BEBOP_FATAL;
    }

    bebop__parse_tokens_into(ctx, stream, schema);

    if (bebop_context_last_error(ctx) != BEBOP_ERR_NONE) {
      return BEBOP_FATAL;
    }

    bebop__result_add_schema(result, schema);

    if (!bebop__process_imports(wl, schema)) {
      return BEBOP_FATAL;
    }
  }

  result->total_error_count = 0;
  result->total_warning_count = 0;
  for (uint32_t i = 0; i < result->schema_count; i++) {
    result->total_error_count += result->schemas[i]->error_count;
    result->total_warning_count += result->schemas[i]->warning_count;
  }

  if (result->total_error_count > 0) {
    return BEBOP_ERROR;
  }

  return bebop_validate(result);
}

bebop_status_t bebop_parse_source(
    bebop_context_t* ctx, const bebop_source_t* source, bebop_parse_result_t** out
)
{
  BEBOP_ASSERT(ctx != NULL);
  BEBOP_ASSERT(out != NULL);
  *out = NULL;

  bebop_parse_result_t* result = bebop__result_create(ctx);
  if (!result) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate parse result");
    return BEBOP_FATAL;
  }

  bebop__work_list_t wl = {
      .ctx = ctx,
      .result = result,
      .items = bebop_arena_new(BEBOP_ARENA(ctx), bebop__work_item_t, 16),
      .count = 0,
      .capacity = 16,
      .head = 0,
  };

  if (!wl.items) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate work list");
    return BEBOP_FATAL;
  }

  wl.items[wl.count++] =
      (bebop__work_item_t) {.path = source->path, .source = source->source, .len = source->len};

  const bebop_status_t status = bebop__parse_impl(&wl);
  *out = result;
  return status;
}

bebop_status_t bebop_parse_sources(
    bebop_context_t* ctx,
    const bebop_source_t* sources,
    const size_t count,
    bebop_parse_result_t** out
)
{
  BEBOP_ASSERT(ctx != NULL);
  BEBOP_ASSERT(out != NULL);
  *out = NULL;

  if (count == 0) {
    bebop__context_set_error(ctx, BEBOP_ERR_INTERNAL, "No sources provided");
    return BEBOP_FATAL;
  }

  bebop_parse_result_t* result = bebop__result_create(ctx);
  if (!result) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate parse result");
    return BEBOP_FATAL;
  }

  bebop__work_list_t wl = {
      .ctx = ctx,
      .result = result,
      .items = bebop_arena_new(BEBOP_ARENA(ctx), bebop__work_item_t, count + 16),
      .count = 0,
      .capacity = count + 16,
      .head = 0,
  };

  if (!wl.items) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate work list");
    return BEBOP_FATAL;
  }

  for (size_t i = 0; i < count; i++) {
    const char* source_copy =
        bebop_arena_strndup(BEBOP_ARENA(ctx), sources[i].source, sources[i].len);
    if (!source_copy) {
      bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to copy source content");
      return BEBOP_FATAL;
    }
    wl.items[wl.count++] = (bebop__work_item_t) {
        .path = sources[i].path,
        .source = source_copy,
        .len = sources[i].len,
    };
  }

  const bebop_status_t status = bebop__parse_impl(&wl);
  *out = result;
  return status;
}

bebop_status_t bebop_parse(
    bebop_context_t* ctx, const char** paths, size_t path_count, bebop_parse_result_t** out
)
{
  BEBOP_ASSERT(ctx != NULL);
  BEBOP_ASSERT(out != NULL);
  *out = NULL;

  if (path_count == 0) {
    bebop__context_set_error(ctx, BEBOP_ERR_INTERNAL, "No paths provided");
    return BEBOP_FATAL;
  }

  bebop_parse_result_t* result = bebop__result_create(ctx);
  if (!result) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate parse result");
    return BEBOP_FATAL;
  }

  const bebop_file_reader_fn reader = ctx->host.file_reader.read;
  void* reader_ctx = ctx->host.file_reader.ctx;
  const bebop_host_alloc_fn host_alloc = ctx->host.allocator.alloc;
  void* alloc_ctx = ctx->host.allocator.ctx;

  bebop__work_list_t wl = {
      .ctx = ctx,
      .result = result,
      .items = bebop_arena_new(BEBOP_ARENA(ctx), bebop__work_item_t, path_count + 16),
      .count = 0,
      .capacity = path_count + 16,
      .head = 0,
  };

  if (!wl.items) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate work list");
    return BEBOP_FATAL;
  }

  for (size_t i = 0; i < path_count; i++) {
    if (bebop__path_seen(&wl, paths[i])) {
      continue;
    }

    const bebop_file_result_t file = reader(paths[i], reader_ctx);
    if (file.error) {
      bebop__context_set_error(ctx, BEBOP_ERR_FILE_NOT_FOUND, file.error);
      return BEBOP_FATAL;
    }

    char* source_copy = bebop_arena_strndup(BEBOP_ARENA(ctx), file.content, file.content_len);
    host_alloc(file.content, file.content_len, 0, alloc_ctx);

    if (!source_copy) {
      bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to copy file content");
      return BEBOP_FATAL;
    }

    if (!bebop__work_list_add(&wl, paths[i], source_copy, file.content_len)) {
      bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to add file to work list");
      return BEBOP_FATAL;
    }
  }

  const bebop_status_t status = bebop__parse_impl(&wl);
  *out = result;
  return status;
}

uint32_t bebop_result_schema_count(const bebop_parse_result_t* result)
{
  return result ? result->schema_count : 0;
}

const bebop_schema_t* bebop_result_schema_at(const bebop_parse_result_t* result, const uint32_t idx)
{
  if (!result || idx >= result->schema_count) {
    return NULL;
  }
  return result->schemas[idx];
}

uint32_t bebop_result_error_count(const bebop_parse_result_t* result)
{
  return result ? result->total_error_count : 0;
}

uint32_t bebop_result_warning_count(const bebop_parse_result_t* result)
{
  return result ? result->total_warning_count : 0;
}

uint32_t bebop_result_diagnostic_count(const bebop_parse_result_t* result)
{
  if (!result) {
    return 0;
  }
  uint32_t count = 0;
  for (uint32_t i = 0; i < result->schema_count; i++) {
    count += result->schemas[i]->diagnostic_count;
  }
  return count;
}

const bebop_diagnostic_t* bebop_result_diagnostic_at(
    const bebop_parse_result_t* result, const uint32_t idx
)
{
  if (!result) {
    return NULL;
  }
  uint32_t offset = 0;
  for (uint32_t i = 0; i < result->schema_count; i++) {
    const bebop_schema_t* schema = result->schemas[i];
    if (idx < offset + schema->diagnostic_count) {
      return &schema->diagnostics[idx - offset];
    }
    offset += schema->diagnostic_count;
  }
  return NULL;
}

bebop_diag_severity_t bebop_diagnostic_severity(const bebop_diagnostic_t* diag)
{
  return diag ? diag->severity : BEBOP_DIAG_ERROR;
}

uint32_t bebop_diagnostic_code(const bebop_diagnostic_t* diag)
{
  return diag ? diag->code : 0;
}

bebop_span_t bebop_diagnostic_span(const bebop_diagnostic_t* diag)
{
  return diag ? diag->span : BEBOP_SPAN_INVALID;
}

const char* bebop_diagnostic_path(const bebop_diagnostic_t* diag)
{
  return diag ? diag->path : NULL;
}

const char* bebop_diagnostic_message(const bebop_diagnostic_t* diag)
{
  return diag ? diag->message : NULL;
}

const char* bebop_diagnostic_hint(const bebop_diagnostic_t* diag)
{
  return diag ? diag->hint : NULL;
}

uint32_t bebop_diagnostic_label_count(const bebop_diagnostic_t* diag)
{
  return diag ? diag->label_count : 0;
}

bebop_span_t bebop_diagnostic_label_span(const bebop_diagnostic_t* diag, uint32_t idx)
{
  if (!diag || idx >= diag->label_count) {
    return BEBOP_SPAN_INVALID;
  }
  return diag->labels[idx].span;
}

const char* bebop_diagnostic_label_message(const bebop_diagnostic_t* diag, uint32_t idx)
{
  if (!diag || idx >= diag->label_count) {
    return NULL;
  }
  return diag->labels[idx].message;
}

const char* bebop_diag_severity_name(const bebop_diag_severity_t sev)
{
  switch (sev) {
#define X(name, str) \
  case BEBOP_DIAG_##name: \
    return str;
    BEBOP_DIAG_SEVERITIES(X)
#undef X
  }
  BEBOP_UNREACHABLE();
}

const bebop_def_t* bebop_result_find(const bebop_parse_result_t* result, const char* name)
{
  if (!result || !name) {
    return NULL;
  }

  return bebop__result_find_def(BEBOP_DISCARD_CONST(bebop_parse_result_t*, result), name);
}

const char* bebop_schema_path(const bebop_schema_t* schema)
{
  return schema ? schema->path : NULL;
}

uint32_t bebop_schema_definition_count(const bebop_schema_t* schema)
{
  return schema ? schema->definition_count : 0;
}

const bebop_def_t* bebop_schema_definition_at(const bebop_schema_t* schema, const uint32_t idx)
{
  if (!schema || idx >= schema->definition_count) {
    return NULL;
  }
  BEBOP_ASSERT(schema->sorted_defs && "Schema must be validated before accessing definitions");
  return schema->sorted_defs[idx];
}

bebop_edition_t bebop_schema_edition(const bebop_schema_t* schema)
{
  return schema ? schema->edition : BEBOP_ED_2026;
}

const char* bebop_schema_package(const bebop_schema_t* schema)
{
  if (!schema || bebop_str_is_null(schema->package)) {
    return NULL;
  }
  return BEBOP_STR(schema->ctx, schema->package);
}

bebop_def_kind_t bebop_def_kind(const bebop_def_t* def)
{
  return def ? def->kind : BEBOP_DEF_STRUCT;
}

const char* bebop_def_name(const bebop_def_t* def)
{
  if (!def || bebop_str_is_null(def->name)) {
    return NULL;
  }
  return BEBOP_STR(def->schema->ctx, def->name);
}

const char* bebop_def_fqn(const bebop_def_t* def)
{
  if (!def || bebop_str_is_null(def->fqn)) {
    return NULL;
  }
  return BEBOP_STR(def->schema->ctx, def->fqn);
}

bebop_span_t bebop_def_span(const bebop_def_t* def)
{
  return def ? def->span : (bebop_span_t) {0};
}

bebop_span_t bebop_def_name_span(const bebop_def_t* def)
{
  return def ? def->name_span : (bebop_span_t) {0};
}

const char* bebop_def_documentation(const bebop_def_t* def)
{
  if (!def || bebop_str_is_null(def->documentation)) {
    return NULL;
  }
  return BEBOP_STR(def->schema->ctx, def->documentation);
}

uint32_t bebop_def_decorator_count(const bebop_def_t* def)
{
  if (!def) {
    return 0;
  }
  uint32_t count = 0;
  for (const bebop_decorator_t* d = def->decorators; d; d = d->next) {
    count++;
  }
  return count;
}

const bebop_decorator_t* bebop_def_decorator_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def) {
    return NULL;
  }
  const bebop_decorator_t* d = def->decorators;
  for (uint32_t i = 0; i < idx && d; i++) {
    d = d->next;
  }
  return d;
}

const bebop_decorator_t* bebop_def_decorator_find(const bebop_def_t* def, const char* name)
{
  if (!def || !name || !def->schema) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(def->schema->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }
  for (const bebop_decorator_t* d = def->decorators; d; d = d->next) {
    if (bebop_str_eq(d->name, interned)) {
      return d;
    }
  }
  return NULL;
}

const bebop_def_t* bebop_def_parent(const bebop_def_t* def)
{
  return def ? def->parent : NULL;
}

bool bebop_def_is_accessible(const bebop_def_t* def)
{
  return bebop__def_is_accessible(def);
}

uint32_t bebop_def_nested_count(const bebop_def_t* def)
{
  return def ? def->nested_def_count : 0;
}

const bebop_def_t* bebop_def_nested_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def) {
    return NULL;
  }
  const bebop_def_t* nested = def->nested_defs;
  for (uint32_t i = 0; i < idx && nested; i++) {
    nested = nested->next;
  }
  return nested;
}

const bebop_def_t* bebop_def_nested_find(const bebop_def_t* def, const char* name)
{
  if (!def || !name) {
    return NULL;
  }
  const bebop_str_t name_str = bebop_intern(BEBOP_INTERN(def->schema->ctx), name);
  return bebop__def_find_nested(BEBOP_DISCARD_CONST(bebop_def_t*, def), name_str);
}

uint32_t bebop_def_field_count(const bebop_def_t* def)
{
  if (!def) {
    return 0;
  }
  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
      return def->struct_def.field_count;
    case BEBOP_DEF_MESSAGE:
      return def->message_def.field_count;
    default:
      return 0;
  }
}

const bebop_field_t* bebop_def_field_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def) {
    return NULL;
  }
  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
      if (idx >= def->struct_def.field_count) {
        return NULL;
      }
      return &def->struct_def.fields[idx];
    case BEBOP_DEF_MESSAGE:
      if (idx >= def->message_def.field_count) {
        return NULL;
      }
      return &def->message_def.fields[idx];
    default:
      return NULL;
  }
}

uint32_t bebop_def_member_count(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_ENUM) {
    return 0;
  }
  return def->enum_def.member_count;
}

const bebop_enum_member_t* bebop_def_member_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_ENUM) {
    return NULL;
  }
  if (idx >= def->enum_def.member_count) {
    return NULL;
  }
  return &def->enum_def.members[idx];
}

uint32_t bebop_def_branch_count(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_UNION) {
    return 0;
  }
  return def->union_def.branch_count;
}

const bebop_union_branch_t* bebop_def_branch_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_UNION) {
    return NULL;
  }
  if (idx >= def->union_def.branch_count) {
    return NULL;
  }
  return &def->union_def.branches[idx];
}

bool bebop_def_is_mutable(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_STRUCT) {
    return false;
  }
  return def->struct_def.is_mutable;
}

uint32_t bebop_def_method_count(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_SERVICE) {
    return 0;
  }
  return def->service_def.method_count;
}

const bebop_method_t* bebop_def_method_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_SERVICE) {
    return NULL;
  }
  if (idx >= def->service_def.method_count) {
    return NULL;
  }
  return &def->service_def.methods[idx];
}

const bebop_type_t* bebop_method_request_type(const bebop_method_t* method)
{
  return method ? method->request_type : NULL;
}

const bebop_type_t* bebop_method_response_type(const bebop_method_t* method)
{
  return method ? method->response_type : NULL;
}

bebop_method_type_t bebop_method_type(const bebop_method_t* method)
{
  return method ? method->method_type : BEBOP_METHOD_UNARY;
}

const char* bebop_field_name(const bebop_field_t* field)
{
  if (!field || bebop_str_is_null(field->name)) {
    return NULL;
  }
  return BEBOP_STR(field->parent->schema->ctx, field->name);
}

const bebop_type_t* bebop_field_type(const bebop_field_t* field)
{
  return field ? field->type : NULL;
}

uint32_t bebop_field_index(const bebop_field_t* field)
{
  return field ? field->index : 0;
}

uint32_t bebop_field_decorator_count(const bebop_field_t* field)
{
  if (!field) {
    return 0;
  }
  uint32_t count = 0;
  for (const bebop_decorator_t* d = field->decorators; d; d = d->next) {
    count++;
  }
  return count;
}

const bebop_decorator_t* bebop_field_decorator_at(const bebop_field_t* field, const uint32_t idx)
{
  if (!field) {
    return NULL;
  }
  const bebop_decorator_t* d = field->decorators;
  for (uint32_t i = 0; i < idx && d; i++) {
    d = d->next;
  }
  return d;
}

const bebop_decorator_t* bebop_field_decorator_find(const bebop_field_t* field, const char* name)
{
  if (!field || !name || !field->parent) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(field->parent->schema->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }
  for (const bebop_decorator_t* d = field->decorators; d; d = d->next) {
    if (bebop_str_eq(d->name, interned)) {
      return d;
    }
  }
  return NULL;
}

const bebop_def_t* bebop_field_parent(const bebop_field_t* field)
{
  return field ? field->parent : NULL;
}

const char* bebop_member_name(const bebop_enum_member_t* member)
{
  if (!member || bebop_str_is_null(member->name)) {
    return NULL;
  }
  return BEBOP_STR(member->parent->schema->ctx, member->name);
}

int64_t bebop_member_value(const bebop_enum_member_t* member)
{
  return member ? (int64_t)member->value : 0;
}

uint64_t bebop_member_value_u64(const bebop_enum_member_t* member)
{
  return member ? member->value : 0;
}

const char* bebop_member_value_expr(const bebop_enum_member_t* member)
{
  if (!member || bebop_str_is_null(member->value_expr)) {
    return NULL;
  }
  return BEBOP_STR(member->parent->schema->ctx, member->value_expr);
}

uint32_t bebop_member_decorator_count(const bebop_enum_member_t* member)
{
  if (!member) {
    return 0;
  }
  uint32_t count = 0;
  for (const bebop_decorator_t* d = member->decorators; d; d = d->next) {
    count++;
  }
  return count;
}

const bebop_decorator_t* bebop_member_decorator_at(
    const bebop_enum_member_t* member, const uint32_t idx
)
{
  if (!member) {
    return NULL;
  }
  const bebop_decorator_t* d = member->decorators;
  for (uint32_t i = 0; i < idx && d; i++) {
    d = d->next;
  }
  return d;
}

const bebop_decorator_t* bebop_member_decorator_find(
    const bebop_enum_member_t* member, const char* name
)
{
  if (!member || !name || !member->parent) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(member->parent->schema->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }
  for (const bebop_decorator_t* d = member->decorators; d; d = d->next) {
    if (bebop_str_eq(d->name, interned)) {
      return d;
    }
  }
  return NULL;
}

uint8_t bebop_branch_discriminator(const bebop_union_branch_t* branch)
{
  return branch ? branch->discriminator : 0;
}

const bebop_def_t* bebop_branch_def(const bebop_union_branch_t* branch)
{
  return branch ? branch->def : NULL;
}

const bebop_type_t* bebop_branch_type_ref(const bebop_union_branch_t* branch)
{
  return branch ? branch->type_ref : NULL;
}

const char* bebop_branch_name(const bebop_union_branch_t* branch)
{
  if (!branch || bebop_str_is_null(branch->name)) {
    return NULL;
  }
  return BEBOP_STR(branch->parent->schema->ctx, branch->name);
}

uint32_t bebop_branch_decorator_count(const bebop_union_branch_t* branch)
{
  if (!branch) {
    return 0;
  }
  uint32_t count = 0;
  for (const bebop_decorator_t* d = branch->decorators; d; d = d->next) {
    count++;
  }
  return count;
}

const bebop_decorator_t* bebop_branch_decorator_at(
    const bebop_union_branch_t* branch, const uint32_t idx
)
{
  if (!branch) {
    return NULL;
  }
  const bebop_decorator_t* d = branch->decorators;
  for (uint32_t i = 0; i < idx && d; i++) {
    d = d->next;
  }
  return d;
}

const bebop_decorator_t* bebop_branch_decorator_find(
    const bebop_union_branch_t* branch, const char* name
)
{
  if (!branch || !name || !branch->parent) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(branch->parent->schema->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }
  for (const bebop_decorator_t* d = branch->decorators; d; d = d->next) {
    if (bebop_str_eq(d->name, interned)) {
      return d;
    }
  }
  return NULL;
}

const bebop_type_t* bebop_def_const_type(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_CONST) {
    return NULL;
  }
  return def->const_def.type;
}

const bebop_literal_t* bebop_def_const_value(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_CONST) {
    return NULL;
  }
  return &def->const_def.value;
}

bebop_type_kind_t bebop_type_kind(const bebop_type_t* type)
{
  return type ? type->kind : BEBOP_TYPE_BOOL;
}

const bebop_type_t* bebop_type_element(const bebop_type_t* type)
{
  if (!type) {
    return NULL;
  }
  if (type->kind == BEBOP_TYPE_ARRAY) {
    return type->array.element;
  }
  if (type->kind == BEBOP_TYPE_FIXED_ARRAY) {
    return type->fixed_array.element;
  }
  return NULL;
}

uint32_t bebop_type_fixed_array_size(const bebop_type_t* type)
{
  if (!type || type->kind != BEBOP_TYPE_FIXED_ARRAY) {
    return 0;
  }
  return type->fixed_array.size;
}

const bebop_type_t* bebop_type_key(const bebop_type_t* type)
{
  if (!type || type->kind != BEBOP_TYPE_MAP) {
    return NULL;
  }
  return type->map.key;
}

const bebop_type_t* bebop_type_value(const bebop_type_t* type)
{
  if (!type || type->kind != BEBOP_TYPE_MAP) {
    return NULL;
  }
  return type->map.value;
}

const char* bebop_type_name(const bebop_type_t* type)
{
  if (!type || type->kind != BEBOP_TYPE_DEFINED) {
    return NULL;
  }
  bebop_context_t* ctx = type->defined.resolved ? type->defined.resolved->schema->ctx : NULL;
  if (!ctx) {
    return NULL;
  }
  return BEBOP_STR(ctx, type->defined.name);
}

const char* bebop_type_name_in_ctx(const bebop_type_t* type, bebop_context_t* ctx)
{
  if (!type || type->kind != BEBOP_TYPE_DEFINED) {
    return NULL;
  }
  if (!ctx) {
    return bebop_type_name(type);
  }
  return BEBOP_STR(ctx, type->defined.name);
}

const bebop_def_t* bebop_type_resolved(const bebop_type_t* type)
{
  if (!type || type->kind != BEBOP_TYPE_DEFINED) {
    return NULL;
  }
  return type->defined.resolved;
}

const char* bebop_type_kind_name(const bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
      return "bool";
    case BEBOP_TYPE_BYTE:
      return "byte";
    case BEBOP_TYPE_INT8:
      return "int8";
    case BEBOP_TYPE_INT16:
      return "int16";
    case BEBOP_TYPE_UINT16:
      return "uint16";
    case BEBOP_TYPE_INT32:
      return "int32";
    case BEBOP_TYPE_UINT32:
      return "uint32";
    case BEBOP_TYPE_INT64:
      return "int64";
    case BEBOP_TYPE_UINT64:
      return "uint64";
    case BEBOP_TYPE_INT128:
      return "int128";
    case BEBOP_TYPE_UINT128:
      return "uint128";
    case BEBOP_TYPE_FLOAT16:
      return "float16";
    case BEBOP_TYPE_FLOAT32:
      return "float32";
    case BEBOP_TYPE_FLOAT64:
      return "float64";
    case BEBOP_TYPE_BFLOAT16:
      return "bfloat16";
    case BEBOP_TYPE_STRING:
      return "string";
    case BEBOP_TYPE_UUID:
      return "uuid";
    case BEBOP_TYPE_TIMESTAMP:
      return "timestamp";
    case BEBOP_TYPE_DURATION:
      return "duration";
    case BEBOP_TYPE_ARRAY:
      return "array";
    case BEBOP_TYPE_FIXED_ARRAY:
      return "fixed_array";
    case BEBOP_TYPE_MAP:
      return "map";
    case BEBOP_TYPE_DEFINED:
      return "defined";
    case BEBOP_TYPE_UNKNOWN:
      BEBOP_UNREACHABLE();
  }
  BEBOP_UNREACHABLE();
}

const char* bebop_decorator_name(const bebop_decorator_t* dec)
{
  if (!dec || bebop_str_is_null(dec->name)) {
    return NULL;
  }
  if (!dec->schema) {
    return NULL;
  }
  return BEBOP_STR(dec->schema->ctx, dec->name);
}

bebop_span_t bebop_decorator_span(const bebop_decorator_t* dec)
{
  return dec ? dec->span : (bebop_span_t) {0};
}

uint32_t bebop_decorator_arg_count(const bebop_decorator_t* dec)
{
  return dec ? dec->arg_count : 0;
}

const bebop_decorator_arg_t* bebop_decorator_arg_at(
    const bebop_decorator_t* dec, const uint32_t idx
)
{
  if (!dec || idx >= dec->arg_count) {
    return NULL;
  }
  return &dec->args[idx];
}

const char* bebop_arg_name(const bebop_decorator_arg_t* arg)
{
  if (!arg || bebop_str_is_null(arg->name)) {
    return NULL;
  }
  if (!arg->decorator || !arg->decorator->schema) {
    return NULL;
  }
  return BEBOP_STR(arg->decorator->schema->ctx, arg->name);
}

const bebop_literal_t* bebop_arg_value(const bebop_decorator_arg_t* arg)
{
  return arg ? &arg->value : NULL;
}

const bebop_def_t* bebop_decorator_resolved(const bebop_decorator_t* dec)
{
  if (!dec) {
    return NULL;
  }
  return dec->resolved;
}

const bebop_literal_t* bebop_decorator_export(const bebop_decorator_t* dec, const char* key)
{
  if (!dec || !dec->export_data || !key) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(dec->schema->ctx), key);
  for (uint32_t i = 0; i < dec->export_data->count; i++) {
    if (dec->export_data->entries[i].key.idx == interned.idx) {
      return &dec->export_data->entries[i].value;
    }
  }
  return NULL;
}

bebop_decorator_target_t bebop_def_decorator_targets(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return BEBOP_TARGET_NONE;
  }
  return def->decorator_def.targets;
}

bool bebop_def_decorator_allow_multiple(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return false;
  }
  return def->decorator_def.allow_multiple;
}

uint32_t bebop_def_decorator_param_count(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return 0;
  }
  return def->decorator_def.param_count;
}

const char* bebop_def_decorator_param_name(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return NULL;
  }
  if (idx >= def->decorator_def.param_count) {
    return NULL;
  }
  const bebop_macro_param_def_t* p = &def->decorator_def.params[idx];
  if (bebop_str_is_null(p->name)) {
    return NULL;
  }
  return BEBOP_STR(def->schema->ctx, p->name);
}

const char* bebop_def_decorator_param_description(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return NULL;
  }
  if (idx >= def->decorator_def.param_count) {
    return NULL;
  }
  const bebop_macro_param_def_t* p = &def->decorator_def.params[idx];
  if (bebop_str_is_null(p->description)) {
    return NULL;
  }
  return BEBOP_STR(def->schema->ctx, p->description);
}

bebop_type_kind_t bebop_def_decorator_param_type(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return BEBOP_TYPE_BOOL;
  }
  if (idx >= def->decorator_def.param_count) {
    return BEBOP_TYPE_BOOL;
  }
  return def->decorator_def.params[idx].type;
}

bool bebop_def_decorator_param_required(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_DECORATOR) {
    return false;
  }
  if (idx >= def->decorator_def.param_count) {
    return false;
  }
  return def->decorator_def.params[idx].required;
}

bebop_literal_kind_t bebop_literal_kind(const bebop_literal_t* lit)
{
  return lit ? lit->kind : BEBOP_LITERAL_BOOL;
}

bool bebop_literal_as_bool(const bebop_literal_t* lit)
{
  if (!lit || lit->kind != BEBOP_LITERAL_BOOL) {
    return false;
  }
  return lit->bool_val;
}

int64_t bebop_literal_as_int(const bebop_literal_t* lit)
{
  if (!lit || lit->kind != BEBOP_LITERAL_INT) {
    return 0;
  }
  return lit->int_val;
}

double bebop_literal_as_float(const bebop_literal_t* lit)
{
  if (!lit || lit->kind != BEBOP_LITERAL_FLOAT) {
    return 0.0;
  }
  return lit->float_val;
}

const uint8_t* bebop_literal_as_uuid(const bebop_literal_t* lit)
{
  if (!lit || lit->kind != BEBOP_LITERAL_UUID) {
    return NULL;
  }
  return lit->uuid_val;
}

const char* bebop_literal_as_string(const bebop_literal_t* lit, size_t* out_len)
{
  if (!lit || lit->kind != BEBOP_LITERAL_STRING) {
    if (out_len) {
      *out_len = 0;
    }
    return NULL;
  }
  if (!lit->ctx) {
    if (out_len) {
      *out_len = 0;
    }
    return NULL;
  }
  const char* str = BEBOP_STR(lit->ctx, lit->string_val);
  if (out_len) {
    *out_len = str ? strlen(str) : 0;
  }
  return str;
}

const uint8_t* bebop_literal_as_bytes(const bebop_literal_t* lit, size_t* out_len)
{
  if (!lit || lit->kind != BEBOP_LITERAL_BYTES) {
    if (out_len) {
      *out_len = 0;
    }
    return NULL;
  }
  if (out_len) {
    *out_len = lit->bytes_val.len;
  }
  return lit->bytes_val.data;
}

void bebop_literal_as_timestamp(
    const bebop_literal_t* lit, int64_t* out_seconds, int32_t* out_nanos, int32_t* out_offset_ms
)
{
  if (!lit || lit->kind != BEBOP_LITERAL_TIMESTAMP) {
    if (out_seconds) {
      *out_seconds = 0;
    }
    if (out_nanos) {
      *out_nanos = 0;
    }
    if (out_offset_ms) {
      *out_offset_ms = 0;
    }
    return;
  }
  if (out_seconds) {
    *out_seconds = lit->timestamp_val.seconds;
  }
  if (out_nanos) {
    *out_nanos = lit->timestamp_val.nanos;
  }
  if (out_offset_ms) {
    *out_offset_ms = lit->timestamp_val.offset_ms;
  }
}

void bebop_literal_as_duration(const bebop_literal_t* lit, int64_t* out_seconds, int32_t* out_nanos)
{
  if (!lit || lit->kind != BEBOP_LITERAL_DURATION) {
    if (out_seconds) {
      *out_seconds = 0;
    }
    if (out_nanos) {
      *out_nanos = 0;
    }
    return;
  }
  if (out_seconds) {
    *out_seconds = lit->duration_val.seconds;
  }
  if (out_nanos) {
    *out_nanos = lit->duration_val.nanos;
  }
}

const char* bebop_literal_kind_name(const bebop_literal_kind_t kind)
{
  switch (kind) {
    case BEBOP_LITERAL_BOOL:
      return "bool";
    case BEBOP_LITERAL_INT:
      return "integer";
    case BEBOP_LITERAL_FLOAT:
      return "float";
    case BEBOP_LITERAL_STRING:
      return "string";
    case BEBOP_LITERAL_UUID:
      return "uuid";
    case BEBOP_LITERAL_BYTES:
      return "bytes";
    case BEBOP_LITERAL_TIMESTAMP:
      return "timestamp";
    case BEBOP_LITERAL_DURATION:
      return "duration";
    case BEBOP_LITERAL_UNKNOWN:
      BEBOP_UNREACHABLE();
  }
  BEBOP_UNREACHABLE();
}

bebop_span_t bebop_literal_span(const bebop_literal_t* lit)
{
  return lit ? lit->span : BEBOP_SPAN_INVALID;
}

bool bebop_literal_has_env_var(const bebop_literal_t* lit)
{
  return lit ? lit->has_env_var : false;
}

const char* bebop_literal_raw_value(const bebop_literal_t* lit, size_t* out_len)
{
  if (!lit || bebop_str_is_null(lit->raw_value)) {
    if (out_len) {
      *out_len = 0;
    }
    return NULL;
  }
  if (!lit->ctx) {
    if (out_len) {
      *out_len = 0;
    }
    return NULL;
  }
  const char* str = BEBOP_STR(lit->ctx, lit->raw_value);
  if (out_len) {
    *out_len = str ? BEBOP_STR_LEN(lit->ctx, lit->raw_value) : 0;
  }
  return str;
}

uint32_t bebop_result_definition_count(const bebop_parse_result_t* result)
{
  return result ? result->all_def_count : 0;
}

const bebop_def_t* bebop_result_definition_at(
    const bebop_parse_result_t* result, const uint32_t idx
)
{
  if (!result || idx >= result->all_def_count) {
    return NULL;
  }
  return result->all_defs[idx];
}

bebop_context_t* bebop_schema_context(const bebop_schema_t* schema)
{
  return schema ? schema->ctx : NULL;
}

const bebop_schema_t* bebop_def_schema(const bebop_def_t* def)
{
  return def ? def->schema : NULL;
}

bebop_span_t bebop_field_span(const bebop_field_t* field)
{
  return field ? field->span : BEBOP_SPAN_INVALID;
}

bebop_span_t bebop_field_name_span(const bebop_field_t* field)
{
  return field ? field->name_span : BEBOP_SPAN_INVALID;
}

const bebop_def_t* bebop_member_parent(const bebop_enum_member_t* member)
{
  return member ? member->parent : NULL;
}

bebop_span_t bebop_member_span(const bebop_enum_member_t* member)
{
  return member ? member->span : BEBOP_SPAN_INVALID;
}

const bebop_def_t* bebop_branch_parent(const bebop_union_branch_t* branch)
{
  return branch ? branch->parent : NULL;
}

bebop_span_t bebop_branch_span(const bebop_union_branch_t* branch)
{
  return branch ? branch->span : BEBOP_SPAN_INVALID;
}

const bebop_def_t* bebop_method_parent(const bebop_method_t* method)
{
  return method ? method->parent : NULL;
}

const char* bebop_method_name(const bebop_method_t* method)
{
  if (!method || bebop_str_is_null(method->name)) {
    return NULL;
  }
  return BEBOP_STR(method->parent->schema->ctx, method->name);
}

bebop_span_t bebop_method_span(const bebop_method_t* method)
{
  return method ? method->span : BEBOP_SPAN_INVALID;
}

const char* bebop_method_documentation(const bebop_method_t* method)
{
  if (!method || bebop_str_is_null(method->documentation)) {
    return NULL;
  }
  return BEBOP_STR(method->parent->schema->ctx, method->documentation);
}

uint32_t bebop_method_id(const bebop_method_t* method)
{
  return method ? method->id : 0;
}

uint32_t bebop_method_decorator_count(const bebop_method_t* method)
{
  if (!method) {
    return 0;
  }
  uint32_t count = 0;
  for (const bebop_decorator_t* d = method->decorators; d; d = d->next) {
    count++;
  }
  return count;
}

const bebop_decorator_t* bebop_method_decorator_at(const bebop_method_t* method, const uint32_t idx)
{
  if (!method) {
    return NULL;
  }
  const bebop_decorator_t* d = method->decorators;
  for (uint32_t i = 0; i < idx && d; i++) {
    d = d->next;
  }
  return d;
}

const bebop_decorator_t* bebop_method_decorator_find(const bebop_method_t* method, const char* name)
{
  if (!method || !name || !method->parent) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(method->parent->schema->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }
  for (const bebop_decorator_t* d = method->decorators; d; d = d->next) {
    if (bebop_str_eq(d->name, interned)) {
      return d;
    }
  }
  return NULL;
}

uint32_t bebop_def_mixin_count(const bebop_def_t* def)
{
  if (!def || def->kind != BEBOP_DEF_SERVICE) {
    return 0;
  }
  return def->service_def.mixin_count;
}

const bebop_type_t* bebop_def_mixin_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || def->kind != BEBOP_DEF_SERVICE) {
    return NULL;
  }
  if (idx >= def->service_def.mixin_count) {
    return NULL;
  }
  return def->service_def.mixins[idx];
}

const bebop_decorator_arg_t* bebop_decorator_arg_find(
    const bebop_decorator_t* dec, const char* name
)
{
  if (!dec || !name || !dec->schema) {
    return NULL;
  }
  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(dec->schema->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }
  for (uint32_t i = 0; i < dec->arg_count; i++) {
    const bebop_decorator_arg_t* arg = &dec->args[i];
    if (bebop_str_eq(arg->name, interned)) {
      return arg;
    }
  }
  return NULL;
}

bebop_span_t bebop_type_span(const bebop_type_t* type)
{
  return type ? type->span : BEBOP_SPAN_INVALID;
}

static uint32_t bebop__scalar_fixed_size(const bebop_type_kind_t kind)
{
  switch (kind) {
    case BEBOP_TYPE_BOOL:
    case BEBOP_TYPE_BYTE:
    case BEBOP_TYPE_INT8:
      return 1;
    case BEBOP_TYPE_INT16:
    case BEBOP_TYPE_UINT16:
      return 2;
    case BEBOP_TYPE_INT32:
    case BEBOP_TYPE_UINT32:
      return 4;
    case BEBOP_TYPE_INT64:
    case BEBOP_TYPE_UINT64:
      return 8;
    case BEBOP_TYPE_INT128:
    case BEBOP_TYPE_UINT128:
      return 16;
    case BEBOP_TYPE_FLOAT16:
      return 2;
    case BEBOP_TYPE_FLOAT32:
      return 4;
    case BEBOP_TYPE_FLOAT64:
      return 8;
    case BEBOP_TYPE_BFLOAT16:
      return 2;
    case BEBOP_TYPE_UUID:
      return 16;
    case BEBOP_TYPE_TIMESTAMP:
      return 16;
    case BEBOP_TYPE_DURATION:
      return 12;
    default:
      return 0;
  }
}

bool bebop_type_is_fixed(const bebop_type_t* type)
{
  while (type) {
    switch (type->kind) {
      case BEBOP_TYPE_BOOL:
      case BEBOP_TYPE_BYTE:
      case BEBOP_TYPE_INT8:
      case BEBOP_TYPE_INT16:
      case BEBOP_TYPE_UINT16:
      case BEBOP_TYPE_INT32:
      case BEBOP_TYPE_UINT32:
      case BEBOP_TYPE_INT64:
      case BEBOP_TYPE_UINT64:
      case BEBOP_TYPE_INT128:
      case BEBOP_TYPE_UINT128:
      case BEBOP_TYPE_FLOAT16:
      case BEBOP_TYPE_FLOAT32:
      case BEBOP_TYPE_FLOAT64:
      case BEBOP_TYPE_BFLOAT16:
      case BEBOP_TYPE_UUID:
      case BEBOP_TYPE_TIMESTAMP:
      case BEBOP_TYPE_DURATION:
        return true;
      case BEBOP_TYPE_STRING:
      case BEBOP_TYPE_ARRAY:
      case BEBOP_TYPE_MAP:
        return false;
      case BEBOP_TYPE_FIXED_ARRAY:
        type = type->fixed_array.element;
        continue;
      case BEBOP_TYPE_DEFINED:
        if (!type->defined.resolved) {
          return false;
        }
        if (type->defined.resolved->kind == BEBOP_DEF_ENUM) {
          return true;
        }
        if (type->defined.resolved->kind == BEBOP_DEF_STRUCT) {
          return type->defined.resolved->struct_def.fixed_size > 0;
        }
        return false;
      case BEBOP_TYPE_UNKNOWN:
        BEBOP_UNREACHABLE();
    }
    break;
  }
  return false;
}

uint32_t bebop_type_fixed_size(const bebop_type_t* type)
{
  uint32_t multiplier = 1;
  while (type) {
    const uint32_t scalar_size = bebop__scalar_fixed_size(type->kind);
    if (scalar_size > 0) {
      if (BEBOP_MUL_WOULD_OVERFLOW_U32(scalar_size, multiplier)) {
        return 0;
      }
      return scalar_size * multiplier;
    }
    if (type->kind == BEBOP_TYPE_FIXED_ARRAY) {
      if (BEBOP_MUL_WOULD_OVERFLOW_U32(multiplier, type->fixed_array.size)) {
        return 0;
      }
      multiplier *= type->fixed_array.size;
      type = type->fixed_array.element;
      continue;
    }
    if (type->kind == BEBOP_TYPE_DEFINED && type->defined.resolved) {
      uint32_t base = 0;
      if (type->defined.resolved->kind == BEBOP_DEF_ENUM) {
        base = bebop__scalar_fixed_size(type->defined.resolved->enum_def.base_type);
      } else if (type->defined.resolved->kind == BEBOP_DEF_STRUCT) {
        base = type->defined.resolved->struct_def.fixed_size;
      }
      if (base == 0) {
        return 0;
      }
      if (BEBOP_MUL_WOULD_OVERFLOW_U32(base, multiplier)) {
        return 0;
      }
      return base * multiplier;
    }
    return 0;
  }
  return 0;
}

uint32_t bebop_type_min_wire_size(const bebop_type_t* type)
{
  uint32_t multiplier = 1;
  while (type) {
    const uint32_t fixed = bebop_type_fixed_size(type);
    if (fixed > 0) {
      if (BEBOP_MUL_WOULD_OVERFLOW_U32(fixed, multiplier)) {
        return 0;
      }
      return fixed * multiplier;
    }
    switch (type->kind) {
      case BEBOP_TYPE_STRING:
        return 5 * multiplier;
      case BEBOP_TYPE_ARRAY:
        return 4 * multiplier;
      case BEBOP_TYPE_FIXED_ARRAY:
        if (BEBOP_MUL_WOULD_OVERFLOW_U32(multiplier, type->fixed_array.size)) {
          return 0;
        }
        multiplier *= type->fixed_array.size;
        type = type->fixed_array.element;
        continue;
      case BEBOP_TYPE_MAP:
        return 4 * multiplier;
      case BEBOP_TYPE_DEFINED:
        if (!type->defined.resolved) {
          return 0;
        }
        if (type->defined.resolved->kind == BEBOP_DEF_MESSAGE) {
          return 4 * multiplier;
        }
        if (type->defined.resolved->kind == BEBOP_DEF_UNION) {
          return 5 * multiplier;
        }
        return 0;
      default:
        return 0;
    }
  }
  return 0;
}

static inline bool bebop__offset_in_span(uint32_t offset, bebop_span_t span)
{
  return span.len > 0 && offset >= span.off && offset < span.off + span.len;
}

static const bebop_def_t* bebop__find_def_at_offset(
    const bebop_parse_result_t* result, const char* path, uint32_t offset
)
{
  if (!result) {
    return NULL;
  }
  for (uint32_t i = 0; i < result->schema_count; i++) {
    const bebop_schema_t* schema = result->schemas[i];
    if (path && schema->path && !bebop__streq(path, schema->path)) {
      continue;
    }
    for (const bebop_def_t* def = schema->definitions; def; def = def->next) {
      if (bebop__offset_in_span(offset, def->span)) {
        return def;
      }
    }
  }
  return NULL;
}

static bool bebop__locate_in_def(const bebop_def_t* def, uint32_t offset, bebop_location_t* out);

static bool bebop__locate_in_type(
    const bebop_type_t* type, const bebop_def_t* parent, uint32_t offset, bebop_location_t* out
)
{
  if (!type) {
    return false;
  }
  if (!bebop__offset_in_span(offset, type->span)) {
    return false;
  }

  switch (type->kind) {
    case BEBOP_TYPE_ARRAY:
    case BEBOP_TYPE_FIXED_ARRAY:
      if (bebop__locate_in_type(type->fixed_array.element, parent, offset, out)) {
        return true;
      }
      break;
    case BEBOP_TYPE_MAP:
      if (bebop__locate_in_type(type->map.key, parent, offset, out)) {
        return true;
      }
      if (bebop__locate_in_type(type->map.value, parent, offset, out)) {
        return true;
      }
      break;
    default:
      break;
  }

  if (type->kind == BEBOP_TYPE_DEFINED) {
    out->kind = BEBOP_LOC_TYPE_REF;
    out->span = type->span;
    out->def = type->defined.resolved;
    out->parent = parent;
    out->type = type;
    return true;
  }

  return false;
}

static bool bebop__locate_in_decorators(
    const bebop_decorator_t* dec, const bebop_def_t* parent, uint32_t offset, bebop_location_t* out
)
{
  for (; dec; dec = dec->next) {
    if (bebop__offset_in_span(offset, dec->span)) {
      out->kind = BEBOP_LOC_DECORATOR;
      out->span = dec->span;
      out->def = parent;
      out->parent = parent ? parent->parent : NULL;
      out->decorator = dec;
      return true;
    }
  }
  return false;
}

static bool bebop__locate_in_fields(const bebop_def_t* def, uint32_t offset, bebop_location_t* out)
{
  const uint32_t field_count = bebop_def_field_count(def);
  for (uint32_t i = 0; i < field_count; i++) {
    const bebop_field_t* field = bebop_def_field_at(def, i);
    if (!field || !bebop__offset_in_span(offset, field->span)) {
      continue;
    }

    if (bebop__locate_in_decorators(field->decorators, def, offset, out)) {
      return true;
    }

    if (bebop__locate_in_type(field->type, def, offset, out)) {
      // Keep TYPE_REF for defined types (e.g., Point in map[string, Point])
      if (out->kind != BEBOP_LOC_TYPE_REF) {
        out->kind = BEBOP_LOC_FIELD_TYPE;
        out->field = field;
      }
      return true;
    }

    if (bebop__offset_in_span(offset, field->name_span)) {
      out->kind = BEBOP_LOC_FIELD_NAME;
      out->span = field->name_span;
      out->def = def;
      out->parent = def->parent;
      out->field = field;
      return true;
    }

    // Fallback: on field but not on name or type
    out->kind = BEBOP_LOC_FIELD_NAME;
    out->span = field->span;
    out->def = def;
    out->parent = def->parent;
    out->field = field;
    return true;
  }
  return false;
}

static bool bebop__locate_in_def(const bebop_def_t* def, uint32_t offset, bebop_location_t* out)
{
  if (bebop__locate_in_decorators(def->decorators, def, offset, out)) {
    return true;
  }

  // Check nested definitions first for most specific match
  for (const bebop_def_t* nested = def->nested_defs; nested; nested = nested->next) {
    if (bebop__offset_in_span(offset, nested->span)) {
      if (bebop__locate_in_def(nested, offset, out)) {
        return true;
      }
      out->kind = BEBOP_LOC_DEF;
      out->span = nested->name_span.len > 0 ? nested->name_span : nested->span;
      out->def = nested;
      out->parent = def;
      return true;
    }
  }

  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
    case BEBOP_DEF_MESSAGE:
      if (bebop__locate_in_fields(def, offset, out)) {
        return true;
      }
      break;

    case BEBOP_DEF_UNION:
      for (uint32_t i = 0; i < def->union_def.branch_count; i++) {
        const bebop_union_branch_t* branch = &def->union_def.branches[i];
        if (!bebop__offset_in_span(offset, branch->span)) {
          continue;
        }

        if (bebop__locate_in_decorators(branch->decorators, def, offset, out)) {
          return true;
        }

        if (branch->def && bebop__offset_in_span(offset, branch->def->span)) {
          if (bebop__locate_in_def(branch->def, offset, out)) {
            return true;
          }
        }

        if (branch->type_ref && bebop__locate_in_type(branch->type_ref, def, offset, out)) {
          return true;
        }

        out->kind = BEBOP_LOC_BRANCH;
        out->span = branch->span;
        out->def = branch->def;
        out->parent = def;
        out->branch = branch;
        return true;
      }
      break;

    case BEBOP_DEF_ENUM:
      for (uint32_t i = 0; i < def->enum_def.member_count; i++) {
        const bebop_enum_member_t* member = &def->enum_def.members[i];
        if (bebop__offset_in_span(offset, member->span)) {
          if (bebop__locate_in_decorators(member->decorators, def, offset, out)) {
            return true;
          }
          out->kind = BEBOP_LOC_MEMBER;
          out->span = member->span;
          out->def = def;
          out->parent = def->parent;
          out->member = member;
          return true;
        }
      }
      break;

    case BEBOP_DEF_SERVICE:
      for (uint32_t i = 0; i < def->service_def.mixin_count; i++) {
        const bebop_type_t* mixin = def->service_def.mixins[i];
        if (mixin && bebop__offset_in_span(offset, mixin->span)) {
          out->kind = BEBOP_LOC_MIXIN;
          out->span = mixin->span;
          out->def = def;
          out->parent = def->parent;
          out->type = mixin;
          return true;
        }
      }
      for (uint32_t i = 0; i < def->service_def.method_count; i++) {
        const bebop_method_t* method = &def->service_def.methods[i];
        if (!bebop__offset_in_span(offset, method->span)) {
          continue;
        }

        if (bebop__locate_in_decorators(method->decorators, def, offset, out)) {
          return true;
        }

        if (method->request_type && bebop__locate_in_type(method->request_type, def, offset, out)) {
          return true;
        }

        if (method->response_type && bebop__locate_in_type(method->response_type, def, offset, out))
        {
          return true;
        }

        out->kind = BEBOP_LOC_METHOD;
        out->span = method->span;
        out->def = def;
        out->parent = def->parent;
        out->method = method;
        return true;
      }
      break;

    default:
      break;
  }

  if (def->name_span.len > 0 && bebop__offset_in_span(offset, def->name_span)) {
    out->kind = BEBOP_LOC_DEF;
    out->span = def->name_span;
    out->def = def;
    out->parent = def->parent;
    return true;
  }

  return false;
}

bool bebop_result_locate(
    const bebop_parse_result_t* result, const char* path, uint32_t offset, bebop_location_t* out
)
{
  memset(out, 0, sizeof(*out));
  out->kind = BEBOP_LOC_NONE;

  // Check decorators first since they may be outside the def's span
  if (result) {
    for (uint32_t i = 0; i < result->schema_count; i++) {
      const bebop_schema_t* schema = result->schemas[i];
      if (path && schema->path && !bebop__streq(path, schema->path)) {
        continue;
      }
      for (const bebop_def_t* def = schema->definitions; def; def = def->next) {
        if (bebop__locate_in_decorators(def->decorators, def, offset, out)) {
          return true;
        }
      }
    }
  }

  const bebop_def_t* def = bebop__find_def_at_offset(result, path, offset);
  if (!def) {
    return false;
  }

  if (bebop__locate_in_def(def, offset, out)) {
    return true;
  }

  // Only match on the name span, not elsewhere in the body
  if (def->name_span.len > 0 && bebop__offset_in_span(offset, def->name_span)) {
    out->kind = BEBOP_LOC_DEF;
    out->span = def->name_span;
    out->def = def;
    out->parent = def->parent;
    return true;
  }

  return false;
}

bool bebop_def_is_fixed_size(const bebop_def_t* def)
{
  if (!def) {
    return false;
  }
  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
      // Empty struct or struct with all fixed-size fields
      // Check if any field is variable size
      for (uint32_t i = 0; i < def->struct_def.field_count; i++) {
        if (!bebop_type_is_fixed(def->struct_def.fields[i].type)) {
          return false;
        }
      }
      return true;
    case BEBOP_DEF_ENUM:
      return true;
    default:
      return false;
  }
}

uint32_t bebop_def_fixed_size(const bebop_def_t* def)
{
  if (!def) {
    return 0;
  }
  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
      return def->struct_def.fixed_size;
    case BEBOP_DEF_ENUM:
      return bebop__scalar_fixed_size(def->enum_def.base_type);
    default:
      return 0;
  }
}

uint32_t bebop_def_min_wire_size(const bebop_def_t* def)
{
  if (!def) {
    return 0;
  }
  switch (def->kind) {
    case BEBOP_DEF_STRUCT: {
      if (def->struct_def.fixed_size > 0) {
        return def->struct_def.fixed_size;
      }
      // Calculate minimum size from fields
      uint32_t size = 0;
      for (uint32_t i = 0; i < def->struct_def.field_count; i++) {
        size += bebop_type_min_wire_size(def->struct_def.fields[i].type);
      }
      return size;
    }
    case BEBOP_DEF_MESSAGE:
      return 4;  // Length prefix
    case BEBOP_DEF_UNION:
      return 5;  // Length prefix + discriminator
    case BEBOP_DEF_ENUM:
      return bebop__scalar_fixed_size(def->enum_def.base_type);
    default:
      return 0;
  }
}

uint32_t bebop_def_dependents_count(const bebop_def_t* def)
{
  return def ? def->dependents_count : 0;
}

const bebop_def_t* bebop_def_dependent_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || idx >= def->dependents_count) {
    return NULL;
  }
  return def->dependents[idx];
}

uint32_t bebop_def_references_count(const bebop_def_t* def)
{
  return def ? def->references_count : 0;
}

bebop_span_t bebop_def_reference_at(const bebop_def_t* def, const uint32_t idx)
{
  if (!def || idx >= def->references_count) {
    return BEBOP_SPAN_INVALID;
  }
  return def->references[idx];
}

const char* bebop_emit_def(const bebop_def_t* def, size_t* len)
{
  if (!def || !def->schema) {
    if (len) {
      *len = 0;
    }
    return NULL;
  }

  bebop__emit_buf_t buf;
  bebop__emit_init(&buf, def->schema);
  emitter__emit_def(&buf, def);

  if (buf.error) {
    if (len) {
      *len = 0;
    }
    return NULL;
  }

  bebop__emit_char(&buf, '\0');
  if (buf.error) {
    if (len) {
      *len = 0;
    }
    return NULL;
  }

  if (len) {
    *len = buf.len - 1;
  }
  return buf.data;
}

const char* bebop_def_kind_name(const bebop_def_kind_t kind)
{
  switch (kind) {
    case BEBOP_DEF_ENUM:
      return "enum";
    case BEBOP_DEF_STRUCT:
      return "struct";
    case BEBOP_DEF_MESSAGE:
      return "message";
    case BEBOP_DEF_UNION:
      return "union";
    case BEBOP_DEF_SERVICE:
      return "service";
    case BEBOP_DEF_CONST:
      return "const";
    case BEBOP_DEF_DECORATOR:
      return "decorator";
    case BEBOP_DEF_UNKNOWN:
      BEBOP_UNREACHABLE();
  }
  BEBOP_UNREACHABLE();
}
