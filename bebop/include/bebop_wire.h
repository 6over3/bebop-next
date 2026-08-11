#ifndef BEBOP_WIRE_H_
#define BEBOP_WIRE_H_

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__cplusplus) && !defined(BEBOP__MAX_ALIGN_T_DEFINED)
#define BEBOP__MAX_ALIGN_T_DEFINED

typedef struct {
  long long __max_align_ll;
  long double __max_align_ld;
} max_align_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BEBOP_API
#ifdef BEBOP_STATIC
#define BEBOP_API static
#elif defined(BEBOP_STATIC_LIB)
#define BEBOP_API
#elif defined(_WIN32)
#ifdef BEBOP_BUILDING
#define BEBOP_API __declspec(dllexport)
#else
#define BEBOP_API __declspec(dllimport)
#endif
#else
#define BEBOP_API __attribute__((visibility("default")))
#endif
#endif

#ifndef BEBOP_WIRE_UNUSED
#define BEBOP_WIRE_UNUSED(x) (void)(x)
#endif

// Cast away const for decoding into immutable struct fields.
#define BEBOP_WIRE_MUTPTR(type, ptr) ((type*)(uintptr_t)(ptr))

// Cast pointer to different type, suppressing alignment and const warnings.
#define BEBOP_WIRE_CASTPTR(type, ptr) ((type)(void*)(uintptr_t)(ptr))

// #region Build Configuration

#ifndef BEBOP_WIRE_ASSUME_LE
#define BEBOP_WIRE_ASSUME_LE 1
#endif

#ifndef BEBOP_WIRE_LIKELY
#if defined(__GNUC__) || defined(__clang__)
#define BEBOP_WIRE_LIKELY(x) __builtin_expect(!!(x), 1)
#define BEBOP_WIRE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define BEBOP_WIRE_LIKELY(x) (x)
#define BEBOP_WIRE_UNLIKELY(x) (x)
#endif
#endif

#ifndef BEBOP_WIRE_HOT
#if defined(__GNUC__) || defined(__clang__)
#define BEBOP_WIRE_HOT __attribute__((hot))
#define BEBOP_WIRE_PURE __attribute__((pure))
#else
#define BEBOP_WIRE_HOT
#define BEBOP_WIRE_PURE
#endif
#endif

#ifndef BEBOP_WIRE_PREFETCH
#if defined(__GNUC__) || defined(__clang__)
#define BEBOP_WIRE_PREFETCH_R(addr) __builtin_prefetch((addr), 0, 3)
#define BEBOP_WIRE_PREFETCH_W(addr) __builtin_prefetch((addr), 1, 3)
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#define BEBOP_WIRE_PREFETCH_R(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#define BEBOP_WIRE_PREFETCH_W(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#elif defined(_MSC_VER) && defined(_M_ARM64)
#include <intrin.h>
#define BEBOP_WIRE_PREFETCH_R(addr) __prefetch((addr))
#define BEBOP_WIRE_PREFETCH_W(addr) __prefetch((addr))
#else
#define BEBOP_WIRE_PREFETCH_R(addr) ((void)(addr))
#define BEBOP_WIRE_PREFETCH_W(addr) ((void)(addr))
#endif
#endif

#ifdef BEBOP_WIRE_SINGLE_THREADED
#define BEBOP_WIRE_ATOMIC_LOAD(ptr) (*(ptr))
#define BEBOP_WIRE_ATOMIC_STORE(ptr, val) (*(ptr) = (val))
#define BEBOP_WIRE_ATOMIC_FETCH_ADD(ptr, val) ((*(ptr)) += (val))
#define BEBOP_WIRE_ATOMIC_CAS_WEAK(ptr, expected, desired) \
  (*(ptr) == *(expected) ? (*(ptr) = (desired), true) : (*(expected) = *(ptr), false))
#define BEBOP_WIRE_ATOMIC_INIT(ptr, val) (*(ptr) = (val))
#else
#include <stdatomic.h>
#define BEBOP_WIRE_ATOMIC_LOAD(ptr) atomic_load(ptr)
#define BEBOP_WIRE_ATOMIC_STORE(ptr, val) atomic_store(ptr, val)
#define BEBOP_WIRE_ATOMIC_FETCH_ADD(ptr, val) atomic_fetch_add(ptr, val)
#define BEBOP_WIRE_ATOMIC_CAS_WEAK(ptr, expected, desired) \
  atomic_compare_exchange_weak(ptr, expected, desired)
#define BEBOP_WIRE_ATOMIC_INIT(ptr, val) atomic_init(ptr, val)
#endif

// #endregion

// #region Wire Type Sizes

#define BEBOP_WIRE_SIZE_BOOL 1
#define BEBOP_WIRE_SIZE_BYTE 1
#define BEBOP_WIRE_SIZE_INT8 1
#define BEBOP_WIRE_SIZE_INT16 2
#define BEBOP_WIRE_SIZE_UINT16 2
#define BEBOP_WIRE_SIZE_INT32 4
#define BEBOP_WIRE_SIZE_UINT32 4
#define BEBOP_WIRE_SIZE_INT64 8
#define BEBOP_WIRE_SIZE_UINT64 8
#define BEBOP_WIRE_SIZE_INT128 16
#define BEBOP_WIRE_SIZE_UINT128 16
#define BEBOP_WIRE_SIZE_FLOAT16 2
#define BEBOP_WIRE_SIZE_FLOAT32 4
#define BEBOP_WIRE_SIZE_FLOAT64 8
#define BEBOP_WIRE_SIZE_BFLOAT16 2
#define BEBOP_WIRE_SIZE_UUID 16
#define BEBOP_WIRE_SIZE_TIMESTAMP 16
#define BEBOP_WIRE_SIZE_DURATION 12
#define BEBOP_WIRE_SIZE_LEN 4
#define BEBOP_WIRE_SIZE_NUL 1

