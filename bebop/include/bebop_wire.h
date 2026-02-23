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
#define BEBOP_WIRE_COLD __attribute__((cold))
#define BEBOP_WIRE_PURE __attribute__((pure))
#define BEBOP_WIRE_FLATTEN __attribute__((flatten))
#else
#define BEBOP_WIRE_HOT
#define BEBOP_WIRE_COLD
#define BEBOP_WIRE_PURE
#define BEBOP_WIRE_FLATTEN
#endif
#endif

#ifndef BEBOP_WIRE_RESTRICT
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define BEBOP_WIRE_RESTRICT __restrict
#else
#define BEBOP_WIRE_RESTRICT
#endif
#endif

#ifndef BEBOP_WIRE_TRY
#define BEBOP_WIRE_TRY(expr) do { \
    Bebop_WireResult _r = (expr); \
    if (BEBOP_WIRE_UNLIKELY(_r != BEBOP_WIRE_OK)) return _r; \
} while(0)
#define BEBOP_WIRE_TRY_NEG(expr) do { \
    Bebop_WireResult _r = (expr); \
    if (BEBOP_WIRE_UNLIKELY(_r != BEBOP_WIRE_OK)) return -(int)_r; \
} while(0)
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
  BEBOP_WIRE_OK = 0,
  BEBOP_WIRE_ERR_MALFORMED = 1,
  BEBOP_WIRE_ERR_OVERFLOW = 2,
  BEBOP_WIRE_ERR_OOM = 3,
  BEBOP_WIRE_ERR_NULL = 4,
  BEBOP_WIRE_ERR_INVALID = 5
} Bebop_WireResult;

// #endregion

// #region Allocator

// Lua-style unified allocator function:
//   ptr==NULL, old==0  -> malloc(new)
//   new==0             -> free(ptr, old), returns NULL
//   otherwise          -> realloc(ptr, old, new)
typedef void* (*Bebop_WireAllocFn)(void* ptr, size_t old_size, size_t new_size, void* ctx);

typedef struct {
  Bebop_WireAllocFn alloc;
  void* ctx;
} Bebop_WireAllocator;

// #endregion

// #region Core Data Types

// clang-format off
#ifndef BEBOP_WIRE_DEPRECATED
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #ifdef __has_c_attribute
    #if __has_c_attribute(deprecated)
      #define BEBOP_WIRE_DEPRECATED [[deprecated]]
      #define BEBOP_WIRE_DEPRECATED_MSG(msg) [[deprecated(msg)]]
    #endif
  #endif
#endif

#ifndef BEBOP_WIRE_DEPRECATED
  #if defined(__cplusplus) && __cplusplus >= 201402L
    #ifdef __has_cpp_attribute
      #if __has_cpp_attribute(deprecated)
        #define BEBOP_WIRE_DEPRECATED [[deprecated]]
        #define BEBOP_WIRE_DEPRECATED_MSG(msg) [[deprecated(msg)]]
      #endif
    #endif
  #endif

  #ifndef BEBOP_WIRE_DEPRECATED
    #if defined(__GNUC__) || defined(__clang__)
      #define BEBOP_WIRE_DEPRECATED __attribute__((deprecated))
      #define BEBOP_WIRE_DEPRECATED_MSG(msg) __attribute__((deprecated(msg)))
    #elif defined(_MSC_VER)
      #define BEBOP_WIRE_DEPRECATED __declspec(deprecated)
      #define BEBOP_WIRE_DEPRECATED_MSG(msg) __declspec(deprecated(msg))
    #else
      #define BEBOP_WIRE_DEPRECATED
      #define BEBOP_WIRE_DEPRECATED_MSG(msg)
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
}
Bebop_Timestamp;

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

static inline float Bebop_BFloat16_ToFloat(Bebop_BFloat16 v)
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

static inline Bebop_BFloat16 Bebop_BFloat16_FromFloat(float f)
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
} Bebop_Str;

// Zero-copy byte array view
typedef struct {
  const uint8_t* data;
  size_t length;
} Bebop_Bytes;

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
  Bebop_Str* data;
  size_t length;
  size_t capacity;
} Bebop_Str_Array;

#define BEBOP_ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BEBOP_DECONST(type, ptr) ((type*)&(ptr))

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
#define BEBOP_WIRE_ARRAY_PUSH(ctx, arr, val) \
  do { \
    assert(((arr).capacity > 0 || (arr).data == NULL) && "cannot push to decoded view"); \
    if ((arr).length >= (arr).capacity) { \
      size_t _old_cap = (arr).capacity; \
      size_t _new_cap = _old_cap ? _old_cap * 2 : 8; \
      (arr).data = Bebop_WireCtx_Realloc( \
          (ctx), (arr).data, _old_cap * sizeof(*(arr).data), _new_cap * sizeof(*(arr).data) \
      ); \
      (arr).capacity = _new_cap; \
    } \
    (arr).data[(arr).length++] = (val); \
  } while (0)