// #endregion

// #region Error Handling

typedef enum {
  BEBOP_RESULT_OK = 0,
  BEBOP_RESULT_MALFORMED = 1,
  BEBOP_RESULT_OVERFLOW = 2,
  BEBOP_RESULT_OOM = 3,
  BEBOP_RESULT_NULL = 4,
  BEBOP_RESULT_INVALID = 5
} Bebop_Result;

// #endregion

// #region Allocator

// Lua-style unified allocator function:
//   ptr==NULL, old==0  -> malloc(new)
//   new==0             -> free(ptr, old), returns NULL
//   otherwise          -> realloc(ptr, old, new)
typedef void* (*Bebop_AllocFn)(void* ptr, size_t old_size, size_t new_size, void* ctx);

typedef struct {
  Bebop_AllocFn alloc;
  void* ctx;
} Bebop_Allocator;

// #endregion

// #region Core Data Types

// clang-format off
#ifndef BEBOP_DEPRECATED
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #ifdef __has_c_attribute
    #if __has_c_attribute(deprecated)
      #define BEBOP_DEPRECATED [[deprecated]]
      #define BEBOP_DEPRECATED_MSG(msg) [[deprecated(msg)]]
    #endif
  #endif
#endif

#ifndef BEBOP_DEPRECATED
  #if defined(__cplusplus) && __cplusplus >= 201402L
    #ifdef __has_cpp_attribute
      #if __has_cpp_attribute(deprecated)
        #define BEBOP_DEPRECATED [[deprecated]]
        #define BEBOP_DEPRECATED_MSG(msg) [[deprecated(msg)]]
      #endif
    #endif
  #endif

  #ifndef BEBOP_DEPRECATED
    #if defined(__GNUC__) || defined(__clang__)
      #define BEBOP_DEPRECATED __attribute__((deprecated))
      #define BEBOP_DEPRECATED_MSG(msg) __attribute__((deprecated(msg)))
    #elif defined(_MSC_VER)
      #define BEBOP_DEPRECATED __declspec(deprecated)
      #define BEBOP_DEPRECATED_MSG(msg) __declspec(deprecated(msg))
    #else
      #define BEBOP_DEPRECATED
      #define BEBOP_DEPRECATED_MSG(msg)
    #endif
  #endif
#endif
#endif
// clang-format on

#if defined(__GNUC__) || defined(__clang__)
#define BEBOP_WIRE_EMPTY_STRUCT uint8_t bebop__wire_empty : 1
#else
#define BEBOP_WIRE_EMPTY_STRUCT uint8_t bebop__wire_empty
#endif

// UUID (16 raw bytes, RFC 4122 compatible)
typedef struct {
  uint8_t bytes[16];
} Bebop_UUID;

// Timestamp - point in time as seconds + nanoseconds since Unix epoch
// with ISO 8601-2 offset in milliseconds
typedef struct {
  int64_t seconds;
  int32_t nanos;
  int32_t offset_ms;
} Bebop_Timestamp;

// Duration - signed time span
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#pragma pack(push, 1)
#endif
typedef struct {
  int64_t seconds;
  int32_t nanos;
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((packed))
#endif
Bebop_Duration;
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#pragma pack(pop)
#endif

#if defined(__FLT16_MAX__) \
    && (defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L) \
        || defined(__clang__) || !defined(__GNUC__))
#define BEBOP_WIRE_HAS_F16 1
typedef _Float16 Bebop_Float16;
#else
#define BEBOP_WIRE_HAS_F16 0
typedef uint16_t Bebop_Float16;
#endif

#if defined(__BFLT16_MAX__)
#define BEBOP_WIRE_HAS_BF16 1
typedef __bf16 Bebop_BFloat16;
#elif defined(__clang__) \
    && (defined(__aarch64__) || defined(__arm__) || defined(__riscv) || defined(__loongarch__) \
        || ((defined(__x86_64__) || defined(__i386__)) && defined(__SSE2__)))
#define BEBOP_WIRE_HAS_BF16 1
typedef __bf16 Bebop_BFloat16;
#else
#define BEBOP_WIRE_HAS_BF16 0
typedef uint16_t Bebop_BFloat16;
#endif

static inline float bebop_bfloat16_to_float(Bebop_BFloat16 v)
{
#if BEBOP_WIRE_HAS_BF16
  return (float)v;
#else
  uint32_t bits = (uint32_t)v << 16;
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
#endif
}

static inline Bebop_BFloat16 bebop_bfloat16_from_float(float f)
{
#if BEBOP_WIRE_HAS_BF16
  return (__bf16)f;
#else
  uint32_t bits;
  memcpy(&bits, &f, sizeof(bits));
  bits += 0x7FFF + ((bits >> 16) & 1);
  return (uint16_t)(bits >> 16);
#endif
}

#if defined(__SIZEOF_INT128__) && !defined(BEBOP_WIRE_NO_I128)
#define BEBOP_WIRE_HAS_I128 1
typedef __uint128_t Bebop_UInt128;
typedef __int128_t Bebop_Int128;
#else
#define BEBOP_WIRE_HAS_I128 0

typedef struct {
  uint64_t low;
  uint64_t high;
} Bebop_UInt128;