// #endregion

// #region Forward Declarations

typedef struct Bebop_WireCtx Bebop_WireCtx;
typedef struct Bebop_Reader Bebop_Reader;
typedef struct Bebop_Writer Bebop_Writer;
typedef struct Bebop_Any Bebop_Any;

// #endregion

// #region Map Type (SwissTable implementation)

typedef uint64_t (*Bebop_MapHashFn)(const void* key);
typedef bool (*Bebop_MapEqFn)(const void* a, const void* b);

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
  Bebop_WireCtx* ctx;
} Bebop_Map;

// Iterator for map traversal
typedef struct {
  const Bebop_Map* map;
  size_t index;
} Bebop_MapIter;

BEBOP_API void Bebop_Map_Init(
    Bebop_Map* m, Bebop_WireCtx* ctx, Bebop_MapHashFn hash, Bebop_MapEqFn eq
);
BEBOP_API void* Bebop_Map_Get(const Bebop_Map* m, const void* key);
BEBOP_API bool Bebop_Map_Put(Bebop_Map* m, void* key, void* value);
BEBOP_API bool Bebop_Map_Del(Bebop_Map* m, const void* key);
BEBOP_API void Bebop_Map_Clear(Bebop_Map* m);

// Iterator functions
BEBOP_API void Bebop_MapIter_Init(Bebop_MapIter* it, const Bebop_Map* m);
BEBOP_API bool Bebop_MapIter_Next(Bebop_MapIter* it, void** key, void** value);

// Map key hash functions (for use with Bebop_Map_Init)
BEBOP_API uint64_t Bebop_MapHash_Bool(const void* key);
BEBOP_API uint64_t Bebop_MapHash_Byte(const void* key);
BEBOP_API uint64_t Bebop_MapHash_I8(const void* key);
BEBOP_API uint64_t Bebop_MapHash_U8(const void* key);
BEBOP_API uint64_t Bebop_MapHash_I16(const void* key);
BEBOP_API uint64_t Bebop_MapHash_U16(const void* key);
BEBOP_API uint64_t Bebop_MapHash_I32(const void* key);
BEBOP_API uint64_t Bebop_MapHash_U32(const void* key);
BEBOP_API uint64_t Bebop_MapHash_I64(const void* key);
BEBOP_API uint64_t Bebop_MapHash_U64(const void* key);
BEBOP_API uint64_t Bebop_MapHash_I128(const void* key);
BEBOP_API uint64_t Bebop_MapHash_U128(const void* key);
BEBOP_API uint64_t Bebop_MapHash_Str(const void* key);
BEBOP_API uint64_t Bebop_MapHash_UUID(const void* key);