typedef struct {
  uint64_t low;
  int64_t high;
} Bebop_Int128;
#endif

// Zero-copy string view
typedef struct {
  const char* data;
  size_t length;
} Bebop_String;

// Zero-copy byte array view
typedef struct {
  const uint8_t* data;
  size_t length;
} Bebop_Bytes;

// Universal immutable view over an exact encoded Bebop value. Generated view
// types build their typed APIs on this lifetime-preserving span.
typedef struct {
  const uint8_t* data;
  size_t length;
} Bebop_View;

typedef struct {
  Bebop_View elements;
  uint32_t count;
} Bebop_SequenceView;

typedef struct {
  Bebop_View entries;
  uint32_t count;
} Bebop_MapView;

// Allocation-free cursor over an encoded array or map. Generated next()
// functions advance remaining and decrement remaining_count. A non-OK result
// ends iteration and records malformed input without requiring error plumbing
// in the loop body.
typedef struct {
  Bebop_View remaining;
  uint32_t remaining_count;
  Bebop_Result result;
} Bebop_ViewIterator;

static inline Bebop_String bebop_string_view(const char* data, size_t length)
{
  return (Bebop_String) {.data = data, .length = length};
}

static inline Bebop_Bytes bebop_bytes(const void* data, size_t length)
{
  return (Bebop_Bytes) {.data = (const uint8_t*)data, .length = length};
}

static inline Bebop_View bebop_view(const void* data, size_t length)
{
  return (Bebop_View) {.data = (const uint8_t*)data, .length = length};
}

// #endregion

// #region Primitive Array Types
//
// Unified array type with capacity field:
//   capacity = 0  -> borrowed view (zero-copy from decode buffer, read-only)
//   capacity > 0  -> owned allocation (can grow via BEBOP_ARRAY_PUSH)
//

typedef struct {
  int8_t* data;
  size_t length;
  size_t capacity;
} Bebop_I8_Array;

typedef struct {
  uint8_t* data;
  size_t length;
  size_t capacity;
} Bebop_U8_Array;

typedef struct {
  int16_t* data;
  size_t length;
  size_t capacity;
} Bebop_I16_Array;

typedef struct {
  uint16_t* data;
  size_t length;
  size_t capacity;
} Bebop_U16_Array;

typedef struct {
  int32_t* data;
  size_t length;
  size_t capacity;
} Bebop_I32_Array;

typedef struct {
  uint32_t* data;
  size_t length;
  size_t capacity;
} Bebop_U32_Array;

typedef struct {
  int64_t* data;
  size_t length;
  size_t capacity;
} Bebop_I64_Array;

typedef struct {
  uint64_t* data;
  size_t length;
  size_t capacity;
} Bebop_U64_Array;

typedef struct {
  Bebop_Int128* data;
  size_t length;
  size_t capacity;
} Bebop_I128_Array;

typedef struct {
  Bebop_UInt128* data;
  size_t length;
  size_t capacity;
} Bebop_U128_Array;

typedef struct {
  Bebop_Float16* data;
  size_t length;
  size_t capacity;
} Bebop_F16_Array;

typedef struct {
  Bebop_BFloat16* data;
  size_t length;
  size_t capacity;
} Bebop_BF16_Array;

typedef struct {
  float* data;
  size_t length;
  size_t capacity;
} Bebop_F32_Array;

typedef struct {
  double* data;
  size_t length;
  size_t capacity;
} Bebop_F64_Array;

typedef struct {
  bool* data;
  size_t length;
  size_t capacity;
} Bebop_Bool_Array;

typedef struct {
  Bebop_UUID* data;
  size_t length;
  size_t capacity;
} Bebop_UUID_Array;

typedef struct {
  Bebop_Timestamp* data;
  size_t length;
  size_t capacity;
} Bebop_Timestamp_Array;

typedef struct {
  Bebop_Duration* data;
  size_t length;
  size_t capacity;
} Bebop_Duration_Array;

typedef struct {
  Bebop_String* data;
  size_t length;
  size_t capacity;
} Bebop_String_Array;

#define BEBOP_ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

// Check if array is a borrowed view (zero-copy from decode)
#define BEBOP_ARRAY_IS_VIEW(arr) ((arr).capacity == 0)

// Initialize an empty owned array
#define BEBOP_ARRAY_INIT(arr) \
  do { \
    (arr).data = NULL; \
    (arr).length = 0; \
    (arr).capacity = 0; \
  } while (0)

// Push element to array, growing if needed (uses arena realloc optimization)
// Asserts if called on a decoded view (capacity=0 but data!=NULL)
#define BEBOP_ARRAY_PUSH(ctx, arr, val) \
  do { \
    assert(((arr).capacity > 0 || (arr).data == NULL) && "cannot push to decoded view"); \
    if ((arr).length >= (arr).capacity) { \
      size_t _old_cap = (arr).capacity; \
      size_t _new_cap = _old_cap ? _old_cap * 2 : 8; \
      (arr).data = bebop_context_realloc( \
          (ctx), (arr).data, _old_cap * sizeof(*(arr).data), _new_cap * sizeof(*(arr).data) \
      ); \
      (arr).capacity = _new_cap; \
    } \
    (arr).data[(arr).length++] = (val); \
  } while (0)

// #endregion

// #region Forward Declarations

typedef struct Bebop_Context Bebop_Context;

typedef struct Bebop_Reader Bebop_Reader;
typedef struct Bebop_Writer Bebop_Writer;
typedef struct Bebop_Any Bebop_Any;

// #endregion

// #region Map Type (SwissTable implementation)

typedef uint64_t (*Bebop_MapHashFn)(const void* key);
typedef bool (*Bebop_MapEqFn)(const void* a, const void* b);

typedef enum {
  BEBOP_MAP_KEY_BOOL,
  BEBOP_MAP_KEY_I8,
  BEBOP_MAP_KEY_U8,
  BEBOP_MAP_KEY_I16,
  BEBOP_MAP_KEY_U16,
  BEBOP_MAP_KEY_I32,
  BEBOP_MAP_KEY_U32,
  BEBOP_MAP_KEY_I64,
  BEBOP_MAP_KEY_U64,
  BEBOP_MAP_KEY_I128,
  BEBOP_MAP_KEY_U128,
  BEBOP_MAP_KEY_STRING,
  BEBOP_MAP_KEY_UUID
} Bebop_MapKeyKind;

typedef struct {
  void* key;
  void* value;
} Bebop_MapSlot;

typedef struct {
  int8_t* ctrl;  // control bytes (H2 or sentinel)
  Bebop_MapSlot* slots;  // key-value pairs
  size_t length;  // number of occupied slots
  size_t capacity;  // number of slots (power of 2)
  size_t growth_left;  // insertions before resize
  Bebop_MapHashFn hash;
  Bebop_MapEqFn eq;
  Bebop_Context* ctx;
} Bebop_Map;

// Iterator for map traversal
typedef struct {
  const Bebop_Map* map;
  size_t index;
} Bebop_MapIter;

BEBOP_API void bebop_map_init(Bebop_Map* map, Bebop_Context* context, Bebop_MapKeyKind key_kind);
BEBOP_API void* bebop_map_get(const Bebop_Map* m, const void* key);
BEBOP_API bool bebop_map_set(Bebop_Map* m, void* key, void* value);
BEBOP_API bool bebop_map_remove(Bebop_Map* m, const void* key);
BEBOP_API void bebop_map_clear(Bebop_Map* m);

// Iterator functions
BEBOP_API void bebop_map_iter_init(Bebop_MapIter* it, const Bebop_Map* m);
BEBOP_API bool bebop_map_iter_next(Bebop_MapIter* it, void** key, void** value);

#ifndef __cplusplus
#define BEBOP_MAP_INIT(map, context, key_type) \
  bebop_map_init( \
      (map), \
      (context), \
      _Generic( \
          (key_type) {0}, \
          bool: BEBOP_MAP_KEY_BOOL, \
          int8_t: BEBOP_MAP_KEY_I8, \
          uint8_t: BEBOP_MAP_KEY_U8, \
          int16_t: BEBOP_MAP_KEY_I16, \
          uint16_t: BEBOP_MAP_KEY_U16, \
          int32_t: BEBOP_MAP_KEY_I32, \
          uint32_t: BEBOP_MAP_KEY_U32, \
          int64_t: BEBOP_MAP_KEY_I64, \
          uint64_t: BEBOP_MAP_KEY_U64, \
          Bebop_Int128: BEBOP_MAP_KEY_I128, \
          Bebop_UInt128: BEBOP_MAP_KEY_U128, \
          Bebop_String: BEBOP_MAP_KEY_STRING, \
          Bebop_UUID: BEBOP_MAP_KEY_UUID \
      ) \
  )
#endif

// #endregion

// #region Optional Type System

#define BEBOP_OPTIONAL(T) \
  struct { \
    bool has_value; \
    T value; \
  }

#define BEBOP_OPTIONAL_FIXED(T, ...) \
  struct { \
    bool has_value; \
    T value __VA_ARGS__; \
  }

#define BEBOP_NONE() {.has_value = false}
#define BEBOP_SOME(val) {.has_value = true, .value = (val)}
#define BEBOP_HAS_VALUE(optional) ((optional).has_value)
#define BEBOP_IS_EMPTY(optional) (!(optional).has_value)
#define BEBOP_VALUE(optional) ((optional).value)
#define BEBOP_VALUE_OR(optional, default_val) \
  ((optional).has_value ? (optional).value : (default_val))

#define BEBOP_SET(optional_field, val) \
  do { \
    (optional_field).has_value = true; \
    (optional_field).value = (val); \
  } while (0)

#define BEBOP_CLEAR(optional_field) \
  do { \
    (optional_field).has_value = false; \
  } while (0)

// #endregion

// #region Value Helpers

#define BEBOP_STRING(s) ((Bebop_String) {.data = (s), .length = sizeof(s) - 1})
#define BEBOP_STRING_EQUALS(s, cstr) \
  ((s).length == sizeof(cstr) - 1 && memcmp((s).data, (cstr), (s).length) == 0)
#define BEBOP_TIMESTAMP(sec, ns) \
  ((Bebop_Timestamp) {.seconds = (sec), .nanos = (ns), .offset_ms = 0})
#define BEBOP_TIMESTAMP_OFFSET(sec, ns, off_ms) \
  ((Bebop_Timestamp) {.seconds = (sec), .nanos = (ns), .offset_ms = (off_ms)})
#define BEBOP_DURATION(sec, ns) ((Bebop_Duration) {.seconds = (sec), .nanos = (ns)})

#if !BEBOP_WIRE_HAS_I128
#define BEBOP_I128(hi, lo) ((Bebop_Int128) {.low = (lo), .high = (hi)})
#define BEBOP_U128(hi, lo) ((Bebop_UInt128) {.low = (lo), .high = (hi)})
#endif

#define BEBOP_BF16(f) bebop_bfloat16_from_float(f)

// #endregion

// #region Context Types