// Map key equality functions (for use with Bebop_Map_Init)
BEBOP_API bool Bebop_MapEq_Bool(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_Byte(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_I8(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_U8(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_I16(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_U16(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_I32(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_U32(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_I64(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_U64(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_I128(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_U128(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_Str(const void* a, const void* b);
BEBOP_API bool Bebop_MapEq_UUID(const void* a, const void* b);

// Type-safe access macros
#define BEBOP_MAP_KEY(bucket, type) ((type*)(bucket)->key)
#define BEBOP_MAP_VAL(bucket, type) ((type*)(bucket)->value)

// Type-specific map initialization macros
#define BEBOP_MAP_INIT_BOOL(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_Bool, Bebop_MapEq_Bool)
#define BEBOP_MAP_INIT_BYTE(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_Byte, Bebop_MapEq_Byte)
#define BEBOP_MAP_INIT_I8(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_I8, Bebop_MapEq_I8)
#define BEBOP_MAP_INIT_U8(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_U8, Bebop_MapEq_U8)
#define BEBOP_MAP_INIT_I16(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_I16, Bebop_MapEq_I16)
#define BEBOP_MAP_INIT_U16(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_U16, Bebop_MapEq_U16)
#define BEBOP_MAP_INIT_I32(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_I32, Bebop_MapEq_I32)
#define BEBOP_MAP_INIT_U32(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_U32, Bebop_MapEq_U32)
#define BEBOP_MAP_INIT_I64(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_I64, Bebop_MapEq_I64)
#define BEBOP_MAP_INIT_U64(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_U64, Bebop_MapEq_U64)
#define BEBOP_MAP_INIT_I128(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_I128, Bebop_MapEq_I128)
#define BEBOP_MAP_INIT_U128(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_U128, Bebop_MapEq_U128)
#define BEBOP_MAP_INIT_STR(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_Str, Bebop_MapEq_Str)
#define BEBOP_MAP_INIT_UUID(m, ctx) Bebop_Map_Init(m, ctx, Bebop_MapHash_UUID, Bebop_MapEq_UUID)

// Helper to allocate and put a string-keyed entry
#define BEBOP_MAP_PUT_STR(ctx, m, key_str, val_ptr) \
  do { \
    Bebop_Str* _k = (Bebop_Str*)Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Str)); \
    if (_k) { \
      _k->data = (key_str); \
      _k->length = (uint32_t)strlen(key_str); \
      Bebop_Map_Put(m, _k, val_ptr); \
    } \
  } while (0)

// #endregion

// #region Optional Type System

#define BEBOP_WIRE_OPT(T) \
  struct { \
    bool has_value; \
    T value; \
  }

#define BEBOP_WIRE_OPT_FA(T, ...) \
  struct { \
    bool has_value; \
    T value __VA_ARGS__; \
  }

#define BEBOP_WIRE_NONE() {.has_value = false}
#define BEBOP_WIRE_SOME(val) {.has_value = true, .value = (val)}
#define BEBOP_WIRE_IS_SOME(optional) ((optional).has_value)
#define BEBOP_WIRE_IS_NONE(optional) (!(optional).has_value)
#define BEBOP_WIRE_UNWRAP(optional) ((optional).value)
#define BEBOP_WIRE_UNWRAP_OR(optional, default_val) \
  ((optional).has_value ? (optional).value : (default_val))

#define BEBOP_WIRE_SET_SOME(optional_field, val) \
  do { \
    (optional_field).has_value = true; \
    (optional_field).value = (val); \
  } while (0)

#define BEBOP_WIRE_SET_NONE(optional_field) \
  do { \
    (optional_field).has_value = false; \
  } while (0)

#define BEBOP_WIRE_SET_OPT(writer, optional, write_func) \
  do { \
    Bebop_WireResult _res = Bebop_Writer_SetBool(writer, (optional).has_value); \
    if (_res != BEBOP_WIRE_OK) \
      return _res; \
    if ((optional).has_value) { \
      _res = write_func(writer, (optional).value); \
      if (_res != BEBOP_WIRE_OK) \
        return _res; \
    } \
  } while (0)

#define BEBOP_WIRE_GET_OPT(reader, optional_ptr, read_func) \
  do { \
    Bebop_WireResult _res = Bebop_Reader_GetBool(reader, &(optional_ptr)->has_value); \
    if (_res != BEBOP_WIRE_OK) \
      return _res; \
    if ((optional_ptr)->has_value) { \
      _res = read_func(reader, &(optional_ptr)->value); \
      if (_res != BEBOP_WIRE_OK) \
        return _res; \
    } \
  } while (0)

// #endregion

// #region Builder Helpers

#define BEBOP_WIRE_STR(s) ((Bebop_Str) {.data = (s), .length = sizeof(s) - 1})
#define BEBOP_WIRE_STR_N(ptr, len) ((Bebop_Str) {.data = (ptr), .length = (len)})
#define BEBOP_WIRE_STR_EQ(s, cstr) \
  ((s).length == sizeof(cstr) - 1 && memcmp((s).data, (cstr), (s).length) == 0)
#define BEBOP_WIRE_STR_EQ_N(s, cstr) \
  ((s).length == strlen(cstr) && memcmp((s).data, (cstr), (s).length) == 0)
#define BEBOP_WIRE_STR_CMP(a, b) \
  ((a).length == (b).length && memcmp((a).data, (b).data, (a).length) == 0)
#define BEBOP_WIRE_BYTES(ptr, len) ((Bebop_Bytes) {.data = (const uint8_t*)(ptr), .length = (len)})
#define BEBOP_WIRE_UUID(s) Bebop_UUID_FromString(s)
#define BEBOP_WIRE_TIMESTAMP(sec, ns) ((Bebop_Timestamp) {.seconds = (sec), .nanos = (ns), .offset_ms = 0})
#define BEBOP_WIRE_TIMESTAMP_OFFSET(sec, ns, off_ms) ((Bebop_Timestamp) {.seconds = (sec), .nanos = (ns), .offset_ms = (off_ms)})
#define BEBOP_WIRE_DURATION(sec, ns) ((Bebop_Duration) {.seconds = (sec), .nanos = (ns)})

#if !BEBOP_WIRE_HAS_I128
#define BEBOP_WIRE_I128(hi, lo) ((Bebop_Int128) {.low = (lo), .high = (hi)})
#define BEBOP_WIRE_U128(hi, lo) ((Bebop_UInt128) {.low = (lo), .high = (hi)})
#endif

#define BEBOP_WIRE_BF16(f) Bebop_BFloat16_FromFloat(f)

// #endregion

// #region Context Types

typedef struct {
  size_t initial_block_size;
  size_t max_block_size;
  Bebop_WireAllocator allocator;
} Bebop_ArenaOpts;

typedef struct {
  Bebop_ArenaOpts arena_options;
  size_t initial_writer_size;
} Bebop_WireCtxOpts;

// #endregion

// #region Context API

BEBOP_API Bebop_WireCtx* Bebop_WireCtx_New(const Bebop_WireCtxOpts* options);
BEBOP_API void Bebop_WireCtx_Free(Bebop_WireCtx* ctx);
BEBOP_API void Bebop_WireCtx_Reset(Bebop_WireCtx* ctx);
BEBOP_API size_t Bebop_WireCtx_Allocated(const Bebop_WireCtx* ctx);
BEBOP_API size_t Bebop_WireCtx_Used(const Bebop_WireCtx* ctx);
BEBOP_API void* Bebop_WireCtx_Alloc(Bebop_WireCtx* ctx, size_t size);
BEBOP_API void* Bebop_WireCtx_Realloc(
    Bebop_WireCtx* ctx, void* ptr, size_t old_size, size_t new_size
);
BEBOP_API Bebop_WireCtxOpts Bebop_WireCtx_DefaultOpts(void);

// #endregion

// #region Reader API

BEBOP_API Bebop_WireResult Bebop_WireCtx_Reader(
    Bebop_WireCtx* ctx, const uint8_t* buf, size_t len, Bebop_Reader** out
);
BEBOP_API void Bebop_Reader_Reset(Bebop_Reader* rd, const uint8_t* buf, size_t len);
BEBOP_API void Bebop_Reader_Seek(Bebop_Reader* rd, const uint8_t* pos);
BEBOP_API void Bebop_Reader_Skip(Bebop_Reader* rd, size_t amount);
BEBOP_API size_t Bebop_Reader_Pos(const Bebop_Reader* rd);
BEBOP_API const uint8_t* Bebop_Reader_Ptr(const Bebop_Reader* rd);

BEBOP_API Bebop_WireResult Bebop_Reader_GetByte(Bebop_Reader* rd, uint8_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetI8(Bebop_Reader* rd, int8_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetU16(Bebop_Reader* rd, uint16_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetU32(Bebop_Reader* rd, uint32_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetU64(Bebop_Reader* rd, uint64_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetI16(Bebop_Reader* rd, int16_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetI32(Bebop_Reader* rd, int32_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetI64(Bebop_Reader* rd, int64_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetBool(Bebop_Reader* rd, bool* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetF16(Bebop_Reader* rd, Bebop_Float16* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetBF16(Bebop_Reader* rd, Bebop_BFloat16* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetF32(Bebop_Reader* rd, float* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetF64(Bebop_Reader* rd, double* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetI128(Bebop_Reader* rd, Bebop_Int128* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetU128(Bebop_Reader* rd, Bebop_UInt128* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetUUID(Bebop_Reader* rd, Bebop_UUID* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetTimestamp(Bebop_Reader* rd, Bebop_Timestamp* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetDuration(Bebop_Reader* rd, Bebop_Duration* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetLen(Bebop_Reader* rd, uint32_t* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetStr(Bebop_Reader* rd, Bebop_Str* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetByteArray(Bebop_Reader* rd, Bebop_Bytes* out);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedBytes(
    Bebop_Reader* rd, size_t count, Bebop_Bytes* out
);

// Fixed array readers
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedU8Array(
    Bebop_Reader* rd, uint8_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedI8Array(
    Bebop_Reader* rd, int8_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedBoolArray(
    Bebop_Reader* rd, bool* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedU16Array(
    Bebop_Reader* rd, uint16_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedI16Array(
    Bebop_Reader* rd, int16_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedU32Array(
    Bebop_Reader* rd, uint32_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedI32Array(
    Bebop_Reader* rd, int32_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedU64Array(
    Bebop_Reader* rd, uint64_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedI64Array(
    Bebop_Reader* rd, int64_t* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedF16Array(
    Bebop_Reader* rd, Bebop_Float16* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedBF16Array(
    Bebop_Reader* rd, Bebop_BFloat16* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedF32Array(
    Bebop_Reader* rd, float* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedF64Array(
    Bebop_Reader* rd, double* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedI128Array(
    Bebop_Reader* rd, Bebop_Int128* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedU128Array(
    Bebop_Reader* rd, Bebop_UInt128* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedUUIDArray(
    Bebop_Reader* rd, Bebop_UUID* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedTimestampArray(
    Bebop_Reader* rd, Bebop_Timestamp* out, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Reader_GetFixedDurationArray(
    Bebop_Reader* rd, Bebop_Duration* out, size_t count
);

// #endregion

// #region Writer API

BEBOP_API Bebop_WireResult Bebop_WireCtx_Writer(Bebop_WireCtx* ctx, Bebop_Writer** out);
BEBOP_API Bebop_WireResult Bebop_WireCtx_WriterHint(
    Bebop_WireCtx* ctx, size_t hint, Bebop_Writer** out
);
BEBOP_API void Bebop_Writer_Reset(Bebop_Writer* w);
BEBOP_API Bebop_WireResult Bebop_Writer_Ensure(Bebop_Writer* w, size_t additional);
BEBOP_API size_t Bebop_Writer_Len(const Bebop_Writer* w);
BEBOP_API size_t Bebop_Writer_Remaining(const Bebop_Writer* w);
BEBOP_API Bebop_WireResult Bebop_Writer_Buf(Bebop_Writer* w, uint8_t** buf, size_t* len);

BEBOP_API Bebop_WireResult Bebop_Writer_SetByte(Bebop_Writer* w, uint8_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI8(Bebop_Writer* w, int8_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU16(Bebop_Writer* w, uint16_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU32(Bebop_Writer* w, uint32_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU64(Bebop_Writer* w, uint64_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI16(Bebop_Writer* w, int16_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI32(Bebop_Writer* w, int32_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI64(Bebop_Writer* w, int64_t val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetBool(Bebop_Writer* w, bool val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetF16(Bebop_Writer* w, Bebop_Float16 val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetBF16(Bebop_Writer* w, Bebop_BFloat16 val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetF32(Bebop_Writer* w, float val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetF64(Bebop_Writer* w, double val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI128(Bebop_Writer* w, Bebop_Int128 val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU128(Bebop_Writer* w, Bebop_UInt128 val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetUUID(Bebop_Writer* w, Bebop_UUID val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetTimestamp(Bebop_Writer* w, Bebop_Timestamp val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetDuration(Bebop_Writer* w, Bebop_Duration val);
BEBOP_API Bebop_WireResult Bebop_Writer_SetStr(Bebop_Writer* w, const char* data, size_t len);
BEBOP_API Bebop_WireResult Bebop_Writer_SetStrView(Bebop_Writer* w, Bebop_Str view);
BEBOP_API Bebop_WireResult Bebop_Writer_SetByteArray(
    Bebop_Writer* w, const uint8_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetByteArrayView(Bebop_Writer* w, Bebop_Bytes view);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedBytes(
    Bebop_Writer* w, const uint8_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetLen(Bebop_Writer* w, size_t* pos);
BEBOP_API Bebop_WireResult Bebop_Writer_FillLen(Bebop_Writer* w, size_t pos, uint32_t len);

// Bulk array writers
BEBOP_API Bebop_WireResult Bebop_Writer_SetU8Array(
    Bebop_Writer* w, const uint8_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI8Array(Bebop_Writer* w, const int8_t* data, size_t len);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU16Array(
    Bebop_Writer* w, const uint16_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI16Array(
    Bebop_Writer* w, const int16_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU32Array(
    Bebop_Writer* w, const uint32_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI32Array(
    Bebop_Writer* w, const int32_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU64Array(
    Bebop_Writer* w, const uint64_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI64Array(
    Bebop_Writer* w, const int64_t* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetF16Array(
    Bebop_Writer* w, const Bebop_Float16* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetBF16Array(
    Bebop_Writer* w, const Bebop_BFloat16* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetF32Array(Bebop_Writer* w, const float* data, size_t len);
BEBOP_API Bebop_WireResult Bebop_Writer_SetF64Array(
    Bebop_Writer* w, const double* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetI128Array(
    Bebop_Writer* w, const Bebop_Int128* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetU128Array(
    Bebop_Writer* w, const Bebop_UInt128* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetBoolArray(Bebop_Writer* w, const bool* data, size_t len);
BEBOP_API Bebop_WireResult Bebop_Writer_SetUUIDArray(
    Bebop_Writer* w, const Bebop_UUID* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetTimestampArray(
    Bebop_Writer* w, const Bebop_Timestamp* data, size_t len
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetDurationArray(
    Bebop_Writer* w, const Bebop_Duration* data, size_t len
);

// Fixed array writers (no length prefix)
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedU8Array(
    Bebop_Writer* w, const uint8_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedI8Array(
    Bebop_Writer* w, const int8_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedBoolArray(
    Bebop_Writer* w, const bool* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedU16Array(
    Bebop_Writer* w, const uint16_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedI16Array(
    Bebop_Writer* w, const int16_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedU32Array(
    Bebop_Writer* w, const uint32_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedI32Array(
    Bebop_Writer* w, const int32_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedU64Array(
    Bebop_Writer* w, const uint64_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedI64Array(
    Bebop_Writer* w, const int64_t* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedF16Array(
    Bebop_Writer* w, const Bebop_Float16* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedBF16Array(
    Bebop_Writer* w, const Bebop_BFloat16* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedF32Array(
    Bebop_Writer* w, const float* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedF64Array(
    Bebop_Writer* w, const double* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedI128Array(
    Bebop_Writer* w, const Bebop_Int128* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedU128Array(
    Bebop_Writer* w, const Bebop_UInt128* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedUUIDArray(
    Bebop_Writer* w, const Bebop_UUID* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedTimestampArray(
    Bebop_Writer* w, const Bebop_Timestamp* data, size_t count
);
BEBOP_API Bebop_WireResult Bebop_Writer_SetFixedDurationArray(
    Bebop_Writer* w, const Bebop_Duration* data, size_t count
);

// #endregion

// #region Utility Functions

BEBOP_API Bebop_UUID Bebop_UUID_FromString(const char* str);
BEBOP_API size_t Bebop_UUID_ToString(Bebop_UUID uuid, char* buf, size_t len);
BEBOP_API bool Bebop_UUID_Equal(Bebop_UUID a, Bebop_UUID b);
BEBOP_API Bebop_Str Bebop_Str_FromCStr(const char* str);
BEBOP_API bool Bebop_Str_Equal(Bebop_Str a, Bebop_Str b);

// #endregion

// #region Any Type Helpers

extern const char BEBOP_TYPE_URL_PREFIX[];

typedef size_t (*Bebop_SizeFn)(const void* record);
typedef Bebop_WireResult (*Bebop_EncodeFn)(Bebop_Writer* w, const void* record);
typedef Bebop_WireResult (*Bebop_DecodeFn)(Bebop_WireCtx* ctx, Bebop_Reader* r, void* record);

typedef struct {
  const char* type_fqn;
  const char* prefix;
  Bebop_SizeFn size_fn;
  Bebop_EncodeFn encode_fn;
  Bebop_DecodeFn decode_fn;
} Bebop_TypeInfo;

BEBOP_API Bebop_WireResult Bebop_Any_Pack(
    Bebop_WireCtx* ctx, Bebop_Any* any, const void* record, const Bebop_TypeInfo* type_info
);

BEBOP_API bool Bebop_Any_Is(const Bebop_Any* any, const char* type_fqn);

BEBOP_API const char* Bebop_Any_TypeName(const Bebop_Any* any);

BEBOP_API Bebop_WireResult Bebop_Any_Unpack(
    Bebop_WireCtx* ctx, const Bebop_Any* any, void* record, const Bebop_TypeInfo* type_info
);

// #endregion

// #region Work Stack for Iterative Encode/Decode/Size

#ifndef BEBOP_WIRE_WORK_STACK_SIZE
#define BEBOP_WIRE_WORK_STACK_SIZE 64
#endif

struct Bebop_Work;
struct Bebop_WorkStack;

typedef int (*Bebop_EncodeStepFn)(
    Bebop_Writer* w, struct Bebop_WorkStack* stack, struct Bebop_Work* item
);
typedef int (*Bebop_DecodeStepFn)(
    Bebop_WireCtx* ctx, Bebop_Reader* rd, struct Bebop_WorkStack* stack, struct Bebop_Work* item
);
typedef size_t (*Bebop_SizeStepFn)(struct Bebop_WorkStack* stack, struct Bebop_Work* item);

typedef struct Bebop_Work {
  void* data;
  uint32_t state;

  union {
    Bebop_EncodeStepFn encode;
    Bebop_DecodeStepFn decode;
    Bebop_SizeStepFn size;
  } step;

  union {
    struct {
      size_t len_pos;
      size_t start_pos;
    } enc;

    struct {
      const uint8_t* end;
    } dec;
  };
} Bebop_Work;

typedef struct Bebop_WorkStack {
  Bebop_Work items[BEBOP_WIRE_WORK_STACK_SIZE];
  uint8_t depth;
} Bebop_WorkStack;

static inline void Bebop_WorkStack_Init(Bebop_WorkStack* s)
{
  s->depth = 0;
}

static inline bool Bebop_WorkStack_Push(Bebop_WorkStack* s, Bebop_Work w)
{
  if (s->depth >= BEBOP_WIRE_WORK_STACK_SIZE) {
    return false;
  }
  s->items[s->depth++] = w;
  return true;
}

static inline Bebop_Work* Bebop_WorkStack_Top(Bebop_WorkStack* s)
{
  return s->depth > 0 ? &s->items[s->depth - 1] : NULL;
}

static inline void Bebop_WorkStack_Pop(Bebop_WorkStack* s)
{
  if (s->depth > 0) {
    s->depth--;
  }
}

static inline Bebop_Work* Bebop_WorkStack_PopGet(Bebop_WorkStack* s)
{
  return s->depth > 0 ? &s->items[--s->depth] : NULL;
}

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

// #region BBM - Type-Safe Map Macros (GNU/Clang only)
//
// These macros provide ergonomic, type-safe map operations using C11 _Generic
// and GNU extensions (__typeof__, statement expressions).
//
// Usage:
//   Bebop_Map scores;
//   BBM_INIT(&scores, ctx, Bebop_Str);
//
//   BBM_BEGIN(&scores, ctx, Bebop_Str, int32_t)
//     BBM_PUT("alice", 100);
//     BBM_PUT("bob", 200);
//     int32_t* val = BBM_GET("alice");  // returns int32_t*, no cast needed
//     if (BBM_HAS("charlie")) { ... }
//     BBM_DEL("bob");
//     BBM_EACH(name, score) { printf("%.*s: %d\n", ...); }
//   BBM_END()
//

// Generic init - derives hash/eq from key type (C11 _Generic)
#define BBM_INIT(m, ctx, KT) \
  _Generic( \
      (KT) {0}, \
      bool: Bebop_Map_Init((m), (ctx), Bebop_MapHash_Bool, Bebop_MapEq_Bool), \
      int8_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_I8, Bebop_MapEq_I8), \
      uint8_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_U8, Bebop_MapEq_U8), \
      int16_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_I16, Bebop_MapEq_I16), \
      uint16_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_U16, Bebop_MapEq_U16), \
      int32_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_I32, Bebop_MapEq_I32), \
      uint32_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_U32, Bebop_MapEq_U32), \
      int64_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_I64, Bebop_MapEq_I64), \
      uint64_t: Bebop_Map_Init((m), (ctx), Bebop_MapHash_U64, Bebop_MapEq_U64), \
      Bebop_Int128: Bebop_Map_Init((m), (ctx), Bebop_MapHash_I128, Bebop_MapEq_I128), \
      Bebop_UInt128: Bebop_Map_Init((m), (ctx), Bebop_MapHash_U128, Bebop_MapEq_U128), \
      Bebop_Str: Bebop_Map_Init((m), (ctx), Bebop_MapHash_Str, Bebop_MapEq_Str), \
      Bebop_UUID: Bebop_Map_Init((m), (ctx), Bebop_MapHash_UUID, Bebop_MapEq_UUID) \
  )

// Block scope - declares key/value types for operations
#define BBM_BEGIN(m, ctx, KT, VT) \
  { \
    Bebop_Map* _bbm = (m); \
    Bebop_WireCtx* _bbc = (ctx); \
    typedef KT _bbK; \
    typedef VT _bbV; \
    typedef struct { \
      _bbK w; \
    } _bbKW; \
    typedef struct { \
      _bbV w; \
    } _bbVW; \
    (void)_bbm; \
    (void)_bbc; \
    (void)sizeof(_bbKW); \
    (void)sizeof(_bbVW);

#define BBM_END() }

// String creation helpers
#define BBM_STR(s) ((Bebop_Str) {.data = (s), .length = (uint32_t)strlen(s)})
#define BBM_STRLIT(s) ((Bebop_Str) {.data = ("" s ""), .length = sizeof(s) - 1})

#if (defined(__GNUC__) || defined(__clang__)) && !defined(__cplusplus)

// Internal helpers - take void* so all _Generic branches type-check
static inline void _bbm_put_str(
    Bebop_Map* m, Bebop_WireCtx* c, const void* kptr, const void* v, size_t vsz
)
{
  const char* str = *(const char* const*)kptr;
  Bebop_Str* kp = (Bebop_Str*)Bebop_WireCtx_Alloc(c, sizeof(Bebop_Str));
  void* vp = Bebop_WireCtx_Alloc(c, vsz);
  if (kp && vp) {
    kp->data = str;
    kp->length = (uint32_t)strlen(str);
    memcpy(vp, v, vsz);
    Bebop_Map_Put(m, kp, vp);
  }
}

static inline void _bbm_put_any(
    Bebop_Map* m, Bebop_WireCtx* c, const void* kptr, size_t ksz, const void* v, size_t vsz
)
{
  void* kp = Bebop_WireCtx_Alloc(c, ksz);
  void* vp = Bebop_WireCtx_Alloc(c, vsz);
  if (kp && vp) {
    memcpy(kp, kptr, ksz);
    memcpy(vp, v, vsz);
    Bebop_Map_Put(m, kp, vp);
  }
}

static inline void* _bbm_get_str(Bebop_Map* m, const void* kptr)
{
  const char* s = *(const char* const*)kptr;
  Bebop_Str key = {.data = s, .length = (uint32_t)strlen(s)};
  return Bebop_Map_Get(m, &key);
}

static inline void* _bbm_get_any(Bebop_Map* m, const void* kptr)
{
  return Bebop_Map_Get(m, kptr);
}

static inline bool _bbm_has_str(Bebop_Map* m, const void* kptr)
{
  const char* s = *(const char* const*)kptr;
  Bebop_Str key = {.data = s, .length = (uint32_t)strlen(s)};
  return Bebop_Map_Get(m, &key) != NULL;
}

static inline bool _bbm_has_any(Bebop_Map* m, const void* kptr)
{
  return Bebop_Map_Get(m, kptr) != NULL;
}

static inline bool _bbm_del_str(Bebop_Map* m, const void* kptr)
{
  const char* s = *(const char* const*)kptr;
  Bebop_Str key = {.data = s, .length = (uint32_t)strlen(s)};
  return Bebop_Map_Del(m, &key);
}

static inline bool _bbm_del_any(Bebop_Map* m, const void* kptr)
{
  return Bebop_Map_Del(m, kptr);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif

// PUT: BBM_PUT(key, value) or BBM_PUT(key, {.x=1, .y=2})
// String literals auto-convert to Bebop_Str keys
#define BBM_PUT(k, ...) \
  do { \
    __typeof__((0, (k))) _k = (k); \
    _bbV _v = ((_bbVW) {__VA_ARGS__}).w; \
    _Generic( \
        _k, \
        char*: _bbm_put_str(_bbm, _bbc, (const void*)&_k, &_v, sizeof(_v)), \
        const char*: _bbm_put_str(_bbm, _bbc, (const void*)&_k, &_v, sizeof(_v)), \
        default: _bbm_put_any(_bbm, _bbc, &((_bbKW) {_k}).w, sizeof(_bbK), &_v, sizeof(_v)) \
    ); \
  } while (0)

// GET: returns VT* or NULL - no cast needed
#define BBM_GET(k) \
  ({ \
    __typeof__((0, (k))) _gk = (k); \
    (_bbV*)_Generic( \
        _gk, \
        char*: _bbm_get_str(_bbm, (const void*)&_gk), \
        const char*: _bbm_get_str(_bbm, (const void*)&_gk), \
        default: _bbm_get_any(_bbm, &((_bbKW) {_gk}).w) \
    ); \
  })

// HAS: returns bool
#define BBM_HAS(k) \
  ({ \
    __typeof__((0, (k))) _hk = (k); \
    _Generic( \
        _hk, \
        char*: _bbm_has_str(_bbm, (const void*)&_hk), \
        const char*: _bbm_has_str(_bbm, (const void*)&_hk), \
        default: _bbm_has_any(_bbm, &((_bbKW) {_hk}).w) \
    ); \
  })

// DEL: returns bool (true if deleted)
#define BBM_DEL(k) \
  ({ \
    __typeof__((0, (k))) _dk = (k); \
    _Generic( \
        _dk, \
        char*: _bbm_del_str(_bbm, (const void*)&_dk), \
        const char*: _bbm_del_str(_bbm, (const void*)&_dk), \
        default: _bbm_del_any(_bbm, &((_bbKW) {_dk}).w) \
    ); \
  })

// LEN: map length
#define BBM_LEN() (_bbm->length)

// EACH: iterate inside BBM_BEGIN/END block
#define BBM_EACH(kvar, vvar) \
  for (struct { \
         Bebop_MapIter it; \
         _bbK* k; \
         _bbV* v; \
       } _f = {.it = {_bbm, 0}}; \
       Bebop_MapIter_Next(&_f.it, (void**)&_f.k, (void**)&_f.v);) \
    for (_bbK* kvar = _f.k; kvar; kvar = NULL) \
      for (_bbV* vvar = _f.v; vvar; vvar = NULL)

// FOREACH: iterate outside block (requires explicit types)
#define BBM_FOREACH(m, KT, kvar, VT, vvar) \
  for (struct { \
         Bebop_MapIter it; \
         KT* k; \
         VT* v; \
         int _s; \
       } _f = {.it = {m, 0}, ._s = 1}; \
       _f._s && Bebop_MapIter_Next(&_f.it, (void**)&_f.k, (void**)&_f.v);) \
    for (KT* kvar = _f.k; kvar; kvar = NULL) \
      for (VT* vvar = _f.v; vvar; vvar = NULL)

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif  // __GNUC__ || __clang__ (BBM macros not available on MSVC)

// #endregion

#ifdef __cplusplus
}
#endif

#endif