typedef struct {
  size_t initial_block_size;
  size_t max_block_size;
  Bebop_Allocator allocator;
} Bebop_ArenaOptions;

// Caps generated-decoder recursion on untrusted input; 0 means
// BEBOP_WIRE_DEFAULT_MAX_DECODE_DEPTH.
#define BEBOP_WIRE_DEFAULT_MAX_DECODE_DEPTH 64

typedef struct {
  Bebop_ArenaOptions arena_options;
  size_t initial_writer_size;
  uint32_t max_decode_depth;
} Bebop_ContextOptions;

// #endregion

// #region Context API

BEBOP_API Bebop_ContextOptions bebop_context_options(void);
// NULL uses bebop_context_options().
BEBOP_API Bebop_Context* bebop_context_new(const Bebop_ContextOptions* options);
BEBOP_API void bebop_context_free(Bebop_Context* ctx);
BEBOP_API void bebop_context_reset(Bebop_Context* ctx);
BEBOP_API size_t bebop_context_allocated(const Bebop_Context* ctx);
BEBOP_API size_t bebop_context_used(const Bebop_Context* ctx);
BEBOP_API void* bebop_context_alloc(Bebop_Context* ctx, size_t size);
BEBOP_API void* bebop_context_realloc(
    Bebop_Context* ctx, void* ptr, size_t old_size, size_t new_size
);
// Overflow-checked count * elem_size allocation; NULL on overflow or OOM.
BEBOP_API void* bebop_context_alloc_array(Bebop_Context* ctx, size_t count, size_t elem_size);
BEBOP_API Bebop_Result bebop_context_enter_decode(Bebop_Context* ctx);
BEBOP_API void bebop_context_leave_decode(Bebop_Context* ctx);

// #endregion

// #region Reader API

BEBOP_API Bebop_Reader* bebop_context_reader(Bebop_Context* ctx, Bebop_View source);
BEBOP_API void bebop_reader_reset(Bebop_Reader* rd, Bebop_View source);
BEBOP_API void bebop_reader_seek(Bebop_Reader* rd, const uint8_t* pos);
BEBOP_API void bebop_reader_skip(Bebop_Reader* rd, size_t amount);
BEBOP_API size_t bebop_reader_position(const Bebop_Reader* rd);
BEBOP_API const uint8_t* bebop_reader_data(const Bebop_Reader* rd);
BEBOP_API size_t bebop_reader_remaining(const Bebop_Reader* rd);
// Clamp the readable window to len bytes from the current position so nested
// frames cannot read past their parent; restore with bebop_reader_pop_limit().
BEBOP_API Bebop_Result bebop_reader_push_limit(
    Bebop_Reader* rd, uint32_t len, const uint8_t** old_end
);
BEBOP_API void bebop_reader_pop_limit(Bebop_Reader* rd, const uint8_t* old_end);

BEBOP_API Bebop_Result bebop_reader_read_byte(Bebop_Reader* rd, uint8_t* out);
BEBOP_API Bebop_Result bebop_reader_read_i8(Bebop_Reader* rd, int8_t* out);
BEBOP_API Bebop_Result bebop_reader_read_u16(Bebop_Reader* rd, uint16_t* out);
BEBOP_API Bebop_Result bebop_reader_read_u32(Bebop_Reader* rd, uint32_t* out);
BEBOP_API Bebop_Result bebop_reader_read_u64(Bebop_Reader* rd, uint64_t* out);
BEBOP_API Bebop_Result bebop_reader_read_i16(Bebop_Reader* rd, int16_t* out);
BEBOP_API Bebop_Result bebop_reader_read_i32(Bebop_Reader* rd, int32_t* out);
BEBOP_API Bebop_Result bebop_reader_read_i64(Bebop_Reader* rd, int64_t* out);
BEBOP_API Bebop_Result bebop_reader_read_bool(Bebop_Reader* rd, bool* out);
BEBOP_API Bebop_Result bebop_reader_read_f16(Bebop_Reader* rd, Bebop_Float16* out);
BEBOP_API Bebop_Result bebop_reader_read_bf16(Bebop_Reader* rd, Bebop_BFloat16* out);
BEBOP_API Bebop_Result bebop_reader_read_f32(Bebop_Reader* rd, float* out);
BEBOP_API Bebop_Result bebop_reader_read_f64(Bebop_Reader* rd, double* out);
BEBOP_API Bebop_Result bebop_reader_read_i128(Bebop_Reader* rd, Bebop_Int128* out);
BEBOP_API Bebop_Result bebop_reader_read_u128(Bebop_Reader* rd, Bebop_UInt128* out);
BEBOP_API Bebop_Result bebop_reader_read_uuid(Bebop_Reader* rd, Bebop_UUID* out);
BEBOP_API Bebop_Result bebop_reader_read_timestamp(Bebop_Reader* rd, Bebop_Timestamp* out);
BEBOP_API Bebop_Result bebop_reader_read_duration(Bebop_Reader* rd, Bebop_Duration* out);
BEBOP_API Bebop_Result bebop_reader_read_length(Bebop_Reader* rd, uint32_t* out);
BEBOP_API Bebop_Result bebop_reader_read_string(Bebop_Reader* rd, Bebop_String* out);
BEBOP_API Bebop_Result bebop_reader_read_bytes(Bebop_Reader* rd, Bebop_Bytes* out);
BEBOP_API Bebop_Result bebop_reader_read_fixed_bytes(
    Bebop_Reader* rd, size_t count, Bebop_Bytes* out
);

// Fixed array readers
BEBOP_API Bebop_Result bebop_reader_read_fixed_u8_array(
    Bebop_Reader* rd, uint8_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_i8_array(
    Bebop_Reader* rd, int8_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_bool_array(
    Bebop_Reader* rd, bool* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_u16_array(
    Bebop_Reader* rd, uint16_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_i16_array(
    Bebop_Reader* rd, int16_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_u32_array(
    Bebop_Reader* rd, uint32_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_i32_array(
    Bebop_Reader* rd, int32_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_u64_array(
    Bebop_Reader* rd, uint64_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_i64_array(
    Bebop_Reader* rd, int64_t* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_f16_array(
    Bebop_Reader* rd, Bebop_Float16* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_bf16_array(
    Bebop_Reader* rd, Bebop_BFloat16* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_f32_array(
    Bebop_Reader* rd, float* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_f64_array(
    Bebop_Reader* rd, double* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_i128_array(
    Bebop_Reader* rd, Bebop_Int128* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_u128_array(
    Bebop_Reader* rd, Bebop_UInt128* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_uuid_array(
    Bebop_Reader* rd, Bebop_UUID* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_timestamp_array(
    Bebop_Reader* rd, Bebop_Timestamp* out, size_t count
);
BEBOP_API Bebop_Result bebop_reader_read_fixed_duration_array(
    Bebop_Reader* rd, Bebop_Duration* out, size_t count
);

// #endregion

// #region Writer API

BEBOP_API Bebop_Writer* bebop_context_writer(Bebop_Context* ctx, size_t capacity);
BEBOP_API void bebop_writer_reset(Bebop_Writer* w);
BEBOP_API Bebop_Result bebop_writer_reserve(Bebop_Writer* w, size_t additional);
BEBOP_API size_t bebop_writer_length(const Bebop_Writer* w);
BEBOP_API Bebop_View bebop_writer_view(const Bebop_Writer* w);
BEBOP_API size_t bebop_writer_available(const Bebop_Writer* w);

BEBOP_API Bebop_Result bebop_writer_write_byte(Bebop_Writer* w, uint8_t val);
BEBOP_API Bebop_Result bebop_writer_write_i8(Bebop_Writer* w, int8_t val);
BEBOP_API Bebop_Result bebop_writer_write_u16(Bebop_Writer* w, uint16_t val);
BEBOP_API Bebop_Result bebop_writer_write_u32(Bebop_Writer* w, uint32_t val);
BEBOP_API Bebop_Result bebop_writer_write_u64(Bebop_Writer* w, uint64_t val);
BEBOP_API Bebop_Result bebop_writer_write_i16(Bebop_Writer* w, int16_t val);
BEBOP_API Bebop_Result bebop_writer_write_i32(Bebop_Writer* w, int32_t val);
BEBOP_API Bebop_Result bebop_writer_write_i64(Bebop_Writer* w, int64_t val);
BEBOP_API Bebop_Result bebop_writer_write_bool(Bebop_Writer* w, bool val);
BEBOP_API Bebop_Result bebop_writer_write_f16(Bebop_Writer* w, Bebop_Float16 val);
BEBOP_API Bebop_Result bebop_writer_write_bf16(Bebop_Writer* w, Bebop_BFloat16 val);
BEBOP_API Bebop_Result bebop_writer_write_f32(Bebop_Writer* w, float val);
BEBOP_API Bebop_Result bebop_writer_write_f64(Bebop_Writer* w, double val);
BEBOP_API Bebop_Result bebop_writer_write_i128(Bebop_Writer* w, Bebop_Int128 val);
BEBOP_API Bebop_Result bebop_writer_write_u128(Bebop_Writer* w, Bebop_UInt128 val);
BEBOP_API Bebop_Result bebop_writer_write_uuid(Bebop_Writer* w, Bebop_UUID val);
BEBOP_API Bebop_Result bebop_writer_write_timestamp(Bebop_Writer* w, Bebop_Timestamp val);
BEBOP_API Bebop_Result bebop_writer_write_duration(Bebop_Writer* w, Bebop_Duration val);
BEBOP_API Bebop_Result bebop_writer_write_string(Bebop_Writer* w, Bebop_String view);
BEBOP_API Bebop_Result bebop_writer_write_bytes(Bebop_Writer* w, Bebop_Bytes view);
BEBOP_API Bebop_Result bebop_writer_write_fixed_bytes(Bebop_Writer* w, Bebop_Bytes bytes);
BEBOP_API Bebop_Result bebop_writer_begin_length(Bebop_Writer* w, size_t* pos);
BEBOP_API Bebop_Result bebop_writer_end_length(Bebop_Writer* w, size_t pos, uint32_t len);

// Bulk array writers
BEBOP_API Bebop_Result bebop_writer_write_u8_array(
    Bebop_Writer* w, const uint8_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_i8_array(Bebop_Writer* w, const int8_t* data, size_t len);
BEBOP_API Bebop_Result bebop_writer_write_u16_array(
    Bebop_Writer* w, const uint16_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_i16_array(
    Bebop_Writer* w, const int16_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_u32_array(
    Bebop_Writer* w, const uint32_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_i32_array(
    Bebop_Writer* w, const int32_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_u64_array(
    Bebop_Writer* w, const uint64_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_i64_array(
    Bebop_Writer* w, const int64_t* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_f16_array(
    Bebop_Writer* w, const Bebop_Float16* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_bf16_array(
    Bebop_Writer* w, const Bebop_BFloat16* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_f32_array(Bebop_Writer* w, const float* data, size_t len);
BEBOP_API Bebop_Result bebop_writer_write_f64_array(
    Bebop_Writer* w, const double* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_i128_array(
    Bebop_Writer* w, const Bebop_Int128* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_u128_array(
    Bebop_Writer* w, const Bebop_UInt128* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_bool_array(Bebop_Writer* w, const bool* data, size_t len);
BEBOP_API Bebop_Result bebop_writer_write_uuid_array(
    Bebop_Writer* w, const Bebop_UUID* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_timestamp_array(
    Bebop_Writer* w, const Bebop_Timestamp* data, size_t len
);
BEBOP_API Bebop_Result bebop_writer_write_duration_array(
    Bebop_Writer* w, const Bebop_Duration* data, size_t len
);

// Fixed array writers (no length prefix)
BEBOP_API Bebop_Result bebop_writer_write_fixed_u8_array(
    Bebop_Writer* w, const uint8_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_i8_array(
    Bebop_Writer* w, const int8_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_bool_array(
    Bebop_Writer* w, const bool* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_u16_array(
    Bebop_Writer* w, const uint16_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_i16_array(
    Bebop_Writer* w, const int16_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_u32_array(
    Bebop_Writer* w, const uint32_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_i32_array(
    Bebop_Writer* w, const int32_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_u64_array(
    Bebop_Writer* w, const uint64_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_i64_array(
    Bebop_Writer* w, const int64_t* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_f16_array(
    Bebop_Writer* w, const Bebop_Float16* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_bf16_array(
    Bebop_Writer* w, const Bebop_BFloat16* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_f32_array(
    Bebop_Writer* w, const float* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_f64_array(
    Bebop_Writer* w, const double* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_i128_array(
    Bebop_Writer* w, const Bebop_Int128* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_u128_array(
    Bebop_Writer* w, const Bebop_UInt128* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_uuid_array(
    Bebop_Writer* w, const Bebop_UUID* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_timestamp_array(
    Bebop_Writer* w, const Bebop_Timestamp* data, size_t count
);
BEBOP_API Bebop_Result bebop_writer_write_fixed_duration_array(
    Bebop_Writer* w, const Bebop_Duration* data, size_t count
);

// #endregion

// #region Utility Functions

BEBOP_API Bebop_UUID bebop_uuid_parse(const char* str);
BEBOP_API size_t bebop_uuid_format(Bebop_UUID uuid, char* buf, size_t len);
BEBOP_API bool bebop_uuid_equal(Bebop_UUID a, Bebop_UUID b);
BEBOP_API Bebop_String bebop_string(const char* str);
BEBOP_API bool bebop_string_equal(Bebop_String a, Bebop_String b);

// #endregion

// #region Any Type Helpers

extern const char BEBOP_TYPE_URL_PREFIX[];

typedef size_t (*Bebop_SizeFn)(const void* record);
typedef Bebop_Result (*Bebop_EncodeFn)(Bebop_Writer* w, const void* record);
typedef Bebop_Result (*Bebop_DecodeFn)(Bebop_Context* ctx, Bebop_Reader* r, void* record);

typedef struct {
  const char* type_fqn;
  const char* prefix;
  Bebop_SizeFn size_fn;
  Bebop_EncodeFn encode_fn;
  Bebop_DecodeFn decode_fn;
} Bebop_TypeInfo;

BEBOP_API Bebop_Result bebop_any_pack(
    Bebop_Context* ctx, Bebop_Any* any, const void* record, const Bebop_TypeInfo* type_info
);

BEBOP_API bool bebop_any_is(const Bebop_Any* any, const char* type_fqn);

BEBOP_API const char* bebop_any_type_name(const Bebop_Any* any);

BEBOP_API Bebop_Result bebop_any_unpack(
    Bebop_Context* ctx, const Bebop_Any* any, void* record, const Bebop_TypeInfo* type_info
);

// #endregion

// #region Reflection Types

#define BEBOP_REFLECTION_MAGIC 0xBEB09C00

typedef enum {
  BEBOP_REFLECTION_TYPE_BOOL = 1,
  BEBOP_REFLECTION_TYPE_BYTE = 2,
  BEBOP_REFLECTION_TYPE_INT8 = 3,
  BEBOP_REFLECTION_TYPE_INT16 = 4,
  BEBOP_REFLECTION_TYPE_UINT16 = 5,
  BEBOP_REFLECTION_TYPE_INT32 = 6,
  BEBOP_REFLECTION_TYPE_UINT32 = 7,
  BEBOP_REFLECTION_TYPE_INT64 = 8,
  BEBOP_REFLECTION_TYPE_UINT64 = 9,
  BEBOP_REFLECTION_TYPE_INT128 = 10,
  BEBOP_REFLECTION_TYPE_UINT128 = 11,
  BEBOP_REFLECTION_TYPE_FLOAT16 = 12,
  BEBOP_REFLECTION_TYPE_FLOAT32 = 13,
  BEBOP_REFLECTION_TYPE_FLOAT64 = 14,
  BEBOP_REFLECTION_TYPE_BFLOAT16 = 15,
  BEBOP_REFLECTION_TYPE_STRING = 16,
  BEBOP_REFLECTION_TYPE_UUID = 17,
  BEBOP_REFLECTION_TYPE_TIMESTAMP = 18,
  BEBOP_REFLECTION_TYPE_DURATION = 19,
  BEBOP_REFLECTION_TYPE_ARRAY = 20,
  BEBOP_REFLECTION_TYPE_FIXED_ARRAY = 21,
  BEBOP_REFLECTION_TYPE_MAP = 22,
  BEBOP_REFLECTION_TYPE_DEFINED = 23,
} BebopReflection_TypeKind;

typedef enum {
  BEBOP_REFLECTION_DEF_ENUM = 1,
  BEBOP_REFLECTION_DEF_STRUCT = 2,
  BEBOP_REFLECTION_DEF_MESSAGE = 3,
  BEBOP_REFLECTION_DEF_UNION = 4,
  BEBOP_REFLECTION_DEF_SERVICE = 5,
} BebopReflection_DefKind;

typedef enum {
  BEBOP_REFLECTION_METHOD_UNARY = 1,
  BEBOP_REFLECTION_METHOD_SERVER_STREAM = 2,
  BEBOP_REFLECTION_METHOD_CLIENT_STREAM = 3,
  BEBOP_REFLECTION_METHOD_DUPLEX_STREAM = 4,
} BebopReflection_MethodType;

typedef struct BebopReflection_TypeDescriptor BebopReflection_TypeDescriptor;

struct BebopReflection_TypeDescriptor {
  BebopReflection_TypeKind kind;
  const BebopReflection_TypeDescriptor* element;
  const BebopReflection_TypeDescriptor* key;
  const BebopReflection_TypeDescriptor* value;
  uint32_t fixed_size;
  const char* fqn;
};

typedef struct {
  const char* name;
  const BebopReflection_TypeDescriptor* type;
  uint32_t index;
  size_t offset;
} BebopReflection_FieldDescriptor;

typedef struct {
  const char* name;
  int64_t value;
} BebopReflection_EnumMemberDescriptor;

typedef struct {
  uint8_t discriminator;
  const char* name;
  const char* type_fqn;
  size_t offset;
} BebopReflection_UnionBranchDescriptor;

typedef struct {
  const char* name;
  const BebopReflection_TypeDescriptor* request;
  const BebopReflection_TypeDescriptor* response;
  uint32_t id;
  BebopReflection_MethodType method_type;
} BebopReflection_MethodDescriptor;

typedef struct {
  BebopReflection_TypeKind base_type;
  uint32_t n_members;
  const BebopReflection_EnumMemberDescriptor* members;
  bool is_flags;
} BebopReflection_EnumDef;

typedef struct {
  uint32_t n_fields;
  const BebopReflection_FieldDescriptor* fields;
  size_t sizeof_type;
  uint32_t fixed_size;
  bool is_mutable;
} BebopReflection_StructDef;

typedef struct {
  uint32_t n_fields;
  const BebopReflection_FieldDescriptor* fields;
  size_t sizeof_type;
} BebopReflection_MessageDef;

typedef struct {
  uint32_t n_branches;
  const BebopReflection_UnionBranchDescriptor* branches;
  size_t sizeof_type;
} BebopReflection_UnionDef;

typedef struct {
  uint32_t n_methods;
  const BebopReflection_MethodDescriptor* methods;
} BebopReflection_ServiceDef;

typedef struct BebopReflection_DefinitionDescriptor BebopReflection_DefinitionDescriptor;

struct BebopReflection_DefinitionDescriptor {
  uint32_t magic;
  BebopReflection_DefKind kind;
  const char* name;
  const char* fqn;
  const char* package;

  union {
    BebopReflection_EnumDef enum_def;
    BebopReflection_StructDef struct_def;
    BebopReflection_MessageDef message_def;
    BebopReflection_UnionDef union_def;
    BebopReflection_ServiceDef service_def;
  };
};

extern const BebopReflection_TypeDescriptor BebopReflection_Type_Bool;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Byte;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Int8;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Int16;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_UInt16;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Int32;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_UInt32;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Int64;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_UInt64;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Int128;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_UInt128;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Float16;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Float32;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Float64;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_BFloat16;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_String;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_UUID;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Timestamp;
extern const BebopReflection_TypeDescriptor BebopReflection_Type_Duration;

// #endregion

// #region Static Assertions

#define BEBOP_WIRE_UUID_STR_LEN 36

_Static_assert(sizeof(int8_t) == 1, "sizeof(int8_t) should be 1");
_Static_assert(sizeof(uint8_t) == 1, "sizeof(uint8_t) should be 1");
_Static_assert(sizeof(uint16_t) == 2, "sizeof(uint16_t) should be 2");
_Static_assert(sizeof(uint32_t) == 4, "sizeof(uint32_t) should be 4");
_Static_assert(sizeof(uint64_t) == 8, "sizeof(uint64_t) should be 8");
_Static_assert(sizeof(float) == 4, "sizeof(float) should be 4");
_Static_assert(sizeof(double) == 8, "sizeof(double) should be 8");
_Static_assert(sizeof(Bebop_Float16) == 2, "sizeof(Bebop_Float16) should be 2");
_Static_assert(sizeof(Bebop_BFloat16) == 2, "sizeof(Bebop_BFloat16) should be 2");
_Static_assert(sizeof(Bebop_Int128) == 16, "sizeof(Bebop_Int128) should be 16");
_Static_assert(sizeof(Bebop_UInt128) == 16, "sizeof(Bebop_UInt128) should be 16");
_Static_assert(sizeof(Bebop_UUID) == 16, "sizeof(Bebop_UUID) should be 16");
_Static_assert(sizeof(Bebop_Timestamp) == 16, "sizeof(Bebop_Timestamp) should be 16");
_Static_assert(sizeof(Bebop_Duration) == 12, "sizeof(Bebop_Duration) should be 12");

// #endregion


#ifdef __cplusplus
}
#endif

#endif
