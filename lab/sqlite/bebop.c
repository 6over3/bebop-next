#ifdef BSQL_ENABLE_SCHEMA_COMPILER
# include "bebop.h"
#endif
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3ext.h>
#include "bebop_wire_codegen.h"
#include "descriptor.bb.h"

#ifdef SQLITE_OMIT_LOAD_EXTENSION
# error "SQLite headers must support loadable extensions"
#endif
#if SQLITE_VERSION_NUMBER < 3031000
# error "SQLite 3.31.0 or newer headers are required"
#endif
SQLITE_EXTENSION_INIT1

/* Export only the documented extension entry points. */
#if defined(_WIN32)
# define BSQL_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
# define BSQL_EXPORT __attribute__((visibility("default")))
#else
# define BSQL_EXPORT
#endif

/* Count elements of an actual array, never of a pointer. */
#define BSQL_COUNT(A) (sizeof(A) / sizeof((A)[0]))

/* Override this macro to connect boundary conditions to a test harness. */
#ifndef BSQL_TESTCASE
# define BSQL_TESTCASE(X) ((void)(X))
#endif

/* Reject platforms whose byte or floating-point layouts cannot carry Bebop. */
typedef char BsqlByteCheck[CHAR_BIT == 8 ? 1 : -1];
typedef char BsqlAsciiCheck[
  'A' == 65 && 'a' == 97 && '0' == 48 && '+' == 43 && '/' == 47 ? 1 : -1
];
typedef char BsqlFloatCheck[
  sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 &&
  FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 ? 1 : -1
];
typedef char BsqlDoubleCheck[
  sizeof(double) == sizeof(uint64_t) && DBL_MANT_DIG == 53 &&
  DBL_MAX_EXP == 1024 ? 1 : -1
];
typedef char BsqlLengthCheck[BEBOP_WIRE_SIZE_LEN == 4 ? 1 : -1];
typedef char BsqlUuidCheck[BEBOP_WIRE_SIZE_UUID == 16 ? 1 : -1];

enum {
  BSQL_MAX_DEPTH = 64,
  BSQL_MAX_PATH_STEPS = BSQL_MAX_DEPTH - 1,
  BSQL_MAX_PATH_BYTES = 16384,
  BSQL_MAX_SPEC_BYTES = 1024 * 1024,
  BSQL_MAX_TYPE_BYTES = BSQL_MAX_SPEC_BYTES,
  BSQL_MAX_ARENA_BYTES = 32 * 1024 * 1024,
  BSQL_MAX_CACHE_BYTES = 8 * 1024 * 1024,
  BSQL_MAX_DEFINITIONS = 4096,
  BSQL_MAX_ITEMS = 1000000,
  BSQL_MAX_VISITS = 1000000,
  BSQL_CACHE_SLOTS = 8,
  BSQL_MIN_BUFFER_BYTES = 128,
  BSQL_MIN_PARTS = 8,
  BSQL_MIN_KEYS = 16,
  BSQL_INTERRUPT_INTERVAL = 256,
  BSQL_BYTE_POLL_INTERVAL = 4096,
  BSQL_MIN_SQLITE_VERSION = 3031000,
  BSQL_INTERRUPT_SQLITE_VERSION = 3041000,
  BSQL_MAGIC_BYTES = 8,
  BSQL_VERSION_OFFSET = BSQL_MAGIC_BYTES,
  BSQL_FLAGS_OFFSET = BSQL_VERSION_OFFSET + 2,
  BSQL_SPEC_ROOT_OFFSET = BSQL_FLAGS_OFFSET + 2,
  BSQL_SPEC_DESCRIPTOR_OFFSET = BSQL_SPEC_ROOT_OFFSET + 4,
  BSQL_SPEC_HEADER_BYTES = BSQL_SPEC_DESCRIPTOR_OFFSET + 4,
  BSQL_VALUE_SPEC_OFFSET = BSQL_FLAGS_OFFSET + 2,
  BSQL_VALUE_PAYLOAD_OFFSET = BSQL_VALUE_SPEC_OFFSET + 4,
  BSQL_VALUE_HEADER_BYTES = BSQL_VALUE_PAYLOAD_OFFSET + 8,
  BSQL_FORMAT_VERSION = 1,
  BSQL_TAG_BYTES = 1,
  BSQL_TAG_SLOTS = UINT8_MAX + 1,
  BSQL_UNION_HEADER_BYTES = BEBOP_WIRE_SIZE_LEN + BSQL_TAG_BYTES,
  BSQL_KIND_BASE = BEBOP_TYPE_KIND_DEFINED + 1,
  BSQL_KIND_ENUM = BSQL_KIND_BASE + BEBOP_DEFINITION_KIND_ENUM,
  BSQL_KIND_STRUCT = BSQL_KIND_BASE + BEBOP_DEFINITION_KIND_STRUCT,
  BSQL_KIND_MESSAGE = BSQL_KIND_BASE + BEBOP_DEFINITION_KIND_MESSAGE,
  BSQL_KIND_UNION = BSQL_KIND_BASE + BEBOP_DEFINITION_KIND_UNION,
  BSQL_HEX_VALID = 0x10,
  BSQL_HEX_MASK = 0x0f,
  BSQL_HEX_SHIFT = 4,
  BSQL_UTF8_MAX_BYTES = 4,
  BSQL_UTF8_CONT_BITS = 6,
  BSQL_UTF8_CONT_MASK = 0x3f,
  BSQL_UNICODE_ESCAPE_BYTES = 6,
  BSQL_SURROGATE_ESCAPE_BYTES = 12,
  BSQL_HIGH_SURROGATE = 0xd800,
  BSQL_LOW_SURROGATE = 0xdc00,
  BSQL_SURROGATE_MASK = 0xfc00,
  BSQL_SURROGATE_PAYLOAD = 0x3ff,
  BSQL_SURROGATE_BITS = 10,
  BSQL_SUPPLEMENTARY_START = 0x10000,
  BSQL_UNICODE_MAX = 0x10ffff,
  BSQL_B64_INPUT_BYTES = 3,
  BSQL_B64_OUTPUT_BYTES = 4,
  BSQL_B64_BITS = 6,
  BSQL_B64_MASK = 0x3f,
  BSQL_B64_INVALID = 0x80,
  BSQL_B64_TWO_PAD_MASK = (1 << (2 * BSQL_B64_BITS - CHAR_BIT)) - 1,
  BSQL_B64_ONE_PAD_MASK = (1 << (3 * BSQL_B64_BITS - 2 * CHAR_BIT)) - 1,
  BSQL_DECIMAL_RADIX = 10,
  BSQL_DECIMAL_CHUNK_BASE = 1000000000,
  BSQL_DECIMAL_CHUNK_DIGITS = 9,
  BSQL_WIDE_BYTES = 16,
  BSQL_WIDE_WORDS = 4,
  BSQL_WIDE_CHUNKS = 5,
  BSQL_WIDE_DECIMAL_DIGITS = 39,
  BSQL_DECIMAL_TEXT_BYTES = BSQL_WIDE_DECIMAL_DIGITS + 2,
  BSQL_UUID_GROUPS = 5,
  BSQL_UUID_TEXT_LENGTH = BEBOP_WIRE_SIZE_UUID * 2 + BSQL_UUID_GROUPS - 1,
  BSQL_UUID_TEXT_BYTES = BSQL_UUID_TEXT_LENGTH + 1,
  BSQL_TIME_TEXT_BYTES = 48,
  BSQL_NUMBER_TEXT_BYTES = 64,
  BSQL_NANOS_PER_SECOND = 1000000000,
  BSQL_TIME_SECONDS_OFFSET = 0,
  BSQL_TIME_NANOS_OFFSET = 8,
  BSQL_TIME_ZONE_OFFSET = 12,
  BSQL_HALF_FRACTION_BITS = 10,
  BSQL_HALF_EXPONENT_MASK = 31,
  BSQL_HALF_FRACTION_MASK = 1023,
  BSQL_HALF_BIAS = 15,
  BSQL_HALF_SIGN = 0x8000,
  BSQL_HALF_INFINITY = 0x7c00,
  BSQL_HALF_QUIET_NAN = 0x7e00,
  BSQL_HALF_HIDDEN_BIT = 1024,
  BSQL_BFLOAT_SHIFT = 16
};

static const size_t bsqlMaxValueBytes = INT32_MAX;
static const uint8_t bsqlValueMagic[BSQL_MAGIC_BYTES] = {
  'B', 'E', 'B', 'O', 'P', 'S', 'Q', 0
};
static const uint8_t bsqlSpecMagic[BSQL_MAGIC_BYTES] = {
  'B', 'E', 'B', 'O', 'P', 'T', 'Y', 0
};
static const char bsqlHexAlphabet[] = "0123456789abcdef";
static const uint8_t bsqlUuidGroups[BSQL_UUID_GROUPS] = {4, 2, 2, 2, 6};

/* One row supplies both the spelling and the fixed wire width of a scalar. */
typedef struct BsqlPrimitive BsqlPrimitive;
struct BsqlPrimitive {
  const char *zName;              /* Static type-expression spelling. */
  size_t nName;                   /* Bytes in zName, excluding the NUL. */
  unsigned nWire;                 /* Zero denotes a variable-width string. */
};

/* Derive literal lengths at compile time instead of repeatedly using strlen. */
#define BSQL_PRIMITIVE(Z, N) {Z, sizeof(Z) - 1, N}
static const BsqlPrimitive bsqlPrimitives[] = {
  {0, 0, 0},
  BSQL_PRIMITIVE("bool", BEBOP_WIRE_SIZE_BOOL),
  BSQL_PRIMITIVE("byte", BEBOP_WIRE_SIZE_BYTE),
  BSQL_PRIMITIVE("int8", BEBOP_WIRE_SIZE_INT8),
  BSQL_PRIMITIVE("int16", BEBOP_WIRE_SIZE_INT16),
  BSQL_PRIMITIVE("uint16", BEBOP_WIRE_SIZE_UINT16),
  BSQL_PRIMITIVE("int32", BEBOP_WIRE_SIZE_INT32),
  BSQL_PRIMITIVE("uint32", BEBOP_WIRE_SIZE_UINT32),
  BSQL_PRIMITIVE("int64", BEBOP_WIRE_SIZE_INT64),
  BSQL_PRIMITIVE("uint64", BEBOP_WIRE_SIZE_UINT64),
  BSQL_PRIMITIVE("int128", BEBOP_WIRE_SIZE_INT128),
  BSQL_PRIMITIVE("uint128", BEBOP_WIRE_SIZE_UINT128),
  BSQL_PRIMITIVE("float16", BEBOP_WIRE_SIZE_FLOAT16),
  BSQL_PRIMITIVE("float32", BEBOP_WIRE_SIZE_FLOAT32),
  BSQL_PRIMITIVE("float64", BEBOP_WIRE_SIZE_FLOAT64),
  BSQL_PRIMITIVE("bfloat16", BEBOP_WIRE_SIZE_BFLOAT16),
  BSQL_PRIMITIVE("string", 0),
  BSQL_PRIMITIVE("uuid", BEBOP_WIRE_SIZE_UUID),
  BSQL_PRIMITIVE("timestamp", BEBOP_WIRE_SIZE_TIMESTAMP),
  BSQL_PRIMITIVE("duration", BEBOP_WIRE_SIZE_DURATION)
};
#undef BSQL_PRIMITIVE

/* The table above is indexed by these generated wire-kind identifiers. */
typedef char BsqlPrimitiveOrderCheck[
  BEBOP_TYPE_KIND_BOOL == 1 && BEBOP_TYPE_KIND_BYTE == 2 &&
  BEBOP_TYPE_KIND_INT_8 == 3 && BEBOP_TYPE_KIND_INT_16 == 4 &&
  BEBOP_TYPE_KIND_UINT_16 == 5 && BEBOP_TYPE_KIND_INT_32 == 6 &&
  BEBOP_TYPE_KIND_UINT_32 == 7 && BEBOP_TYPE_KIND_INT_64 == 8 &&
  BEBOP_TYPE_KIND_UINT_64 == 9 && BEBOP_TYPE_KIND_INT_128 == 10 &&
  BEBOP_TYPE_KIND_UINT_128 == 11 && BEBOP_TYPE_KIND_FLOAT_16 == 12 &&
  BEBOP_TYPE_KIND_FLOAT_32 == 13 && BEBOP_TYPE_KIND_FLOAT_64 == 14 &&
  BEBOP_TYPE_KIND_BFLOAT_16 == 15 && BEBOP_TYPE_KIND_STRING == 16 &&
  BEBOP_TYPE_KIND_UUID == 17 && BEBOP_TYPE_KIND_TIMESTAMP == 18 &&
  BEBOP_TYPE_KIND_DURATION == 19 &&
  BEBOP_TYPE_KIND_ARRAY == BSQL_COUNT(bsqlPrimitives) ? 1 : -1
];

typedef Bebop_TypeDescriptor BsqlType;
typedef Bebop_DefinitionDescriptor BsqlDefinition;
typedef Bebop_FieldDescriptor BsqlField;
typedef Bebop_UnionBranchDescriptor BsqlBranch;

typedef enum BsqlStatus {
  BSQL_OK, BSQL_INVALID, BSQL_LIMIT, BSQL_NOMEM,
  BSQL_INTERRUPTED, BSQL_SQLITE_ERROR
} BsqlStatus;

/* Error messages are static. The first error survives all subsequent errors. */
typedef struct BsqlError BsqlError;
struct BsqlError {
  const char *zMessage;            /* Static diagnostic, or NULL on success. */
  size_t iOffset;                  /* Byte offset when bOffset is true. */
  BsqlStatus eCode;                /* Internal classification. */
  int sqliteCode;                  /* SQLite code, including extended codes. */
  int bOffset;                     /* Offset zero is a meaningful location. */
};

/* aOwned is NULL for borrowed data. Only aOwned may be resized or freed. */
typedef struct BsqlBuffer BsqlBuffer;
struct BsqlBuffer {
  const uint8_t *aData;            /* Readable prefix of nData bytes. */
  uint8_t *aOwned;                 /* Allocation, or NULL for borrowed data. */
  size_t nData;                    /* Initialized, logically visible bytes. */
  size_t nAlloc;                   /* Capacity of aOwned, zero if borrowed. */
};

/* Bebop allocator accounting includes live arena and writer allocations. */
typedef struct BsqlAllocator BsqlAllocator;
struct BsqlAllocator {
  size_t nByte;                    /* Bytes currently allocated. */
  size_t nLimit;                   /* Maximum live bytes for this arena. */
  BsqlStatus eFailure;             /* First allocation failure, if any. */
};

/* A sorted name index retains original field/member/branch order separately. */
typedef struct BsqlName BsqlName;
struct BsqlName {
  Bebop_String name;               /* Borrowed from the owned descriptor. */
  size_t iOriginal;                /* Position in the descriptor array. */
};

/* Auxiliary definition indexes are immutable after schema construction. */
typedef struct BsqlDefinitionInfo BsqlDefinitionInfo;
struct BsqlDefinitionInfo {
  BsqlName *aName;                 /* Names sorted lexicographically. */
  size_t nName;                    /* Number of entries in aName. */
  uint16_t *aTag;                  /* Tag -> original index + 1, or NULL. */
};

/* A schema owns its bytes, arena, canonical key, and all derived indexes. */
typedef struct BsqlSchema BsqlSchema;
struct BsqlSchema {
  Bebop_Context *pArena;           /* Arena backed by allocator. */
  BsqlAllocator allocator;        /* Storage and failure accounting. */
  uint8_t *aDescriptor;            /* Stable backing for decoded strings. */
  size_t nDescriptor;              /* Allocation size of aDescriptor. */
  uint8_t *aKey;                   /* Owned canonical specification. */
  size_t nKey;                     /* Used bytes in aKey. */
  size_t nKeyAlloc;                /* Allocated bytes in aKey. */
  Bebop_DescriptorSet descriptor;  /* Decoded input descriptor set. */
  /* Root whose names point into this schema. */
  BsqlType root;
  BsqlDefinition **apDefinition;   /* Definitions sorted by qualified name. */
  BsqlType *aDefinitionType;       /* Stable types for union branch views. */
  BsqlDefinitionInfo *aInfo;       /* Parallel per-definition indexes. */
  size_t nDefinition;              /* Number of populated index entries. */
  unsigned nRef;                  /* Caller and cache references. */
};

/* Views borrow both the schema and their wire bytes; absence is explicit. */
typedef struct BsqlView BsqlView;
struct BsqlView {
  const BsqlSchema *pSchema;       /* Must outlive this view. */
  const BsqlType *pType;           /* NULL for unknown message/union fields. */
  Bebop_View bytes;               /* Bounded wire slice. */
  int bPresent;                   /* False means a missing path. */
};

/* SQLite serializes callbacks on a serialized connection. NOMUTEX users must
** supply their own connection serialization, as required by SQLite itself.
*/
typedef struct BsqlConnection BsqlConnection;
struct BsqlConnection {
  sqlite3 *db;                    /* Connection owning the registrations. */
  BsqlSchema *apCache[BSQL_CACHE_SLOTS]; /* Most-recently-used first. */
  unsigned nRef;                  /* Registration and virtual-table owners. */
};

/* Nested JSON statements are reused by depth, not re-prepared for each item. */
typedef struct BsqlJsonStatements BsqlJsonStatements;
struct BsqlJsonStatements {
  sqlite3_stmt *apEach[BSQL_MAX_DEPTH]; /* Container traversal by depth. */
  sqlite3_stmt *apPair[BSQL_MAX_DEPTH]; /* Map-pair traversal by depth. */
};

/* Work is operation-local. A virtual-table cursor retains it across rows. */
typedef struct BsqlWork BsqlWork;
struct BsqlWork {
  sqlite3 *db;                    /* For interruption checks and JSON SQL. */
  BsqlJsonStatements *pJson;      /* Lazily allocated SQLite statement pool. */
  size_t nRemaining;              /* Remaining traversal visits. */
  BsqlError error;                /* First failure. */
};

/* Record an error without overwriting an earlier failure. No allocation. */
static int bsqlFail(BsqlError *p, BsqlStatus eCode, const char *zMessage){
  assert(p != 0);
  if( p->eCode == BSQL_OK ){
    p->eCode = eCode;
    p->zMessage = zMessage;
    p->sqliteCode = eCode == BSQL_NOMEM ? SQLITE_NOMEM :
                    eCode == BSQL_LIMIT ? SQLITE_TOOBIG :
                    eCode == BSQL_INTERRUPTED ? SQLITE_INTERRUPT :
                    SQLITE_ERROR;
  }
  return 0;
}

/* Preserve an SQLite failure, including extended I/O and interruption codes. */
static int bsqlSqlFail(BsqlWork *p, int rc, const char *zMessage){
  BsqlStatus eCode;
  int primary;
  if( p->error.eCode != BSQL_OK ){
    return 0;
  }
  primary = rc & 0xff;
  if( primary == SQLITE_OK || primary == SQLITE_ROW || primary == SQLITE_DONE ){
    rc = SQLITE_ERROR;
    primary = SQLITE_ERROR;
  }
  eCode = primary == SQLITE_NOMEM ? BSQL_NOMEM :
          primary == SQLITE_INTERRUPT ? BSQL_INTERRUPTED :
          primary == SQLITE_ERROR ? BSQL_INVALID : BSQL_SQLITE_ERROR;
  bsqlFail(&p->error, eCode, zMessage ? zMessage : sqlite3_errstr(rc));
  p->error.sqliteCode = rc;
  return 0;
}

/* Poll cancellation when supported by both the headers and the host runtime. */
static int bsqlPoll(BsqlWork *p){
  if( p->error.eCode != BSQL_OK ){
    return 0;
  }
#if SQLITE_VERSION_NUMBER >= 3041000
  if( p->db && sqlite3_libversion_number() >= BSQL_INTERRUPT_SQLITE_VERSION
   && sqlite3_is_interrupted(p->db) ){
    return bsqlFail(&p->error, BSQL_INTERRUPTED, "query interrupted");
  }
#endif
  return 1;
}

/* Consume one bounded traversal visit. Failure leaves the budget unchanged. */
static int bsqlVisit(BsqlWork *p, unsigned nDepth){
  if( p->error.eCode != BSQL_OK ){
    return 0;
  }
  if( nDepth >= BSQL_MAX_DEPTH || p->nRemaining == 0 ){
    return bsqlFail(&p->error, BSQL_LIMIT, "traversal limit exceeded");
  }
  --p->nRemaining;
  if( p->nRemaining % BSQL_INTERRUPT_INTERVAL == 0 ){
    return bsqlPoll(p);
  }
  return 1;
}

/* Initialize an operation-local budget without allocating memory. */
static BsqlWork bsqlWork(sqlite3 *db){
  BsqlWork work;
  memset(&work, 0, sizeof(work));
  work.db = db;
  work.nRemaining = BSQL_MAX_VISITS;
  return work;
}

/* Finalize pooled statements and free their SQLite allocation on every exit. */
static void bsqlWorkClear(BsqlWork *p){
  size_t i;
  if( p->pJson ){
    for(i=0; i<BSQL_MAX_DEPTH; ++i){
      sqlite3_finalize(p->pJson->apEach[i]);
      sqlite3_finalize(p->pJson->apPair[i]);
    }
    sqlite3_free(p->pJson);
    p->pJson = 0;
  }
}

/* Construct an immutable view. This neither allocates nor retains a schema. */
static BsqlView bsqlView(
  const BsqlSchema *pSchema, const BsqlType *pType, Bebop_View bytes,
  int bPresent
){
  BsqlView view;
  view.pSchema = pSchema;
  view.pType = pType;
  view.bytes = bytes;
  view.bPresent = bPresent;
  return view;
}

/* Construct a borrowed buffer. Its bytes must outlive the returned buffer. */
static BsqlBuffer bsqlBorrow(Bebop_View bytes){
  BsqlBuffer buffer;
  memset(&buffer, 0, sizeof(buffer));
  buffer.aData = bytes.data;
  buffer.nData = bytes.length;
  return buffer;
}

#ifndef NDEBUG
/* Check the explicit ownership invariant without reading any buffer bytes. */
static int bsqlBufferInvariant(const BsqlBuffer *p){
  if( p->nData && !p->aData ){
    return 0;
  }
  if( p->aOwned ){
    return p->aData == p->aOwned && p->nData <= p->nAlloc;
  }
  return p->nAlloc == 0;
}
#endif

/* Reserve additional bytes in the SQLite heap. Borrowed bytes are copied on
** first growth. OOM/overflow leaves the original logical contents intact.
*/
static int bsqlReserve(BsqlBuffer *p, size_t nExtra, BsqlError *pError){
  size_t nNeed;
  size_t nAlloc;
  uint8_t *aNew;
  assert(bsqlBufferInvariant(p));
  if( pError->eCode != BSQL_OK ){
    return 0;
  }
  if( p->nData > bsqlMaxValueBytes
   || nExtra > bsqlMaxValueBytes - p->nData ){
    return bsqlFail(pError, BSQL_LIMIT, "value byte limit exceeded");
  }
  nNeed = p->nData + nExtra;
  if( nNeed == 0 || (p->aOwned && nNeed <= p->nAlloc) ){
    return 1;
  }
  nAlloc = p->nAlloc ? p->nAlloc : BSQL_MIN_BUFFER_BYTES;
  while( nAlloc < nNeed ){
    if( nAlloc > bsqlMaxValueBytes / 2 ){
      nAlloc = bsqlMaxValueBytes;
    }else{
      nAlloc *= 2;
    }
  }
  BSQL_TESTCASE(nNeed == bsqlMaxValueBytes);
  aNew = sqlite3_realloc64(p->aOwned, (sqlite3_uint64)nAlloc);
  if( !aNew ){
    return bsqlFail(pError, BSQL_NOMEM, "out of memory");
  }
  if( !p->aOwned && p->nData ){
    memcpy(aNew, p->aData, p->nData);
  }
  p->aOwned = aNew;
  p->aData = aNew;
  p->nAlloc = nAlloc;
  assert(bsqlBufferInvariant(p));
  return 1;
}

/* Append bytes, including a slice of this buffer. Alias offsets survive a
** realloc. SQLite OOM or invalid lengths leave nData unchanged.
*/
static int bsqlAppend(
  BsqlBuffer *p, const void *aBytes, size_t nByte, BsqlError *pError
){
  const uint8_t *aSource;
  size_t iAlias;
  uintptr_t sourceAddress;
  uintptr_t bufferAddress;
  int bAlias;
  if( pError->eCode != BSQL_OK ){
    return 0;
  }
  if( nByte == 0 ){
    return 1;
  }
  if( !aBytes ){
    return bsqlFail(pError, BSQL_INVALID, "missing buffer bytes");
  }
  aSource = aBytes;
  sourceAddress = (uintptr_t)aSource;
  bufferAddress = (uintptr_t)p->aData;
  bAlias = p->aData && sourceAddress >= bufferAddress
        && sourceAddress - bufferAddress < p->nData;
  iAlias = bAlias ? (size_t)(sourceAddress - bufferAddress) : 0;
  if( bAlias && nByte > p->nData - iAlias ){
    return bsqlFail(pError, BSQL_INVALID, "invalid buffer slice");
  }
  if( !bsqlReserve(p, nByte, pError) ){
    return 0;
  }
  if( bAlias ){
    aSource = p->aData + iAlias;
  }
  memmove(p->aOwned + p->nData, aSource, nByte);
  p->nData += nByte;
  return 1;
}

/* Append a static string literal, deriving its length at compile time. */
#define BSQL_APPEND_LITERAL(P, Z, E) bsqlAppend(P, Z, sizeof(Z) - 1, E)

/* Release only owned storage. A borrowed backing allocation is never freed. */
static void bsqlBufferClear(BsqlBuffer *p){
  assert(bsqlBufferInvariant(p));
  sqlite3_free(p->aOwned);
  memset(p, 0, sizeof(*p));
}

/*
** Read 1..8 little-endian bytes without alignment or host-endian assumptions.
*/
static uint64_t bsqlLoadLe(const uint8_t *a, unsigned nByte){
  uint64_t value;
  unsigned i;
  assert(a != 0 && nByte > 0 && nByte <= sizeof(value));
  value = 0;
  for(i=0; i<nByte; ++i){
    value |= (uint64_t)a[i] << (i * CHAR_BIT);
  }
  return value;
}

/* Store 1..8 little-endian bytes without unaligned typed accesses. */
static void bsqlStoreLe(uint8_t *a, uint64_t value, unsigned nByte){
  unsigned i;
  assert(a != 0 && nByte > 0 && nByte <= sizeof(value));
  for(i=0; i<nByte; ++i){
    a[i] = (uint8_t)(value >> (i * CHAR_BIT));
  }
}

/* Append a small integer directly into reserved SQLite-owned storage. */
static int bsqlAppendLe(
  BsqlBuffer *p, uint64_t value, unsigned nByte, BsqlError *pError
){
  assert(nByte > 0 && nByte <= sizeof(value));
  if( !bsqlReserve(p, nByte, pError) ){
    return 0;
  }
  bsqlStoreLe(p->aOwned + p->nData, value, nByte);
  p->nData += nByte;
  return 1;
}

/* Resize a Bebop allocation in the SQLite heap with overflow-safe accounting.
** OOM or a limit violation preserves the old allocation and its byte count.
*/
static void *bsqlArenaAllocate(
  void *pOld, size_t nOld, size_t nNew, void *pUser
){
  BsqlAllocator *p;
  void *pNew;
  p = pUser;
  assert(nOld <= p->nByte);
  if( nOld > p->nByte ){
    if( p->eFailure == BSQL_OK ){
      p->eFailure = BSQL_INVALID;
    }
    return 0;
  }
  if( nNew == 0 ){
    sqlite3_free(pOld);
    p->nByte -= nOld;
    return 0;
  }
  if( nNew > p->nLimit || p->nByte - nOld > p->nLimit - nNew ){
    if( p->eFailure == BSQL_OK ){
      p->eFailure = BSQL_LIMIT;
    }
    return 0;
  }
  pNew = sqlite3_realloc64(pOld, (sqlite3_uint64)nNew);
  if( !pNew ){
    if( p->eFailure == BSQL_OK ){
      p->eFailure = BSQL_NOMEM;
    }
    return 0;
  }
  p->nByte = p->nByte - nOld + nNew;
  return pNew;
}

/* Create a bounded, SQLite-backed Bebop arena. Report OOM or its byte limit. */
static int bsqlArenaInit(
  BsqlAllocator *pAllocator, Bebop_Context **ppArena, size_t nLimit,
  BsqlError *pError
){
  Bebop_ContextOptions options;
  options = bebop_context_options();
  pAllocator->nLimit = nLimit;
  options.arena_options.allocator.alloc = bsqlArenaAllocate;
  options.arena_options.allocator.ctx = pAllocator;
  options.max_decode_depth = BSQL_MAX_DEPTH;
  *ppArena = bebop_context_new(&options);
  if( *ppArena ){
    return 1;
  }
  return bsqlFail(pError, pAllocator->eFailure ? pAllocator->eFailure :
                  BSQL_NOMEM, "cannot allocate Bebop arena");
}

/* Allocate a zeroed array in a Bebop arena. Empty arrays need no allocation. */
static void *bsqlArenaArray(
  Bebop_Context *pArena, size_t nCount, size_t nElement
){
  void *p;
  size_t nByte;
  if( nCount == 0 || nElement == 0 || nCount > SIZE_MAX / nElement ){
    return 0;
  }
  nByte = nCount * nElement;
  p = bebop_context_alloc(pArena, nByte);
  if( p ){
    memset(p, 0, nByte);
  }
  return p;
}

/* Report the arena's first failure rather than disguising a limit as OOM. */
static int bsqlArenaFail(
  const BsqlAllocator *pAllocator, BsqlError *pError, const char *zMessage
){
  return bsqlFail(pError, pAllocator->eFailure ? pAllocator->eFailure :
                  BSQL_NOMEM, zMessage);
}

/*
** Return true for an ASCII decimal digit. No locale-dependent classification.
*/
static int bsqlDigit(unsigned char c){
  return c >= '0' && c <= '9';
}

/*
** Decode one hexadecimal digit. The validity bit distinguishes zero from bad.
*/
static unsigned bsqlHex(unsigned char c){
  if( c >= '0' && c <= '9' ){
    return BSQL_HEX_VALID | (unsigned)(c - '0');
  }
  if( c >= 'a' && c <= 'f' ){
    return BSQL_HEX_VALID | (unsigned)(c - 'a' + 10);
  }
  if( c >= 'A' && c <= 'F' ){
    return BSQL_HEX_VALID | (unsigned)(c - 'A' + 10);
  }
  return 0;
}

/* Read four bounded hexadecimal digits, returning UINT32_MAX on bad syntax. */
static uint32_t bsqlHex4(const char *z){
  unsigned a;
  unsigned b;
  unsigned c;
  unsigned d;
  a = bsqlHex((unsigned char)z[0]);
  b = bsqlHex((unsigned char)z[1]);
  c = bsqlHex((unsigned char)z[2]);
  d = bsqlHex((unsigned char)z[3]);
  if( (a & b & c & d & BSQL_HEX_VALID) == 0 ){
    return UINT32_MAX;
  }
  return ((a & BSQL_HEX_MASK) << (3 * BSQL_HEX_SHIFT)) |
         ((b & BSQL_HEX_MASK) << (2 * BSQL_HEX_SHIFT)) |
         ((c & BSQL_HEX_MASK) << BSQL_HEX_SHIFT) | (d & BSQL_HEX_MASK);
}

/* Validate scalar UTF-8, rejecting overlong encodings, surrogates, and partial
** sequences. NUL is allowed here; identifier callers reject it separately.
*/
static int bsqlUtf8Valid(const uint8_t *a, size_t n){
  size_t i;
  unsigned c;
  unsigned first;
  unsigned nTail;
  uint32_t code;
  uint32_t minimum;
  i = 0;
  if( n && !a ){
    return 0;
  }
  while( i < n ){
    c = a[i++];
    if( c < 0x80 ){
      continue;
    }
    if( c >= 0xc2 && c <= 0xdf ){
      nTail = 1; code = c & 0x1f; minimum = 0x80;
    }else if( c >= 0xe0 && c <= 0xef ){
      nTail = 2; code = c & 0x0f; minimum = 0x800;
    }else if( c >= 0xf0 && c <= 0xf4 ){
      nTail = 3; code = c & 0x07; minimum = 0x10000;
    }else{
      return 0;
    }
    if( nTail > n - i ){
      return 0;
    }
    for(first=0; first<nTail; ++first){
      c = a[i++];
      if( (c & 0xc0) != 0x80 ){
        return 0;
      }
      code = (code << BSQL_UTF8_CONT_BITS) | (c & BSQL_UTF8_CONT_MASK);
    }
    if( code < minimum || code > BSQL_UNICODE_MAX
     || (code >= BSQL_HIGH_SURROGATE && code < 0xe000) ){
      return 0;
    }
  }
  return 1;
}

/* Encode a validated Unicode scalar into at most four caller-owned bytes. */
static size_t bsqlUtf8Encode(uint32_t code, uint8_t a[BSQL_UTF8_MAX_BYTES]){
  size_t n;
  size_t i;
  assert(code <= BSQL_UNICODE_MAX);
  assert(code < BSQL_HIGH_SURROGATE || code >= 0xe000);
  if( code < 0x80 ){
    a[0] = (uint8_t)code;
    return 1;
  }
  if( code < 0x800 ){
    a[0] = (uint8_t)(0xc0 | (code >> 6));
    n = 2;
  }else if( code < 0x10000 ){
    a[0] = (uint8_t)(0xe0 | (code >> 12));
    n = 3;
  }else{
    a[0] = (uint8_t)(0xf0 | (code >> 18));
    n = 4;
  }
  for(i=1; i<n; ++i){
    a[i] = (uint8_t)(0x80 |
      ((code >> (BSQL_UTF8_CONT_BITS * (n - i - 1))) & BSQL_UTF8_CONT_MASK));
  }
  return n;
}

/* Decode one bounded path escape. Return consumed bytes, or zero on error.
** Existing short path escapes are retained; isolated surrogates are rejected.
*/
static size_t bsqlUnescape(const char *z, size_t n, uint32_t *pCode){
  uint32_t code;
  uint32_t low;
  if( n < 2 || z[0] != '\\' ){
    return 0;
  }
  switch( z[1] ){
    case 'u':
      if( n < BSQL_UNICODE_ESCAPE_BYTES ){
        return 0;
      }
      code = bsqlHex4(z + 2);
      if( code == UINT32_MAX ){
        return 0;
      }
      if( (code & BSQL_SURROGATE_MASK) == BSQL_HIGH_SURROGATE ){
        if( n < BSQL_SURROGATE_ESCAPE_BYTES
         || z[BSQL_UNICODE_ESCAPE_BYTES] != '\\'
         || z[BSQL_UNICODE_ESCAPE_BYTES + 1] != 'u' ){
          return 0;
        }
        low = bsqlHex4(z + BSQL_UNICODE_ESCAPE_BYTES + 2);
        if( (low & BSQL_SURROGATE_MASK) != BSQL_LOW_SURROGATE ){
          return 0;
        }
        *pCode = BSQL_SUPPLEMENTARY_START +
          ((code & BSQL_SURROGATE_PAYLOAD) << BSQL_SURROGATE_BITS) +
          (low & BSQL_SURROGATE_PAYLOAD);
        return BSQL_SURROGATE_ESCAPE_BYTES;
      }
      if( (code & BSQL_SURROGATE_MASK) == BSQL_LOW_SURROGATE ){
        return 0;
      }
      *pCode = code;
      return BSQL_UNICODE_ESCAPE_BYTES;
    case 'b': *pCode = '\b'; break;
    case 'f': *pCode = '\f'; break;
    case 'n': *pCode = '\n'; break;
    case 'r': *pCode = '\r'; break;
    case 't': *pCode = '\t'; break;
    case 'v': *pCode = '\v'; break;
    case '0':
      if( n > 2 && bsqlDigit((unsigned char)z[2]) ){
        return 0;
      }
      *pCode = 0;
      break;
    case '\'': case '"': case '/': case '\\':
      *pCode = (unsigned char)z[1];
      break;
    default:
      return 0;
  }
  return 2;
}

typedef enum BsqlPathKind {
  BSQL_PATH_NAME, BSQL_PATH_INDEX, BSQL_PATH_FROM_END, BSQL_PATH_APPEND
} BsqlPathKind;

/* Name escapes are decoded once during parsing, never during field searches. */
typedef struct BsqlPathStep BsqlPathStep;
struct BsqlPathStep {
  /* Member, absolute index, relative, append. */
  BsqlPathKind eKind;
  size_t iSource;                 /* Step's byte offset in the original path. */
  union {
    Bebop_String name;            /* Decoded name in the path allocation. */
    struct {
      /* Original decimal spelling, including '-'. */
      const char *zDigits;
      size_t nDigit;              /* Bytes in zDigits. */
      uint64_t magnitude;         /* Saturated absolute value. */
      int bNegative;              /* A literal leading minus was present. */
      int bOverflow;              /* Magnitude did not fit uint64_t. */
    } index;
  } u;
};

/* A single SQLite allocation contains this header, steps, and backing text. */
typedef struct BsqlPath BsqlPath;
struct BsqlPath {
  BsqlPathStep *aStep;            /* Immediately follows the path header. */
  size_t nStep;                   /* At most BSQL_MAX_PATH_STEPS. */
};

/* Attach an exact source offset to the first path error. No allocation. */
static int bsqlPathFail(
  BsqlError *p, BsqlStatus eCode, const char *zMessage, size_t iOffset
){
  if( p->eCode == BSQL_OK ){
    bsqlFail(p, eCode, zMessage);
    p->bOffset = 1;
    p->iOffset = iOffset;
  }
  return 0;
}

/* Scan only the supplied byte span. A NULL output counts and validates steps;
** otherwise write steps and decoded names into caller-reserved path storage.
** No allocation occurs and no read depends on a trailing NUL.
*/
static int bsqlPathScan(
  const char *z, size_t n, int bAppend, BsqlPath *pOut, char *zNames,
  size_t *pnStep, BsqlError *pError
){
  BsqlPathStep step;
  size_t i;
  size_t first;
  size_t nName;
  size_t consumed;
  size_t nUtf8;
  unsigned digit;
  uint32_t code;
  uint8_t utf8[BSQL_UTF8_MAX_BYTES];
  *pnStep = 0;
  if( n == 0 || z[0] != '$' ){
    return bsqlPathFail(pError, BSQL_INVALID, "expected '$'", 0);
  }
  i = 1;
  while( i < n ){
    if( *pnStep == BSQL_MAX_PATH_STEPS ){
      return bsqlPathFail(pError, BSQL_LIMIT, "path depth exceeded", i);
    }
    memset(&step, 0, sizeof(step));
    step.iSource = i;
    if( z[i] == '.' ){
      step.eKind = BSQL_PATH_NAME;
      ++i;
      if( i < n && z[i] == '"' ){
        ++i;
        nName = 0;
        while( i < n && z[i] != '"' ){
          if( z[i] == '\\' ){
            consumed = bsqlUnescape(z + i, n - i, &code);
            if( consumed == 0 ){
              return bsqlPathFail(pError, BSQL_INVALID,
                                  "invalid name escape", i);
            }
            nUtf8 = bsqlUtf8Encode(code, utf8);
            if( pOut ){
              memcpy(zNames + nName, utf8, nUtf8);
            }
            nName += nUtf8;
            i += consumed;
          }else{
            if( pOut ){
              zNames[nName] = z[i];
            }
            ++nName;
            ++i;
          }
        }
        if( i == n ){
          return bsqlPathFail(pError, BSQL_INVALID,
                              "unterminated quoted name", i);
        }
        ++i;
        if( pOut ){
          step.u.name = bebop_string_view(zNames, nName);
          zNames[nName] = 0;
          zNames += nName + 1;
        }
      }else{
        first = i;
        while( i < n && z[i] != '.' && z[i] != '[' ){
          ++i;
        }
        if( i == first ){
          return bsqlPathFail(pError, BSQL_INVALID, "empty member name", i);
        }
        step.u.name = bebop_string_view(z + first, i - first);
      }
    }else if( z[i] == '[' ){
      step.eKind = BSQL_PATH_INDEX;
      ++i;
      if( i < n && z[i] == '#' ){
        step.eKind = BSQL_PATH_APPEND;
        ++i;
        if( i < n && z[i] == '-' ){
          step.eKind = BSQL_PATH_FROM_END;
          ++i;
        }
      }else if( i < n && z[i] == '-' ){
        step.u.index.bNegative = 1;
        ++i;
      }
      first = i;
      while( i < n && bsqlDigit((unsigned char)z[i]) ){
        digit = (unsigned)(z[i] - '0');
        if( step.u.index.magnitude <=
            (UINT64_MAX - digit) / BSQL_DECIMAL_RADIX ){
          step.u.index.magnitude =
            step.u.index.magnitude * BSQL_DECIMAL_RADIX + digit;
        }else{
          step.u.index.magnitude = UINT64_MAX;
          step.u.index.bOverflow = 1;
        }
        ++i;
      }
      if( i == n || z[i] != ']'
       || (step.eKind != BSQL_PATH_APPEND && i == first)
       || (step.eKind == BSQL_PATH_APPEND && i != first) ){
        return bsqlPathFail(pError, BSQL_INVALID, "invalid array index", i);
      }
      if( step.eKind == BSQL_PATH_APPEND && !bAppend ){
        return bsqlPathFail(pError, BSQL_INVALID,
                            "append index is not readable", step.iSource);
      }
      step.u.index.zDigits = z + first - step.u.index.bNegative;
      step.u.index.nDigit = i - first + step.u.index.bNegative;
      ++i;
    }else{
      return bsqlPathFail(pError, BSQL_INVALID, "expected '.' or '['", i);
    }
    if( pOut ){
      pOut->aStep[*pnStep] = step;
    }
    ++*pnStep;
  }
  return 1;
}

/* Parse a bounded path into one SQLite allocation, with no large stack copy.
** Invalid UTF-8, escapes, NULs, limits, or OOM return NULL and set pError.
*/
static BsqlPath *bsqlPathParse(
  const char *z, size_t n, int bAppend, BsqlError *pError
){
  BsqlPath *p;
  const char *zNul;
  char *zSource;
  char *zNames;
  size_t nStep;
  size_t nAlloc;
  if( !z || n == 0 ){
    bsqlPathFail(pError, BSQL_INVALID, "expected '$'", 0);
    return 0;
  }
  if( n > BSQL_MAX_PATH_BYTES ){
    bsqlPathFail(pError, BSQL_LIMIT, "path byte limit exceeded", 0);
    return 0;
  }
  zNul = memchr(z, 0, n);
  if( zNul ){
    bsqlPathFail(pError, BSQL_INVALID, "embedded NUL in path", zNul - z);
    return 0;
  }
  if( !bsqlUtf8Valid((const uint8_t *)z, n) ){
    bsqlPathFail(pError, BSQL_INVALID, "invalid UTF-8 path", 0);
    return 0;
  }
  if( !bsqlPathScan(z, n, bAppend, 0, 0, &nStep, pError) ){
    return 0;
  }
  nAlloc = sizeof(*p) + nStep * sizeof(*p->aStep) + 2 * (n + 1);
  p = sqlite3_malloc64((sqlite3_uint64)nAlloc);
  if( !p ){
    bsqlFail(pError, BSQL_NOMEM, "out of memory");
    return 0;
  }
  p->aStep = (BsqlPathStep *)(p + 1);
  zSource = (char *)(p->aStep + nStep);
  zNames = zSource + n + 1;
  memcpy(zSource, z, n);
  zSource[n] = 0;
  if( !bsqlPathScan(zSource, n, bAppend, p, zNames, &p->nStep, pError) ){
    sqlite3_free(p);
    return 0;
  }
  return p;
}

/*
** Free a path; the void signature also serves as an SQLite auxdata destructor.
*/
static void bsqlPathFree(void *p){
  sqlite3_free(p);
}

/* Compare byte strings lexicographically without assuming NUL termination. */
static int bsqlStringCompare(Bebop_String a, Bebop_String b){
  size_t n;
  int order;
  n = a.length < b.length ? a.length : b.length;
  order = n ? memcmp(a.data, b.data, n) : 0;
  return order ? order : (a.length > b.length) - (a.length < b.length);
}

/* Compare validated byte strings for equality. Empty strings may be NULL. */
static int bsqlStringEqual(Bebop_String a, Bebop_String b){
  return a.length == b.length &&
         (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

/* Check a nonempty, NUL-free UTF-8 descriptor identifier. No allocation. */
static int bsqlIdentifier(Bebop_String name){
  return name.length && name.data &&
         !memchr(name.data, 0, name.length) &&
         bsqlUtf8Valid((const uint8_t *)name.data, name.length);
}

/* Return zero for a missing type rather than dereferencing an absent value. */
static unsigned bsqlTypeKind(const BsqlType *p){
  return p && p->kind.has_value ? (unsigned)p->kind.value : 0;
}

/* Return the actual array element, never an unrelated optional field. */
static const BsqlType *bsqlElement(const BsqlType *p){
  unsigned kind;
  kind = bsqlTypeKind(p);
  if( kind == BEBOP_TYPE_KIND_ARRAY ){
    return p->array_element.has_value ? p->array_element.value : 0;
  }
  if( kind == BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return p->fixed_array_element.has_value ? p->fixed_array_element.value : 0;
  }
  return 0;
}

/* Construct a primitive or a defined-type shell without allocating memory. */
static BsqlType bsqlPrimitive(unsigned kind){
  BsqlType type;
  memset(&type, 0, sizeof(type));
  BEBOP_SET(type.kind, (Bebop_TypeKind)kind);
  return type;
}

/* Sort definition pointers by qualified name. Used only on decoded schemas. */
static int bsqlDefinitionCompare(const void *a, const void *b){
  const BsqlDefinition *pa;
  const BsqlDefinition *pb;
  pa = *(BsqlDefinition *const *)a;
  pb = *(BsqlDefinition *const *)b;
  return bsqlStringCompare(pa->fqn.value, pb->fqn.value);
}

/* Find a definition index in O(log n). SIZE_MAX denotes an absent name. */
static size_t bsqlDefinitionIndex(const BsqlSchema *p, Bebop_String name){
  size_t lo;
  size_t hi;
  size_t mid;
  int order;
  lo = 0;
  hi = p->nDefinition;
  while( lo < hi ){
    mid = lo + (hi - lo) / 2;
    order = bsqlStringCompare(name, p->apDefinition[mid]->fqn.value);
    if( order == 0 ){
      return mid;
    }
    if( order < 0 ){
      hi = mid;
    }else{
      lo = mid + 1;
    }
  }
  return SIZE_MAX;
}

/* Resolve a qualified name to a borrowed definition, or NULL if absent. */
static const BsqlDefinition *bsqlDefinitionFind(
  const BsqlSchema *p, Bebop_String name
){
  size_t i;
  i = bsqlDefinitionIndex(p, name);
  return i == SIZE_MAX ? 0 : p->apDefinition[i];
}

/* Return a stable defined type for a known qualified name. No allocation. */
static const BsqlType *bsqlDefinitionType(
  const BsqlSchema *p, Bebop_String name
){
  size_t i;
  i = bsqlDefinitionIndex(p, name);
  return i == SIZE_MAX ? 0 : p->aDefinitionType + i;
}

/* Return record fields, or a zero-length array for other definition kinds. */
static Bebop_FieldDescriptor_Array bsqlFields(const BsqlDefinition *p){
  Bebop_FieldDescriptor_Array empty;
  memset(&empty, 0, sizeof(empty));
  if( !p ){
    return empty;
  }
  if( p->kind.value == BEBOP_DEFINITION_KIND_STRUCT
   && p->struct_def.has_value && p->struct_def.value
   && p->struct_def.value->fields.has_value ){
    return p->struct_def.value->fields.value;
  }
  if( p->kind.value == BEBOP_DEFINITION_KIND_MESSAGE
   && p->message_def.has_value && p->message_def.value
   && p->message_def.value->fields.has_value ){
    return p->message_def.value->fields.value;
  }
  return empty;
}

/* Prefer an explicit union reference over an inline qualified name. */
static Bebop_String bsqlBranchName(const BsqlBranch *p){
  return p->type_ref_fqn.has_value ? p->type_ref_fqn.value :
                                    p->inline_fqn.value;
}

/* Compare name-index rows. Index construction later rejects equal names. */
static int bsqlNameCompare(const void *a, const void *b){
  return bsqlStringCompare(((const BsqlName *)a)->name,
                          ((const BsqlName *)b)->name);
}

/* Binary-search names while returning their original descriptor positions. */
static size_t bsqlNameFind(const BsqlDefinitionInfo *p, Bebop_String name){
  size_t lo;
  size_t hi;
  size_t mid;
  int order;
  lo = 0;
  hi = p->nName;
  while( lo < hi ){
    mid = lo + (hi - lo) / 2;
    order = bsqlStringCompare(name, p->aName[mid].name);
    if( order == 0 ){
      return p->aName[mid].iOriginal;
    }
    if( order < 0 ){
      hi = mid;
    }else{
      lo = mid + 1;
    }
  }
  return SIZE_MAX;
}

/* Reserve per-definition names and optional direct tag indexes in its arena.
** Allocation failure is reported with the arena's real failure category.
*/
static int bsqlNameReserve(
  BsqlSchema *p, BsqlDefinitionInfo *pInfo, size_t n, int bTags,
  BsqlError *pError
){
  if( n > BSQL_MAX_DEFINITIONS ){
    return bsqlFail(pError, BSQL_LIMIT, "definition member limit exceeded");
  }
  pInfo->nName = n;
  pInfo->aName = bsqlArenaArray(p->pArena, n, sizeof(*pInfo->aName));
  if( bTags ){
    pInfo->aTag = bsqlArenaArray(p->pArena, BSQL_TAG_SLOTS,
                                sizeof(*pInfo->aTag));
  }
  if( (n && !pInfo->aName) || (bTags && !pInfo->aTag) ){
    return bsqlArenaFail(&p->allocator, pError, "cannot allocate name index");
  }
  return 1;
}

/*
** Sort and validate names in O(n log n), replacing quadratic duplicate scans.
*/
static int bsqlNamesFinish(BsqlDefinitionInfo *p, BsqlError *pError){
  size_t i;
  for(i=0; i<p->nName; ++i){
    if( !bsqlIdentifier(p->aName[i].name) ){
      return bsqlFail(pError, BSQL_INVALID, "invalid member name");
    }
  }
  if( p->nName > 1 ){
    qsort(p->aName, p->nName, sizeof(*p->aName), bsqlNameCompare);
  }
  for(i=1; i<p->nName; ++i){
    if( bsqlStringEqual(p->aName[i-1].name, p->aName[i].name) ){
      return bsqlFail(pError, BSQL_INVALID, "duplicate member name");
    }
  }
  return 1;
}

/* Count nested definitions when the index is NULL; otherwise fill the exact
** reserved index. Recursion is bounded independently of descriptor decoding.
*/
static int bsqlGatherDefinitions(
  BsqlSchema *p, Bebop_DefinitionDescriptor_Array definitions,
  unsigned nDepth, BsqlError *pError
){
  size_t i;
  BsqlDefinition *pDefinition;
  if( nDepth >= BSQL_MAX_DEPTH ){
    return bsqlFail(pError, BSQL_LIMIT, "descriptor depth exceeded");
  }
  if( definitions.length && !definitions.data ){
    return bsqlFail(pError, BSQL_INVALID, "missing definition array");
  }
  for(i=0; i<definitions.length; ++i){
    pDefinition = definitions.data + i;
    if( !pDefinition->kind.has_value || !pDefinition->name.has_value
     || !pDefinition->fqn.has_value
     || !bsqlIdentifier(pDefinition->name.value)
     || !bsqlIdentifier(pDefinition->fqn.value) ){
      return bsqlFail(pError, BSQL_INVALID, "invalid definition name");
    }
    if( p->nDefinition == BSQL_MAX_DEFINITIONS ){
      return bsqlFail(pError, BSQL_LIMIT, "definition limit exceeded");
    }
    if( p->apDefinition ){
      p->apDefinition[p->nDefinition] = pDefinition;
    }
    ++p->nDefinition;
    if( pDefinition->nested.has_value &&
        !bsqlGatherDefinitions(p, pDefinition->nested.value,
                               nDepth + 1, pError) ){
      return 0;
    }
  }
  return 1;
}

/* Validate type shape, references, map keys, and fixed-array bounds without
** traversing reference cycles. No allocation; both depth and work are bounded.
*/
static int bsqlValidateType(
  const BsqlSchema *p, const BsqlType *pType, unsigned nDepth,
  size_t *pnBudget, BsqlError *pError
){
  unsigned kind;
  unsigned key;
  const BsqlDefinition *pDefinition;
  if( nDepth >= BSQL_MAX_DEPTH || *pnBudget == 0 ){
    return bsqlFail(pError, BSQL_LIMIT, "type complexity exceeded");
  }
  --*pnBudget;
  kind = bsqlTypeKind(pType);
  if( kind == 0 || kind > BEBOP_TYPE_KIND_DEFINED ){
    return bsqlFail(pError, BSQL_INVALID, "invalid type kind");
  }
  if( kind < BEBOP_TYPE_KIND_ARRAY ){
    return kind < BSQL_COUNT(bsqlPrimitives) ||
           bsqlFail(pError, BSQL_INVALID, "unsupported primitive type");
  }
  if( kind == BEBOP_TYPE_KIND_DEFINED ){
    pDefinition = pType->defined_fqn.has_value ?
      bsqlDefinitionFind(p, pType->defined_fqn.value) : 0;
    if( !pDefinition || pDefinition->kind.value < BEBOP_DEFINITION_KIND_ENUM
     || pDefinition->kind.value > BEBOP_DEFINITION_KIND_UNION ){
      return bsqlFail(pError, BSQL_INVALID, "unresolved data type");
    }
    return 1;
  }
  if( kind == BEBOP_TYPE_KIND_MAP ){
    if( !pType->map_key.has_value || !pType->map_value.has_value ){
      return bsqlFail(pError, BSQL_INVALID, "missing map type");
    }
    key = bsqlTypeKind(pType->map_key.value);
    if( !(key >= BEBOP_TYPE_KIND_BOOL && key <= BEBOP_TYPE_KIND_UINT_128)
     && key != BEBOP_TYPE_KIND_STRING && key != BEBOP_TYPE_KIND_UUID ){
      return bsqlFail(pError, BSQL_INVALID, "invalid map key type");
    }
    return bsqlValidateType(p, pType->map_key.value, nDepth + 1,
                            pnBudget, pError) &&
           bsqlValidateType(p, pType->map_value.value, nDepth + 1,
                            pnBudget, pError);
  }
  if( kind != BEBOP_TYPE_KIND_ARRAY && kind != BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return bsqlFail(pError, BSQL_INVALID, "unsupported container type");
  }
  if( kind == BEBOP_TYPE_KIND_FIXED_ARRAY &&
      (!pType->fixed_array_size.has_value || !pType->fixed_array_size.value
       || pType->fixed_array_size.value > UINT16_MAX) ){
    return bsqlFail(pError, BSQL_INVALID, "invalid fixed array size");
  }
  return bsqlValidateType(p, bsqlElement(pType), nDepth + 1,
                          pnBudget, pError);
}

/* Inline layout states are separate from legal recursive message references. */
enum { BSQL_LAYOUT_NEW, BSQL_LAYOUT_ACTIVE, BSQL_LAYOUT_DONE };

/* Reject cycles consisting only of structs and fixed arrays. Arena-backed
** state provides memoization; recursion is bounded and does not allocate.
*/
static int bsqlInlineLayout(
  const BsqlSchema *p, const BsqlType *pType, uint8_t *aState,
  unsigned nDepth, BsqlError *pError
){
  size_t i;
  size_t index;
  const BsqlDefinition *pDefinition;
  Bebop_FieldDescriptor_Array fields;
  if( nDepth >= BSQL_MAX_DEPTH ){
    return bsqlFail(pError, BSQL_LIMIT, "inline layout depth exceeded");
  }
  if( bsqlTypeKind(pType) == BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return bsqlInlineLayout(p, bsqlElement(pType), aState, nDepth + 1,
                            pError);
  }
  if( bsqlTypeKind(pType) != BEBOP_TYPE_KIND_DEFINED ){
    return 1;
  }
  index = bsqlDefinitionIndex(p, pType->defined_fqn.value);
  if( index == SIZE_MAX ){
    return bsqlFail(pError, BSQL_INVALID, "unresolved inline type");
  }
  pDefinition = p->apDefinition[index];
  if( pDefinition->kind.value != BEBOP_DEFINITION_KIND_STRUCT ){
    return 1;
  }
  if( aState[index] == BSQL_LAYOUT_ACTIVE ){
    return bsqlFail(pError, BSQL_INVALID, "recursive inline struct layout");
  }
  if( aState[index] == BSQL_LAYOUT_DONE ){
    return 1;
  }
  aState[index] = BSQL_LAYOUT_ACTIVE;
  fields = bsqlFields(pDefinition);
  for(i=0; i<fields.length; ++i){
    if( !bsqlInlineLayout(p, fields.data[i].type.value, aState,
                          nDepth + 1, pError) ){
      return 0;
    }
  }
  aState[index] = BSQL_LAYOUT_DONE;
  return 1;
}

/* Normalize optional empty lists and index an enum in its schema arena.
** Invalid bases/members and allocation failures are reported through pError.
*/
static int bsqlIndexEnum(
  BsqlSchema *p, BsqlDefinition *pDefinition, BsqlDefinitionInfo *pInfo,
  BsqlError *pError
){
  Bebop_EnumDef *pEnum;
  Bebop_EnumMemberDescriptor_Array empty;
  Bebop_EnumMemberDescriptor_Array members;
  size_t i;
  if( !pDefinition->enum_def.has_value || !pDefinition->enum_def.value ){
    return bsqlFail(pError, BSQL_INVALID, "missing enum definition");
  }
  pEnum = pDefinition->enum_def.value;
  if( !pEnum->base_type.has_value
   || pEnum->base_type.value < BEBOP_TYPE_KIND_BYTE
   || pEnum->base_type.value > BEBOP_TYPE_KIND_UINT_64 ){
    return bsqlFail(pError, BSQL_INVALID, "invalid enum base");
  }
  if( !pEnum->is_flags.has_value ){
    BEBOP_SET(pEnum->is_flags, 0);
  }
  if( !pEnum->members.has_value ){
    memset(&empty, 0, sizeof(empty));
    BEBOP_SET(pEnum->members, empty);
  }
  members = pEnum->members.value;
  if( members.length && !members.data ){
    return bsqlFail(pError, BSQL_INVALID, "missing enum members");
  }
  if( !bsqlNameReserve(p, pInfo, members.length, 0, pError) ){
    return 0;
  }
  for(i=0; i<members.length; ++i){
    if( !members.data[i].name.has_value || !members.data[i].value.has_value ){
      return bsqlFail(pError, BSQL_INVALID, "invalid enum member");
    }
    pInfo->aName[i].name = members.data[i].name.value;
    pInfo->aName[i].iOriginal = i;
  }
  return bsqlNamesFinish(pInfo, pError);
}

/* Normalize and index a struct or message. Names use a sorted index; message
** tags use a direct table, giving linear tag validation and constant lookup.
*/
static int bsqlIndexRecord(
  BsqlSchema *p, BsqlDefinition *pDefinition, BsqlDefinitionInfo *pInfo,
  size_t *pnBudget, BsqlError *pError
){
  Bebop_FieldDescriptor_Array empty;
  Bebop_FieldDescriptor_Array fields;
  const BsqlField *pField;
  int bMessage;
  size_t i;
  unsigned tag;
  bMessage = pDefinition->kind.value == BEBOP_DEFINITION_KIND_MESSAGE;
  memset(&empty, 0, sizeof(empty));
  if( bMessage ){
    if( !pDefinition->message_def.has_value
      || !pDefinition->message_def.value ){
      return bsqlFail(pError, BSQL_INVALID, "missing message definition");
    }
    if( !pDefinition->message_def.value->fields.has_value ){
      BEBOP_SET(pDefinition->message_def.value->fields, empty);
    }
  }else{
    if( !pDefinition->struct_def.has_value || !pDefinition->struct_def.value ){
      return bsqlFail(pError, BSQL_INVALID, "missing struct definition");
    }
    if( !pDefinition->struct_def.value->fields.has_value ){
      BEBOP_SET(pDefinition->struct_def.value->fields, empty);
    }
  }
  fields = bsqlFields(pDefinition);
  if( (fields.length && !fields.data) || (bMessage
    && fields.length > UINT8_MAX) ){
    return bsqlFail(pError, BSQL_INVALID, "invalid record field count");
  }
  if( !bsqlNameReserve(p, pInfo, fields.length, bMessage, pError) ){
    return 0;
  }
  for(i=0; i<fields.length; ++i){
    pField = fields.data + i;
    if( !pField->name.has_value ){
      return bsqlFail(pError, BSQL_INVALID, "missing field name");
    }
    pInfo->aName[i].name = pField->name.value;
    pInfo->aName[i].iOriginal = i;
    if( bMessage ){
      if( !pField->index.has_value || pField->index.value == 0
       || pField->index.value > UINT8_MAX ){
        return bsqlFail(pError, BSQL_INVALID, "invalid field tag");
      }
      tag = (unsigned)pField->index.value;
      if( pInfo->aTag[tag] ){
        return bsqlFail(pError, BSQL_INVALID, "duplicate field tag");
      }
      pInfo->aTag[tag] = (uint16_t)(i + 1);
    }
    if( !bsqlValidateType(p, pField->type.has_value ? pField->type.value : 0,
                          0, pnBudget, pError) ){
      return 0;
    }
  }
  return bsqlNamesFinish(pInfo, pError);
}

/* Validate union targets and build name/tag indexes in the schema arena. */
static int bsqlIndexUnion(
  BsqlSchema *p, BsqlDefinition *pDefinition, BsqlDefinitionInfo *pInfo,
  BsqlError *pError
){
  Bebop_UnionBranchDescriptor_Array branches;
  const BsqlBranch *pBranch;
  const BsqlDefinition *pTarget;
  size_t i;
  unsigned tag;
  if( !pDefinition->union_def.has_value || !pDefinition->union_def.value
   || !pDefinition->union_def.value->branches.has_value ){
    return bsqlFail(pError, BSQL_INVALID, "missing union definition");
  }
  branches = pDefinition->union_def.value->branches.value;
  if( branches.length > UINT8_MAX || (branches.length && !branches.data) ){
    return bsqlFail(pError, BSQL_INVALID, "invalid union branch count");
  }
  if( !bsqlNameReserve(p, pInfo, branches.length, 1, pError) ){
    return 0;
  }
  for(i=0; i<branches.length; ++i){
    pBranch = branches.data + i;
    if( !pBranch->name.has_value || !pBranch->discriminator.has_value
     || (!pBranch->type_ref_fqn.has_value && !pBranch->inline_fqn.has_value) ){
      return bsqlFail(pError, BSQL_INVALID, "incomplete union branch");
    }
    tag = (unsigned)pBranch->discriminator.value;
    pTarget = bsqlDefinitionFind(p, bsqlBranchName(pBranch));
    if( tag == 0 || tag > UINT8_MAX || !pTarget
     || (pTarget->kind.value != BEBOP_DEFINITION_KIND_STRUCT
         && pTarget->kind.value != BEBOP_DEFINITION_KIND_MESSAGE) ){
      return bsqlFail(pError, BSQL_INVALID, "invalid union branch");
    }
    if( pInfo->aTag[tag] ){
      return bsqlFail(pError, BSQL_INVALID, "duplicate union discriminator");
    }
    pInfo->aTag[tag] = (uint16_t)(i + 1);
    pInfo->aName[i].name = pBranch->name.value;
    pInfo->aName[i].iOriginal = i;
  }
  return bsqlNamesFinish(pInfo, pError);
}

/* Build all immutable schema indexes in its bounded arena. Caller releases
** the entire schema on failure, so partially built indexes cannot leak.
*/
static int bsqlSchemaIndex(BsqlSchema *p, BsqlError *pError){
  Bebop_SchemaDescriptor *pPart;
  BsqlDefinition *pDefinition;
  uint8_t *aState;
  size_t i;
  size_t nBudget;
  if( !p->descriptor.schemas.has_value
   || (p->descriptor.schemas.value.length &&
       !p->descriptor.schemas.value.data) ){
    return bsqlFail(pError, BSQL_INVALID, "missing schema descriptors");
  }
  for(i=0; i<p->descriptor.schemas.value.length; ++i){
    pPart = p->descriptor.schemas.value.data + i;
    if( !pPart->edition.has_value
      || pPart->edition.value != BEBOP_EDITION_2026 ){
      return bsqlFail(pError, BSQL_INVALID, "unsupported wire edition");
    }
    if( pPart->definitions.has_value &&
        !bsqlGatherDefinitions(p, pPart->definitions.value, 0, pError) ){
      return 0;
    }
  }
  if( p->nDefinition ){
    p->apDefinition = bsqlArenaArray(p->pArena, p->nDefinition,
                                    sizeof(*p->apDefinition));
    if( !p->apDefinition ){
      return bsqlArenaFail(&p->allocator, pError,
                           "cannot allocate definition index");
    }
    p->nDefinition = 0;
    for(i=0; i<p->descriptor.schemas.value.length; ++i){
      pPart = p->descriptor.schemas.value.data + i;
      if( pPart->definitions.has_value &&
          !bsqlGatherDefinitions(p, pPart->definitions.value, 0, pError) ){
        return 0;
      }
    }
  }
  if( p->nDefinition > 1 ){
    qsort(p->apDefinition, p->nDefinition, sizeof(*p->apDefinition),
          bsqlDefinitionCompare);
  }
  p->aDefinitionType = bsqlArenaArray(p->pArena, p->nDefinition,
                                     sizeof(*p->aDefinitionType));
  p->aInfo = bsqlArenaArray(p->pArena, p->nDefinition, sizeof(*p->aInfo));
  aState = bsqlArenaArray(p->pArena, p->nDefinition, sizeof(*aState));
  if( p->nDefinition && (!p->aDefinitionType || !p->aInfo || !aState) ){
    return bsqlArenaFail(&p->allocator, pError, "out of memory");
  }
  for(i=0; i<p->nDefinition; ++i){
    pDefinition = p->apDefinition[i];
    if( i && bsqlStringEqual(pDefinition->fqn.value,
                             p->apDefinition[i-1]->fqn.value) ){
      return bsqlFail(pError, BSQL_INVALID, "duplicate definition name");
    }
    BEBOP_SET(p->aDefinitionType[i].kind, BEBOP_TYPE_KIND_DEFINED);
    BEBOP_SET(p->aDefinitionType[i].defined_fqn, pDefinition->fqn.value);
  }
  nBudget = BSQL_MAX_ITEMS;
  for(i=0; i<p->nDefinition; ++i){
    pDefinition = p->apDefinition[i];
    switch( pDefinition->kind.value ){
      case BEBOP_DEFINITION_KIND_ENUM:
        if( !bsqlIndexEnum(p, pDefinition, p->aInfo + i, pError) ){
          return 0;
        }
        break;
      case BEBOP_DEFINITION_KIND_STRUCT:
      case BEBOP_DEFINITION_KIND_MESSAGE:
        if( !bsqlIndexRecord(p, pDefinition, p->aInfo + i, &nBudget, pError) ){
          return 0;
        }
        break;
      case BEBOP_DEFINITION_KIND_UNION:
        if( !bsqlIndexUnion(p, pDefinition, p->aInfo + i, pError) ){
          return 0;
        }
        break;
      default:
        /* Non-data definitions may exist but cannot be referenced as types. */
        break;
    }
  }
  for(i=0; i<p->nDefinition; ++i){
    if( !bsqlInlineLayout(p, p->aDefinitionType + i, aState, 0, pError) ){
      return 0;
    }
  }
  return 1;
}

/* Retain a live schema. Connection serialization protects this counter. */
static BsqlSchema *bsqlSchemaRetain(BsqlSchema *p){
  assert(p && p->nRef > 0 && p->nRef < UINT_MAX);
  ++p->nRef;
  return p;
}

/* Release a schema and its SQLite-backed arena after the final reference. */
static void bsqlSchemaRelease(BsqlSchema *p){
  if( p ){
    assert(p->nRef > 0);
    if( --p->nRef == 0 ){
      if( p->pArena ){
        bebop_context_free(p->pArena);
      }
      sqlite3_free(p->aDescriptor);
      sqlite3_free(p->aKey);
      sqlite3_free(p);
    }
  }
}

/* Decode a bounded descriptor from an owned copy. All allocations use SQLite;
** malformed input, arena limits, and OOM release the entire partial schema.
*/
static BsqlSchema *bsqlSchemaDecode(Bebop_View bytes, BsqlError *pError){
  BsqlSchema *p;
  if( !bytes.data || bytes.length == 0 || bytes.length > BSQL_MAX_SPEC_BYTES ){
    bsqlFail(pError, BSQL_INVALID, "invalid descriptor size");
    return 0;
  }
  p = sqlite3_malloc64(sizeof(*p));
  if( !p ){
    bsqlFail(pError, BSQL_NOMEM, "out of memory");
    return 0;
  }
  memset(p, 0, sizeof(*p));
  p->nRef = 1;
  p->aDescriptor = sqlite3_malloc64((sqlite3_uint64)bytes.length);
  if( !p->aDescriptor ){
    bsqlFail(pError, BSQL_NOMEM, "out of memory");
    goto failed;
  }
  memcpy(p->aDescriptor, bytes.data, bytes.length);
  p->nDescriptor = bytes.length;
  if( !bsqlArenaInit(&p->allocator, &p->pArena, BSQL_MAX_ARENA_BYTES, pError) ){
    goto failed;
  }
  if( Bebop_DescriptorSet_decode(p->pArena,
        bebop_view(p->aDescriptor, p->nDescriptor), &p->descriptor)
      != BEBOP_RESULT_OK ){
    bsqlFail(pError, p->allocator.eFailure ? p->allocator.eFailure :
              BSQL_INVALID, "invalid descriptor");
    goto failed;
  }
  if( !bsqlSchemaIndex(p, pError) ){
    goto failed;
  }
  return p;
failed:
  bsqlSchemaRelease(p);
  return 0;
}

/* A parser borrows its text and allocates type nodes in the schema's arena. */
typedef struct BsqlTypeParser BsqlTypeParser;
struct BsqlTypeParser {
  BsqlSchema *pSchema;            /* Owner of the resulting type nodes. */
  const char *zCurrent;           /* Next unread byte. */
  const char *zEnd;               /* One past the input. */
  BsqlError *pError;              /* First parser or allocation failure. */
};

/* Parse primitive, qualified, map, variable-array, and fixed-array types.
** Arena allocation and recursion are bounded; failure returns NULL.
*/
static BsqlType *bsqlParseType(BsqlTypeParser *p, unsigned nDepth){
  const char *zStart;
  const BsqlDefinition *pDefinition;
  BsqlType *pType;
  BsqlType *pKey;
  BsqlType *pValue;
  BsqlType *pParent;
  size_t nName;
  unsigned kind;
  unsigned i;
  uint32_t nCount;
  unsigned digit;
  int bFixed;
  if( nDepth >= BSQL_MAX_DEPTH ){
    bsqlFail(p->pError, BSQL_LIMIT, "type expression depth exceeded");
    return 0;
  }
  zStart = p->zCurrent;
  while( p->zCurrent < p->zEnd && *p->zCurrent != '['
      && *p->zCurrent != ']' && *p->zCurrent != ',' ){
    ++p->zCurrent;
  }
  nName = (size_t)(p->zCurrent - zStart);
  pType = bsqlArenaArray(p->pSchema->pArena, 1, sizeof(*pType));
  if( !pType ){
    goto nomem;
  }
  if( nName == sizeof("map") - 1 && !memcmp(zStart, "map", nName)
   && p->zCurrent < p->zEnd && *p->zCurrent == '[' ){
    ++p->zCurrent;
    pKey = bsqlParseType(p, nDepth + 1);
    if( !pKey || p->zCurrent == p->zEnd || *p->zCurrent++ != ',' ){
      goto invalid;
    }
    pValue = bsqlParseType(p, nDepth + 1);
    if( !pValue || p->zCurrent == p->zEnd || *p->zCurrent++ != ']' ){
      goto invalid;
    }
    BEBOP_SET(pType->kind, BEBOP_TYPE_KIND_MAP);
    BEBOP_SET(pType->map_key, pKey);
    BEBOP_SET(pType->map_value, pValue);
  }else{
    kind = 0;
    for(i=1; i<BSQL_COUNT(bsqlPrimitives); ++i){
      if( bsqlPrimitives[i].nName == nName &&
          !memcmp(zStart, bsqlPrimitives[i].zName, nName) ){
        kind = i;
        break;
      }
    }
    if( kind ){
      BEBOP_SET(pType->kind, (Bebop_TypeKind)kind);
    }else{
      pDefinition = bsqlDefinitionFind(p->pSchema,
                                       bebop_string_view(zStart, nName));
      if( !pDefinition ){
        goto invalid;
      }
      BEBOP_SET(pType->kind, BEBOP_TYPE_KIND_DEFINED);
      BEBOP_SET(pType->defined_fqn, pDefinition->fqn.value);
    }
  }
  while( p->zCurrent < p->zEnd && *p->zCurrent == '[' ){
    ++p->zCurrent;
    nCount = 0;
    bFixed = p->zCurrent < p->zEnd &&
             bsqlDigit((unsigned char)*p->zCurrent);
    while( p->zCurrent < p->zEnd && bsqlDigit((unsigned char)*p->zCurrent) ){
      digit = (unsigned)(*p->zCurrent++ - '0');
      if( nCount > (UINT16_MAX - digit) / BSQL_DECIMAL_RADIX ){
        goto invalid;
      }
      nCount = nCount * BSQL_DECIMAL_RADIX + digit;
    }
    if( p->zCurrent == p->zEnd || *p->zCurrent++ != ']' ||
        (bFixed && nCount == 0) ){
      goto invalid;
    }
    if( ++nDepth >= BSQL_MAX_DEPTH ){
      bsqlFail(p->pError, BSQL_LIMIT, "type expression depth exceeded");
      return 0;
    }
    pParent = bsqlArenaArray(p->pSchema->pArena, 1, sizeof(*pParent));
    if( !pParent ){
      goto nomem;
    }
    if( bFixed ){
      BEBOP_SET(pParent->kind, BEBOP_TYPE_KIND_FIXED_ARRAY);
      BEBOP_SET(pParent->fixed_array_element, pType);
      BEBOP_SET(pParent->fixed_array_size, nCount);
    }else{
      BEBOP_SET(pParent->kind, BEBOP_TYPE_KIND_ARRAY);
      BEBOP_SET(pParent->array_element, pType);
    }
    pType = pParent;
  }
  return pType;
nomem:
  bsqlArenaFail(&p->pSchema->allocator, p->pError, "out of memory");
  return 0;
invalid:
  bsqlFail(p->pError, BSQL_INVALID, "invalid root type expression");
  return 0;
}

/* Copy only wire-significant type properties into a temporary arena. Strings
** remain borrowed until the caller encodes the result. NULL signals failure.
*/
static BsqlType *bsqlTypeCopy(
  Bebop_Context *pArena, const BsqlType *pSource, unsigned nDepth
){
  BsqlType *pOut;
  BsqlType *pChild;
  BsqlType *pValue;
  if( !pSource || nDepth >= BSQL_MAX_DEPTH ){
    return 0;
  }
  pOut = bsqlArenaArray(pArena, 1, sizeof(*pOut));
  if( !pOut ){
    return 0;
  }
  BEBOP_SET(pOut->kind, pSource->kind.value);
  switch( bsqlTypeKind(pSource) ){
    case BEBOP_TYPE_KIND_DEFINED:
      BEBOP_SET(pOut->defined_fqn, pSource->defined_fqn.value);
      break;
    case BEBOP_TYPE_KIND_ARRAY:
      pChild = bsqlTypeCopy(pArena, pSource->array_element.value, nDepth + 1);
      if( !pChild ){
        return 0;
      }
      BEBOP_SET(pOut->array_element, pChild);
      break;
    case BEBOP_TYPE_KIND_FIXED_ARRAY:
      pChild = bsqlTypeCopy(pArena, pSource->fixed_array_element.value,
                            nDepth + 1);
      if( !pChild ){
        return 0;
      }
      BEBOP_SET(pOut->fixed_array_element, pChild);
      BEBOP_SET(pOut->fixed_array_size, pSource->fixed_array_size.value);
      break;
    case BEBOP_TYPE_KIND_MAP:
      pChild = bsqlTypeCopy(pArena, pSource->map_key.value, nDepth + 1);
      pValue = bsqlTypeCopy(pArena, pSource->map_value.value, nDepth + 1);
      if( !pChild || !pValue ){
        return 0;
      }
      BEBOP_SET(pOut->map_key, pChild);
      BEBOP_SET(pOut->map_value, pValue);
      break;
    default:
      break;
  }
  return pOut;
}

/* Compare message fields by tag for deterministic canonical encoding. */
static int bsqlFieldTagCompare(const void *a, const void *b){
  uint32_t x;
  uint32_t y;
  x = ((const BsqlField *)a)->index.value;
  y = ((const BsqlField *)b)->index.value;
  return (x > y) - (x < y);
}

/* Compare union branches by discriminator without quadratic insertion sort. */
static int bsqlBranchTagCompare(const void *a, const void *b){
  unsigned x;
  unsigned y;
  x = ((const BsqlBranch *)a)->discriminator.value;
  y = ((const BsqlBranch *)b)->discriminator.value;
  return (x > y) - (x < y);
}

/* Copy a validated definition's wire properties into a temporary arena.
** Names borrow the source schema. Zero means allocation or unsupported kind.
*/
static int bsqlDefinitionCopy(
  Bebop_Context *pArena, const BsqlDefinition *pSource, BsqlDefinition *pOut
){
  Bebop_FieldDescriptor_Array inputFields;
  Bebop_FieldDescriptor_Array fields;
  Bebop_EnumMemberDescriptor_Array inputMembers;
  Bebop_EnumMemberDescriptor_Array members;
  Bebop_UnionBranchDescriptor_Array inputBranches;
  Bebop_UnionBranchDescriptor_Array branches;
  Bebop_StructDef *pStruct;
  Bebop_MessageDef *pMessage;
  Bebop_EnumDef *pEnum;
  Bebop_UnionDef *pUnion;
  BsqlType *pType;
  size_t i;
  BEBOP_SET(pOut->kind, pSource->kind.value);
  BEBOP_SET(pOut->fqn, pSource->fqn.value);
  BEBOP_SET(pOut->name, pSource->name.value);
  switch( pSource->kind.value ){
    case BEBOP_DEFINITION_KIND_STRUCT:
    case BEBOP_DEFINITION_KIND_MESSAGE:
      inputFields = bsqlFields(pSource);
      memset(&fields, 0, sizeof(fields));
      fields.length = inputFields.length;
      fields.data = bsqlArenaArray(pArena, fields.length, sizeof(*fields.data));
      if( fields.length && !fields.data ){
        return 0;
      }
      for(i=0; i<fields.length; ++i){
        pType = bsqlTypeCopy(pArena, inputFields.data[i].type.value, 0);
        if( !pType ){
          return 0;
        }
        BEBOP_SET(fields.data[i].name, inputFields.data[i].name.value);
        BEBOP_SET(fields.data[i].index,
          pSource->kind.value == BEBOP_DEFINITION_KIND_MESSAGE ?
          inputFields.data[i].index.value : 0);
        BEBOP_SET(fields.data[i].type, pType);
      }
      if( pSource->kind.value == BEBOP_DEFINITION_KIND_STRUCT ){
        pStruct = bsqlArenaArray(pArena, 1, sizeof(*pStruct));
        if( !pStruct ){
          return 0;
        }
        BEBOP_SET(pStruct->fields, fields);
        BEBOP_SET(pOut->struct_def, pStruct);
      }else{
        if( fields.length > 1 ){
          qsort(fields.data, fields.length, sizeof(*fields.data),
                bsqlFieldTagCompare);
        }
        pMessage = bsqlArenaArray(pArena, 1, sizeof(*pMessage));
        if( !pMessage ){
          return 0;
        }
        BEBOP_SET(pMessage->fields, fields);
        BEBOP_SET(pOut->message_def, pMessage);
      }
      return 1;
    case BEBOP_DEFINITION_KIND_ENUM:
      pEnum = bsqlArenaArray(pArena, 1, sizeof(*pEnum));
      if( !pEnum ){
        return 0;
      }
      BEBOP_SET(pEnum->base_type, pSource->enum_def.value->base_type.value);
      BEBOP_SET(pEnum->is_flags, pSource->enum_def.value->is_flags.value);
      inputMembers = pSource->enum_def.value->members.value;
      memset(&members, 0, sizeof(members));
      members.length = inputMembers.length;
      members.data = bsqlArenaArray(pArena, members.length,
                                    sizeof(*members.data));
      if( members.length && !members.data ){
        return 0;
      }
      for(i=0; i<members.length; ++i){
        BEBOP_SET(members.data[i].name, inputMembers.data[i].name.value);
        BEBOP_SET(members.data[i].value, inputMembers.data[i].value.value);
      }
      BEBOP_SET(pEnum->members, members);
      BEBOP_SET(pOut->enum_def, pEnum);
      return 1;
    case BEBOP_DEFINITION_KIND_UNION:
      pUnion = bsqlArenaArray(pArena, 1, sizeof(*pUnion));
      if( !pUnion ){
        return 0;
      }
      inputBranches = pSource->union_def.value->branches.value;
      memset(&branches, 0, sizeof(branches));
      branches.length = inputBranches.length;
      branches.data = bsqlArenaArray(pArena, branches.length,
                                     sizeof(*branches.data));
      if( branches.length && !branches.data ){
        return 0;
      }
      for(i=0; i<branches.length; ++i){
        BEBOP_SET(branches.data[i].name, inputBranches.data[i].name.value);
        BEBOP_SET(branches.data[i].discriminator,
                  inputBranches.data[i].discriminator.value);
        BEBOP_SET(branches.data[i].type_ref_fqn,
                  bsqlBranchName(inputBranches.data + i));
      }
      if( branches.length > 1 ){
        qsort(branches.data, branches.length, sizeof(*branches.data),
              bsqlBranchTagCompare);
      }
      BEBOP_SET(pUnion->branches, branches);
      BEBOP_SET(pOut->union_def, pUnion);
      return 1;
    default:
      return 0;
  }
}

/* Reachability uses a queue: each definition is enqueued and processed once. */
typedef struct BsqlReachability BsqlReachability;
struct BsqlReachability {
  uint8_t *aMarked;               /* One flag per indexed definition. */
  size_t *aQueue;                 /* Definition indexes, without duplicates. */
  size_t nQueue;                  /* Tail of the bounded queue. */
};

/* Mark definitions mentioned in one type tree. No allocation; depth is bounded
** and binary lookup avoids repeated scans of the entire definition index.
*/
static int bsqlMarkType(
  const BsqlSchema *p, const BsqlType *pType, BsqlReachability *pReach,
  unsigned nDepth, BsqlError *pError
){
  unsigned kind;
  size_t i;
  if( nDepth >= BSQL_MAX_DEPTH ){
    return bsqlFail(pError, BSQL_LIMIT, "type complexity exceeded");
  }
  kind = bsqlTypeKind(pType);
  if( kind == BEBOP_TYPE_KIND_ARRAY || kind == BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return bsqlMarkType(p, bsqlElement(pType), pReach, nDepth + 1, pError);
  }
  if( kind == BEBOP_TYPE_KIND_MAP ){
    return bsqlMarkType(p, pType->map_key.value, pReach, nDepth + 1, pError)
        && bsqlMarkType(p, pType->map_value.value, pReach, nDepth + 1, pError);
  }
  if( kind == BEBOP_TYPE_KIND_DEFINED ){
    i = bsqlDefinitionIndex(p, pType->defined_fqn.value);
    if( i == SIZE_MAX ){
      return bsqlFail(pError, BSQL_INVALID, "unresolved data type");
    }
    if( !pReach->aMarked[i] ){
      assert(pReach->nQueue < p->nDefinition);
      pReach->aMarked[i] = 1;
      pReach->aQueue[pReach->nQueue++] = i;
    }
  }
  return 1;
}

/* Append the version-1 canonical specification, retaining original enum-member
** order and sorting definitions/message tags/union tags as before. Temporary
** data uses a bounded SQLite-backed arena; output uses the SQLite heap.
*/
static int bsqlMakeSpec(
  const BsqlSchema *p, const BsqlType *pRoot, BsqlBuffer *pOut,
  BsqlError *pError
){
  BsqlAllocator allocator;
  Bebop_Context *pArena;
  BsqlReachability reach;
  const BsqlDefinition *pDefinition;
  Bebop_FieldDescriptor_Array fields;
  Bebop_UnionBranchDescriptor_Array branches;
  Bebop_DefinitionDescriptor_Array definitions;
  Bebop_SchemaDescriptor part;
  Bebop_SchemaDescriptor_Array parts;
  Bebop_DescriptorSet set;
  BsqlType branchType;
  BsqlType *pCleanRoot;
  Bebop_Writer *pWriter;
  Bebop_View encoded;
  uint8_t header[BSQL_SPEC_HEADER_BYTES];
  size_t i;
  size_t j;
  size_t head;
  size_t nRoot;
  int ok;
  memset(&allocator, 0, sizeof(allocator));
  memset(&reach, 0, sizeof(reach));
  pArena = 0;
  ok = 0;
  if( !bsqlArenaInit(&allocator, &pArena, BSQL_MAX_ARENA_BYTES, pError) ){
    return 0;
  }
  reach.aMarked = bsqlArenaArray(pArena, p->nDefinition,
                                 sizeof(*reach.aMarked));
  reach.aQueue = bsqlArenaArray(pArena, p->nDefinition,
                                sizeof(*reach.aQueue));
  if( p->nDefinition && (!reach.aMarked || !reach.aQueue) ){
    goto cleanup;
  }
  if( !bsqlMarkType(p, pRoot, &reach, 0, pError) ){
    goto cleanup;
  }
  for(head=0; head<reach.nQueue; ++head){
    pDefinition = p->apDefinition[reach.aQueue[head]];
    fields = bsqlFields(pDefinition);
    for(j=0; j<fields.length; ++j){
      if( !bsqlMarkType(p, fields.data[j].type.value, &reach, 0, pError) ){
        goto cleanup;
      }
    }
    if( pDefinition->kind.value == BEBOP_DEFINITION_KIND_UNION ){
      branches = pDefinition->union_def.value->branches.value;
      for(j=0; j<branches.length; ++j){
        branchType = bsqlPrimitive(BEBOP_TYPE_KIND_DEFINED);
        BEBOP_SET(branchType.defined_fqn, bsqlBranchName(branches.data + j));
        if( !bsqlMarkType(p, &branchType, &reach, 0, pError) ){
          goto cleanup;
        }
      }
    }
  }
  memset(&definitions, 0, sizeof(definitions));
  definitions.length = reach.nQueue;
  definitions.data = bsqlArenaArray(pArena, definitions.length,
                                    sizeof(*definitions.data));
  if( definitions.length && !definitions.data ){
    goto cleanup;
  }
  for(i=0, j=0; i<p->nDefinition; ++i){
    if( reach.aMarked[i] ){
      if( !bsqlDefinitionCopy(pArena, p->apDefinition[i],
                              definitions.data + j) ){
        goto cleanup;
      }
      ++j;
    }
  }
  memset(&part, 0, sizeof(part));
  memset(&parts, 0, sizeof(parts));
  memset(&set, 0, sizeof(set));
  BEBOP_SET(part.edition, BEBOP_EDITION_2026);
  BEBOP_SET(part.definitions, definitions);
  parts.data = &part;
  parts.length = 1;
  BEBOP_SET(set.schemas, parts);
  pCleanRoot = bsqlTypeCopy(pArena, pRoot, 0);
  pWriter = bebop_context_writer(pArena, 0);
  if( !pCleanRoot || !pWriter ||
      Bebop_TypeDescriptor_encode(pWriter, pCleanRoot) != BEBOP_RESULT_OK ){
    goto cleanup;
  }
  encoded = bebop_writer_view(pWriter);
  nRoot = encoded.length;
  if( Bebop_DescriptorSet_encode(pWriter, &set) != BEBOP_RESULT_OK ){
    goto cleanup;
  }
  encoded = bebop_writer_view(pWriter);
  if( encoded.length > BSQL_MAX_SPEC_BYTES - BSQL_SPEC_HEADER_BYTES ){
    bsqlFail(pError, BSQL_LIMIT, "spec byte limit exceeded");
    goto cleanup;
  }
  memset(header, 0, sizeof(header));
  memcpy(header, bsqlSpecMagic, sizeof(bsqlSpecMagic));
  bsqlStoreLe(header + BSQL_VERSION_OFFSET, BSQL_FORMAT_VERSION,
               sizeof(uint16_t));
  bsqlStoreLe(header + BSQL_SPEC_ROOT_OFFSET, nRoot, sizeof(uint32_t));
  bsqlStoreLe(header + BSQL_SPEC_DESCRIPTOR_OFFSET,
               encoded.length - nRoot, sizeof(uint32_t));
  ok = bsqlReserve(pOut, sizeof(header) + encoded.length, pError)
    && bsqlAppend(pOut, header, sizeof(header), pError)
    && bsqlAppend(pOut, encoded.data, encoded.length, pError);
cleanup:
  if( !ok && pError->eCode == BSQL_OK ){
    bsqlArenaFail(&allocator, pError, "cannot encode type specification");
  }
  bebop_context_free(pArena);
  return ok;
}

/* Rebase root reference names into the owned descriptor after decoding an
** external specification. No allocation; invalid references/depth fail.
*/
static int bsqlOwnRoot(BsqlSchema *p, BsqlType *pType, unsigned nDepth){
  const BsqlDefinition *pDefinition;
  if( nDepth >= BSQL_MAX_DEPTH ){
    return 0;
  }
  switch( bsqlTypeKind(pType) ){
    case BEBOP_TYPE_KIND_DEFINED:
      pDefinition = bsqlDefinitionFind(p, pType->defined_fqn.value);
      if( !pDefinition ){
        return 0;
      }
      pType->defined_fqn.value = pDefinition->fqn.value;
      return 1;
    case BEBOP_TYPE_KIND_ARRAY:
      return bsqlOwnRoot(p, pType->array_element.value, nDepth + 1);
    case BEBOP_TYPE_KIND_FIXED_ARRAY:
      return bsqlOwnRoot(p, pType->fixed_array_element.value, nDepth + 1);
    case BEBOP_TYPE_KIND_MAP:
      return bsqlOwnRoot(p, pType->map_key.value, nDepth + 1)
          && bsqlOwnRoot(p, pType->map_value.value, nDepth + 1);
    default:
      return 1;
  }
}

/* Load and verify a canonical specification. Transfer the verified canonical
** buffer into the schema rather than allocating another copy of the key.
** Any malformed input or allocation failure releases all temporary storage.
*/
static BsqlSchema *bsqlLoadSpec(Bebop_View bytes, BsqlError *pError){
  BsqlSchema *p;
  BsqlBuffer canonical;
  size_t nRoot;
  size_t nDescriptor;
  size_t nBudget;
  if( !bytes.data || bytes.length < BSQL_SPEC_HEADER_BYTES
   || bytes.length > BSQL_MAX_SPEC_BYTES
   || memcmp(bytes.data, bsqlSpecMagic, sizeof(bsqlSpecMagic))
   || bsqlLoadLe(bytes.data + BSQL_VERSION_OFFSET, sizeof(uint16_t)) !=
        BSQL_FORMAT_VERSION
   || bsqlLoadLe(bytes.data + BSQL_FLAGS_OFFSET, sizeof(uint16_t)) ){
    bsqlFail(pError, BSQL_INVALID, "invalid type specification header");
    return 0;
  }
  nRoot = (size_t)bsqlLoadLe(bytes.data + BSQL_SPEC_ROOT_OFFSET,
                            sizeof(uint32_t));
  nDescriptor = (size_t)bsqlLoadLe(bytes.data + BSQL_SPEC_DESCRIPTOR_OFFSET,
                                  sizeof(uint32_t));
  if( nRoot > bytes.length - BSQL_SPEC_HEADER_BYTES
   || nDescriptor != bytes.length - BSQL_SPEC_HEADER_BYTES - nRoot ){
    bsqlFail(pError, BSQL_INVALID, "invalid type specification lengths");
    return 0;
  }
  p = bsqlSchemaDecode(bebop_view(bytes.data + BSQL_SPEC_HEADER_BYTES + nRoot,
                                 nDescriptor), pError);
  if( !p ){
    return 0;
  }
  memset(&canonical, 0, sizeof(canonical));
  nBudget = BSQL_MAX_ITEMS;
  if( Bebop_TypeDescriptor_decode(p->pArena,
        bebop_view(bytes.data + BSQL_SPEC_HEADER_BYTES, nRoot), &p->root)
      != BEBOP_RESULT_OK ){
    bsqlFail(pError, p->allocator.eFailure ? p->allocator.eFailure :
              BSQL_INVALID, "invalid root type");
    goto failed;
  }
  if( !bsqlValidateType(p, &p->root, 0, &nBudget, pError)
   || !bsqlOwnRoot(p, &p->root, 0) ){
    bsqlFail(pError, BSQL_INVALID, "invalid root type");
    goto failed;
  }
  if( !bsqlMakeSpec(p, &p->root, &canonical, pError) ){
    goto failed;
  }
  if( canonical.nData != bytes.length ||
      memcmp(canonical.aData, bytes.data, bytes.length) ){
    bsqlFail(pError, BSQL_INVALID, "noncanonical type specification");
    goto failed;
  }
  p->aKey = canonical.aOwned;
  p->nKey = canonical.nData;
  p->nKeyAlloc = canonical.nAlloc;
  memset(&canonical, 0, sizeof(canonical));
  return p;
failed:
  bsqlBufferClear(&canonical);
  bsqlSchemaRelease(p);
  return 0;
}

/* Split a typed value without allocating or overflowing header arithmetic. */
static int bsqlSplitValue(
  Bebop_View bytes, Bebop_View *pSpec, Bebop_View *pPayload, BsqlError *pError
){
  size_t nSpec;
  uint64_t nPayload;
  if( !bytes.data || bytes.length < BSQL_VALUE_HEADER_BYTES
   || memcmp(bytes.data, bsqlValueMagic, sizeof(bsqlValueMagic))
   || bsqlLoadLe(bytes.data + BSQL_VERSION_OFFSET, sizeof(uint16_t)) !=
        BSQL_FORMAT_VERSION
   || bsqlLoadLe(bytes.data + BSQL_FLAGS_OFFSET, sizeof(uint16_t)) ){
    return bsqlFail(pError, BSQL_INVALID, "invalid typed Bebop value header");
  }
  nSpec = (size_t)bsqlLoadLe(bytes.data + BSQL_VALUE_SPEC_OFFSET,
                            sizeof(uint32_t));
  nPayload = bsqlLoadLe(bytes.data + BSQL_VALUE_PAYLOAD_OFFSET,
                        sizeof(uint64_t));
  if( nSpec > BSQL_MAX_SPEC_BYTES
   || nSpec > bytes.length - BSQL_VALUE_HEADER_BYTES
   || nPayload != bytes.length - BSQL_VALUE_HEADER_BYTES - nSpec ){
    return bsqlFail(pError, BSQL_INVALID, "invalid typed value lengths");
  }
  *pSpec = bebop_view(bytes.data + BSQL_VALUE_HEADER_BYTES, nSpec);
  *pPayload = bebop_view(bytes.data + BSQL_VALUE_HEADER_BYTES + nSpec,
                         (size_t)nPayload);
  return 1;
}

/* Append an envelope after one checked reservation. Output is SQLite-owned;
** overflow or OOM cannot leave a partial envelope in the logical buffer.
*/
static int bsqlWrapValue(
  Bebop_View spec, Bebop_View payload, BsqlBuffer *pOut, BsqlError *pError
){
  uint8_t *a;
  size_t n;
  if( spec.length > BSQL_MAX_SPEC_BYTES ||
      payload.length > bsqlMaxValueBytes - BSQL_VALUE_HEADER_BYTES ||
      spec.length > bsqlMaxValueBytes - BSQL_VALUE_HEADER_BYTES -
                    payload.length ){
    return bsqlFail(pError, BSQL_LIMIT, "value byte limit exceeded");
  }
  if( (spec.length && !spec.data) || (payload.length && !payload.data) ){
    return bsqlFail(pError, BSQL_INVALID, "missing envelope bytes");
  }
  n = BSQL_VALUE_HEADER_BYTES + spec.length + payload.length;
  if( !bsqlReserve(pOut, n, pError) ){
    return 0;
  }
  a = pOut->aOwned + pOut->nData;
  memset(a, 0, BSQL_VALUE_HEADER_BYTES);
  memcpy(a, bsqlValueMagic, sizeof(bsqlValueMagic));
  bsqlStoreLe(a + BSQL_VERSION_OFFSET, BSQL_FORMAT_VERSION, sizeof(uint16_t));
  bsqlStoreLe(a + BSQL_VALUE_SPEC_OFFSET, spec.length, sizeof(uint32_t));
  bsqlStoreLe(a + BSQL_VALUE_PAYLOAD_OFFSET, payload.length, sizeof(uint64_t));
  if( spec.length ){
    memcpy(a + BSQL_VALUE_HEADER_BYTES, spec.data, spec.length);
  }
  if( payload.length ){
    memcpy(a + BSQL_VALUE_HEADER_BYTES + spec.length, payload.data,
           payload.length);
  }
  pOut->nData += n;
  return 1;
}

/* Account actual owned capacities, including the schema structure itself. */
static size_t bsqlSchemaBytes(const BsqlSchema *p){
  return sizeof(*p) + p->allocator.nByte + p->nDescriptor + p->nKeyAlloc;
}

/* Return one retained schema using a small bounded LRU. Cache misses allocate
** through bsqlLoadSpec. Oversized entries remain usable but are not cached.
** Eviction subtracts freed entries from the running total before continuing.
*/
static BsqlSchema *bsqlCachedSchema(
  BsqlConnection *pConnection, Bebop_View spec, BsqlError *pError
){
  BsqlSchema *p;
  size_t i;
  size_t nByte;
  for(i=0; i<BSQL_CACHE_SLOTS; ++i){
    p = pConnection->apCache[i];
    if( p && p->nKey == spec.length &&
        !memcmp(p->aKey, spec.data, spec.length) ){
      memmove(pConnection->apCache + 1, pConnection->apCache,
              i * sizeof(*pConnection->apCache));
      pConnection->apCache[0] = p;
      return bsqlSchemaRetain(p);
    }
  }
  p = bsqlLoadSpec(spec, pError);
  if( !p || bsqlSchemaBytes(p) > BSQL_MAX_CACHE_BYTES ){
    return p;
  }
  bsqlSchemaRelease(pConnection->apCache[BSQL_CACHE_SLOTS - 1]);
  memmove(pConnection->apCache + 1, pConnection->apCache,
          (BSQL_CACHE_SLOTS - 1) * sizeof(*pConnection->apCache));
  pConnection->apCache[0] = bsqlSchemaRetain(p);
  nByte = 0;
  for(i=0; i<BSQL_CACHE_SLOTS; ++i){
    if( pConnection->apCache[i] ){
      nByte += bsqlSchemaBytes(pConnection->apCache[i]);
    }
  }
  for(i=BSQL_CACHE_SLOTS; nByte > BSQL_MAX_CACHE_BYTES && i>1; ){
    --i;
    if( pConnection->apCache[i] ){
      nByte -= bsqlSchemaBytes(pConnection->apCache[i]);
      bsqlSchemaRelease(pConnection->apCache[i]);
      pConnection->apCache[i] = 0;
    }
  }
  return p;
}

/* Release registration/table ownership and cached schemas at the last owner. */
static void bsqlConnectionRelease(void *pPointer){
  BsqlConnection *p;
  size_t i;
  p = pPointer;
  assert(p && p->nRef > 0);
  if( --p->nRef ){
    return;
  }
  for(i=0; i<BSQL_CACHE_SLOTS; ++i){
    bsqlSchemaRelease(p->apCache[i]);
  }
  sqlite3_free(p);
}

/* Resolve a view's kind without depending on arbitrary enum-number gaps. */
static unsigned bsqlViewKind(BsqlView view){
  const BsqlDefinition *pDefinition;
  unsigned kind;
  kind = bsqlTypeKind(view.pType);
  if( kind != BEBOP_TYPE_KIND_DEFINED ){
    return kind;
  }
  pDefinition = bsqlDefinitionFind(view.pSchema, view.pType->defined_fqn.value);
  return pDefinition ? BSQL_KIND_BASE + (unsigned)pDefinition->kind.value : 0;
}

/* Resolve enums to their scalar storage kind; other kinds are unchanged. */
static unsigned bsqlStorageKind(BsqlView view){
  const BsqlDefinition *pDefinition;
  unsigned kind;
  kind = bsqlViewKind(view);
  if( kind == BSQL_KIND_ENUM ){
    pDefinition = bsqlDefinitionFind(view.pSchema,
                                      view.pType->defined_fqn.value);
    return (unsigned)pDefinition->enum_def.value->base_type.value;
  }
  return kind;
}

/*
** Test byte-array shape directly, rather than comparing presentation strings.
*/
static int bsqlIsBytes(const BsqlType *pType){
  unsigned kind;
  kind = bsqlTypeKind(pType);
  return (kind == BEBOP_TYPE_KIND_ARRAY || kind == BEBOP_TYPE_KIND_FIXED_ARRAY)
      && bsqlTypeKind(bsqlElement(pType)) == BEBOP_TYPE_KIND_BYTE;
}

/*
** Return whether a view has traversable children, including byte-array items.
*/
static int bsqlIterable(BsqlView view){
  unsigned kind;
  kind = bsqlViewKind(view);
  return kind == BEBOP_TYPE_KIND_ARRAY || kind == BEBOP_TYPE_KIND_FIXED_ARRAY
      || kind == BEBOP_TYPE_KIND_MAP || kind == BSQL_KIND_STRUCT
      || kind == BSQL_KIND_MESSAGE || kind == BSQL_KIND_UNION;
}

/*
** Sign-extend a fixed-width integer without implementation-defined narrowing.
*/
static int64_t bsqlSignedLe(const uint8_t *a, unsigned nByte){
  uint64_t bits;
  int64_t value;
  assert(nByte > 0 && nByte <= sizeof(value));
  bits = bsqlLoadLe(a, nByte);
  if( nByte < sizeof(bits) &&
      (bits & (UINT64_C(1) << (nByte * CHAR_BIT - 1))) ){
    bits |= UINT64_MAX << (nByte * CHAR_BIT);
  }
  memcpy(&value, &bits, sizeof(value));
  return value;
}

/*
** Consume a bounded wire slice. Zero-length slices need no pointer arithmetic.
*/
static int bsqlTake(
  Bebop_View *pInput, size_t n, Bebop_View *pOut, BsqlWork *pWork
){
  if( n > pInput->length || (n && !pInput->data) ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "truncated Bebop value");
  }
  *pOut = bebop_view(pInput->data, n);
  if( n ){
    pInput->data += n;
    pInput->length -= n;
  }
  return 1;
}

/* An iterator caches its kind and record indexes instead of resolving them for
** every child. Its parent and all returned entries borrow their backing data.
*/
typedef struct BsqlIterator BsqlIterator;
struct BsqlIterator {
  BsqlView parent;                /* Container being traversed. */
  Bebop_View remaining;           /* Unconsumed sequential fields. */
  Bebop_MessageIndex message;     /* Parsed indexed-message directory. */
  Bebop_MessageFieldIterator tags;/* Iterator over wire message tags. */
  const BsqlDefinition *pDefinition; /* Resolved definition, if applicable. */
  const BsqlDefinitionInfo *pInfo;/* Cached name/tag indexes. */
  Bebop_FieldDescriptor_Array fields; /* Struct/message fields. */
  size_t iPosition;               /* Next ordinal position. */
  size_t nItem;                   /* Declared child count. */
  unsigned eKind;                 /* Resolved view kind. */
};

/* Unknown wire fields have pType == NULL but retain their tag and raw bytes. */
typedef struct BsqlEntry BsqlEntry;
struct BsqlEntry {
  BsqlView value;                 /* Child value. */
  BsqlView key;                   /* Map key, absent for other containers. */
  Bebop_String name;              /* Field or active union-branch name. */
  size_t iIndex;                  /* Container ordinal. */
  uint8_t tag;                    /* Message tag or union discriminator. */
};

static int bsqlMeasure(BsqlView, int, unsigned, size_t *, BsqlWork *);

/* Initialize a bounded iterator without allocating memory. Malformed lengths,
** directories, discriminators, or item counts set the first work error.
*/
static int bsqlIteratorInit(
  BsqlView view, BsqlIterator *p, BsqlWork *pWork
){
  Bebop_View count;
  size_t iDefinition;
  uint64_t nBody;
  memset(p, 0, sizeof(*p));
  p->parent = view;
  p->remaining = view.bytes;
  p->eKind = bsqlViewKind(view);
  if( bsqlTypeKind(view.pType) == BEBOP_TYPE_KIND_DEFINED ){
    iDefinition = bsqlDefinitionIndex(view.pSchema,
                                      view.pType->defined_fqn.value);
    if( iDefinition == SIZE_MAX ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "unresolved data type");
    }
    p->pDefinition = view.pSchema->apDefinition[iDefinition];
    p->pInfo = view.pSchema->aInfo + iDefinition;
    p->fields = bsqlFields(p->pDefinition);
  }
  switch( p->eKind ){
    case BEBOP_TYPE_KIND_ARRAY:
    case BEBOP_TYPE_KIND_MAP:
      if( !bsqlTake(&p->remaining, BEBOP_WIRE_SIZE_LEN, &count, pWork) ){
        return 0;
      }
      p->nItem = (size_t)bsqlLoadLe(count.data, BEBOP_WIRE_SIZE_LEN);
      break;
    case BEBOP_TYPE_KIND_FIXED_ARRAY:
      p->nItem = view.pType->fixed_array_size.value;
      break;
    case BSQL_KIND_STRUCT:
      p->nItem = p->fields.length;
      break;
    case BSQL_KIND_MESSAGE:
    case BSQL_KIND_UNION:
      if( view.bytes.length < BEBOP_WIRE_SIZE_LEN ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "truncated record length");
      }
      nBody = bsqlLoadLe(view.bytes.data, BEBOP_WIRE_SIZE_LEN);
      if( nBody != view.bytes.length - BEBOP_WIRE_SIZE_LEN ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "invalid record length");
      }
      if( p->eKind == BSQL_KIND_MESSAGE ){
        if( bebop_message_index_init(&p->message, view.bytes)
            != BEBOP_RESULT_OK ){
          return bsqlFail(&pWork->error, BSQL_INVALID,
                           "malformed message directory");
        }
        p->nItem = p->message.field_count;
        bebop_message_field_iterator_init(&p->tags, &p->message);
      }else{
        if( view.bytes.length < BSQL_UNION_HEADER_BYTES ||
            view.bytes.data[BEBOP_WIRE_SIZE_LEN] == 0 ){
          return bsqlFail(&pWork->error, BSQL_INVALID, "malformed union");
        }
        p->nItem = 1;
      }
      break;
    default:
      break;
  }
  if( p->nItem > BSQL_MAX_ITEMS ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "container item limit exceeded");
  }
  return 1;
}

/* Return the next borrowed child. Zero means end-of-container or failure;
** inspect pWork->error to distinguish them. Measurement is depth bounded.
*/
static int bsqlIteratorNext(
  BsqlIterator *p, BsqlEntry *pEntry, BsqlWork *pWork
){
  const BsqlType *pType;
  const BsqlField *pField;
  const BsqlBranch *pBranch;
  Bebop_View bytes;
  BsqlView child;
  BsqlView key;
  size_t n;
  unsigned iTag;
  bool bPresent;
  memset(pEntry, 0, sizeof(*pEntry));
  if( p->iPosition >= p->nItem ){
    return 0;
  }
  if( !bsqlVisit(pWork, 0) ){
    return 0;
  }
  pType = 0;
  bytes = bebop_view(0, 0);
  pEntry->iIndex = p->iPosition;
  if( p->eKind == BSQL_KIND_MESSAGE ){
    bPresent = 0;
    if( bebop_message_field_iterator_next(&p->tags, &pEntry->tag,
          &bytes, &bPresent) != BEBOP_RESULT_OK || !bPresent ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid message field");
    }
    iTag = p->pInfo->aTag[pEntry->tag];
    if( iTag ){
      assert(iTag <= p->fields.length);
      pField = p->fields.data + iTag - 1;
      pType = pField->type.value;
      pEntry->name = pField->name.value;
    }
  }else if( p->eKind == BSQL_KIND_UNION ){
    pEntry->tag = p->parent.bytes.data[BEBOP_WIRE_SIZE_LEN];
    bytes = bebop_view(p->parent.bytes.data + BSQL_UNION_HEADER_BYTES,
                       p->parent.bytes.length - BSQL_UNION_HEADER_BYTES);
    iTag = p->pInfo->aTag[pEntry->tag];
    if( iTag ){
      pBranch = p->pDefinition->union_def.value->branches.value.data +
                iTag - 1;
      pType = bsqlDefinitionType(p->parent.pSchema, bsqlBranchName(pBranch));
      pEntry->name = pBranch->name.value;
    }
  }else{
    if( p->eKind == BSQL_KIND_STRUCT ){
      pField = p->fields.data + p->iPosition;
      pType = pField->type.value;
      pEntry->name = pField->name.value;
    }else if( p->eKind == BEBOP_TYPE_KIND_MAP ){
      key = bsqlView(p->parent.pSchema, p->parent.pType->map_key.value,
                     p->remaining, 1);
      if( !bsqlMeasure(key, 0, 0, &n, pWork) ||
          !bsqlTake(&p->remaining, n, &key.bytes, pWork) ){
        return 0;
      }
      pEntry->key = key;
      pType = p->parent.pType->map_value.value;
    }else{
      pType = bsqlElement(p->parent.pType);
    }
    child = bsqlView(p->parent.pSchema, pType, p->remaining, 1);
    if( !bsqlMeasure(child, 0, 0, &n, pWork) ||
        !bsqlTake(&p->remaining, n, &bytes, pWork) ){
      return 0;
    }
  }
  pEntry->value = bsqlView(p->parent.pSchema, pType, bytes, 1);
  ++p->iPosition;
  return 1;
}

/* Compare validated map-key encodings. Fixed-width integers and terminated
** strings have a unique encoding for each representable key value.
*/
static int bsqlKeyViewCompare(const void *a, const void *b){
  const Bebop_View *pa;
  const Bebop_View *pb;
  pa = a;
  pb = b;
  return bsqlStringCompare(
    bebop_string_view((const char *)pa->data, pa->length),
    bebop_string_view((const char *)pb->data, pb->length));
}

/* Sort caller-owned key views and reject duplicates without copying payloads.
** No allocation occurs here. Cancellation is checked before and after sorting.
*/
static int bsqlUniqueKeyViews(Bebop_View *a, size_t n, BsqlWork *pWork){
  size_t i;
  if( n < 2 ){
    return pWork->error.eCode == BSQL_OK;
  }
  if( !bsqlPoll(pWork) ){
    return 0;
  }
  qsort(a, n, sizeof(*a), bsqlKeyViewCompare);
  if( !bsqlPoll(pWork) ){
    return 0;
  }
  for(i=1; i<n; ++i){
    if( bsqlKeyViewCompare(a + i - 1, a + i) == 0 ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "duplicate map key");
    }
  }
  return 1;
}

/*
** Validate the nanosecond component without rejecting valid signed durations.
*/
static int bsqlTemporalValid(
  unsigned kind, const uint8_t *a, BsqlWork *pWork
){
  int64_t nanos;
  if( kind == BEBOP_TYPE_KIND_TIMESTAMP ){
    if( bsqlLoadLe(a + BSQL_TIME_NANOS_OFFSET, sizeof(uint32_t)) >=
        BSQL_NANOS_PER_SECOND ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid nanoseconds");
    }
  }else{
    nanos = bsqlSignedLe(a + BSQL_TIME_NANOS_OFFSET, sizeof(int32_t));
    if( nanos <= -BSQL_NANOS_PER_SECOND || nanos >= BSQL_NANOS_PER_SECOND ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid nanoseconds");
    }
  }
  return 1;
}

/* Validate packed booleans a machine word at a time without alignment casts. */
static int bsqlBooleansValid(
  const uint8_t *a, size_t n, BsqlWork *pWork
){
  static const uint64_t invalidBits = UINT64_C(0xfefefefefefefefe);
  uint64_t bits;
  size_t i;
  i = 0;
  while( n - i >= sizeof(bits) ){
    if( i % BSQL_BYTE_POLL_INTERVAL == 0 && !bsqlPoll(pWork) ){
      return 0;
    }
    memcpy(&bits, a + i, sizeof(bits));
    if( bits & invalidBits ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid boolean encoding");
    }
    i += sizeof(bits);
  }
  for(; i<n; ++i){
    if( a[i] > 1 ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid boolean encoding");
    }
  }
  return 1;
}

/* Measure one wire value in a larger slice, optionally validating all nested
** contents. Scalar-array measurement is constant time; boolean validation is
** word-wise. Map keys are captured during this traversal, not by traversing
** every nested value a second time. Key views use bounded SQLite allocation.
*/
static int bsqlMeasure(
  BsqlView view, int bValidate, unsigned nDepth, size_t *pnSize,
  BsqlWork *pWork
){
  BsqlIterator it;
  BsqlEntry entry;
  BsqlView child;
  BsqlView key;
  Bebop_View consumed;
  Bebop_View *aKeys;
  const BsqlType *pElement;
  const BsqlType *pType;
  unsigned kind;
  unsigned elementKind;
  size_t n;
  size_t nString;
  size_t nWidth;
  size_t i;
  uint64_t nBody;
  int ok;
  if( !bsqlVisit(pWork, nDepth) ){
    return 0;
  }
  kind = bsqlStorageKind(view);
  if( kind > 0 && kind < BSQL_COUNT(bsqlPrimitives) ){
    if( kind == BEBOP_TYPE_KIND_STRING ){
      if( view.bytes.length < BEBOP_WIRE_SIZE_LEN ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "truncated string length");
      }
      nString = (size_t)bsqlLoadLe(view.bytes.data, BEBOP_WIRE_SIZE_LEN);
      n = view.bytes.length - BEBOP_WIRE_SIZE_LEN;
      if( nString >= n ||
          view.bytes.data[BEBOP_WIRE_SIZE_LEN + nString] != 0 ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "malformed string");
      }
      if( !bsqlUtf8Valid(view.bytes.data + BEBOP_WIRE_SIZE_LEN, nString) ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "invalid UTF-8 string");
      }
      *pnSize = BEBOP_WIRE_SIZE_LEN + nString + 1;
      return 1;
    }
    *pnSize = bsqlPrimitives[kind].nWire;
    if( *pnSize > view.bytes.length ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "truncated scalar");
    }
    if( kind == BEBOP_TYPE_KIND_BOOL && view.bytes.data[0] > 1 ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid boolean encoding");
    }
    if( bValidate && (kind == BEBOP_TYPE_KIND_TIMESTAMP ||
                      kind == BEBOP_TYPE_KIND_DURATION) ){
      return bsqlTemporalValid(kind, view.bytes.data, pWork);
    }
    return 1;
  }
  if( kind == BSQL_KIND_MESSAGE || kind == BSQL_KIND_UNION ){
    if( view.bytes.length < BEBOP_WIRE_SIZE_LEN ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "truncated record length");
    }
    nBody = bsqlLoadLe(view.bytes.data, BEBOP_WIRE_SIZE_LEN);
    if( nBody > view.bytes.length - BEBOP_WIRE_SIZE_LEN ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid record length");
    }
    *pnSize = BEBOP_WIRE_SIZE_LEN + (size_t)nBody;
    view.bytes.length = *pnSize;
    if( !bsqlIteratorInit(view, &it, pWork) ){
      return 0;
    }
    if( !bValidate ){
      return 1;
    }
    while( bsqlIteratorNext(&it, &entry, pWork) ){
      if( entry.value.pType ){
        if( !bsqlMeasure(entry.value, 1, nDepth + 1, &n, pWork) ){
          return 0;
        }
        if( n != entry.value.bytes.length ){
          return bsqlFail(&pWork->error, BSQL_INVALID, "trailing field bytes");
        }
      }
    }
    return pWork->error.eCode == BSQL_OK;
  }
  if( kind != BEBOP_TYPE_KIND_ARRAY && kind != BEBOP_TYPE_KIND_FIXED_ARRAY
   && kind != BEBOP_TYPE_KIND_MAP && kind != BSQL_KIND_STRUCT ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "unsupported data type");
  }
  if( !bsqlIteratorInit(view, &it, pWork) ){
    return 0;
  }
  if( kind == BEBOP_TYPE_KIND_ARRAY || kind == BEBOP_TYPE_KIND_FIXED_ARRAY ){
    pElement = bsqlElement(view.pType);
    child = bsqlView(view.pSchema, pElement, it.remaining, 1);
    elementKind = bsqlStorageKind(child);
    if( elementKind > 0 && elementKind < BSQL_COUNT(bsqlPrimitives)
     && bsqlPrimitives[elementKind].nWire ){
      nWidth = bsqlPrimitives[elementKind].nWire;
      if( it.nItem > it.remaining.length / nWidth ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "truncated array");
      }
      if( bValidate && elementKind == BEBOP_TYPE_KIND_BOOL &&
          !bsqlBooleansValid(it.remaining.data, it.nItem, pWork) ){
        return 0;
      }
      if( bValidate && (elementKind == BEBOP_TYPE_KIND_TIMESTAMP ||
                        elementKind == BEBOP_TYPE_KIND_DURATION) ){
        for(i=0; i<it.nItem; ++i){
          if( i % BSQL_INTERRUPT_INTERVAL == 0 && !bsqlPoll(pWork) ){
            return 0;
          }
          if( !bsqlTemporalValid(elementKind, it.remaining.data + i*nWidth,
                                 pWork) ){
            return 0;
          }
        }
      }
      *pnSize = (kind == BEBOP_TYPE_KIND_ARRAY ? BEBOP_WIRE_SIZE_LEN : 0)
              + it.nItem * nWidth;
      return 1;
    }
  }
  aKeys = 0;
  if( bValidate && kind == BEBOP_TYPE_KIND_MAP && it.nItem > 1 ){
    aKeys = sqlite3_malloc64((sqlite3_uint64)it.nItem * sizeof(*aKeys));
    if( !aKeys ){
      return bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
    }
  }
  ok = 0;
  for(i=0; i<it.nItem; ++i){
    if( kind == BSQL_KIND_STRUCT ){
      pType = it.fields.data[i].type.value;
    }else{
      pType = kind == BEBOP_TYPE_KIND_MAP ? view.pType->map_value.value :
                                          bsqlElement(view.pType);
    }
    if( kind == BEBOP_TYPE_KIND_MAP ){
      key = bsqlView(view.pSchema, view.pType->map_key.value, it.remaining, 1);
      if( !bsqlMeasure(key, bValidate, nDepth + 1, &n, pWork) ||
          !bsqlTake(&it.remaining, n, &consumed, pWork) ){
        goto cleanup;
      }
      if( aKeys ){
        aKeys[i] = consumed;
      }
    }
    child = bsqlView(view.pSchema, pType, it.remaining, 1);
    if( !bsqlMeasure(child, bValidate, nDepth + 1, &n, pWork) ||
        !bsqlTake(&it.remaining, n, &consumed, pWork) ){
      goto cleanup;
    }
  }
  *pnSize = view.bytes.length - it.remaining.length;
  ok = !aKeys || bsqlUniqueKeyViews(aKeys, it.nItem, pWork);
cleanup:
  sqlite3_free(aKeys);
  return ok;
}

/*
** Validate exactly one complete view, rejecting a valid prefix with a suffix.
*/
static int bsqlValidateView(BsqlView view, BsqlWork *pWork){
  size_t n;
  if( !bsqlMeasure(view, 1, 0, &n, pWork) ){
    return 0;
  }
  return n == view.bytes.length ||
    bsqlFail(&pWork->error, BSQL_INVALID, "trailing payload bytes");
}

/* Read an SQLite BLOB without coercion. Empty BLOBs may have a NULL pointer. */
static int bsqlBlobArgument(
  sqlite3_value *pValue, Bebop_View *pBytes, BsqlError *pError
){
  if( sqlite3_value_type(pValue) != SQLITE_BLOB ){
    return bsqlFail(pError, BSQL_INVALID, "expected a BLOB");
  }
  pBytes->data = sqlite3_value_blob(pValue);
  pBytes->length = (size_t)sqlite3_value_bytes(pValue);
  if( pBytes->length && !pBytes->data ){
    return bsqlFail(pError, BSQL_NOMEM, "out of memory");
  }
  return 1;
}

/* Open a typed value, returning one retained schema and a borrowed root view.
** Envelope/schema failures set pWork->error; caller always releases *ppSchema.
*/
static int bsqlOpenValue(
  BsqlConnection *pConnection, sqlite3_value *pValue, BsqlSchema **ppSchema,
  BsqlView *pRoot, BsqlWork *pWork
){
  Bebop_View bytes;
  Bebop_View spec;
  Bebop_View payload;
  if( !bsqlBlobArgument(pValue, &bytes, &pWork->error) ||
      !bsqlSplitValue(bytes, &spec, &payload, &pWork->error) ){
    return 0;
  }
  *ppSchema = bsqlCachedSchema(pConnection, spec, &pWork->error);
  if( !*ppSchema ){
    return 0;
  }
  *pRoot = bsqlView(*ppSchema, &(*ppSchema)->root, payload, 1);
  return 1;
}

/* Convert a retained diagnostic to an SQLite result. Formatting uses SQLite
** allocation; formatting OOM reports SQLITE_NOMEM instead of losing the error.
*/
static void bsqlResultError(sqlite3_context *pContext, const BsqlError *pError){
  char *z;
  const char *zMessage;
  if( pError->eCode == BSQL_NOMEM ){
    sqlite3_result_error_nomem(pContext);
    return;
  }
  zMessage = pError->zMessage ? pError->zMessage : "invalid Bebop value";
  if( pError->bOffset ){
    z = sqlite3_mprintf("%s at byte %llu", zMessage,
                        (sqlite3_uint64)pError->iOffset);
    if( !z ){
      sqlite3_result_error_nomem(pContext);
      return;
    }
    sqlite3_result_error(pContext, z, -1);
    sqlite3_free(z);
  }else{
    sqlite3_result_error(pContext, zMessage, -1);
  }
  if( pError->sqliteCode ){
    sqlite3_result_error_code(pContext, pError->sqliteCode);
  }
}

/* Publish a borrowed result by copying it through SQLite. The source remains
** usable, which is essential for persistent virtual-table path buffers.
*/
static void bsqlResultBuffer(
  sqlite3_context *pContext, const BsqlBuffer *p, int bText
){
  if( bText ){
    sqlite3_result_text64(pContext, p->aData ? (const char *)p->aData : "",
      (sqlite3_uint64)p->nData, SQLITE_TRANSIENT, SQLITE_UTF8);
  }else{
    sqlite3_result_blob64(pContext, p->aData ? p->aData : (const void *)"",
      (sqlite3_uint64)p->nData, SQLITE_TRANSIENT);
  }
}

/* Transfer a temporary owned result to SQLite without a second payload copy.
** SQLite owns the allocation even if publishing the result encounters OOM.
** Borrowed buffers are copied; the supplied buffer is empty after this call.
*/
static void bsqlResultTake(
  sqlite3_context *pContext, BsqlBuffer *p, int bText
){
  if( !p->aOwned ){
    bsqlResultBuffer(pContext, p, bText);
  }else if( bText ){
    sqlite3_result_text64(pContext, (const char *)p->aOwned,
      (sqlite3_uint64)p->nData, sqlite3_free, SQLITE_UTF8);
  }else{
    sqlite3_result_blob64(pContext, p->aOwned,
      (sqlite3_uint64)p->nData, sqlite3_free);
  }
  memset(p, 0, sizeof(*p));
}

/* Render a validated type expression into a SQLite-backed buffer. Recursion,
** output growth, and arena-independent allocation failures are bounded.
*/
static int bsqlTypeText(
  const BsqlType *pType, BsqlBuffer *pOut, unsigned nDepth, BsqlError *pError
){
  unsigned kind;
  char zSize[BSQL_NUMBER_TEXT_BYTES];
  if( nDepth >= BSQL_MAX_DEPTH ){
    return bsqlFail(pError, BSQL_LIMIT, "type expression depth exceeded");
  }
  kind = bsqlTypeKind(pType);
  if( kind > 0 && kind < BSQL_COUNT(bsqlPrimitives) ){
    return bsqlAppend(pOut, bsqlPrimitives[kind].zName,
                       bsqlPrimitives[kind].nName, pError);
  }
  if( kind == BEBOP_TYPE_KIND_DEFINED ){
    return bsqlAppend(pOut, pType->defined_fqn.value.data,
                       pType->defined_fqn.value.length, pError);
  }
  if( kind == BEBOP_TYPE_KIND_MAP ){
    return BSQL_APPEND_LITERAL(pOut, "map[", pError)
        && bsqlTypeText(pType->map_key.value, pOut, nDepth + 1, pError)
        && BSQL_APPEND_LITERAL(pOut, ",", pError)
        && bsqlTypeText(pType->map_value.value, pOut, nDepth + 1, pError)
        && BSQL_APPEND_LITERAL(pOut, "]", pError);
  }
  if( kind != BEBOP_TYPE_KIND_ARRAY && kind != BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return bsqlFail(pError, BSQL_INVALID, "invalid type kind");
  }
  if( !bsqlTypeText(bsqlElement(pType), pOut, nDepth + 1, pError) ){
    return 0;
  }
  if( kind == BEBOP_TYPE_KIND_ARRAY ){
    return BSQL_APPEND_LITERAL(pOut, "[]", pError);
  }
  sqlite3_snprintf(sizeof(zSize), zSize, "[%u]",
                    (unsigned)pType->fixed_array_size.value);
  return bsqlAppend(pOut, zSize, strlen(zSize), pError);
}

/* Return a static presentation name. Type tests elsewhere use enums instead. */
static const char *bsqlKindText(BsqlView view){
  unsigned kind;
  kind = bsqlViewKind(view);
  if( bsqlIsBytes(view.pType) ){
    return "bytes";
  }
  if( kind == BEBOP_TYPE_KIND_BOOL ){
    return "boolean";
  }
  if( kind >= BEBOP_TYPE_KIND_BYTE && kind <= BEBOP_TYPE_KIND_UINT_128 ){
    return "integer";
  }
  if( kind >= BEBOP_TYPE_KIND_FLOAT_16 && kind <= BEBOP_TYPE_KIND_BFLOAT_16 ){
    return "real";
  }
  switch( kind ){
    case BEBOP_TYPE_KIND_ARRAY:
    case BEBOP_TYPE_KIND_FIXED_ARRAY: return "array";
    case BEBOP_TYPE_KIND_MAP: return "map";
    case BSQL_KIND_ENUM: return "enum";
    case BSQL_KIND_STRUCT: return "struct";
    case BSQL_KIND_MESSAGE: return "message";
    case BSQL_KIND_UNION: return "union";
    default:
      return kind > 0 && kind < BSQL_COUNT(bsqlPrimitives) ?
             bsqlPrimitives[kind].zName : "unknown";
  }
}

/* Reuse a schema's root specification; generate only genuine subtype specs.
** Generated bytes belong to pTemporary and must outlive the returned view.
*/
static int bsqlSpecForType(
  const BsqlSchema *p, const BsqlType *pType, BsqlBuffer *pTemporary,
  Bebop_View *pSpec, BsqlError *pError
){
  if( pType == &p->root && p->aKey ){
    *pSpec = bebop_view(p->aKey, p->nKey);
    return 1;
  }
  if( !bsqlMakeSpec(p, pType, pTemporary, pError) ){
    return 0;
  }
  *pSpec = bebop_view(pTemporary->aData, pTemporary->nData);
  return 1;
}

/* Wrap a borrowed view as a typed result. Temporary spec storage is always
** released; pOut owns the SQLite allocation on success or partial failure.
*/
static int bsqlTypedResult(BsqlView view, BsqlBuffer *pOut, BsqlWork *pWork){
  BsqlBuffer temporary;
  Bebop_View spec;
  int ok;
  memset(&temporary, 0, sizeof(temporary));
  ok = bsqlSpecForType(view.pSchema, view.pType, &temporary, &spec,
                       &pWork->error)
    && bsqlWrapValue(spec, view.bytes, pOut, &pWork->error);
  bsqlBufferClear(&temporary);
  return ok;
}

/*
** Write an unsigned decimal chunk with optional leading zeroes. No allocation.
*/
static size_t bsqlDecimalChunk(char *z, uint32_t value, unsigned nMinimum){
  char reversed[BSQL_DECIMAL_CHUNK_DIGITS + 1];
  size_t n;
  size_t i;
  n = 0;
  do{
    reversed[n++] = (char)('0' + value % BSQL_DECIMAL_RADIX);
    value /= BSQL_DECIMAL_RADIX;
  }while( value );
  while( n < nMinimum ){
    reversed[n++] = '0';
  }
  for(i=0; i<n; ++i){
    z[i] = reversed[n - i - 1];
  }
  return n;
}

/* Convert up to 128 wire bits to decimal using base-1e9 chunks and 32-bit
** limbs. This replaces a division pass over every byte for every digit.
** Caller supplies BSQL_DECIMAL_TEXT_BYTES bytes; no allocation occurs.
*/
static size_t bsqlDecimalText(
  const uint8_t *aData, unsigned nByte, int bSigned,
  char zOut[BSQL_DECIMAL_TEXT_BYTES]
){
  uint8_t bytes[BSQL_WIDE_BYTES];
  uint32_t words[BSQL_WIDE_WORDS];
  uint32_t chunks[BSQL_WIDE_CHUNKS];
  uint64_t dividend;
  uint32_t remainder;
  uint32_t nonzero;
  unsigned carry;
  unsigned i;
  unsigned nChunk;
  size_t nOut;
  int bNegative;
  assert(nByte > 0 && nByte <= sizeof(bytes));
  memset(bytes, 0, sizeof(bytes));
  memset(words, 0, sizeof(words));
  memcpy(bytes, aData, nByte);
  bNegative = bSigned && (bytes[nByte-1] & 0x80);
  if( bNegative ){
    carry = 1;
    for(i=0; i<nByte; ++i){
      carry += (uint8_t)~bytes[i];
      bytes[i] = (uint8_t)carry;
      carry >>= CHAR_BIT;
    }
  }
  for(i=0; i<BSQL_WIDE_WORDS; ++i){
    words[i] = (uint32_t)bsqlLoadLe(bytes + i*sizeof(uint32_t),
                                    sizeof(uint32_t));
  }
  nChunk = 0;
  do{
    remainder = 0;
    nonzero = 0;
    for(i=BSQL_WIDE_WORDS; i>0; ){
      --i;
      dividend = ((uint64_t)remainder << (sizeof(uint32_t) * CHAR_BIT))
                 | words[i];
      words[i] = (uint32_t)(dividend / BSQL_DECIMAL_CHUNK_BASE);
      remainder = (uint32_t)(dividend % BSQL_DECIMAL_CHUNK_BASE);
      nonzero |= words[i];
    }
    assert(nChunk < BSQL_WIDE_CHUNKS);
    chunks[nChunk++] = remainder;
  }while( nonzero );
  nOut = 0;
  if( bNegative ){
    zOut[nOut++] = '-';
  }
  nOut += bsqlDecimalChunk(zOut + nOut, chunks[--nChunk], 0);
  while( nChunk ){
    nOut += bsqlDecimalChunk(zOut + nOut, chunks[--nChunk],
                             BSQL_DECIMAL_CHUNK_DIGITS);
  }
  zOut[nOut] = 0;
  assert(nOut < BSQL_DECIMAL_TEXT_BYTES);
  return nOut;
}

/* Preserve NaN, infinities, and the sign of zero without C99 math macros. */
static int bsqlIsNan(double value){
  return value != value;
}

/* Test finiteness without performing a conversion to another numeric type. */
static int bsqlIsFinite(double value){
  return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

/* Read the IEEE-754 sign bit without aliasing violations. No allocation. */
static int bsqlSignBit(double value){
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return (int)(bits >> (sizeof(bits) * CHAR_BIT - 1));
}

/* Construct a canonical IEEE-754 infinity or quiet NaN. No FP exceptions. */
static double bsqlSpecialFloat(int bNan, int bNegative){
  uint64_t bits;
  double value;
  bits = bNan ? UINT64_C(0x7ff8000000000000) :
                UINT64_C(0x7ff0000000000000);
  if( bNegative ){
    bits |= UINT64_C(0x8000000000000000);
  }
  memcpy(&value, &bits, sizeof(value));
  return value;
}

/*
** Decode one IEEE-754 Bebop floating-point scalar using endian-safe bit loads.
*/
static double bsqlFloatValue(unsigned kind, const uint8_t *a){
  uint64_t bits64;
  uint32_t bits32;
  unsigned bits16;
  unsigned exponent;
  unsigned fraction;
  float value32;
  double value;
  if( kind == BEBOP_TYPE_KIND_FLOAT_64 ){
    bits64 = bsqlLoadLe(a, sizeof(bits64));
    memcpy(&value, &bits64, sizeof(value));
    return value;
  }
  if( kind == BEBOP_TYPE_KIND_FLOAT_32 || kind == BEBOP_TYPE_KIND_BFLOAT_16 ){
    bits32 = (uint32_t)bsqlLoadLe(a, kind == BEBOP_TYPE_KIND_FLOAT_32 ?
                                sizeof(uint32_t) : sizeof(uint16_t));
    if( kind == BEBOP_TYPE_KIND_BFLOAT_16 ){
      bits32 <<= BSQL_BFLOAT_SHIFT;
    }
    memcpy(&value32, &bits32, sizeof(value32));
    return value32;
  }
  assert(kind == BEBOP_TYPE_KIND_FLOAT_16);
  bits16 = (unsigned)bsqlLoadLe(a, sizeof(uint16_t));
  exponent = (bits16 >> BSQL_HALF_FRACTION_BITS) & BSQL_HALF_EXPONENT_MASK;
  fraction = bits16 & BSQL_HALF_FRACTION_MASK;
  if( exponent == BSQL_HALF_EXPONENT_MASK ){
    return bsqlSpecialFloat(fraction != 0, (bits16 & BSQL_HALF_SIGN) != 0);
  }
  if( exponent ){
    value = ldexp(1.0 + (double)fraction / BSQL_HALF_HIDDEN_BIT,
                   (int)exponent - BSQL_HALF_BIAS);
  }else{
    value = ldexp((double)fraction,
                   1 - BSQL_HALF_BIAS - BSQL_HALF_FRACTION_BITS);
  }
  return bits16 & BSQL_HALF_SIGN ? -value : value;
}

/* Round a double to binary16 using round-to-nearest, ties-to-even. Callers
** enforcing exact conversion compare the decoded result with the input.
*/
static uint16_t bsqlHalfBits(double value){
  uint16_t sign;
  int exponent;
  unsigned mantissa;
  double scaled;
  double base;
  double fraction;
  sign = bsqlSignBit(value) ? BSQL_HALF_SIGN : 0;
  value = fabs(value);
  if( bsqlIsNan(value) ){
    return (uint16_t)(sign | BSQL_HALF_QUIET_NAN);
  }
  if( !bsqlIsFinite(value) ){
    return (uint16_t)(sign | BSQL_HALF_INFINITY);
  }
  if( value == 0 ){
    return sign;
  }
  frexp(value, &exponent);
  if( exponent > BSQL_HALF_BIAS + 1 ){
    return (uint16_t)(sign | BSQL_HALF_INFINITY);
  }
  scaled = ldexp(value, exponent <= 1 - BSQL_HALF_BIAS ?
    BSQL_HALF_BIAS + BSQL_HALF_FRACTION_BITS - 1 :
    BSQL_HALF_FRACTION_BITS + 1 - exponent);
  base = floor(scaled);
  fraction = scaled - base;
  mantissa = (unsigned)base;
  if( fraction > 0.5 || (fraction == 0.5 && (mantissa & 1)) ){
    ++mantissa;
  }
  if( exponent <= 1 - BSQL_HALF_BIAS ){
    return (uint16_t)(sign | mantissa);
  }
  if( mantissa == 2 * BSQL_HALF_HIDDEN_BIT ){
    ++exponent;
    mantissa = BSQL_HALF_HIDDEN_BIT;
  }
  return (uint16_t)(sign |
    ((unsigned)(exponent + BSQL_HALF_BIAS - 1) << BSQL_HALF_FRACTION_BITS) |
    (mantissa - BSQL_HALF_HIDDEN_BIT));
}

/* Render a UUID once for both scalar and JSON output. No allocation. */
static size_t bsqlUuidText(const uint8_t *a, char z[BSQL_UUID_TEXT_BYTES]){
  size_t group;
  size_t i;
  size_t offset;
  size_t n;
  offset = 0;
  n = 0;
  for(group=0; group<BSQL_COUNT(bsqlUuidGroups); ++group){
    if( group ){
      z[n++] = '-';
    }
    for(i=0; i<bsqlUuidGroups[group]; ++i){
      z[n++] = bsqlHexAlphabet[a[offset] >> BSQL_HEX_SHIFT];
      z[n++] = bsqlHexAlphabet[a[offset] & BSQL_HEX_MASK];
      ++offset;
    }
  }
  z[n] = 0;
  assert(n == BSQL_UUID_TEXT_LENGTH);
  return n;
}

/* Render seconds:nanos[:offset] consistently for SQL scalars and JSON. */
static size_t bsqlTimeText(
  unsigned kind, const uint8_t *a, char z[BSQL_TIME_TEXT_BYTES]
){
  if( kind == BEBOP_TYPE_KIND_TIMESTAMP ){
    sqlite3_snprintf(BSQL_TIME_TEXT_BYTES, z, "%lld:%u:%d",
      (sqlite3_int64)bsqlSignedLe(a + BSQL_TIME_SECONDS_OFFSET,
                                 sizeof(int64_t)),
      (unsigned)bsqlLoadLe(a + BSQL_TIME_NANOS_OFFSET, sizeof(uint32_t)),
      (int)bsqlSignedLe(a + BSQL_TIME_ZONE_OFFSET, sizeof(int32_t)));
  }else{
    sqlite3_snprintf(BSQL_TIME_TEXT_BYTES, z, "%lld:%d",
      (sqlite3_int64)bsqlSignedLe(a + BSQL_TIME_SECONDS_OFFSET,
                                 sizeof(int64_t)),
      (int)bsqlSignedLe(a + BSQL_TIME_NANOS_OFFSET, sizeof(int32_t)));
  }
  return strlen(z);
}

/* Publish a scalar or typed container. Wide integers remain decimal TEXT;
** byte arrays remain BLOBs. SQLite has no NaN REAL result, so NaN is an error.
** All temporary allocations use SQLite and are released or transferred.
*/
static int bsqlScalarResult(
  sqlite3_context *pContext, BsqlView view, BsqlWork *pWork
){
  const uint8_t *a;
  unsigned kind;
  size_t n;
  size_t prefix;
  double value;
  char zNumber[BSQL_DECIMAL_TEXT_BYTES];
  char zUuid[BSQL_UUID_TEXT_BYTES];
  char zTime[BSQL_TIME_TEXT_BYTES];
  BsqlBuffer out;
  int ok;
  if( !view.bPresent ){
    sqlite3_result_null(pContext);
    return 1;
  }
  if( !view.pType ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "unknown wire field has no type");
  }
  if( !bsqlMeasure(view, 0, 0, &n, pWork) ){
    return 0;
  }
  if( n != view.bytes.length ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid scalar length");
  }
  kind = bsqlStorageKind(view);
  a = view.bytes.data;
  if( kind == BEBOP_TYPE_KIND_STRING ){
    sqlite3_result_text64(pContext, (const char *)a + BEBOP_WIRE_SIZE_LEN,
      bsqlLoadLe(a, BEBOP_WIRE_SIZE_LEN), SQLITE_TRANSIENT, SQLITE_UTF8);
  }else if( kind == BEBOP_TYPE_KIND_BOOL || kind == BEBOP_TYPE_KIND_BYTE ||
            kind == BEBOP_TYPE_KIND_UINT_16
              || kind == BEBOP_TYPE_KIND_UINT_32 ){
    sqlite3_result_int64(pContext,
      (sqlite3_int64)bsqlLoadLe(a, bsqlPrimitives[kind].nWire));
  }else if( kind == BEBOP_TYPE_KIND_INT_8 || kind == BEBOP_TYPE_KIND_INT_16 ||
            kind == BEBOP_TYPE_KIND_INT_32 || kind == BEBOP_TYPE_KIND_INT_64 ){
    sqlite3_result_int64(pContext, bsqlSignedLe(a, bsqlPrimitives[kind].nWire));
  }else if( kind >= BEBOP_TYPE_KIND_UINT_64
    && kind <= BEBOP_TYPE_KIND_UINT_128 ){
    n = bsqlDecimalText(a, bsqlPrimitives[kind].nWire,
                        kind == BEBOP_TYPE_KIND_INT_128, zNumber);
    sqlite3_result_text(pContext, zNumber, (int)n, SQLITE_TRANSIENT);
  }else if( kind >= BEBOP_TYPE_KIND_FLOAT_16 &&
            kind <= BEBOP_TYPE_KIND_BFLOAT_16 ){
    value = bsqlFloatValue(kind, a);
    if( bsqlIsNan(value) ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
        "NaN has no SQLite REAL representation; use bebop_get");
    }
    sqlite3_result_double(pContext, value);
  }else if( kind == BEBOP_TYPE_KIND_UUID ){
    n = bsqlUuidText(a, zUuid);
    sqlite3_result_text(pContext, zUuid, (int)n, SQLITE_TRANSIENT);
  }else if( kind == BEBOP_TYPE_KIND_TIMESTAMP ||
            kind == BEBOP_TYPE_KIND_DURATION ){
    n = bsqlTimeText(kind, a, zTime);
    sqlite3_result_text(pContext, zTime, (int)n, SQLITE_TRANSIENT);
  }else if( bsqlIsBytes(view.pType) ){
    prefix = bsqlTypeKind(view.pType) == BEBOP_TYPE_KIND_ARRAY ?
             BEBOP_WIRE_SIZE_LEN : 0;
    sqlite3_result_blob64(pContext, a + prefix,
      (sqlite3_uint64)(view.bytes.length - prefix), SQLITE_TRANSIENT);
  }else{
    memset(&out, 0, sizeof(out));
    ok = bsqlTypedResult(view, &out, pWork);
    if( ok ){
      bsqlResultTake(pContext, &out, 0);
    }
    bsqlBufferClear(&out);
    return ok;
  }
  return 1;
}

/*
** An atom preserves SQLite's storage class; strings/blobs borrow their value.
*/
typedef struct BsqlAtom BsqlAtom;
struct BsqlAtom {
  const uint8_t *aData;           /* Borrowed text/blob bytes. */
  /* Bytes, not including any text terminator. */
  size_t nData;
  sqlite3_int64 integer;          /* Used only for SQLITE_INTEGER. */
  double real;                   /* Used only for SQLITE_FLOAT. */
  int eKind;                     /* One of SQLite's storage-class constants. */
};

/* Read an SQLite atom, recording conversion OOM before a NULL pointer can be
** mistaken for an empty string. A zero-byte BLOB may legitimately be NULL.
*/
static BsqlAtom bsqlSqlAtom(sqlite3_value *pValue, BsqlWork *pWork){
  BsqlAtom atom;
  memset(&atom, 0, sizeof(atom));
  if( !pValue ){
    bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
    return atom;
  }
  atom.eKind = sqlite3_value_type(pValue);
  switch( atom.eKind ){
    case SQLITE_INTEGER:
      atom.integer = sqlite3_value_int64(pValue);
      break;
    case SQLITE_FLOAT:
      atom.real = sqlite3_value_double(pValue);
      break;
    case SQLITE_TEXT:
    case SQLITE_BLOB:
      atom.aData = atom.eKind == SQLITE_TEXT ? sqlite3_value_text(pValue) :
                                              sqlite3_value_blob(pValue);
      atom.nData = (size_t)sqlite3_value_bytes(pValue);
      if( !atom.aData && (atom.eKind == SQLITE_TEXT || atom.nData) ){
        bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
      }
      break;
    default:
      break;
  }
  return atom;
}

/* Parse wide decimal TEXT using 32-bit limbs and checked carries. Leading
** zeroes, non-digits, unsigned negatives, and out-of-range values fail.
** Caller supplies 16 bytes. No dynamic allocation occurs.
*/
static int bsqlDecimalBytes(
  BsqlAtom atom, unsigned nByte, int bSigned,
  uint8_t aOut[BSQL_WIDE_BYTES], BsqlWork *pWork
){
  uint32_t words[BSQL_WIDE_WORDS];
  uint64_t carry;
  uint64_t product;
  size_t offset;
  unsigned nWord;
  unsigned i;
  int bNegative;
  int bMinimum;
  assert(nByte == sizeof(uint64_t) || nByte == BSQL_WIDE_BYTES);
  if( atom.eKind != SQLITE_TEXT || !atom.aData || atom.nData == 0 ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "wide integer requires decimal TEXT");
  }
  bNegative = atom.aData[0] == '-';
  offset = bNegative ? 1 : 0;
  if( (bNegative && !bSigned) || offset == atom.nData ||
      atom.nData - offset > BSQL_WIDE_DECIMAL_DIGITS ||
      (atom.nData - offset > 1 && atom.aData[offset] == '0') ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid decimal integer");
  }
  memset(words, 0, sizeof(words));
  nWord = nByte / sizeof(uint32_t);
  for(; offset<atom.nData; ++offset){
    if( !bsqlDigit(atom.aData[offset]) ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid decimal integer");
    }
    carry = atom.aData[offset] - '0';
    for(i=0; i<nWord; ++i){
      product = (uint64_t)words[i] * BSQL_DECIMAL_RADIX + carry;
      words[i] = (uint32_t)product;
      carry = product >> (sizeof(uint32_t) * CHAR_BIT);
    }
    if( carry ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "integer out of range");
    }
  }
  if( bSigned && (words[nWord-1] & UINT32_C(0x80000000)) ){
    bMinimum = bNegative && words[nWord-1] == UINT32_C(0x80000000);
    for(i=0; i+1<nWord; ++i){
      bMinimum = bMinimum && words[i] == 0;
    }
    if( !bMinimum ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "integer out of range");
    }
  }
  if( bNegative ){
    carry = 1;
    for(i=0; i<nWord; ++i){
      product = (uint64_t)(uint32_t)~words[i] + carry;
      words[i] = (uint32_t)product;
      carry = product >> (sizeof(uint32_t) * CHAR_BIT);
    }
  }
  memset(aOut, 0, BSQL_WIDE_BYTES);
  for(i=0; i<nWord; ++i){
    bsqlStoreLe(aOut + i*sizeof(uint32_t), words[i], sizeof(uint32_t));
  }
  return 1;
}

/* Parse a signed decimal component from a byte span with explicit overflow
** checks. No whitespace, plus signs, or embedded NULs are accepted.
*/
static int bsqlSignedComponent(
  const uint8_t *a, size_t n, size_t *pi, int64_t *pValue
){
  uint64_t magnitude;
  uint64_t limit;
  unsigned digit;
  size_t first;
  int bNegative;
  bNegative = *pi < n && a[*pi] == '-';
  if( bNegative ){
    ++*pi;
  }
  first = *pi;
  magnitude = 0;
  limit = (uint64_t)INT64_MAX + (bNegative ? 1 : 0);
  while( *pi < n && bsqlDigit(a[*pi]) ){
    digit = a[(*pi)++] - '0';
    if( magnitude > (limit - digit) / BSQL_DECIMAL_RADIX ){
      return 0;
    }
    magnitude = magnitude * BSQL_DECIMAL_RADIX + digit;
  }
  if( *pi == first ){
    return 0;
  }
  if( bNegative && magnitude == (uint64_t)INT64_MAX + 1 ){
    *pValue = INT64_MIN;
  }else{
    *pValue = bNegative ? -(int64_t)magnitude : (int64_t)magnitude;
  }
  return 1;
}

/* Parse the bounded time text format without locale-sensitive libc parsers.
** All range checks precede narrowing into the wire representation.
*/
static int bsqlTimeBytes(
  unsigned kind, BsqlAtom atom, uint8_t aOut[BSQL_WIDE_BYTES],
  BsqlWork *pWork
){
  int64_t seconds;
  int64_t nanos;
  int64_t zone;
  size_t i;
  if( atom.eKind != SQLITE_TEXT || atom.nData == 0 ||
      atom.nData >= BSQL_TIME_TEXT_BYTES ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "time value requires seconds:nanos[:offset] TEXT");
  }
  i = 0;
  if( !bsqlSignedComponent(atom.aData, atom.nData, &i, &seconds) ||
      i == atom.nData || atom.aData[i++] != ':' ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid time seconds");
  }
  if( !bsqlSignedComponent(atom.aData, atom.nData, &i, &nanos) ||
      nanos <= -BSQL_NANOS_PER_SECOND || nanos >= BSQL_NANOS_PER_SECOND ||
      (kind == BEBOP_TYPE_KIND_TIMESTAMP && nanos < 0) ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid nanoseconds");
  }
  bsqlStoreLe(aOut + BSQL_TIME_SECONDS_OFFSET, (uint64_t)seconds,
               sizeof(int64_t));
  bsqlStoreLe(aOut + BSQL_TIME_NANOS_OFFSET, (uint32_t)nanos,
               sizeof(int32_t));
  if( kind == BEBOP_TYPE_KIND_TIMESTAMP ){
    if( i == atom.nData || atom.aData[i++] != ':' ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "missing timestamp offset");
    }
    if( !bsqlSignedComponent(atom.aData, atom.nData, &i, &zone) ||
        zone < INT32_MIN || zone > INT32_MAX ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid timestamp offset");
    }
    bsqlStoreLe(aOut + BSQL_TIME_ZONE_OFFSET, (uint32_t)zone,
                 sizeof(int32_t));
  }
  return i == atom.nData ||
    bsqlFail(&pWork->error, BSQL_INVALID, "invalid time suffix");
}

/* Encode a numeric atom exactly into the requested floating-point format.
** Finite overflow is checked before narrowing to float, avoiding an undefined
** out-of-range conversion. NaNs remain NaNs when rounding to bfloat16.
*/
static int bsqlFloatBytes(
  unsigned kind, BsqlAtom atom, uint8_t aOut[BSQL_WIDE_BYTES],
  BsqlWork *pWork
){
  double value;
  double encoded;
  double int64Boundary;
  float narrowed;
  uint64_t bits64;
  uint32_t bits32;
  unsigned nByte;
  if( atom.eKind != SQLITE_FLOAT && atom.eKind != SQLITE_INTEGER ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "numeric value required");
  }
  value = atom.eKind == SQLITE_FLOAT ? atom.real : (double)atom.integer;
  nByte = bsqlPrimitives[kind].nWire;
  if( kind == BEBOP_TYPE_KIND_FLOAT_64 ){
    memcpy(&bits64, &value, sizeof(bits64));
    bsqlStoreLe(aOut, bits64, sizeof(bits64));
  }else if( kind == BEBOP_TYPE_KIND_FLOAT_16 ){
    bsqlStoreLe(aOut, bsqlHalfBits(value), sizeof(uint16_t));
  }else{
    if( bsqlIsFinite(value) && (value > FLT_MAX || value < -FLT_MAX) ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
                       "lossy floating-point conversion");
    }
    narrowed = (float)value;
    memcpy(&bits32, &narrowed, sizeof(bits32));
    if( kind == BEBOP_TYPE_KIND_BFLOAT_16 ){
      if( (bits32 & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
          (bits32 & UINT32_C(0x007fffff)) ){
        bits32 = (bits32 >> BSQL_BFLOAT_SHIFT) | UINT32_C(0x0040);
      }else{
        bits32 = (bits32 + UINT32_C(0x7fff) +
                    ((bits32 >> BSQL_BFLOAT_SHIFT) & 1)) >> BSQL_BFLOAT_SHIFT;
      }
    }
    bsqlStoreLe(aOut, bits32, nByte);
  }
  encoded = bsqlFloatValue(kind, aOut);
  if( bsqlIsFinite(value) && encoded != value ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "lossy floating-point conversion");
  }
  int64Boundary = ldexp(1.0, sizeof(int64_t) * CHAR_BIT - 1);
  if( atom.eKind == SQLITE_INTEGER &&
      (value < -int64Boundary || value >= int64Boundary ||
       (sqlite3_int64)value != atom.integer) ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "lossy integer to float conversion");
  }
  return 1;
}

/* Encode an atom or a matching typed BLOB. NULLs, coercions, lossy conversions,
** malformed text, and mismatched specifications fail. Output allocations use
** SQLite; temporary specifications are always freed. Inputs are never changed.
*/
static int bsqlEncodeAtom(
  const BsqlSchema *pSchema, const BsqlType *pType, BsqlAtom atom,
  BsqlBuffer *pOut, BsqlWork *pWork
){
  BsqlView view;
  BsqlBuffer temporary;
  Bebop_View spec;
  Bebop_View payload;
  Bebop_View expected;
  uint8_t bytes[BSQL_WIDE_BYTES];
  uint8_t *a;
  unsigned kind;
  unsigned nByte;
  unsigned high;
  unsigned low;
  size_t group;
  size_t i;
  size_t position;
  size_t offset;
  int bSigned;
  int64_t limit;
  int ok;
  if( pWork->error.eCode != BSQL_OK ){
    return 0;
  }
  if( (atom.eKind == SQLITE_TEXT || atom.eKind == SQLITE_BLOB) &&
      !atom.aData && (atom.eKind == SQLITE_TEXT || atom.nData) ){
    return bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
  }
  kind = bsqlTypeKind(pType);
  if( kind == 0 ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "missing target type");
  }
  if( atom.eKind == SQLITE_BLOB && !bsqlIsBytes(pType) ){
    memset(&temporary, 0, sizeof(temporary));
    ok = bsqlSplitValue(bebop_view(atom.aData, atom.nData), &spec, &payload,
                         &pWork->error)
      && bsqlSpecForType(pSchema, pType, &temporary, &expected, &pWork->error);
    if( ok && (spec.length != expected.length ||
               memcmp(spec.data, expected.data, spec.length)) ){
      ok = bsqlFail(&pWork->error, BSQL_INVALID,
                     "replacement type does not match");
    }
    if( ok ){
      view = bsqlView(pSchema, pType, payload, 1);
      ok = bsqlValidateView(view, pWork)
        && bsqlAppend(pOut, payload.data, payload.length, &pWork->error);
    }
    bsqlBufferClear(&temporary);
    return ok;
  }
  if( atom.eKind == SQLITE_NULL ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "Bebop values cannot be NULL");
  }
  view = bsqlView(pSchema, pType, bebop_view(0, 0), 1);
  kind = bsqlStorageKind(view);
  memset(bytes, 0, sizeof(bytes));
  if( kind >= BEBOP_TYPE_KIND_BOOL && kind <= BEBOP_TYPE_KIND_INT_64 ){
    if( atom.eKind != SQLITE_INTEGER ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "integer value required");
    }
    nByte = bsqlPrimitives[kind].nWire;
    bSigned = kind == BEBOP_TYPE_KIND_INT_8 || kind == BEBOP_TYPE_KIND_INT_16
           || kind == BEBOP_TYPE_KIND_INT_32 || kind == BEBOP_TYPE_KIND_INT_64;
    if( kind == BEBOP_TYPE_KIND_BOOL && atom.integer != 0
      && atom.integer != 1 ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "bool requires 0 or 1");
    }
    if( bSigned && nByte < sizeof(int64_t) ){
      limit = INT64_C(1) << (nByte * CHAR_BIT - 1);
      if( atom.integer < -limit || atom.integer >= limit ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "integer out of range");
      }
    }else if( !bSigned && (atom.integer < 0 ||
               (uint64_t)atom.integer >=
               (UINT64_C(1) << (nByte * CHAR_BIT))) ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "integer out of range");
    }
    return bsqlAppendLe(pOut, (uint64_t)atom.integer, nByte, &pWork->error);
  }
  if( kind >= BEBOP_TYPE_KIND_UINT_64 && kind <= BEBOP_TYPE_KIND_UINT_128 ){
    nByte = bsqlPrimitives[kind].nWire;
    return bsqlDecimalBytes(atom, nByte, kind == BEBOP_TYPE_KIND_INT_128,
                            bytes, pWork)
        && bsqlAppend(pOut, bytes, nByte, &pWork->error);
  }
  if( kind >= BEBOP_TYPE_KIND_FLOAT_16 && kind <= BEBOP_TYPE_KIND_BFLOAT_16 ){
    return bsqlFloatBytes(kind, atom, bytes, pWork)
        && bsqlAppend(pOut, bytes, bsqlPrimitives[kind].nWire, &pWork->error);
  }
  if( kind == BEBOP_TYPE_KIND_STRING ){
    if( atom.eKind != SQLITE_TEXT ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "string requires TEXT");
    }
    if( !bsqlUtf8Valid(atom.aData, atom.nData) ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid UTF-8 string");
    }
    if( atom.nData > bsqlMaxValueBytes - BEBOP_WIRE_SIZE_LEN - 1 ){
      return bsqlFail(&pWork->error, BSQL_LIMIT, "string byte limit exceeded");
    }
    if( !bsqlReserve(pOut, BEBOP_WIRE_SIZE_LEN + atom.nData + 1,
                     &pWork->error) ){
      return 0;
    }
    a = pOut->aOwned + pOut->nData;
    bsqlStoreLe(a, atom.nData, BEBOP_WIRE_SIZE_LEN);
    if( atom.nData ){
      memcpy(a + BEBOP_WIRE_SIZE_LEN, atom.aData, atom.nData);
    }
    a[BEBOP_WIRE_SIZE_LEN + atom.nData] = 0;
    pOut->nData += BEBOP_WIRE_SIZE_LEN + atom.nData + 1;
    return 1;
  }
  if( kind == BEBOP_TYPE_KIND_UUID ){
    if( atom.eKind != SQLITE_TEXT || atom.nData != BSQL_UUID_TEXT_LENGTH ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
                       "UUID requires canonical text");
    }
    position = 0;
    offset = 0;
    for(group=0; group<BSQL_COUNT(bsqlUuidGroups); ++group){
      if( group && atom.aData[position++] != '-' ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "invalid UUID");
      }
      for(i=0; i<bsqlUuidGroups[group]; ++i){
        high = bsqlHex(atom.aData[position++]);
        low = bsqlHex(atom.aData[position++]);
        if( (high & low & BSQL_HEX_VALID) == 0 ){
          return bsqlFail(&pWork->error, BSQL_INVALID, "invalid UUID");
        }
        bytes[offset++] = (uint8_t)(((high & BSQL_HEX_MASK) <<
                                    BSQL_HEX_SHIFT) | (low & BSQL_HEX_MASK));
      }
    }
    return bsqlAppend(pOut, bytes, BEBOP_WIRE_SIZE_UUID, &pWork->error);
  }
  if( kind == BEBOP_TYPE_KIND_TIMESTAMP || kind == BEBOP_TYPE_KIND_DURATION ){
    return bsqlTimeBytes(kind, atom, bytes, pWork)
        && bsqlAppend(pOut, bytes, bsqlPrimitives[kind].nWire, &pWork->error);
  }
  if( bsqlIsBytes(pType) ){
    if( atom.eKind != SQLITE_BLOB ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "bytes require a BLOB");
    }
    if( atom.nData > BSQL_MAX_ITEMS ){
      return bsqlFail(&pWork->error, BSQL_LIMIT,
        "container item limit exceeded");
    }
    if( kind == BEBOP_TYPE_KIND_FIXED_ARRAY &&
        atom.nData != pType->fixed_array_size.value ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
                       "fixed byte array length mismatch");
    }
    return (kind != BEBOP_TYPE_KIND_ARRAY ||
             bsqlAppendLe(pOut, atom.nData, BEBOP_WIRE_SIZE_LEN, &pWork->error))
        && bsqlAppend(pOut, atom.aData, atom.nData, &pWork->error);
  }
  return bsqlFail(&pWork->error, BSQL_INVALID,
                   "container requires a typed Bebop BLOB");
}

/* Quote UTF-8 JSON text after one exact checked reservation. Control bytes use
** six-byte escapes, preserving the prior JSON spelling. OOM, invalid UTF-8,
** overflow, or cancellation leaves the logical output length unchanged.
*/
static int bsqlJsonQuote(
  BsqlBuffer *pOut, const uint8_t *aText, size_t nText, BsqlWork *pWork
){
  uint8_t *aOut;
  size_t nNeed;
  size_t i;
  size_t j;
  size_t extra;
  unsigned c;
  if( nText > bsqlMaxValueBytes - 2 ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "JSON byte limit exceeded");
  }
  if( !bsqlUtf8Valid(aText, nText) ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid UTF-8 JSON text");
  }
  nNeed = nText + 2;
  for(i=0; i<nText; ++i){
    if( i % BSQL_BYTE_POLL_INTERVAL == 0 && !bsqlPoll(pWork) ){
      return 0;
    }
    c = aText[i];
    extra = c < 0x20 ? BSQL_UNICODE_ESCAPE_BYTES - 1 :
            (c == '"' || c == '\\') ? 1 : 0;
    if( nNeed > bsqlMaxValueBytes - extra ){
      return bsqlFail(&pWork->error, BSQL_LIMIT, "JSON byte limit exceeded");
    }
    nNeed += extra;
  }
  if( !bsqlReserve(pOut, nNeed, &pWork->error) ){
    return 0;
  }
  aOut = pOut->aOwned + pOut->nData;
  j = 0;
  aOut[j++] = '"';
  for(i=0; i<nText; ++i){
    c = aText[i];
    if( c < 0x20 ){
      aOut[j++] = '\\';
      aOut[j++] = 'u';
      aOut[j++] = '0';
      aOut[j++] = '0';
      aOut[j++] = (uint8_t)bsqlHexAlphabet[c >> BSQL_HEX_SHIFT];
      aOut[j++] = (uint8_t)bsqlHexAlphabet[c & BSQL_HEX_MASK];
    }else{
      if( c == '"' || c == '\\' ){
        aOut[j++] = '\\';
      }
      aOut[j++] = (uint8_t)c;
    }
  }
  aOut[j++] = '"';
  assert(j == nNeed);
  pOut->nData += nNeed;
  return 1;
}

static const char bsqlBase64Alphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* A row of invalid bytes keeps the full 256-entry decoder table readable. */
#define BSQL_B64_BAD_ROW \
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, \
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, \
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, \
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID

/* Table entries 0..63 are sextets. The high bit marks every invalid byte. */
static const uint8_t bsqlBase64Decode[UCHAR_MAX + 1] = {
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, 62,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID, 63,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
  BSQL_B64_INVALID, BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_INVALID, BSQL_B64_INVALID,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW,
  BSQL_B64_BAD_ROW
};
#undef BSQL_B64_BAD_ROW

/* Encode quoted base64 with one reservation and direct writes. Complete
** triples have no tail branches and no per-quartet append or allocation.
** Bounds are checked before multiplying; cancellation preserves nData.
*/
static int bsqlBase64Json(
  BsqlBuffer *pOut, Bebop_View bytes, BsqlWork *pWork
){
  uint8_t *aOut;
  const uint8_t *a;
  size_t nGroup;
  size_t nNeed;
  size_t nFull;
  size_t i;
  size_t j;
  size_t tail;
  uint32_t bits;
  if( bytes.length && !bytes.data ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "missing byte-array data");
  }
  nGroup = bytes.length / BSQL_B64_INPUT_BYTES;
  tail = bytes.length % BSQL_B64_INPUT_BYTES;
  nGroup += tail != 0;
  if( nGroup > (bsqlMaxValueBytes - 2) / BSQL_B64_OUTPUT_BYTES ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "base64 byte limit exceeded");
  }
  nNeed = nGroup * BSQL_B64_OUTPUT_BYTES + 2;
  if( !bsqlReserve(pOut, nNeed, &pWork->error) ){
    return 0;
  }
  aOut = pOut->aOwned + pOut->nData;
  a = bytes.data;
  nFull = bytes.length - tail;
  j = 0;
  aOut[j++] = '"';
  for(i=0; i<nFull; i+=BSQL_B64_INPUT_BYTES){
    if( i % BSQL_BYTE_POLL_INTERVAL == 0 && !bsqlPoll(pWork) ){
      return 0;
    }
    bits = ((uint32_t)a[i] << (2 * CHAR_BIT)) |
           ((uint32_t)a[i+1] << CHAR_BIT) | a[i+2];
    aOut[j++] = (uint8_t)bsqlBase64Alphabet[bits >> (3 * BSQL_B64_BITS)];
    aOut[j++] = (uint8_t)bsqlBase64Alphabet[
      (bits >> (2 * BSQL_B64_BITS)) & BSQL_B64_MASK];
    aOut[j++] = (uint8_t)bsqlBase64Alphabet[
      (bits >> BSQL_B64_BITS) & BSQL_B64_MASK];
    aOut[j++] = (uint8_t)bsqlBase64Alphabet[bits & BSQL_B64_MASK];
  }
  if( tail ){
    bits = (uint32_t)a[nFull] << (2 * CHAR_BIT);
    if( tail == 2 ){
      bits |= (uint32_t)a[nFull+1] << CHAR_BIT;
    }
    aOut[j++] = (uint8_t)bsqlBase64Alphabet[bits >> (3 * BSQL_B64_BITS)];
    aOut[j++] = (uint8_t)bsqlBase64Alphabet[
      (bits >> (2 * BSQL_B64_BITS)) & BSQL_B64_MASK];
    aOut[j++] = tail == 2 ? (uint8_t)bsqlBase64Alphabet[
      (bits >> BSQL_B64_BITS) & BSQL_B64_MASK] : '=';
    aOut[j++] = '=';
  }
  aOut[j++] = '"';
  assert(j == nNeed);
  pOut->nData += nNeed;
  return 1;
}

/* Decode strict canonical base64 using a 256-entry lookup table. Padding may
** appear only in the final quartet; unused tail bits must be zero. Reserve
** once and commit nData only after every quartet is valid. OOM and malformed
** input leave the old logical contents intact. No alphabet searches occur.
*/
static int bsqlDecodeBase64(
  BsqlAtom atom, BsqlBuffer *pOut, BsqlWork *pWork
){
  const uint8_t *a;
  uint8_t *aOut;
  size_t i;
  size_t j;
  size_t last;
  size_t nDecoded;
  unsigned padding;
  unsigned x0;
  unsigned x1;
  unsigned x2;
  unsigned x3;
  if( atom.eKind != SQLITE_TEXT || atom.nData % BSQL_B64_OUTPUT_BYTES ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid base64");
  }
  if( atom.nData == 0 ){
    return pWork->error.eCode == BSQL_OK;
  }
  if( !atom.aData ){
    return bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
  }
  a = atom.aData;
  last = atom.nData - BSQL_B64_OUTPUT_BYTES;
  padding = (a[atom.nData-1] == '=') + (a[atom.nData-2] == '=');
  nDecoded = atom.nData / BSQL_B64_OUTPUT_BYTES * BSQL_B64_INPUT_BYTES
           - padding;
  if( !bsqlReserve(pOut, nDecoded, &pWork->error) ){
    return 0;
  }
  aOut = pOut->aOwned + pOut->nData;
  j = 0;
  for(i=0; i<last; i+=BSQL_B64_OUTPUT_BYTES){
    if( i % BSQL_BYTE_POLL_INTERVAL == 0 && !bsqlPoll(pWork) ){
      return 0;
    }
    x0 = bsqlBase64Decode[a[i]];
    x1 = bsqlBase64Decode[a[i+1]];
    x2 = bsqlBase64Decode[a[i+2]];
    x3 = bsqlBase64Decode[a[i+3]];
    if( (x0 | x1 | x2 | x3) & BSQL_B64_INVALID ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid base64");
    }
    aOut[j++] = (uint8_t)((x0 << 2) | (x1 >> 4));
    aOut[j++] = (uint8_t)((x1 << 4) | (x2 >> 2));
    aOut[j++] = (uint8_t)((x2 << 6) | x3);
  }
  x0 = bsqlBase64Decode[a[last]];
  x1 = bsqlBase64Decode[a[last+1]];
  if( (x0 | x1) & BSQL_B64_INVALID ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid base64");
  }
  if( a[last+2] == '=' ){
    if( a[last+3] != '=' ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid base64 padding");
    }
    if( x1 & BSQL_B64_TWO_PAD_MASK ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "noncanonical base64");
    }
    aOut[j++] = (uint8_t)((x0 << 2) | (x1 >> 4));
  }else{
    x2 = bsqlBase64Decode[a[last+2]];
    if( x2 & BSQL_B64_INVALID ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid base64");
    }
    aOut[j++] = (uint8_t)((x0 << 2) | (x1 >> 4));
    aOut[j++] = (uint8_t)((x1 << 4) | (x2 >> 2));
    if( a[last+3] == '=' ){
      if( x2 & BSQL_B64_ONE_PAD_MASK ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "noncanonical base64");
      }
    }else{
      x3 = bsqlBase64Decode[a[last+3]];
      if( x3 & BSQL_B64_INVALID ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "invalid base64");
      }
      aOut[j++] = (uint8_t)((x2 << 6) | x3);
    }
  }
  assert(j == nDecoded);
  pOut->nData += nDecoded;
  return 1;
}

/* Render a validated value as JSON, preserving wide-integer strings, base64
** byte arrays, special floating-point strings, and [key,value] map entries.
** Output uses SQLite allocation; depth, visits, size, and interruption are
** checked. Unknown wire fields cannot be represented and produce an error.
*/
static int bsqlWriteJson(
  BsqlView view, BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  unsigned kind;
  const uint8_t *a;
  const char *zSpecial;
  char zNumber[BSQL_NUMBER_TEXT_BYTES];
  char zUuid[BSQL_UUID_TEXT_BYTES];
  char zTime[BSQL_TIME_TEXT_BYTES];
  size_t n;
  size_t nItem;
  size_t prefix;
  double value;
  int bSigned;
  int bObject;
  BsqlIterator it;
  BsqlEntry entry;
  if( !bsqlVisit(pWork, nDepth) ){
    return 0;
  }
  kind = bsqlStorageKind(view);
  a = view.bytes.data;
  if( kind == BEBOP_TYPE_KIND_BOOL ){
    return a[0] ? BSQL_APPEND_LITERAL(pOut, "true", &pWork->error) :
                  BSQL_APPEND_LITERAL(pOut, "false", &pWork->error);
  }
  if( kind >= BEBOP_TYPE_KIND_BYTE && kind <= BEBOP_TYPE_KIND_INT_64 ){
    bSigned = kind == BEBOP_TYPE_KIND_INT_8 || kind == BEBOP_TYPE_KIND_INT_16
           || kind == BEBOP_TYPE_KIND_INT_32 || kind == BEBOP_TYPE_KIND_INT_64;
    if( bSigned ){
      sqlite3_snprintf(sizeof(zNumber), zNumber, "%lld",
        (sqlite3_int64)bsqlSignedLe(a, bsqlPrimitives[kind].nWire));
    }else{
      sqlite3_snprintf(sizeof(zNumber), zNumber, "%llu",
        (sqlite3_uint64)bsqlLoadLe(a, bsqlPrimitives[kind].nWire));
    }
    return bsqlAppend(pOut, zNumber, strlen(zNumber), &pWork->error);
  }
  if( kind >= BEBOP_TYPE_KIND_UINT_64 && kind <= BEBOP_TYPE_KIND_UINT_128 ){
    n = bsqlDecimalText(a, bsqlPrimitives[kind].nWire,
                        kind == BEBOP_TYPE_KIND_INT_128, zNumber);
    return bsqlJsonQuote(pOut, (const uint8_t *)zNumber, n, pWork);
  }
  if( kind >= BEBOP_TYPE_KIND_FLOAT_16 && kind <= BEBOP_TYPE_KIND_BFLOAT_16 ){
    value = bsqlFloatValue(kind, a);
    if( !bsqlIsFinite(value) ){
      zSpecial = bsqlIsNan(value) ? "NaN" :
                 bsqlSignBit(value) ? "-Infinity" : "Infinity";
      return bsqlJsonQuote(pOut, (const uint8_t *)zSpecial,
                            strlen(zSpecial), pWork);
    }
    if( value == 0 && bsqlSignBit(value) ){
      return BSQL_APPEND_LITERAL(pOut, "-0.0", &pWork->error);
    }
    sqlite3_snprintf(sizeof(zNumber), zNumber, "%!.17g", value);
    return bsqlAppend(pOut, zNumber, strlen(zNumber), &pWork->error);
  }
  if( kind == BEBOP_TYPE_KIND_STRING ){
    return bsqlJsonQuote(pOut, a + BEBOP_WIRE_SIZE_LEN,
      (size_t)bsqlLoadLe(a, BEBOP_WIRE_SIZE_LEN), pWork);
  }
  if( kind == BEBOP_TYPE_KIND_UUID ){
    n = bsqlUuidText(a, zUuid);
    return bsqlJsonQuote(pOut, (const uint8_t *)zUuid, n, pWork);
  }
  if( kind == BEBOP_TYPE_KIND_TIMESTAMP || kind == BEBOP_TYPE_KIND_DURATION ){
    n = bsqlTimeText(kind, a, zTime);
    return bsqlJsonQuote(pOut, (const uint8_t *)zTime, n, pWork);
  }
  if( bsqlIsBytes(view.pType) ){
    prefix = bsqlTypeKind(view.pType) == BEBOP_TYPE_KIND_ARRAY ?
             BEBOP_WIRE_SIZE_LEN : 0;
    return bsqlBase64Json(pOut,
      bebop_view(a + prefix, view.bytes.length - prefix), pWork);
  }
  bObject = kind == BSQL_KIND_STRUCT || kind == BSQL_KIND_MESSAGE
         || kind == BSQL_KIND_UNION;
  if( !bsqlAppend(pOut, bObject ? "{" : "[", 1, &pWork->error)
   || !bsqlIteratorInit(view, &it, pWork) ){
    return 0;
  }
  nItem = 0;
  while( bsqlIteratorNext(&it, &entry, pWork) ){
    if( !entry.value.pType ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
                       "cannot represent unknown wire field in JSON");
    }
    if( nItem++ && !BSQL_APPEND_LITERAL(pOut, ",", &pWork->error) ){
      return 0;
    }
    if( bObject &&
        (!bsqlJsonQuote(pOut, (const uint8_t *)entry.name.data,
                         entry.name.length, pWork) ||
         !BSQL_APPEND_LITERAL(pOut, ":", &pWork->error)) ){
      return 0;
    }
    if( kind == BEBOP_TYPE_KIND_MAP &&
        (!BSQL_APPEND_LITERAL(pOut, "[", &pWork->error) ||
         !bsqlWriteJson(entry.key, pOut, nDepth + 1, pWork) ||
         !BSQL_APPEND_LITERAL(pOut, ",", &pWork->error)) ){
      return 0;
    }
    if( !bsqlWriteJson(entry.value, pOut, nDepth + 1, pWork) ){
      return 0;
    }
    if( kind == BEBOP_TYPE_KIND_MAP &&
        !BSQL_APPEND_LITERAL(pOut, "]", &pWork->error) ){
      return 0;
    }
  }
  return pWork->error.eCode == BSQL_OK &&
         bsqlAppend(pOut, bObject ? "}" : "]", 1, &pWork->error);
}

/* Record assembly keeps borrowed and newly encoded field values separately.
** Arrays and maps use streaming/splicing paths instead of one part per item.
*/
typedef struct BsqlPart BsqlPart;
struct BsqlPart {
  BsqlBuffer value;               /* Borrowed original or owned replacement. */
  size_t iField;                  /* Original struct-field order. */
  uint8_t tag;                    /* Message tag or union discriminator. */
};

typedef struct BsqlParts BsqlParts;
struct BsqlParts {
  BsqlPart *a;                    /* SQLite-owned vector. */
  size_t n;                      /* Initialized entries. */
  size_t nAlloc;                  /* Allocated entries. */
};

/* Append one zeroed record part. Growth is bounded by the descriptor limit;
** SQLite OOM preserves the previous vector and records a work error.
*/
static BsqlPart *bsqlPartsPush(BsqlParts *p, BsqlWork *pWork){
  size_t nAlloc;
  BsqlPart *aNew;
  BsqlPart *pPart;
  if( p->n == BSQL_MAX_DEFINITIONS ){
    bsqlFail(&pWork->error, BSQL_LIMIT, "too many record fields");
    return 0;
  }
  if( p->n == p->nAlloc ){
    nAlloc = p->nAlloc ? p->nAlloc * 2 : BSQL_MIN_PARTS;
    if( nAlloc > BSQL_MAX_DEFINITIONS ){
      nAlloc = BSQL_MAX_DEFINITIONS;
    }
    aNew = sqlite3_realloc64(p->a, (sqlite3_uint64)nAlloc * sizeof(*p->a));
    if( !aNew ){
      bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
      return 0;
    }
    p->a = aNew;
    p->nAlloc = nAlloc;
  }
  pPart = p->a + p->n++;
  memset(pPart, 0, sizeof(*pPart));
  return pPart;
}

/* Release owned field buffers and the vector. Borrowed field bytes survive. */
static void bsqlPartsClear(BsqlParts *p){
  size_t i;
  for(i=0; i<p->n; ++i){
    bsqlBufferClear(&p->a[i].value);
  }
  sqlite3_free(p->a);
  memset(p, 0, sizeof(*p));
}

/* Sort record parts by their message tag. No subtraction can overflow. */
static int bsqlPartTagCompare(const void *a, const void *b){
  unsigned x;
  unsigned y;
  x = ((const BsqlPart *)a)->tag;
  y = ((const BsqlPart *)b)->tag;
  return (x > y) - (x < y);
}

/* Restore original struct-field order after accepting arbitrary JSON order. */
static int bsqlPartFieldCompare(const void *a, const void *b){
  size_t x;
  size_t y;
  x = ((const BsqlPart *)a)->iField;
  y = ((const BsqlPart *)b)->iField;
  return (x > y) - (x < y);
}

/* Message scratch is heap/arena-backed rather than a large automatic array. */
typedef struct BsqlMessageScratch BsqlMessageScratch;
struct BsqlMessageScratch {
  uint8_t aTag[UINT8_MAX];        /* Sorted nonzero wire tags. */
  /* Payload offsets checked before narrowing. */
  uint32_t aOffset[UINT8_MAX];
};

/* Assemble record parts using one output reservation for structs/unions and
** the Bebop indexed-message writer for messages. Temporary message storage
** uses a separately bounded SQLite-backed arena, not the schema-arena limit.
** Invalid tags, overflow, OOM, and interruption are reported without leaks.
*/
static int bsqlAssembleRecord(
  BsqlView parent, BsqlParts *pParts, BsqlBuffer *pOut, BsqlWork *pWork
){
  unsigned kind;
  size_t i;
  size_t nPayload;
  size_t offset;
  BsqlAllocator allocator;
  Bebop_Context *pArena;
  Bebop_Writer *pWriter;
  BsqlMessageScratch *pScratch;
  Bebop_Bytes bytes;
  Bebop_View encoded;
  int ok;
  kind = bsqlViewKind(parent);
  nPayload = 0;
  for(i=0; i<pParts->n; ++i){
    if( pParts->a[i].value.nData > bsqlMaxValueBytes - nPayload ){
      return bsqlFail(&pWork->error, BSQL_LIMIT, "record byte limit exceeded");
    }
    nPayload += pParts->a[i].value.nData;
  }
  if( kind == BSQL_KIND_STRUCT ){
    if( pParts->n != bsqlFields(bsqlDefinitionFind(parent.pSchema,
                      parent.pType->defined_fqn.value)).length ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
                       "missing required struct field");
    }
    if( !bsqlReserve(pOut, nPayload, &pWork->error) ){
      return 0;
    }
    for(i=0; i<pParts->n; ++i){
      if( !bsqlAppend(pOut, pParts->a[i].value.aData,
                       pParts->a[i].value.nData, &pWork->error) ){
        return 0;
      }
    }
    return 1;
  }
  if( kind == BSQL_KIND_UNION ){
    if( pParts->n != 1 || pParts->a[0].tag == 0 ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "union requires one branch");
    }
    if( nPayload > bsqlMaxValueBytes - BSQL_UNION_HEADER_BYTES ){
      return bsqlFail(&pWork->error, BSQL_LIMIT, "record byte limit exceeded");
    }
    return bsqlReserve(pOut, nPayload + BSQL_UNION_HEADER_BYTES, &pWork->error)
        && bsqlAppendLe(pOut, nPayload + BSQL_TAG_BYTES,
                         BEBOP_WIRE_SIZE_LEN, &pWork->error)
        && bsqlAppend(pOut, &pParts->a[0].tag, BSQL_TAG_BYTES, &pWork->error)
        && bsqlAppend(pOut, pParts->a[0].value.aData,
                        pParts->a[0].value.nData, &pWork->error);
  }
  if( kind != BSQL_KIND_MESSAGE || pParts->n > UINT8_MAX ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid record assembly");
  }
  if( nPayload > bsqlMaxValueBytes - BEBOP_WIRE_SIZE_LEN ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "record byte limit exceeded");
  }
  if( pParts->n > 1 ){
    qsort(pParts->a, pParts->n, sizeof(*pParts->a), bsqlPartTagCompare);
  }
  for(i=0; i<pParts->n; ++i){
    if( pParts->a[i].tag == 0 ||
        (i && pParts->a[i-1].tag == pParts->a[i].tag) ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "invalid message tag");
    }
  }
  memset(&allocator, 0, sizeof(allocator));
  pArena = 0;
  ok = 0;
  if( !bsqlArenaInit(&allocator, &pArena, bsqlMaxValueBytes, &pWork->error) ){
    return 0;
  }
  pScratch = bsqlArenaArray(pArena, 1, sizeof(*pScratch));
  pWriter = bebop_context_writer(pArena, 0);
  if( !pScratch || !pWriter ||
      bebop_writer_write_u32(pWriter, 0) != BEBOP_RESULT_OK ){
    goto cleanup;
  }
  offset = 0;
  for(i=0; i<pParts->n; ++i){
    if( !bsqlVisit(pWork, 0) ){
      goto cleanup;
    }
    pScratch->aTag[i] = pParts->a[i].tag;
    pScratch->aOffset[i] = (uint32_t)offset;
    memset(&bytes, 0, sizeof(bytes));
    bytes.data = (uint8_t *)pParts->a[i].value.aData;
    bytes.length = pParts->a[i].value.nData;
    if( bebop_writer_write_fixed_bytes(pWriter, bytes) != BEBOP_RESULT_OK ){
      goto cleanup;
    }
    offset += bytes.length;
  }
  if( bebop_writer_end_indexed_message(pWriter, 0, BEBOP_WIRE_SIZE_LEN,
        pScratch->aTag, pScratch->aOffset, (uint8_t)pParts->n)
      != BEBOP_RESULT_OK ){
    goto cleanup;
  }
  encoded = bebop_writer_view(pWriter);
  ok = bsqlAppend(pOut, encoded.data, encoded.length, &pWork->error);
cleanup:
  if( !ok && pWork->error.eCode == BSQL_OK ){
    bsqlArenaFail(&allocator, &pWork->error, "cannot encode message");
  }
  bebop_context_free(pArena);
  return ok;
}

/* Reset and clear bindings before any borrowed atom backing may expire. */
static void bsqlStatementRelease(sqlite3_stmt *pStatement){
  if( pStatement ){
    sqlite3_reset(pStatement);
    sqlite3_clear_bindings(pStatement);
  }
}

/* Prepare and bind a short-lived JSON statement. SQLITE_STATIC is safe because
** callers reset/finalize before the input atom expires. SQLite errors retain
** their result code. A partially prepared statement remains caller-owned.
*/
static int bsqlPrepareJson(
  const char *zSql, BsqlAtom json, sqlite3_stmt **ppStatement, BsqlWork *pWork
){
  int rc;
  if( pWork->error.eCode != BSQL_OK ){
    return 0;
  }
  rc = sqlite3_prepare_v2(pWork->db, zSql, -1, ppStatement, 0);
  if( rc == SQLITE_OK ){
    rc = sqlite3_bind_text64(*ppStatement, 1, (const char *)json.aData,
      (sqlite3_uint64)json.nData, SQLITE_STATIC, SQLITE_UTF8);
  }
  return rc == SQLITE_OK || bsqlSqlFail(pWork, rc, 0);
}

/* Reuse one json_each statement per depth and purpose. A map's pair cursor
** cannot overwrite its still-active outer container cursor. Pool/prepare OOM
** is reported; all statements belong to pWork until bsqlWorkClear.
*/
static sqlite3_stmt *bsqlJsonStatement(
  BsqlAtom json, unsigned nDepth, int bPair, BsqlWork *pWork
){
  sqlite3_stmt **pp;
  int rc;
  if( nDepth >= BSQL_MAX_DEPTH ){
    bsqlFail(&pWork->error, BSQL_LIMIT, "JSON depth exceeded");
    return 0;
  }
  if( pWork->error.eCode != BSQL_OK ){
    return 0;
  }
  if( !pWork->pJson ){
    pWork->pJson = sqlite3_malloc64(sizeof(*pWork->pJson));
    if( !pWork->pJson ){
      bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
      return 0;
    }
    memset(pWork->pJson, 0, sizeof(*pWork->pJson));
  }
  pp = bPair ? pWork->pJson->apPair + nDepth :
               pWork->pJson->apEach + nDepth;
  if( !*pp ){
    if( !bsqlPrepareJson("SELECT key,value,type FROM json_each(?1)",
                         json, pp, pWork) ){
      bsqlStatementRelease(*pp);
      return 0;
    }
  }else{
    bsqlStatementRelease(*pp);
    rc = sqlite3_bind_text64(*pp, 1, (const char *)json.aData,
      (sqlite3_uint64)json.nData, SQLITE_STATIC, SQLITE_UTF8);
    if( rc != SQLITE_OK ){
      bsqlSqlFail(pWork, rc, 0);
      bsqlStatementRelease(*pp);
      return 0;
    }
  }
  return *pp;
}

static int bsqlEncodeJson(
  const BsqlSchema *, const BsqlType *, BsqlAtom, const char *,
  BsqlBuffer *, unsigned, BsqlWork *
);

/* Read and encode one json_each row. Check both SQLite text/value conversions
** before passing borrowed pointers into recursive encoding.
*/
static int bsqlEncodeJsonRow(
  const BsqlSchema *pSchema, const BsqlType *pType, sqlite3_stmt *pStatement,
  BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  BsqlAtom atom;
  const char *zKind;
  atom = bsqlSqlAtom(sqlite3_column_value(pStatement, 1), pWork);
  zKind = (const char *)sqlite3_column_text(pStatement, 2);
  if( !zKind ){
    return bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
  }
  return bsqlEncodeJson(pSchema, pType, atom, zKind, pOut, nDepth, pWork);
}

/* Encode exactly two map-pair elements directly into the output. The enclosing
** streaming encoder restores its starting length on failure. Statements are
** reused and reset before any row-backed input pointer can become invalid.
*/
static int bsqlEncodeJsonPair(
  const BsqlSchema *pSchema, const BsqlType *pType, BsqlAtom pair,
  BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  sqlite3_stmt *pStatement;
  int rc;
  int ok;
  pStatement = bsqlJsonStatement(pair, nDepth, 1, pWork);
  if( !pStatement ){
    return 0;
  }
  ok = 0;
  rc = sqlite3_step(pStatement);
  if( rc != SQLITE_ROW ){
    if( rc == SQLITE_DONE ){
      bsqlFail(&pWork->error, BSQL_INVALID, "missing map key");
    }else{
      bsqlSqlFail(pWork, rc, 0);
    }
    goto cleanup;
  }
  if( !bsqlEncodeJsonRow(pSchema, pType->map_key.value, pStatement,
                         pOut, nDepth + 1, pWork) ){
    goto cleanup;
  }
  rc = sqlite3_step(pStatement);
  if( rc != SQLITE_ROW ){
    if( rc == SQLITE_DONE ){
      bsqlFail(&pWork->error, BSQL_INVALID, "missing map value");
    }else{
      bsqlSqlFail(pWork, rc, 0);
    }
    goto cleanup;
  }
  if( !bsqlEncodeJsonRow(pSchema, pType->map_value.value, pStatement,
                         pOut, nDepth + 1, pWork) ){
    goto cleanup;
  }
  rc = sqlite3_step(pStatement);
  if( rc == SQLITE_ROW ){
    bsqlFail(&pWork->error, BSQL_INVALID, "map entry must have two elements");
  }else if( rc != SQLITE_DONE ){
    bsqlSqlFail(pWork, rc, 0);
  }else{
    ok = 1;
  }
cleanup:
  bsqlStatementRelease(pStatement);
  return ok;
}

/* Stream JSON arrays/maps into a single growable wire buffer. This avoids a
** separate allocation and Part object for each element. Map-key uniqueness is
** checked by bsqlFromJson's final validation. Failure rolls back nData.
*/
static int bsqlEncodeJsonSequence(
  const BsqlSchema *pSchema, const BsqlType *pType, BsqlAtom json,
  BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  sqlite3_stmt *pStatement;
  BsqlAtom pair;
  const char *zKind;
  unsigned kind;
  size_t start;
  size_t nItem;
  int rc;
  int ok;
  kind = bsqlTypeKind(pType);
  start = pOut->nData;
  nItem = 0;
  ok = 0;
  pStatement = bsqlJsonStatement(json, nDepth, 0, pWork);
  if( !pStatement ){
    return 0;
  }
  if( kind != BEBOP_TYPE_KIND_FIXED_ARRAY &&
      !bsqlAppendLe(pOut, 0, BEBOP_WIRE_SIZE_LEN, &pWork->error) ){
    goto cleanup;
  }
  while( (rc = sqlite3_step(pStatement)) == SQLITE_ROW ){
    if( nItem == BSQL_MAX_ITEMS ){
      bsqlFail(&pWork->error, BSQL_LIMIT, "container item limit exceeded");
      goto cleanup;
    }
    if( kind == BEBOP_TYPE_KIND_MAP ){
      zKind = (const char *)sqlite3_column_text(pStatement, 2);
      if( !zKind ){
        bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
        goto cleanup;
      }
      if( strcmp(zKind, "array") ){
        bsqlFail(&pWork->error, BSQL_INVALID,
                   "map entries require [key,value] pairs");
        goto cleanup;
      }
      pair = bsqlSqlAtom(sqlite3_column_value(pStatement, 1), pWork);
      if( !bsqlEncodeJsonPair(pSchema, pType, pair, pOut, nDepth, pWork) ){
        goto cleanup;
      }
    }else if( !bsqlEncodeJsonRow(pSchema, bsqlElement(pType), pStatement,
                                 pOut, nDepth + 1, pWork) ){
      goto cleanup;
    }
    ++nItem;
  }
  if( rc != SQLITE_DONE ){
    bsqlSqlFail(pWork, rc, 0);
    goto cleanup;
  }
  if( kind == BEBOP_TYPE_KIND_FIXED_ARRAY ){
    if( nItem != pType->fixed_array_size.value ){
      bsqlFail(&pWork->error, BSQL_INVALID, "fixed array length mismatch");
      goto cleanup;
    }
  }else{
    bsqlStoreLe(pOut->aOwned + start, nItem, BEBOP_WIRE_SIZE_LEN);
  }
  ok = 1;
cleanup:
  bsqlStatementRelease(pStatement);
  if( !ok ){
    pOut->nData = start;
  }
  return ok;
}

/* Encode a JSON record using its binary-search name index. An explicit seen
** vector detects duplicates. Parts hold only record fields, not array/map
** items. Temporary allocations and pooled statements are cleaned on failure.
*/
static int bsqlEncodeJsonRecord(
  const BsqlSchema *pSchema, const BsqlType *pType, BsqlAtom json,
  BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  const BsqlDefinition *pDefinition;
  const BsqlDefinitionInfo *pInfo;
  const BsqlType *pChild;
  const BsqlBranch *pBranch;
  Bebop_FieldDescriptor_Array fields;
  Bebop_String name;
  sqlite3_stmt *pStatement;
  BsqlParts parts;
  BsqlPart *pPart;
  uint8_t *aSeen;
  size_t iDefinition;
  size_t iField;
  unsigned kind;
  uint8_t tag;
  int rc;
  int ok;
  iDefinition = bsqlDefinitionIndex(pSchema, pType->defined_fqn.value);
  if( iDefinition == SIZE_MAX ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "unresolved record type");
  }
  pDefinition = pSchema->apDefinition[iDefinition];
  pInfo = pSchema->aInfo + iDefinition;
  kind = (unsigned)pDefinition->kind.value;
  fields = bsqlFields(pDefinition);
  memset(&parts, 0, sizeof(parts));
  aSeen = 0;
  ok = 0;
  pStatement = bsqlJsonStatement(json, nDepth, 0, pWork);
  if( !pStatement ){
    return 0;
  }
  if( fields.length ){
    aSeen = sqlite3_malloc64((sqlite3_uint64)fields.length);
    if( !aSeen ){
      bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
      goto cleanup;
    }
    memset(aSeen, 0, fields.length);
  }
  while( (rc = sqlite3_step(pStatement)) == SQLITE_ROW ){
    name.data = (const char *)sqlite3_column_text(pStatement, 0);
    name.length = (size_t)sqlite3_column_bytes(pStatement, 0);
    if( !name.data ){
      bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
      goto cleanup;
    }
    iField = bsqlNameFind(pInfo, name);
    if( iField == SIZE_MAX ){
      bsqlFail(&pWork->error, BSQL_INVALID, "unknown JSON field or branch");
      goto cleanup;
    }
    tag = 0;
    if( kind == BEBOP_DEFINITION_KIND_UNION ){
      if( parts.n ){
        bsqlFail(&pWork->error, BSQL_INVALID, "union requires one branch");
        goto cleanup;
      }
      pBranch = pDefinition->union_def.value->branches.value.data + iField;
      pChild = bsqlDefinitionType(pSchema, bsqlBranchName(pBranch));
      tag = pBranch->discriminator.value;
    }else{
      if( aSeen[iField] ){
        bsqlFail(&pWork->error, BSQL_INVALID, "duplicate JSON field");
        goto cleanup;
      }
      aSeen[iField] = 1;
      pChild = fields.data[iField].type.value;
      if( kind == BEBOP_DEFINITION_KIND_MESSAGE ){
        tag = (uint8_t)fields.data[iField].index.value;
      }
    }
    pPart = bsqlPartsPush(&parts, pWork);
    if( !pPart ){
      goto cleanup;
    }
    pPart->tag = tag;
    pPart->iField = iField;
    if( !bsqlEncodeJsonRow(pSchema, pChild, pStatement, &pPart->value,
                           nDepth + 1, pWork) ){
      goto cleanup;
    }
  }
  if( rc != SQLITE_DONE ){
    bsqlSqlFail(pWork, rc, 0);
    goto cleanup;
  }
  if( kind == BEBOP_DEFINITION_KIND_STRUCT ){
    if( parts.n != fields.length ){
      bsqlFail(&pWork->error, BSQL_INVALID, "missing required struct field");
      goto cleanup;
    }
    if( parts.n > 1 ){
      qsort(parts.a, parts.n, sizeof(*parts.a), bsqlPartFieldCompare);
    }
  }
  ok = bsqlAssembleRecord(bsqlView(pSchema, pType, bebop_view(0, 0), 1),
                          &parts, pOut, pWork);
cleanup:
  sqlite3_free(aSeen);
  bsqlPartsClear(&parts);
  bsqlStatementRelease(pStatement);
  return ok;
}

/* Decode a base64 JSON string directly into its final byte-array buffer,
** avoiding a temporary decoded buffer and an extra payload copy.
*/
static int bsqlEncodeJsonBytes(
  const BsqlType *pType, BsqlAtom atom, const char *zKind,
  BsqlBuffer *pOut, BsqlWork *pWork
){
  size_t start;
  size_t payloadStart;
  size_t nByte;
  int bVariable;
  if( strcmp(zKind, "text") ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "bytes require a base64 JSON string");
  }
  start = pOut->nData;
  bVariable = bsqlTypeKind(pType) == BEBOP_TYPE_KIND_ARRAY;
  if( atom.nData >
      ((BSQL_MAX_ITEMS + BSQL_B64_INPUT_BYTES - 1) / BSQL_B64_INPUT_BYTES) *
      BSQL_B64_OUTPUT_BYTES ){
    return bsqlFail(&pWork->error, BSQL_LIMIT,
                     "container item limit exceeded");
  }
  if( !bVariable && atom.nData !=
      ((pType->fixed_array_size.value + BSQL_B64_INPUT_BYTES - 1) /
       BSQL_B64_INPUT_BYTES) * BSQL_B64_OUTPUT_BYTES ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "fixed byte array length mismatch");
  }
  if( bVariable &&
      !bsqlAppendLe(pOut, 0, BEBOP_WIRE_SIZE_LEN, &pWork->error) ){
    return 0;
  }
  payloadStart = pOut->nData;
  if( !bsqlDecodeBase64(atom, pOut, pWork) ){
    pOut->nData = start;
    return 0;
  }
  nByte = pOut->nData - payloadStart;
  if( nByte > BSQL_MAX_ITEMS ){
    pOut->nData = start;
    return bsqlFail(&pWork->error, BSQL_LIMIT, "container item limit exceeded");
  }
  if( bVariable ){
    bsqlStoreLe(pOut->aOwned + start, nByte, BEBOP_WIRE_SIZE_LEN);
  }else if( nByte != pType->fixed_array_size.value ){
    pOut->nData = start;
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "fixed byte array length mismatch");
  }
  return 1;
}

/* Dispatch JSON conversion by actual type, never by a formatted kind string.
** Scalar conversion is exact. Recursive containers stream or assemble as
** appropriate; all paths share depth/visit, allocation, and SQLite errors.
*/
static int bsqlEncodeJson(
  const BsqlSchema *pSchema, const BsqlType *pType, BsqlAtom atom,
  const char *zKind, BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  BsqlView view;
  unsigned kind;
  int bObject;
  if( !bsqlVisit(pWork, nDepth) ){
    return 0;
  }
  if( !zKind ){
    return bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
  }
  if( bsqlIsBytes(pType) ){
    return bsqlEncodeJsonBytes(pType, atom, zKind, pOut, pWork);
  }
  view = bsqlView(pSchema, pType, bebop_view(0, 0), 1);
  kind = bsqlViewKind(view);
  if( !bsqlIterable(view) ){
    if( kind >= BEBOP_TYPE_KIND_FLOAT_16 && kind <= BEBOP_TYPE_KIND_BFLOAT_16
     && atom.eKind == SQLITE_TEXT && atom.aData ){
      if( atom.nData == sizeof("NaN") - 1 &&
          !memcmp(atom.aData, "NaN", sizeof("NaN") - 1) ){
        atom.eKind = SQLITE_FLOAT;
        atom.real = bsqlSpecialFloat(1, 0);
      }else if( atom.nData == sizeof("Infinity") - 1 &&
                !memcmp(atom.aData, "Infinity", sizeof("Infinity") - 1) ){
        atom.eKind = SQLITE_FLOAT;
        atom.real = bsqlSpecialFloat(0, 0);
      }else if( atom.nData == sizeof("-Infinity") - 1 &&
                !memcmp(atom.aData, "-Infinity", sizeof("-Infinity") - 1) ){
        atom.eKind = SQLITE_FLOAT;
        atom.real = bsqlSpecialFloat(0, 1);
      }
    }
    if( kind == BEBOP_TYPE_KIND_BOOL &&
        strcmp(zKind, "true") && strcmp(zKind, "false") ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
                       "JSON bool requires true or false");
    }
    return bsqlEncodeAtom(pSchema, pType, atom, pOut, pWork);
  }
  bObject = kind == BSQL_KIND_STRUCT || kind == BSQL_KIND_MESSAGE
         || kind == BSQL_KIND_UNION;
  if( strcmp(zKind, bObject ? "object" : "array") ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
      "JSON container type mismatch");
  }
  if( bObject ){
    return bsqlEncodeJsonRecord(pSchema, pType, atom, pOut, nDepth, pWork);
  }
  return bsqlEncodeJsonSequence(pSchema, pType, atom, pOut, nDepth, pWork);
}

/* Convert strict JSON and validate the final wire value once, including map
** uniqueness. Root extraction does not enumerate the root container twice.
** SQLite statements are finalized on every exit; failed output is rolled back.
*/
static int bsqlFromJson(
  const BsqlSchema *pSchema, BsqlAtom json, BsqlBuffer *pOut, BsqlWork *pWork
){
  static const char rootSql[] =
    "SELECT CASE WHEN json_type(?1) IN ('array','object') THEN ?1 "
    "ELSE json_extract(?1,'$') END,json_type(?1)";
  sqlite3_stmt *pStatement;
  BsqlAtom atom;
  const char *zKind;
  size_t start;
  int rc;
  int ok;
  if( pWork->error.eCode != BSQL_OK ){
    return 0;
  }
  if( json.eKind != SQLITE_TEXT || !json.aData ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "JSON must be TEXT");
  }
  pStatement = 0;
  start = pOut->nData;
  ok = 0;
  if( !bsqlPrepareJson("SELECT json_valid(?1)", json, &pStatement, pWork) ){
    goto cleanup;
  }
  rc = sqlite3_step(pStatement);
  if( rc != SQLITE_ROW ){
    bsqlSqlFail(pWork, rc, "JSON validation failed");
    goto cleanup;
  }
  if( sqlite3_column_int(pStatement, 0) != 1 ){
    bsqlFail(&pWork->error, BSQL_INVALID, "invalid strict JSON");
    goto cleanup;
  }
  sqlite3_finalize(pStatement);
  pStatement = 0;
  if( !bsqlPrepareJson(rootSql, json, &pStatement, pWork) ){
    goto cleanup;
  }
  rc = sqlite3_step(pStatement);
  if( rc != SQLITE_ROW ){
    bsqlSqlFail(pWork, rc, "invalid JSON root");
    goto cleanup;
  }
  atom = bsqlSqlAtom(sqlite3_column_value(pStatement, 0), pWork);
  zKind = (const char *)sqlite3_column_text(pStatement, 1);
  if( !zKind ){
    bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
    goto cleanup;
  }
  ok = bsqlEncodeJson(pSchema, &pSchema->root, atom, zKind, pOut, 0, pWork);
  if( ok ){
    ok = bsqlValidateView(bsqlView(pSchema, &pSchema->root,
      bebop_view(pOut->aData ? pOut->aData + start : 0, pOut->nData - start),
      1), pWork);
  }
cleanup:
  sqlite3_finalize(pStatement);
  if( !ok ){
    pOut->nData = start;
  }
  return ok;
}

/* Convert a decoded path component into the map key's required SQLite atom.
** Wide integers retain their original decimal spelling for range checking.
** No allocation occurs; returned text borrows the parsed path.
*/
static int bsqlPathKey(
  const BsqlType *pType, const BsqlPathStep *pStep, BsqlAtom *pKey,
  BsqlWork *pWork
){
  unsigned kind;
  uint64_t magnitude;
  memset(pKey, 0, sizeof(*pKey));
  kind = bsqlTypeKind(pType);
  if( kind == BEBOP_TYPE_KIND_STRING && pStep->eKind == BSQL_PATH_NAME ){
    pKey->eKind = SQLITE_TEXT;
    pKey->aData = (const uint8_t *)pStep->u.name.data;
    pKey->nData = pStep->u.name.length;
    return 1;
  }
  if( kind >= BEBOP_TYPE_KIND_BYTE && kind <= BEBOP_TYPE_KIND_UINT_128
   && pStep->eKind == BSQL_PATH_INDEX ){
    if( kind >= BEBOP_TYPE_KIND_UINT_64 ){
      pKey->eKind = SQLITE_TEXT;
      pKey->aData = (const uint8_t *)pStep->u.index.zDigits;
      pKey->nData = pStep->u.index.nDigit;
      return 1;
    }
    magnitude = pStep->u.index.magnitude;
    if( pStep->u.index.bOverflow ||
        magnitude > (uint64_t)INT64_MAX + (pStep->u.index.bNegative ? 1 : 0) ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "map key out of range");
    }
    pKey->eKind = SQLITE_INTEGER;
    if( pStep->u.index.bNegative && magnitude == (uint64_t)INT64_MAX + 1 ){
      pKey->integer = INT64_MIN;
    }else{
      pKey->integer = pStep->u.index.bNegative ?
                       -(sqlite3_int64)magnitude : (sqlite3_int64)magnitude;
    }
    return 1;
  }
  return bsqlFail(&pWork->error, BSQL_INVALID,
                   "path is incompatible with map key type");
}

/* Search a map once using an already encoded key. Optional offsets enclose the
** entire key/value pair, including an empty value. A missing key has both
** offsets at the end of the map. This also supports zero-copy mutation splices.
*/
static int bsqlMapLookup(
  BsqlView parent, Bebop_View encodedKey, BsqlView *pSelected,
  size_t *piIndex, size_t *piBegin, size_t *piEnd, BsqlWork *pWork
){
  BsqlIterator it;
  BsqlEntry entry;
  size_t begin;
  *pSelected = bsqlView(parent.pSchema, 0, bebop_view(0, 0), 0);
  if( piIndex ){
    *piIndex = SIZE_MAX;
  }
  if( piBegin ){
    *piBegin = parent.bytes.length;
  }
  if( piEnd ){
    *piEnd = parent.bytes.length;
  }
  if( bsqlTypeKind(parent.pType) != BEBOP_TYPE_KIND_MAP ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "expected a map");
  }
  if( !bsqlIteratorInit(parent, &it, pWork) ){
    return 0;
  }
  while( it.iPosition < it.nItem ){
    begin = parent.bytes.length - it.remaining.length;
    if( !bsqlIteratorNext(&it, &entry, pWork) ){
      return 0;
    }
    if( entry.key.bytes.length == encodedKey.length &&
        (encodedKey.length == 0 ||
         !memcmp(entry.key.bytes.data, encodedKey.data, encodedKey.length)) ){
      *pSelected = entry.value;
      if( piIndex ){
        *piIndex = entry.iIndex;
      }
      if( piBegin ){
        *piBegin = begin;
      }
      if( piEnd ){
        *piEnd = parent.bytes.length - it.remaining.length;
      }
      return 1;
    }
  }
  return pWork->error.eCode == BSQL_OK;
}

/* Encode and look up a supplied SQL map key. Temporary SQLite storage is
** released on every path; missing keys are successful absent views.
*/
static int bsqlMapFind(
  BsqlView parent, BsqlAtom key, BsqlView *pSelected, BsqlWork *pWork
){
  BsqlBuffer encoded;
  int ok;
  if( bsqlTypeKind(parent.pType) != BEBOP_TYPE_KIND_MAP ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "expected a map");
  }
  memset(&encoded, 0, sizeof(encoded));
  ok = bsqlEncodeAtom(parent.pSchema, parent.pType->map_key.value, key,
                       &encoded, pWork)
    && bsqlMapLookup(parent, bebop_view(encoded.aData, encoded.nData),
                      pSelected, 0, 0, 0, pWork);
  bsqlBufferClear(&encoded);
  return ok;
}

/* Select one path component. Arrays with fixed-width scalar elements use
** direct addressing; messages use indexed tags; record names use binary
** search. piIndex receives the original field/element ordinal when known.
** Returned views borrow input bytes. Only map-key encoding allocates memory.
*/
static int bsqlSelectStep(
  BsqlView parent, const BsqlPathStep *pStep, BsqlView *pSelected,
  size_t *piIndex, BsqlWork *pWork
){
  BsqlIterator it;
  BsqlEntry entry;
  BsqlAtom key;
  BsqlBuffer encoded;
  const BsqlDefinition *pDefinition;
  const BsqlDefinitionInfo *pInfo;
  const BsqlField *pField;
  const BsqlType *pElement;
  Bebop_FieldDescriptor_Array fields;
  BsqlView element;
  size_t iDefinition;
  size_t iTarget;
  size_t nWidth;
  unsigned kind;
  unsigned elementKind;
  bool bPresent;
  int ok;
  *pSelected = bsqlView(parent.pSchema, 0, bebop_view(0, 0), 0);
  if( piIndex ){
    *piIndex = SIZE_MAX;
  }
  if( !parent.bPresent ){
    return 1;
  }
  kind = bsqlViewKind(parent);
  if( kind == BEBOP_TYPE_KIND_MAP ){
    memset(&encoded, 0, sizeof(encoded));
    ok = bsqlPathKey(parent.pType->map_key.value, pStep, &key, pWork)
      && bsqlEncodeAtom(parent.pSchema, parent.pType->map_key.value,
                         key, &encoded, pWork)
      && bsqlMapLookup(parent, bebop_view(encoded.aData, encoded.nData),
                        pSelected, piIndex, 0, 0, pWork);
    bsqlBufferClear(&encoded);
    return ok;
  }
  if( kind == BSQL_KIND_STRUCT || kind == BSQL_KIND_MESSAGE ){
    if( pStep->eKind != BSQL_PATH_NAME ){
      if( kind == BSQL_KIND_STRUCT ){
        return 1;
      }
      return bsqlFail(&pWork->error, BSQL_INVALID, "path traverses a scalar");
    }
    iDefinition = bsqlDefinitionIndex(parent.pSchema,
                                      parent.pType->defined_fqn.value);
    assert(iDefinition != SIZE_MAX);
    pDefinition = parent.pSchema->apDefinition[iDefinition];
    pInfo = parent.pSchema->aInfo + iDefinition;
    iTarget = bsqlNameFind(pInfo, pStep->u.name);
    if( iTarget == SIZE_MAX ){
      return 1;
    }
    fields = bsqlFields(pDefinition);
    pField = fields.data + iTarget;
    if( piIndex ){
      *piIndex = iTarget;
    }
    *pSelected = bsqlView(parent.pSchema, pField->type.value,
                           bebop_view(0, 0), 0);
    if( !bsqlIteratorInit(parent, &it, pWork) ){
      return 0;
    }
    if( kind == BSQL_KIND_MESSAGE ){
      bPresent = 0;
      if( bebop_message_index_field(&it.message, (uint8_t)pField->index.value,
            &pSelected->bytes, &bPresent) != BEBOP_RESULT_OK ){
        return bsqlFail(&pWork->error, BSQL_INVALID, "malformed message");
      }
      pSelected->bPresent = bPresent;
      return 1;
    }
    while( bsqlIteratorNext(&it, &entry, pWork) ){
      if( entry.iIndex == iTarget ){
        *pSelected = entry.value;
        return 1;
      }
    }
    return pWork->error.eCode == BSQL_OK;
  }
  if( kind == BSQL_KIND_UNION ){
    if( !bsqlIteratorInit(parent, &it, pWork) ){
      return 0;
    }
    if( bsqlIteratorNext(&it, &entry, pWork) &&
        pStep->eKind == BSQL_PATH_NAME && entry.name.data &&
        bsqlStringEqual(entry.name, pStep->u.name) ){
      *pSelected = entry.value;
      if( piIndex ){
        *piIndex = entry.iIndex;
      }
    }
    return pWork->error.eCode == BSQL_OK;
  }
  if( kind != BEBOP_TYPE_KIND_ARRAY && kind != BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "path traverses a scalar");
  }
  if( !bsqlIteratorInit(parent, &it, pWork) ){
    return 0;
  }
  iTarget = SIZE_MAX;
  if( pStep->eKind == BSQL_PATH_INDEX && !pStep->u.index.bNegative
   && !pStep->u.index.bOverflow && pStep->u.index.magnitude < it.nItem ){
    iTarget = (size_t)pStep->u.index.magnitude;
  }else if( pStep->eKind == BSQL_PATH_FROM_END
         && !pStep->u.index.bOverflow && pStep->u.index.magnitude > 0
         && pStep->u.index.magnitude <= it.nItem ){
    iTarget = it.nItem - (size_t)pStep->u.index.magnitude;
  }
  if( iTarget == SIZE_MAX ){
    return 1;
  }
  if( piIndex ){
    *piIndex = iTarget;
  }
  pElement = bsqlElement(parent.pType);
  element = bsqlView(parent.pSchema, pElement, it.remaining, 1);
  elementKind = bsqlStorageKind(element);
  if( elementKind > 0 && elementKind < BSQL_COUNT(bsqlPrimitives)
   && bsqlPrimitives[elementKind].nWire ){
    nWidth = bsqlPrimitives[elementKind].nWire;
    if( it.nItem > it.remaining.length / nWidth ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "truncated array");
    }
    *pSelected = bsqlView(parent.pSchema, pElement,
      bebop_view(it.remaining.data + iTarget*nWidth, nWidth), 1);
    return 1;
  }
  while( bsqlIteratorNext(&it, &entry, pWork) ){
    if( entry.iIndex == iTarget ){
      *pSelected = entry.value;
      return 1;
    }
  }
  return pWork->error.eCode == BSQL_OK;
}

/* Follow a parsed path without allocating path intermediates. Selection may
** allocate map-key buffers. Missing values remain missing through later steps.
*/
static int bsqlFollowPath(
  BsqlView root, const BsqlPath *pPath, BsqlView *pSelected, BsqlWork *pWork
){
  BsqlView child;
  size_t i;
  if( pPath->nStep > BSQL_MAX_PATH_STEPS ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "path traversal depth exceeded");
  }
  *pSelected = root;
  for(i=0; i<pPath->nStep; ++i){
    if( !bsqlVisit(pWork, (unsigned)i + 1) ||
        !bsqlSelectStep(*pSelected, pPath->aStep + i, &child, 0, pWork) ){
      return 0;
    }
    *pSelected = child;
  }
  return 1;
}

/* Operations are classified explicitly; behavior never depends on their
** numeric ordering in this enumeration.
*/
typedef enum BsqlFunctionKind {
  BSQL_FN_TYPE, BSQL_FN_PACK, BSQL_FN_RAW, BSQL_FN_SPEC, BSQL_FN_TYPEOF,
  BSQL_FN_EXTRACT, BSQL_FN_GET, BSQL_FN_KIND, BSQL_FN_EXISTS, BSQL_FN_LENGTH,
  BSQL_FN_VALID, BSQL_FN_ERROR, BSQL_FN_BRANCH, BSQL_FN_ENUM_NAME,
  BSQL_FN_MAP_GET, BSQL_FN_MAP_HAS, BSQL_FN_FROM_JSON, BSQL_FN_TO_JSON,
  BSQL_FN_SET, BSQL_FN_INSERT, BSQL_FN_REPLACE, BSQL_FN_REMOVE,
  BSQL_FN_APPEND, BSQL_FN_MAP_SET, BSQL_FN_MAP_REMOVE,
  BSQL_FN_GROUP_ARRAY, BSQL_FN_GROUP_MAP, BSQL_FN_ARROW, BSQL_FN_ARROW_SCALAR
} BsqlFunctionKind;

/* Per-registration state is SQLite-owned and holds one connection reference. */
typedef struct BsqlFunction BsqlFunction;
struct BsqlFunction {
  BsqlConnection *pConnection;    /* Shared connection-local schema cache. */
  BsqlFunctionKind eKind;         /* Immutable operation selector. */
};

/* Report whether an operation changes a value. No enum-order assumptions. */
static int bsqlIsMutation(BsqlFunctionKind kind){
  switch( kind ){
    case BSQL_FN_SET: case BSQL_FN_INSERT: case BSQL_FN_REPLACE:
    case BSQL_FN_REMOVE: case BSQL_FN_APPEND:
    case BSQL_FN_MAP_SET: case BSQL_FN_MAP_REMOVE:
      return 1;
    default:
      return 0;
  }
}

/* Distinguish path mutations from SQL-key map mutations. */
static int bsqlIsPathMutation(BsqlFunctionKind kind){
  return bsqlIsMutation(kind) && kind != BSQL_FN_MAP_SET
                            && kind != BSQL_FN_MAP_REMOVE;
}

/* Return whether argv[1] is a path for this registered arity and operation. */
static int bsqlNeedsPath(BsqlFunctionKind kind, int argc){
  switch( kind ){
    case BSQL_FN_EXTRACT: case BSQL_FN_GET: case BSQL_FN_EXISTS:
    case BSQL_FN_SET: case BSQL_FN_INSERT: case BSQL_FN_REPLACE:
    case BSQL_FN_REMOVE: case BSQL_FN_APPEND:
      return 1;
    case BSQL_FN_TYPEOF: case BSQL_FN_KIND: case BSQL_FN_LENGTH:
    case BSQL_FN_BRANCH: case BSQL_FN_ENUM_NAME:
      return argc == 2;
    default:
      return 0;
  }
}

/* Preserve SQL NULL propagation except for replacement values and map keys. */
static int bsqlNullIsError(BsqlFunctionKind kind, int iArgument){
  if( iArgument == 0 ){
    return 0;
  }
  switch( kind ){
    case BSQL_FN_MAP_GET: case BSQL_FN_MAP_HAS:
    case BSQL_FN_MAP_SET: case BSQL_FN_MAP_REMOVE:
      return 1;
    case BSQL_FN_SET: case BSQL_FN_INSERT: case BSQL_FN_REPLACE:
    case BSQL_FN_APPEND:
      return iArgument == 2;
    default:
      return 0;
  }
}

/* Read cached path auxdata or allocate a new parsed path. Ownership is returned
** explicitly. Cache insertion must occur only after the caller's final use:
** SQLite may destroy new auxdata immediately when sqlite3_set_auxdata runs.
*/
static const BsqlPath *bsqlSqlPath(
  sqlite3_context *pContext, sqlite3_value *pValue, int iArgument,
  int bAppend, int *pbOwned, BsqlWork *pWork
){
  const BsqlPath *pPath;
  const char *z;
  size_t n;
  *pbOwned = 0;
  pPath = sqlite3_get_auxdata(pContext, iArgument);
  if( pPath ){
    return pPath;
  }
  if( sqlite3_value_type(pValue) != SQLITE_TEXT ){
    bsqlFail(&pWork->error, BSQL_INVALID, "path must be TEXT");
    return 0;
  }
  z = (const char *)sqlite3_value_text(pValue);
  n = (size_t)sqlite3_value_bytes(pValue);
  if( !z ){
    bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
    return 0;
  }
  pPath = bsqlPathParse(z, n, bAppend, &pWork->error);
  *pbOwned = pPath != 0;
  return pPath;
}

/* Release one function registration and its connection ownership. */
static void bsqlFunctionRelease(void *pPointer){
  BsqlFunction *p;
  p = pPointer;
  bsqlConnectionRelease(p->pConnection);
  sqlite3_free(p);
}

static int bsqlMutate(
  BsqlView, const BsqlPath *, size_t, BsqlFunctionKind, BsqlAtom,
  BsqlBuffer *, unsigned, BsqlWork *
);

/* Rebuild an array/map by copying the unchanged prefix and suffix once.
** iBegin/iEnd include the old count prefix in their offsets. Only the changed
** region needs an encoded buffer. Inputs must not alias pOut's allocation.
** Reservation checks all arithmetic before any logical output is appended.
*/
static int bsqlSpliceSequence(
  BsqlView root, size_t nItem, size_t iBegin, size_t iEnd,
  Bebop_View key, Bebop_View replacement, BsqlBuffer *pOut, BsqlWork *pWork
){
  unsigned kind;
  size_t prefix;
  size_t nNeed;
  kind = bsqlTypeKind(root.pType);
  prefix = kind == BEBOP_TYPE_KIND_FIXED_ARRAY ? 0 : BEBOP_WIRE_SIZE_LEN;
  if( iBegin < prefix || iEnd < iBegin || iEnd > root.bytes.length ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "invalid mutation slice");
  }
  if( nItem > BSQL_MAX_ITEMS ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "container item limit exceeded");
  }
  nNeed = root.bytes.length - (iEnd - iBegin);
  if( nNeed > bsqlMaxValueBytes || key.length > bsqlMaxValueBytes - nNeed ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "value byte limit exceeded");
  }
  nNeed += key.length;
  if( replacement.length > bsqlMaxValueBytes - nNeed ){
    return bsqlFail(&pWork->error, BSQL_LIMIT, "value byte limit exceeded");
  }
  nNeed += replacement.length;
  if( !bsqlReserve(pOut, nNeed, &pWork->error) ){
    return 0;
  }
  return (!prefix ||
           bsqlAppendLe(pOut, nItem, BEBOP_WIRE_SIZE_LEN, &pWork->error))
      && bsqlAppend(pOut, root.bytes.data ? root.bytes.data + prefix : 0,
                      iBegin - prefix, &pWork->error)
      && bsqlAppend(pOut, key.data, key.length, &pWork->error)
      && bsqlAppend(pOut, replacement.data, replacement.length, &pWork->error)
      && bsqlAppend(pOut, root.bytes.data ? root.bytes.data + iEnd : 0,
                      root.bytes.length - iEnd, &pWork->error);
}

/* Mutate one map entry after a single lookup. A non-NULL path recursively
** mutates the selected value; otherwise perform the requested leaf operation.
** The already validated map is spliced, not materialized as per-entry Parts.
** New buffers use SQLite and are always released on failure or success.
*/
static int bsqlMutateMap(
  BsqlView root, BsqlAtom key, BsqlAtom replacement,
  BsqlFunctionKind operation, const BsqlPath *pPath, size_t iNext,
  BsqlBuffer *pOut, unsigned nDepth, BsqlWork *pWork
){
  BsqlBuffer encodedKey;
  BsqlBuffer encodedValue;
  BsqlView selected;
  Bebop_View keyBytes;
  Bebop_View valueBytes;
  size_t begin;
  size_t end;
  size_t nItem;
  int bRemove;
  int ok;
  if( bsqlTypeKind(root.pType) != BEBOP_TYPE_KIND_MAP ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "expected a map");
  }
  memset(&encodedKey, 0, sizeof(encodedKey));
  memset(&encodedValue, 0, sizeof(encodedValue));
  ok = bsqlEncodeAtom(root.pSchema, root.pType->map_key.value, key,
                       &encodedKey, pWork)
    && bsqlMapLookup(root, bebop_view(encodedKey.aData, encodedKey.nData),
                      &selected, 0, &begin, &end, pWork);
  if( !ok ){
    goto cleanup;
  }
  bRemove = !pPath && (operation == BSQL_FN_REMOVE ||
                       operation == BSQL_FN_MAP_REMOVE);
  if( pPath && !selected.bPresent ){
    ok = bsqlFail(&pWork->error, BSQL_INVALID, "missing parent");
    goto cleanup;
  }
  if( !pPath && ((operation == BSQL_FN_INSERT && selected.bPresent) ||
      ((operation == BSQL_FN_REPLACE || bRemove) && !selected.bPresent)) ){
    ok = bsqlAppend(pOut, root.bytes.data, root.bytes.length, &pWork->error);
    goto cleanup;
  }
  nItem = (size_t)bsqlLoadLe(root.bytes.data, BEBOP_WIRE_SIZE_LEN);
  if( bRemove ){
    assert(nItem > 0);
    --nItem;
    keyBytes = bebop_view(0, 0);
    valueBytes = bebop_view(0, 0);
  }else{
    if( !selected.bPresent ){
      if( nItem == BSQL_MAX_ITEMS ){
        ok = bsqlFail(&pWork->error, BSQL_LIMIT,
                       "container item limit exceeded");
        goto cleanup;
      }
      ++nItem;
    }
    if( pPath ){
      ok = bsqlMutate(selected, pPath, iNext, operation, replacement,
                       &encodedValue, nDepth + 1, pWork);
    }else{
      ok = bsqlEncodeAtom(root.pSchema, root.pType->map_value.value,
                           replacement, &encodedValue, pWork);
    }
    if( !ok ){
      goto cleanup;
    }
    keyBytes = bebop_view(encodedKey.aData, encodedKey.nData);
    valueBytes = bebop_view(encodedValue.aData, encodedValue.nData);
  }
  ok = bsqlSpliceSequence(root, nItem, begin, end, keyBytes, valueBytes,
                           pOut, pWork);
cleanup:
  bsqlBufferClear(&encodedKey);
  bsqlBufferClear(&encodedValue);
  return ok;
}

/* Locate an array element's byte interval. Scalar elements use arithmetic;
** variable-width elements are scanned only up to the selected ordinal.
** Caller has already validated the entire array before mutation.
*/
static int bsqlArraySlice(
  BsqlView root, BsqlIterator *pIterator, size_t iTarget,
  BsqlView *pSelected, size_t *piBegin, size_t *piEnd, BsqlWork *pWork
){
  const BsqlType *pElement;
  BsqlEntry entry;
  unsigned kind;
  size_t nWidth;
  size_t prefix;
  pElement = bsqlElement(root.pType);
  kind = bsqlStorageKind(bsqlView(root.pSchema, pElement,
                                 pIterator->remaining, 1));
  if( kind > 0 && kind < BSQL_COUNT(bsqlPrimitives)
   && bsqlPrimitives[kind].nWire ){
    nWidth = bsqlPrimitives[kind].nWire;
    if( pIterator->nItem > pIterator->remaining.length / nWidth ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "truncated array");
    }
    prefix = root.bytes.length - pIterator->remaining.length;
    *piBegin = prefix + iTarget * nWidth;
    *piEnd = *piBegin + nWidth;
    *pSelected = bsqlView(root.pSchema, pElement,
      bebop_view(root.bytes.data + *piBegin, nWidth), 1);
    return 1;
  }
  while( pIterator->iPosition <= iTarget ){
    *piBegin = root.bytes.length - pIterator->remaining.length;
    if( !bsqlIteratorNext(pIterator, &entry, pWork) ){
      return pWork->error.eCode != BSQL_OK ? 0 :
        bsqlFail(&pWork->error, BSQL_INVALID, "missing array element");
    }
    if( entry.iIndex == iTarget ){
      *piEnd = root.bytes.length - pIterator->remaining.length;
      *pSelected = entry.value;
      return 1;
    }
  }
  return bsqlFail(&pWork->error, BSQL_INVALID, "invalid array index");
}

/* Mutate an array through one encoded replacement plus a prefix/suffix splice.
** Fixed arrays cannot resize. Explicit append/from-end/index behavior matches
** the public path operations. No allocation is proportional to element count.
*/
static int bsqlMutateArray(
  BsqlView root, const BsqlPath *pPath, size_t iPosition,
  BsqlFunctionKind operation, BsqlAtom replacement, BsqlBuffer *pOut,
  unsigned nDepth, BsqlWork *pWork
){
  BsqlIterator it;
  BsqlView selected;
  BsqlBuffer encoded;
  const BsqlPathStep *pStep;
  const BsqlType *pElement;
  size_t target;
  size_t begin;
  size_t end;
  size_t nItem;
  int bAppendHere;
  int bLeaf;
  int bFixed;
  int ok;
  memset(&encoded, 0, sizeof(encoded));
  bAppendHere = iPosition == pPath->nStep;
  bFixed = bsqlTypeKind(root.pType) == BEBOP_TYPE_KIND_FIXED_ARRAY;
  if( bAppendHere && bFixed ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "append requires a variable array");
  }
  if( !bsqlIteratorInit(root, &it, pWork) ){
    return 0;
  }
  pElement = bsqlElement(root.pType);
  pStep = bAppendHere ? 0 : pPath->aStep + iPosition;
  bLeaf = bAppendHere || iPosition + 1 == pPath->nStep;
  target = SIZE_MAX;
  if( bAppendHere || pStep->eKind == BSQL_PATH_APPEND ){
    target = it.nItem;
  }else if( pStep->eKind == BSQL_PATH_INDEX && !pStep->u.index.bNegative
         && !pStep->u.index.bOverflow && pStep->u.index.magnitude <= it.nItem ){
    target = (size_t)pStep->u.index.magnitude;
  }else if( pStep->eKind == BSQL_PATH_FROM_END && !pStep->u.index.bOverflow
         && pStep->u.index.magnitude > 0
         && pStep->u.index.magnitude <= it.nItem ){
    target = it.nItem - (size_t)pStep->u.index.magnitude;
  }
  if( target == SIZE_MAX ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "array index out of range");
  }
  if( target == it.nItem && !bAppendHere && pStep->eKind != BSQL_PATH_APPEND
   && operation != BSQL_FN_INSERT && operation != BSQL_FN_REMOVE ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "array index must exist");
  }
  nItem = it.nItem;
  ok = 0;
  if( target == it.nItem ){
    if( !bLeaf || (!bAppendHere && operation == BSQL_FN_APPEND) ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "missing parent");
    }
    if( operation == BSQL_FN_REMOVE || operation == BSQL_FN_REPLACE ){
      return bsqlAppend(pOut, root.bytes.data, root.bytes.length,
        &pWork->error);
    }
    if( bFixed ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "cannot resize fixed array");
    }
    if( nItem == BSQL_MAX_ITEMS ){
      return bsqlFail(&pWork->error, BSQL_LIMIT,
        "container item limit exceeded");
    }
    ++nItem;
    begin = root.bytes.length;
    end = begin;
    ok = bsqlEncodeAtom(root.pSchema, pElement, replacement, &encoded, pWork);
  }else{
    if( !bsqlArraySlice(root, &it, target, &selected, &begin, &end, pWork) ){
      return 0;
    }
    if( bLeaf && (operation == BSQL_FN_REMOVE || operation == BSQL_FN_INSERT) ){
      if( bFixed ){
        return bsqlFail(&pWork->error, BSQL_INVALID,
                         "cannot resize fixed array");
      }
      if( operation == BSQL_FN_REMOVE ){
        --nItem;
        ok = 1;
      }else{
        if( nItem == BSQL_MAX_ITEMS ){
          return bsqlFail(&pWork->error, BSQL_LIMIT,
                           "container item limit exceeded");
        }
        ++nItem;
        end = begin;
        ok = bsqlEncodeAtom(root.pSchema, pElement, replacement,
                             &encoded, pWork);
      }
    }else{
      ok = bsqlMutate(selected, pPath, iPosition + 1, operation, replacement,
                       &encoded, nDepth + 1, pWork);
    }
  }
  if( ok ){
    ok = bsqlSpliceSequence(root, nItem, begin, end, bebop_view(0, 0),
      bebop_view(encoded.aData, encoded.nData), pOut, pWork);
  }
  bsqlBufferClear(&encoded);
  return ok;
}

/* Mutate a validated value at a parsed path. Arrays/maps use streaming splices;
** records borrow unchanged fields and rebuild only the record envelope.
** Identity is an ordinal/tag, not pointer equality, so zero-width fields are
** handled correctly. Recursion, allocation, and cleanup remain bounded.
*/
static int bsqlMutate(
  BsqlView root, const BsqlPath *pPath, size_t iPosition,
  BsqlFunctionKind operation, BsqlAtom replacement, BsqlBuffer *pOut,
  unsigned nDepth, BsqlWork *pWork
){
  const BsqlPathStep *pStep;
  const BsqlType *pTarget;
  const BsqlDefinition *pDefinition;
  Bebop_FieldDescriptor_Array fields;
  BsqlView selected;
  BsqlAtom key;
  BsqlIterator it;
  BsqlEntry entry;
  BsqlParts parts;
  BsqlPart *pPart;
  size_t iSelected;
  unsigned kind;
  uint8_t targetTag;
  int bLeaf;
  int bMatch;
  int ok;
  if( !bsqlVisit(pWork, nDepth) ){
    return 0;
  }
  if( iPosition == pPath->nStep && operation != BSQL_FN_APPEND ){
    if( operation == BSQL_FN_REMOVE || operation == BSQL_FN_INSERT ){
      return bsqlFail(&pWork->error, BSQL_INVALID,
        operation == BSQL_FN_REMOVE ? "cannot remove root" :
                                      "cannot insert root");
    }
    return bsqlEncodeAtom(root.pSchema, root.pType, replacement, pOut, pWork);
  }
  kind = bsqlViewKind(root);
  if( kind == BEBOP_TYPE_KIND_ARRAY || kind == BEBOP_TYPE_KIND_FIXED_ARRAY ){
    return bsqlMutateArray(root, pPath, iPosition, operation, replacement,
                            pOut, nDepth, pWork);
  }
  if( iPosition == pPath->nStep ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "append requires a variable array");
  }
  pStep = pPath->aStep + iPosition;
  bLeaf = iPosition + 1 == pPath->nStep;
  if( kind == BEBOP_TYPE_KIND_MAP ){
    if( !bsqlPathKey(root.pType->map_key.value, pStep, &key, pWork) ){
      return 0;
    }
    return bsqlMutateMap(root, key, replacement, operation,
      bLeaf && operation != BSQL_FN_APPEND ? 0 : pPath,
      iPosition + 1, pOut, nDepth, pWork);
  }
  if( kind != BSQL_KIND_STRUCT && kind != BSQL_KIND_MESSAGE
   && kind != BSQL_KIND_UNION ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "path traverses a scalar");
  }
  if( !bsqlSelectStep(root, pStep, &selected, &iSelected, pWork) ){
    return 0;
  }
  if( !selected.bPresent && (!bLeaf || operation == BSQL_FN_APPEND) ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "missing parent");
  }
  pTarget = selected.pType;
  targetTag = 0;
  if( kind == BSQL_KIND_STRUCT || kind == BSQL_KIND_MESSAGE ){
    if( iSelected == SIZE_MAX || !pTarget ){
      return bsqlFail(&pWork->error, BSQL_INVALID, "unknown field");
    }
    pDefinition = bsqlDefinitionFind(root.pSchema,
      root.pType->defined_fqn.value);
    fields = bsqlFields(pDefinition);
    if( kind == BSQL_KIND_MESSAGE ){
      targetTag = (uint8_t)fields.data[iSelected].index.value;
    }
  }
  if( bLeaf && ((operation == BSQL_FN_INSERT && selected.bPresent) ||
      ((operation == BSQL_FN_REMOVE || operation == BSQL_FN_REPLACE)
       && !selected.bPresent)) ){
    return bsqlAppend(pOut, root.bytes.data, root.bytes.length, &pWork->error);
  }
  if( bLeaf && operation == BSQL_FN_REMOVE && selected.bPresent
   && (kind == BSQL_KIND_STRUCT || kind == BSQL_KIND_UNION) ){
    return bsqlFail(&pWork->error, BSQL_INVALID,
                     "cannot remove required field or union branch");
  }
  if( !selected.bPresent && (kind != BSQL_KIND_MESSAGE || !pTarget) ){
    return bsqlFail(&pWork->error, BSQL_INVALID, "cannot insert at this path");
  }
  memset(&parts, 0, sizeof(parts));
  ok = bsqlIteratorInit(root, &it, pWork);
  while( ok && bsqlIteratorNext(&it, &entry, pWork) ){
    bMatch = selected.bPresent && (kind == BSQL_KIND_MESSAGE ?
      entry.tag == targetTag : entry.iIndex == iSelected);
    if( bMatch && bLeaf && operation == BSQL_FN_REMOVE ){
      continue;
    }
    pPart = bsqlPartsPush(&parts, pWork);
    if( !pPart ){
      ok = 0;
      break;
    }
    pPart->tag = entry.tag;
    pPart->iField = entry.iIndex;
    if( bMatch ){
      ok = bsqlMutate(entry.value, pPath, iPosition + 1, operation,
                       replacement, &pPart->value, nDepth + 1, pWork);
    }else{
      pPart->value = bsqlBorrow(entry.value.bytes);
    }
  }
  if( ok && pWork->error.eCode == BSQL_OK && !selected.bPresent ){
    pPart = bsqlPartsPush(&parts, pWork);
    if( !pPart ){
      ok = 0;
    }else{
      pPart->tag = targetTag;
      pPart->iField = iSelected;
      ok = bsqlEncodeAtom(root.pSchema, pTarget, replacement,
                           &pPart->value, pWork);
    }
  }
  if( ok && pWork->error.eCode == BSQL_OK ){
    ok = bsqlAssembleRecord(root, &parts, pOut, pWork);
  }else{
    ok = 0;
  }
  bsqlPartsClear(&parts);
  return ok;
}

/* Implement ->/->> operand shorthand without changing normal path grammar.
** The returned path is separately SQLite-owned; the temporary buffer may be
** reused or freed immediately. Invalid operands and OOM return NULL.
*/
static BsqlPath *bsqlOperatorPath(
  BsqlAtom operand, BsqlBuffer *pTemporary, BsqlWork *pWork
){
  char zNumber[BSQL_NUMBER_TEXT_BYTES];
  sqlite3_uint64 magnitude;
  if( pWork->error.eCode != BSQL_OK ){
    return 0;
  }
  if( operand.eKind == SQLITE_INTEGER ){
    if( operand.integer < 0 ){
      magnitude = (sqlite3_uint64)(-(operand.integer + 1)) + 1;
      sqlite3_snprintf(sizeof(zNumber), zNumber, "$[#-%llu]", magnitude);
    }else{
      sqlite3_snprintf(sizeof(zNumber), zNumber, "$[%lld]", operand.integer);
    }
    if( !bsqlAppend(pTemporary, zNumber, strlen(zNumber), &pWork->error) ){
      return 0;
    }
  }else if( operand.eKind == SQLITE_TEXT ){
    if( operand.nData && operand.aData[0] == '$' ){
      if( !bsqlAppend(pTemporary, operand.aData, operand.nData,
        &pWork->error) ){
        return 0;
      }
    }else if( !BSQL_APPEND_LITERAL(pTemporary, "$.", &pWork->error) ||
              !bsqlJsonQuote(pTemporary, operand.aData, operand.nData, pWork) ){
      return 0;
    }
  }else{
    bsqlFail(&pWork->error, BSQL_INVALID,
               "operator path requires TEXT or INTEGER");
    return 0;
  }
  return bsqlPathParse((const char *)pTemporary->aData, pTemporary->nData,
                        0, &pWork->error);
}

/* Return a union's active branch name, including #tag for an unknown branch.
** Iterator errors are reported in pWork. Result text is copied by SQLite.
*/
static void bsqlBranchResult(
  sqlite3_context *pContext, BsqlView view, BsqlWork *pWork
){
  BsqlIterator it;
  BsqlEntry entry;
  char zUnknown[BSQL_NUMBER_TEXT_BYTES];
  if( bsqlViewKind(view) != BSQL_KIND_UNION ){
    bsqlFail(&pWork->error, BSQL_INVALID, "branch requires a union");
    return;
  }
  if( bsqlIteratorInit(view, &it, pWork) &&
      bsqlIteratorNext(&it, &entry, pWork) ){
    if( entry.name.data ){
      sqlite3_result_text64(pContext, entry.name.data, entry.name.length,
                             SQLITE_TRANSIENT, SQLITE_UTF8);
    }else{
      sqlite3_snprintf(sizeof(zUnknown), zUnknown, "#%u", entry.tag);
      sqlite3_result_text(pContext, zUnknown, -1, SQLITE_TRANSIENT);
    }
  }
}

/* Return the first declared enum member matching the stored bits. Aliased
** numeric values preserve declaration-order semantics; no match yields NULL.
*/
static void bsqlEnumNameResult(
  sqlite3_context *pContext, BsqlView view, BsqlWork *pWork
){
  const Bebop_EnumDef *pEnum;
  const Bebop_EnumMemberDescriptor *pMember;
  uint64_t value;
  uint64_t mask;
  unsigned nByte;
  size_t i;
  if( bsqlViewKind(view) != BSQL_KIND_ENUM ){
    bsqlFail(&pWork->error, BSQL_INVALID, "enum_name requires an enum");
    return;
  }
  pEnum = bsqlDefinitionFind(view.pSchema,
                             view.pType->defined_fqn.value)->enum_def.value;
  nByte = bsqlPrimitives[pEnum->base_type.value].nWire;
  value = bsqlLoadLe(view.bytes.data, nByte);
  mask = nByte == sizeof(uint64_t) ? UINT64_MAX :
         (UINT64_C(1) << (nByte * CHAR_BIT)) - 1;
  for(i=0; i<pEnum->members.value.length; ++i){
    pMember = pEnum->members.value.data + i;
    if( (pMember->value.value & mask) == value ){
      sqlite3_result_text64(pContext, pMember->name.value.data,
        pMember->name.value.length, SQLITE_TRANSIENT, SQLITE_UTF8);
      return;
    }
  }
  sqlite3_result_null(pContext);
}

/* Return byte length for strings or child count for containers. This function
** assumes the selected view has already been measured as a complete value.
*/
static void bsqlLengthResult(
  sqlite3_context *pContext, BsqlView view, BsqlWork *pWork
){
  BsqlIterator it;
  if( bsqlTypeKind(view.pType) == BEBOP_TYPE_KIND_STRING ){
    sqlite3_result_int64(pContext,
      (sqlite3_int64)bsqlLoadLe(view.bytes.data, BEBOP_WIRE_SIZE_LEN));
  }else if( bsqlIterable(view) ){
    if( bsqlIteratorInit(view, &it, pWork) ){
      sqlite3_result_int64(pContext, (sqlite3_int64)it.nItem);
    }
  }else{
    bsqlFail(&pWork->error, BSQL_INVALID,
               "length requires string, bytes, array, or map");
  }
}

/* Implement bebop_type without mixing descriptor compilation with value
** traversal. All temporary schema and output storage uses SQLite allocation.
*/
static void bsqlTypeFunction(
  sqlite3_context *pContext, sqlite3_value **argv, BsqlWork *pWork
){
  BsqlSchema *pSchema;
  BsqlTypeParser parser;
  BsqlType *pType;
  BsqlBuffer out;
  Bebop_View descriptor;
  const char *zName;
  size_t nName;
  size_t nBudget;
  if( !bsqlBlobArgument(argv[0], &descriptor, &pWork->error) ){
    return;
  }
  pSchema = bsqlSchemaDecode(descriptor, &pWork->error);
  if( !pSchema ){
    return;
  }
  memset(&out, 0, sizeof(out));
  if( sqlite3_value_type(argv[1]) != SQLITE_TEXT ){
    bsqlFail(&pWork->error, BSQL_INVALID, "root type must be TEXT");
    goto cleanup;
  }
  zName = (const char *)sqlite3_value_text(argv[1]);
  nName = (size_t)sqlite3_value_bytes(argv[1]);
  if( !zName ){
    bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
    goto cleanup;
  }
  if( nName > BSQL_MAX_TYPE_BYTES || memchr(zName, 0, nName) ){
    bsqlFail(&pWork->error, nName > BSQL_MAX_TYPE_BYTES ? BSQL_LIMIT :
               BSQL_INVALID, "invalid root type expression");
    goto cleanup;
  }
  parser.pSchema = pSchema;
  parser.zCurrent = zName;
  parser.zEnd = zName + nName;
  parser.pError = &pWork->error;
  pType = bsqlParseType(&parser, 0);
  if( pType && parser.zCurrent != parser.zEnd ){
    bsqlFail(&pWork->error, BSQL_INVALID, "invalid root type suffix");
  }
  nBudget = BSQL_MAX_ITEMS;
  if( pType && pWork->error.eCode == BSQL_OK &&
      bsqlValidateType(pSchema, pType, 0, &nBudget, &pWork->error) &&
      bsqlMakeSpec(pSchema, pType, &out, &pWork->error) ){
    bsqlResultTake(pContext, &out, 0);
  }
cleanup:
  bsqlBufferClear(&out);
  bsqlSchemaRelease(pSchema);
}

/* Pack raw wire bytes or strict JSON using a supplied canonical specification.
** JSON already performs final validation; raw input is validated here. The
** completed result allocation is transferred directly to SQLite.
*/
static void bsqlPackFunction(
  sqlite3_context *pContext, BsqlConnection *pConnection,
  BsqlFunctionKind operation, sqlite3_value **argv, BsqlWork *pWork
){
  BsqlSchema *pSchema;
  BsqlBuffer payloadBuffer;
  BsqlBuffer out;
  Bebop_View spec;
  Bebop_View payload;
  BsqlAtom json;
  int ok;
  pSchema = 0;
  memset(&payloadBuffer, 0, sizeof(payloadBuffer));
  memset(&out, 0, sizeof(out));
  if( !bsqlBlobArgument(argv[1], &spec, &pWork->error) ){
    goto cleanup;
  }
  pSchema = bsqlCachedSchema(pConnection, spec, &pWork->error);
  if( !pSchema ){
    goto cleanup;
  }
  if( operation == BSQL_FN_PACK ){
    ok = bsqlBlobArgument(argv[0], &payload, &pWork->error);
    if( ok ){
      ok = bsqlValidateView(bsqlView(pSchema, &pSchema->root, payload, 1),
                             pWork);
    }
  }else{
    json = bsqlSqlAtom(argv[0], pWork);
    ok = bsqlFromJson(pSchema, json, &payloadBuffer, pWork);
    payload = bebop_view(payloadBuffer.aData, payloadBuffer.nData);
  }
  if( ok && bsqlWrapValue(spec, payload, &out, &pWork->error) ){
    bsqlResultTake(pContext, &out, 0);
  }
cleanup:
  bsqlBufferClear(&out);
  bsqlBufferClear(&payloadBuffer);
  bsqlSchemaRelease(pSchema);
}

/* Dispatch scalar SQL functions. All exit paths release retained schemas,
** temporary buffers, and JSON statements. Newly parsed auxdata is cached only
** after its last use. Diagnostic functions suppress only data-validation
** errors, never OOM, interruption, or unrelated SQLite failures.
*/
static void bsqlSqlFunction(
  sqlite3_context *pContext, int argc, sqlite3_value **argv
){
  BsqlFunction *pFunction;
  BsqlConnection *pConnection;
  BsqlFunctionKind operation;
  BsqlWork work;
  BsqlSchema *pSchema;
  BsqlView root;
  BsqlView selected;
  BsqlView changed;
  BsqlBuffer out;
  BsqlBuffer temporary;
  const BsqlPath *pPath;
  BsqlAtom key;
  BsqlAtom replacement;
  BsqlAtom operand;
  size_t n;
  int i;
  int bOwnedPath;
  int bOperatorPath;
  int ok;
  pFunction = sqlite3_user_data(pContext);
  pConnection = pFunction->pConnection;
  operation = pFunction->eKind;
  work = bsqlWork(pConnection->db);
  pSchema = 0;
  root = bsqlView(0, 0, bebop_view(0, 0), 0);
  selected = root;
  memset(&out, 0, sizeof(out));
  memset(&temporary, 0, sizeof(temporary));
  memset(&replacement, 0, sizeof(replacement));
  pPath = 0;
  bOwnedPath = 0;
  bOperatorPath = 0;
  for(i=0; i<argc; ++i){
    if( sqlite3_value_type(argv[i]) == SQLITE_NULL ){
      if( bsqlNullIsError(operation, i) ){
        sqlite3_result_error(pContext,
          "Bebop replacement and map keys cannot be NULL", -1);
      }else{
        sqlite3_result_null(pContext);
      }
      return;
    }
  }
  if( operation == BSQL_FN_TYPE ){
    bsqlTypeFunction(pContext, argv, &work);
    goto done;
  }
  if( operation == BSQL_FN_PACK || operation == BSQL_FN_FROM_JSON ){
    bsqlPackFunction(pContext, pConnection, operation, argv, &work);
    goto done;
  }
  if( !bsqlOpenValue(pConnection, argv[0], &pSchema, &root, &work) ){
    goto done;
  }
  selected = root;
  if( operation == BSQL_FN_VALID || operation == BSQL_FN_ERROR ){
    bsqlValidateView(root, &work);
    goto done;
  }
  if( operation == BSQL_FN_ARROW || operation == BSQL_FN_ARROW_SCALAR ){
    operand = bsqlSqlAtom(argv[1], &work);
    pPath = bsqlOperatorPath(operand, &temporary, &work);
    bOperatorPath = 1;
    operation = operation == BSQL_FN_ARROW ? BSQL_FN_GET : BSQL_FN_EXTRACT;
    if( !pPath || !bsqlFollowPath(root, pPath, &selected, &work) ){
      goto done;
    }
  }else if( bsqlNeedsPath(operation, argc) ){
    pPath = bsqlSqlPath(pContext, argv[1], 1,
                         bsqlIsPathMutation(operation), &bOwnedPath, &work);
    if( !pPath ){
      goto done;
    }
    if( !bsqlIsPathMutation(operation) &&
        !bsqlFollowPath(root, pPath, &selected, &work) ){
      goto done;
    }
  }
  if( operation == BSQL_FN_MAP_GET || operation == BSQL_FN_MAP_HAS ){
    key = bsqlSqlAtom(argv[1], &work);
    if( !bsqlMapFind(root, key, &selected, &work) ){
      goto done;
    }
    if( operation == BSQL_FN_MAP_HAS ){
      sqlite3_result_int(pContext, selected.bPresent);
    }else{
      bsqlScalarResult(pContext, selected, &work);
    }
    goto done;
  }
  if( bsqlIsMutation(operation) ){
    if( !bsqlValidateView(root, &work) ){
      goto done;
    }
    if( argc == 3 ){
      replacement = bsqlSqlAtom(argv[2], &work);
    }
    if( operation == BSQL_FN_MAP_SET || operation == BSQL_FN_MAP_REMOVE ){
      key = bsqlSqlAtom(argv[1], &work);
      ok = bsqlMutateMap(root, key, replacement, operation, 0, 0,
                          &temporary, 0, &work);
    }else{
      assert(pPath != 0);
      ok = bsqlMutate(root, pPath, 0, operation, replacement,
                       &temporary, 0, &work);
    }
    changed = bsqlView(pSchema, &pSchema->root,
                        bebop_view(temporary.aData, temporary.nData), 1);
    if( ok && bsqlValidateView(changed, &work) &&
        bsqlWrapValue(bebop_view(pSchema->aKey, pSchema->nKey), changed.bytes,
                        &out, &work.error) ){
      bsqlResultTake(pContext, &out, 0);
    }
    goto done;
  }
  if( selected.bPresent ){
    if( !bsqlMeasure(selected, 0, 0, &n, &work) ){
      goto done;
    }
    if( n != selected.bytes.length ){
      bsqlFail(&work.error, BSQL_INVALID, "trailing selected value bytes");
      goto done;
    }
  }
  if( operation == BSQL_FN_EXISTS ){
    sqlite3_result_int(pContext, selected.bPresent);
    goto done;
  }
  if( !selected.bPresent ){
    sqlite3_result_null(pContext);
    goto done;
  }
  switch( operation ){
    case BSQL_FN_RAW:
      sqlite3_result_blob64(pContext, root.bytes.data, root.bytes.length,
                             SQLITE_TRANSIENT);
      break;
    case BSQL_FN_SPEC:
      sqlite3_result_blob64(pContext, pSchema->aKey, pSchema->nKey,
                             SQLITE_TRANSIENT);
      break;
    case BSQL_FN_TYPEOF:
      if( bsqlTypeText(selected.pType, &out, 0, &work.error) ){
        bsqlResultTake(pContext, &out, 1);
      }
      break;
    case BSQL_FN_KIND:
      sqlite3_result_text(pContext, bsqlKindText(selected), -1, SQLITE_STATIC);
      break;
    case BSQL_FN_EXTRACT:
      bsqlScalarResult(pContext, selected, &work);
      break;
    case BSQL_FN_GET:
      if( bsqlTypedResult(selected, &out, &work) ){
        bsqlResultTake(pContext, &out, 0);
      }
      break;
    case BSQL_FN_LENGTH:
      bsqlLengthResult(pContext, selected, &work);
      break;
    case BSQL_FN_BRANCH:
      bsqlBranchResult(pContext, selected, &work);
      break;
    case BSQL_FN_ENUM_NAME:
      bsqlEnumNameResult(pContext, selected, &work);
      break;
    case BSQL_FN_TO_JSON:
      if( bsqlValidateView(root, &work) && bsqlWriteJson(root, &out, 0,
        &work) ){
        bsqlResultTake(pContext, &out, 1);
      }
      break;
    default:
      bsqlFail(&work.error, BSQL_INVALID, "unsupported function operation");
      break;
  }
done:
  if( pFunction->eKind == BSQL_FN_VALID ){
    if( work.error.eCode != BSQL_OK && work.error.eCode != BSQL_INVALID ){
      bsqlResultError(pContext, &work.error);
    }else{
      sqlite3_result_int(pContext, work.error.eCode == BSQL_OK);
    }
  }else if( pFunction->eKind == BSQL_FN_ERROR ){
    if( work.error.eCode == BSQL_INVALID || work.error.eCode == BSQL_LIMIT ){
      sqlite3_result_text(pContext, work.error.zMessage, -1, SQLITE_TRANSIENT);
    }else if( work.error.eCode != BSQL_OK ){
      bsqlResultError(pContext, &work.error);
    }else{
      sqlite3_result_null(pContext);
    }
  }else if( work.error.eCode != BSQL_OK ){
    bsqlResultError(pContext, &work.error);
  }
  bsqlWorkClear(&work);
  bsqlBufferClear(&out);
  bsqlBufferClear(&temporary);
  bsqlSchemaRelease(pSchema);
  if( pPath ){
    if( bOperatorPath ){
      bsqlPathFree((void *)pPath);
    }else if( bOwnedPath ){
      sqlite3_set_auxdata(pContext, 1, (void *)pPath, bsqlPathFree);
    }
  }
}

/* Offsets stay valid while the aggregate's growable result buffer relocates. */
typedef struct BsqlKeyOffset BsqlKeyOffset;
struct BsqlKeyOffset {
  size_t iByte;                  /* Key offset from the typed value's start. */
  size_t nByte;                  /* Encoded key length. */
};

/* Aggregate rows append directly to the eventual typed result. Finalization
** only patches count/length fields and transfers the allocation to SQLite.
*/
typedef struct BsqlAggregate BsqlAggregate;
struct BsqlAggregate {
  BsqlSchema *pSchema;            /* Retained specification for the group. */
  BsqlBuffer value;              /* Complete envelope plus growing payload. */
  BsqlKeyOffset *aKey;            /* Map key positions, NULL for arrays. */
  size_t nKeyAlloc;               /* Capacity of aKey. */
  size_t nItem;                   /* Successfully encoded input rows. */
  size_t iPayload;                /* Count prefix within value. */
  size_t nRemaining;              /* Group-wide traversal budget. */
  BsqlError error;                /* Retained first step failure. */
  int bFailed;                    /* Later steps cannot publish partial data. */
};

/* Ensure room for one map key offset. SQLite OOM leaves old storage intact. */
static int bsqlAggregateKeyReserve(BsqlAggregate *p, BsqlWork *pWork){
  size_t nAlloc;
  BsqlKeyOffset *a;
  if( p->nItem < p->nKeyAlloc ){
    return 1;
  }
  nAlloc = p->nKeyAlloc ? p->nKeyAlloc * 2 : BSQL_MIN_KEYS;
  if( nAlloc > BSQL_MAX_ITEMS ){
    nAlloc = BSQL_MAX_ITEMS;
  }
  if( nAlloc <= p->nItem ){
    return bsqlFail(&pWork->error, BSQL_LIMIT,
      "aggregate element limit exceeded");
  }
  a = sqlite3_realloc64(p->aKey, (sqlite3_uint64)nAlloc * sizeof(*a));
  if( !a ){
    return bsqlFail(&pWork->error, BSQL_NOMEM, "out of memory");
  }
  p->aKey = a;
  p->nKeyAlloc = nAlloc;
  return 1;
}

/* Encode one aggregate row. The first row retains the schema and creates the
** final envelope; later rows must supply the identical specification. Payload
** and key-index storage use SQLite. The first failure is saved for xFinal.
*/
static void bsqlAggregateStep(
  sqlite3_context *pContext, int argc, sqlite3_value **argv
){
  BsqlFunction *pFunction;
  BsqlAggregate *p;
  BsqlWork work;
  const BsqlType *pType;
  BsqlAtom atom;
  Bebop_View spec;
  uint8_t countBytes[BEBOP_WIRE_SIZE_LEN];
  size_t start;
  int bMap;
  pFunction = sqlite3_user_data(pContext);
  work = bsqlWork(pFunction->pConnection->db);
  p = sqlite3_aggregate_context(pContext, (int)sizeof(*p));
  if( !p ){
    sqlite3_result_error_nomem(pContext);
    return;
  }
  if( p->bFailed ){
    return;
  }
  bMap = pFunction->eKind == BSQL_FN_GROUP_MAP;
  if( !bsqlBlobArgument(argv[argc-1], &spec, &work.error) ){
    goto failed;
  }
  if( !p->pSchema ){
    p->pSchema = bsqlCachedSchema(pFunction->pConnection, spec, &work.error);
    if( !p->pSchema ){
      goto failed;
    }
    if( bsqlTypeKind(&p->pSchema->root) !=
        (bMap ? BEBOP_TYPE_KIND_MAP : BEBOP_TYPE_KIND_ARRAY) ){
      bsqlFail(&work.error, BSQL_INVALID,
                 "aggregate specification has wrong container type");
      goto failed;
    }
    memset(countBytes, 0, sizeof(countBytes));
    if( !bsqlWrapValue(spec, bebop_view(countBytes, sizeof(countBytes)),
                        &p->value, &work.error) ){
      goto failed;
    }
    p->iPayload = BSQL_VALUE_HEADER_BYTES + spec.length;
    p->nRemaining = BSQL_MAX_VISITS;
  }else if( spec.length != p->pSchema->nKey ||
            memcmp(spec.data, p->pSchema->aKey, spec.length) ){
    bsqlFail(&work.error, BSQL_INVALID,
               "aggregate specification changed within group");
    goto failed;
  }
  work.nRemaining = p->nRemaining;
  if( p->nItem == BSQL_MAX_ITEMS ){
    bsqlFail(&work.error, BSQL_LIMIT, "aggregate element limit exceeded");
    goto failed;
  }
  if( !bsqlVisit(&work, 0) ){
    goto failed;
  }
  pType = &p->pSchema->root;
  if( bMap ){
    if( !bsqlAggregateKeyReserve(p, &work) ){
      goto failed;
    }
    start = p->value.nData;
    atom = bsqlSqlAtom(argv[0], &work);
    if( !bsqlEncodeAtom(p->pSchema, pType->map_key.value, atom,
                         &p->value, &work) ){
      goto failed;
    }
    p->aKey[p->nItem].iByte = start;
    p->aKey[p->nItem].nByte = p->value.nData - start;
  }
  atom = bsqlSqlAtom(argv[bMap ? 1 : 0], &work);
  if( !bsqlEncodeAtom(p->pSchema, bMap ? pType->map_value.value :
                                        bsqlElement(pType),
                       atom, &p->value, &work) ){
    goto failed;
  }
  ++p->nItem;
  p->nRemaining = work.nRemaining;
  return;
failed:
  p->bFailed = 1;
  p->error = work.error;
  p->nRemaining = work.nRemaining;
  bsqlResultError(pContext, &p->error);
}

/* Validate aggregate key uniqueness, patch envelope/count lengths, and transfer
** the already assembled result. No second full-sized payload/result copy is
** created. Empty groups return NULL because no row supplied a specification.
** All retained state is released even after step failures or finalization OOM.
*/
static void bsqlAggregateFinal(sqlite3_context *pContext){
  BsqlFunction *pFunction;
  BsqlAggregate *p;
  BsqlWork work;
  Bebop_View *aKeys;
  size_t i;
  pFunction = sqlite3_user_data(pContext);
  p = sqlite3_aggregate_context(pContext, 0);
  if( !p ){
    sqlite3_result_null(pContext);
    return;
  }
  work = bsqlWork(pFunction->pConnection->db);
  work.nRemaining = p->nRemaining;
  aKeys = 0;
  if( p->bFailed ){
    bsqlResultError(pContext, &p->error);
    goto cleanup;
  }
  if( !p->pSchema ){
    sqlite3_result_null(pContext);
    goto cleanup;
  }
  if( p->aKey && p->nItem > 1 ){
    aKeys = sqlite3_malloc64((sqlite3_uint64)p->nItem * sizeof(*aKeys));
    if( !aKeys ){
      bsqlFail(&work.error, BSQL_NOMEM, "out of memory");
      goto failed;
    }
    for(i=0; i<p->nItem; ++i){
      aKeys[i] = bebop_view(p->value.aData + p->aKey[i].iByte,
                            p->aKey[i].nByte);
    }
    if( !bsqlUniqueKeyViews(aKeys, p->nItem, &work) ){
      goto failed;
    }
  }
  if( !bsqlPoll(&work) ){
    goto failed;
  }
  assert(p->value.nData >= p->iPayload + BEBOP_WIRE_SIZE_LEN);
  bsqlStoreLe(p->value.aOwned + p->iPayload, p->nItem, BEBOP_WIRE_SIZE_LEN);
  bsqlStoreLe(p->value.aOwned + BSQL_VALUE_PAYLOAD_OFFSET,
               p->value.nData - p->iPayload, sizeof(uint64_t));
  bsqlResultTake(pContext, &p->value, 0);
  goto cleanup;
failed:
  bsqlResultError(pContext, &work.error);
cleanup:
  sqlite3_free(aKeys);
  bsqlBufferClear(&p->value);
  sqlite3_free(p->aKey);
  p->aKey = 0;
  bsqlSchemaRelease(p->pSchema);
  p->pSchema = 0;
  bsqlWorkClear(&work);
}

/* Column identifiers are part of the declared virtual-table interface. */
enum {
  BSQL_COL_KEY,
  BSQL_COL_VALUE,
  BSQL_COL_ATOM,
  BSQL_COL_KIND,
  BSQL_COL_TYPE,
  BSQL_COL_ID,
  BSQL_COL_PARENT,
  BSQL_COL_FULLKEY,
  BSQL_COL_PATH,
  BSQL_COL_INPUT,
  BSQL_COL_ROOT,
  BSQL_PLAN_INPUT = 1,
  BSQL_PLAN_ROOT = 2,
  BSQL_ESTIMATED_ROWS = 100,
  BSQL_MODULE_VERSION = 3
};
static const double bsqlUnboundCost = 1.0e99;

/* A virtual table retains its connection independently of its module owner. */
typedef struct BsqlTable BsqlTable;
struct BsqlTable {
  sqlite3_vtab base;              /* Must be first for SQLite's ABI. */
  BsqlConnection *pConnection;    /* Retained connection-local state. */
  int bRecursive;                 /* bebop_tree rather than bebop_each. */
};

/* Frames are stored inside a heap-allocated cursor, not on the C call stack. */
typedef struct BsqlFrame BsqlFrame;
struct BsqlFrame {
  BsqlIterator iterator;          /* Parent's remaining children. */
  sqlite3_int64 id;               /* Row id of this parent, or -1 for each. */
  size_t nPath;                   /* Parent fullkey length for restoration. */
  unsigned char bPathValid;       /* Parent has a representable path. */
};

/* One cursor owns a stable input copy, retained schema, explicit traversal
** stack, and reusable output paths. Column requests never consume these paths.
*/
typedef struct BsqlCursor BsqlCursor;
struct BsqlCursor {
  sqlite3_vtab_cursor base;       /* Must be first for SQLite's ABI. */
  BsqlSchema *pSchema;            /* Retained decoded specification. */
  uint8_t *aInput;                /* SQLite-owned input snapshot. */
  size_t nInput;                  /* Input bytes. */
  BsqlView current;               /* Current row's borrowed wire value. */
  BsqlEntry entry;                /* Key/name/tag of current child. */
  BsqlFrame aFrame[BSQL_MAX_DEPTH]; /* Explicit bounded DFS stack. */
  size_t nDepth;                  /* Number of active frames. */
  size_t nRootSteps;              /* Steps already used by root selection. */
  BsqlBuffer fullkey;             /* Current row's full path when valid. */
  BsqlBuffer parentPath;          /* Parent path, independently valid. */
  BsqlBuffer rootPath;            /* Original hidden root argument. */
  sqlite3_int64 id;               /* Monotonic row id within this scan. */
  sqlite3_int64 parent;           /* Parent id, or -1. */
  BsqlWork work;                  /* Scan-wide limits and diagnostic. */
  unsigned char bEof;             /* No current row. */
  unsigned char bRecursive;       /* Tree traversal mode. */
  unsigned char bRootRow;         /* Current row is the selected root. */
  unsigned char bPathValid;       /* fullkey is representable. */
  unsigned char bParentPathValid; /* Parent path may survive an invalid key. */
};

/* Declare an eponymous read-only table and allocate its SQLite-owned state.
** Declaration/configuration failures and OOM leave no connection reference.
*/
static int bsqlTableConnect(
  sqlite3 *db, void *pUser, int argc, const char *const *argv,
  sqlite3_vtab **ppTable, char **pzError
){
  static const char declaration[] =
    "CREATE TABLE x(key,value,atom,kind,type,id INTEGER,parent INTEGER,"
    "fullkey TEXT,path TEXT,input HIDDEN,root HIDDEN)";
  BsqlConnection *pConnection;
  BsqlTable *p;
  int rc;
  (void)argc;
  pConnection = pUser;
  rc = sqlite3_declare_vtab(db, declaration);
  if( rc == SQLITE_OK ){
    rc = sqlite3_vtab_config(db, SQLITE_VTAB_INNOCUOUS);
  }
  if( rc != SQLITE_OK ){
    if( pzError ){
      *pzError = sqlite3_mprintf("cannot declare Bebop table: %s",
                                 sqlite3_errmsg(db));
    }
    return rc;
  }
  p = sqlite3_malloc64(sizeof(*p));
  if( !p ){
    return SQLITE_NOMEM;
  }
  memset(p, 0, sizeof(*p));
  p->pConnection = pConnection;
  p->bRecursive = strcmp(argv[0], "bebop_tree") == 0;
  assert(pConnection->nRef < UINT_MAX);
  ++pConnection->nRef;
  *ppTable = &p->base;
  return SQLITE_OK;
}

/* Release table state and its connection ownership. No new allocations. */
static int bsqlTableDisconnect(sqlite3_vtab *pBase){
  BsqlTable *p;
  p = (BsqlTable *)pBase;
  bsqlConnectionRelease(p->pConnection);
  sqlite3_free(p);
  return SQLITE_OK;
}

/* Select usable equality constraints for hidden input/root arguments. An
** unusable duplicate does not reject a usable constraint on the same column.
** Unsatisfied dependencies request a different join order with CONSTRAINT.
*/
static int bsqlTableBestIndex(sqlite3_vtab *pTable, sqlite3_index_info *pInfo){
  int input;
  int root;
  int bInputBlocked;
  int bRootBlocked;
  int i;
  int column;
  (void)pTable;
  input = -1;
  root = -1;
  bInputBlocked = 0;
  bRootBlocked = 0;
  for(i=0; i<pInfo->nConstraint; ++i){
    if( pInfo->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ ){
      continue;
    }
    column = pInfo->aConstraint[i].iColumn;
    if( column == BSQL_COL_INPUT ){
      if( pInfo->aConstraint[i].usable ){
        if( input < 0 ){
          input = i;
        }
      }else{
        bInputBlocked = 1;
      }
    }else if( column == BSQL_COL_ROOT ){
      if( pInfo->aConstraint[i].usable ){
        if( root < 0 ){
          root = i;
        }
      }else{
        bRootBlocked = 1;
      }
    }
  }
  if( (input < 0 && bInputBlocked) || (root < 0 && bRootBlocked) ){
    return SQLITE_CONSTRAINT;
  }
  if( input < 0 ){
    pInfo->estimatedCost = bsqlUnboundCost;
    pInfo->estimatedRows = INT64_MAX;
    pInfo->idxNum = 0;
    return SQLITE_OK;
  }
  pInfo->aConstraintUsage[input].argvIndex = 1;
  pInfo->aConstraintUsage[input].omit = 1;
  pInfo->idxNum = BSQL_PLAN_INPUT;
  if( root >= 0 ){
    pInfo->aConstraintUsage[root].argvIndex = 2;
    pInfo->aConstraintUsage[root].omit = 1;
    pInfo->idxNum |= BSQL_PLAN_ROOT;
  }
  pInfo->estimatedCost = (double)BSQL_ESTIMATED_ROWS;
  pInfo->estimatedRows = BSQL_ESTIMATED_ROWS;
  return SQLITE_OK;
}

/*
** Allocate a cursor and its bounded DFS stack in SQLite's heap. OOM is direct.
*/
static int bsqlTableOpen(sqlite3_vtab *pTable, sqlite3_vtab_cursor **ppCursor){
  BsqlCursor *p;
  p = sqlite3_malloc64(sizeof(*p));
  if( !p ){
    return SQLITE_NOMEM;
  }
  memset(p, 0, sizeof(*p));
  p->base.pVtab = pTable;
  p->bRecursive = ((BsqlTable *)pTable)->bRecursive != 0;
  p->bEof = 1;
  *ppCursor = &p->base;
  return SQLITE_OK;
}

/* Clear every per-scan pointer and flag, retaining only SQLite's base and the
** immutable traversal mode. This prevents stale state when xFilter is reused.
*/
static void bsqlCursorClear(BsqlCursor *p){
  sqlite3_vtab_cursor base;
  unsigned char bRecursive;
  base = p->base;
  bRecursive = p->bRecursive;
  bsqlSchemaRelease(p->pSchema);
  sqlite3_free(p->aInput);
  bsqlBufferClear(&p->fullkey);
  bsqlBufferClear(&p->parentPath);
  bsqlBufferClear(&p->rootPath);
  bsqlWorkClear(&p->work);
  memset(p, 0, sizeof(*p));
  p->base = base;
  p->bRecursive = bRecursive;
  p->bEof = 1;
}

/* Release all scan resources and then the SQLite-owned cursor itself. */
static int bsqlTableClose(sqlite3_vtab_cursor *pBase){
  BsqlCursor *p;
  p = (BsqlCursor *)pBase;
  bsqlCursorClear(p);
  sqlite3_free(p);
  return SQLITE_OK;
}

/* Publish a scan failure, preserving its SQLite result code. Diagnostic
** allocation failure becomes NOMEM. The cursor is left at EOF in every case.
*/
static int bsqlCursorError(BsqlCursor *p){
  BsqlError *pError;
  const char *z;
  pError = &p->work.error;
  sqlite3_free(p->base.pVtab->zErrMsg);
  p->base.pVtab->zErrMsg = 0;
  p->bEof = 1;
  if( pError->eCode == BSQL_NOMEM ){
    return SQLITE_NOMEM;
  }
  z = pError->zMessage ? pError->zMessage : "Bebop traversal failed";
  if( pError->bOffset ){
    p->base.pVtab->zErrMsg = sqlite3_mprintf("%s at byte %llu", z,
      (sqlite3_uint64)pError->iOffset);
  }else{
    p->base.pVtab->zErrMsg = sqlite3_mprintf("%s", z);
  }
  if( !p->base.pVtab->zErrMsg ){
    return SQLITE_NOMEM;
  }
  return pError->sqliteCode ? pError->sqliteCode : SQLITE_ERROR;
}

/* Append a quoted path name only if its complete escaped spelling fits the
** path limit. An unaddressable row remains iterable with a NULL fullkey;
** enormous map keys never trigger enormous temporary path allocations.
*/
static int bsqlPathNameAppend(BsqlCursor *p, Bebop_String name){
  size_t nNeed;
  size_t i;
  size_t extra;
  unsigned c;
  if( p->fullkey.nData > BSQL_MAX_PATH_BYTES ||
      BSQL_MAX_PATH_BYTES - p->fullkey.nData < sizeof(".\"\"") - 1 ){
    p->bPathValid = 0;
    return 1;
  }
  if( name.length > BSQL_MAX_PATH_BYTES - p->fullkey.nData -
                    (sizeof(".\"\"") - 1) ){
    p->bPathValid = 0;
    return 1;
  }
  nNeed = name.length + sizeof(".\"\"") - 1;
  for(i=0; i<name.length; ++i){
    c = (unsigned char)name.data[i];
    extra = c < 0x20 ? BSQL_UNICODE_ESCAPE_BYTES - 1 :
            (c == '"' || c == '\\') ? 1 : 0;
    if( extra > BSQL_MAX_PATH_BYTES - p->fullkey.nData - nNeed ){
      p->bPathValid = 0;
      return 1;
    }
    nNeed += extra;
  }
  return BSQL_APPEND_LITERAL(&p->fullkey, ".", &p->work.error)
      && bsqlJsonQuote(&p->fullkey, (const uint8_t *)name.data,
                        name.length, &p->work);
}

/* Construct one child's fullkey without relying on presentation kind strings.
** Boolean/UUID map keys and unknown wire tags have no path representation.
** Their parent path can still be returned independently.
*/
static int bsqlEntryPath(
  BsqlCursor *p, const BsqlEntry *pEntry, BsqlView parent
){
  Bebop_String name;
  char zNumber[BSQL_NUMBER_TEXT_BYTES];
  char zDecimal[BSQL_DECIMAL_TEXT_BYTES];
  unsigned kind;
  int bSigned;
  size_t n;
  if( !p->bPathValid ){
    return 1;
  }
  if( p->nRootSteps + p->nDepth > BSQL_MAX_PATH_STEPS ){
    p->bPathValid = 0;
    return 1;
  }
  if( pEntry->name.data ){
    return bsqlPathNameAppend(p, pEntry->name);
  }
  if( bsqlTypeKind(parent.pType) == BEBOP_TYPE_KIND_MAP ){
    kind = bsqlTypeKind(pEntry->key.pType);
    if( kind == BEBOP_TYPE_KIND_STRING ){
      name = bebop_string_view(
        (const char *)pEntry->key.bytes.data + BEBOP_WIRE_SIZE_LEN,
        (size_t)bsqlLoadLe(pEntry->key.bytes.data, BEBOP_WIRE_SIZE_LEN));
      return bsqlPathNameAppend(p, name);
    }
    if( kind < BEBOP_TYPE_KIND_BYTE || kind > BEBOP_TYPE_KIND_UINT_128 ){
      p->bPathValid = 0;
      return 1;
    }
    bSigned = kind == BEBOP_TYPE_KIND_INT_8 || kind == BEBOP_TYPE_KIND_INT_16
           || kind == BEBOP_TYPE_KIND_INT_32 || kind == BEBOP_TYPE_KIND_INT_64
           || kind == BEBOP_TYPE_KIND_INT_128;
    bsqlDecimalText(pEntry->key.bytes.data, bsqlPrimitives[kind].nWire,
                     bSigned, zDecimal);
    sqlite3_snprintf(sizeof(zNumber), zNumber, "[%s]", zDecimal);
  }else if( !pEntry->value.pType ){
    p->bPathValid = 0;
    return 1;
  }else{
    sqlite3_snprintf(sizeof(zNumber), zNumber, "[%llu]",
                      (sqlite3_uint64)pEntry->iIndex);
  }
  n = strlen(zNumber);
  if( p->fullkey.nData > BSQL_MAX_PATH_BYTES ||
      n > BSQL_MAX_PATH_BYTES - p->fullkey.nData ){
    p->bPathValid = 0;
    return 1;
  }
  return bsqlAppend(&p->fullkey, zNumber, n, &p->work.error);
}

/* Advance a read-only cursor using its explicit bounded DFS stack. Parent
** paths are restored by length, avoiding path allocation for every ancestor.
** Iterator/path errors leave the cursor at EOF and release no borrowed bytes.
*/
static int bsqlTableNext(sqlite3_vtab_cursor *pBase){
  BsqlCursor *p;
  BsqlFrame *pFrame;
  BsqlEntry entry;
  p = (BsqlCursor *)pBase;
  if( p->bEof ){
    return SQLITE_OK;
  }
  if( p->bRecursive && p->current.pType && bsqlIterable(p->current) ){
    if( p->nDepth == BSQL_MAX_DEPTH ){
      bsqlFail(&p->work.error, BSQL_LIMIT, "tree depth exceeded");
      return bsqlCursorError(p);
    }
    pFrame = p->aFrame + p->nDepth;
    pFrame->id = p->id;
    pFrame->nPath = p->fullkey.nData;
    pFrame->bPathValid = p->bPathValid;
    if( !bsqlIteratorInit(p->current, &pFrame->iterator, &p->work) ){
      return bsqlCursorError(p);
    }
    ++p->nDepth;
  }
  p->bRootRow = 0;
  while( p->nDepth ){
    pFrame = p->aFrame + p->nDepth - 1;
    if( bsqlIteratorNext(&pFrame->iterator, &entry, &p->work) ){
      p->current = entry.value;
      p->entry = entry;
      p->parent = pFrame->id;
      ++p->id;
      p->fullkey.nData = pFrame->nPath;
      p->bPathValid = pFrame->bPathValid;
      p->bParentPathValid = pFrame->bPathValid;
      p->parentPath.nData = 0;
      if( !bsqlAppend(&p->parentPath, p->fullkey.aData, p->fullkey.nData,
                       &p->work.error) ||
          !bsqlEntryPath(p, &entry, pFrame->iterator.parent) ){
        return bsqlCursorError(p);
      }
      return SQLITE_OK;
    }
    if( p->work.error.eCode != BSQL_OK ){
      return bsqlCursorError(p);
    }
    --p->nDepth;
  }
  p->bEof = 1;
  return SQLITE_OK;
}

/* Start or restart a scan. Copy the input into SQLite-owned memory before
** retaining views, parse a bounded root path, and validate the selected value.
** NULL arguments produce no rows. All failed state remains owned by the cursor
** and is freed by the next filter or close, never leaked across rescans.
*/
static int bsqlTableFilter(
  sqlite3_vtab_cursor *pBase, int plan, const char *zDescription,
  int argc, sqlite3_value **argv
){
  BsqlCursor *p;
  BsqlTable *pTable;
  BsqlPath *pPath;
  BsqlFrame *pFrame;
  BsqlView root;
  Bebop_View bytes;
  Bebop_View spec;
  Bebop_View payload;
  const char *zPath;
  size_t nPath;
  int expected;
  int ok;
  (void)zDescription;
  p = (BsqlCursor *)pBase;
  pTable = (BsqlTable *)pBase->pVtab;
  bsqlCursorClear(p);
  p->work = bsqlWork(pTable->pConnection->db);
  expected = 1 + ((plan & BSQL_PLAN_ROOT) != 0);
  if( !(plan & BSQL_PLAN_INPUT) || argc != expected ){
    bsqlFail(&p->work.error, BSQL_INVALID,
               "Bebop table requires a typed value");
    return bsqlCursorError(p);
  }
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      (argc > 1 && sqlite3_value_type(argv[1]) == SQLITE_NULL) ){
    return SQLITE_OK;
  }
  if( !bsqlBlobArgument(argv[0], &bytes, &p->work.error) ){
    return bsqlCursorError(p);
  }
  p->aInput = sqlite3_malloc64(bytes.length ?
                               (sqlite3_uint64)bytes.length : 1);
  if( !p->aInput ){
    bsqlFail(&p->work.error, BSQL_NOMEM, "out of memory");
    return bsqlCursorError(p);
  }
  if( bytes.length ){
    memcpy(p->aInput, bytes.data, bytes.length);
  }
  p->nInput = bytes.length;
  if( !bsqlSplitValue(bebop_view(p->aInput, p->nInput), &spec, &payload,
                       &p->work.error) ){
    return bsqlCursorError(p);
  }
  p->pSchema = bsqlCachedSchema(pTable->pConnection, spec, &p->work.error);
  if( !p->pSchema ){
    return bsqlCursorError(p);
  }
  root = bsqlView(p->pSchema, &p->pSchema->root, payload, 1);
  zPath = "$";
  nPath = sizeof("$") - 1;
  if( argc == 2 ){
    if( sqlite3_value_type(argv[1]) != SQLITE_TEXT ){
      bsqlFail(&p->work.error, BSQL_INVALID, "path requires TEXT");
      return bsqlCursorError(p);
    }
    zPath = (const char *)sqlite3_value_text(argv[1]);
    nPath = (size_t)sqlite3_value_bytes(argv[1]);
    if( !zPath ){
      bsqlFail(&p->work.error, BSQL_NOMEM, "out of memory");
      return bsqlCursorError(p);
    }
  }
  pPath = bsqlPathParse(zPath, nPath, 0, &p->work.error);
  ok = pPath && bsqlFollowPath(root, pPath, &p->current, &p->work);
  if( pPath ){
    p->nRootSteps = pPath->nStep;
  }
  bsqlPathFree(pPath);
  if( !ok ){
    return bsqlCursorError(p);
  }
  if( !p->current.bPresent ){
    return SQLITE_OK;
  }
  if( !bsqlValidateView(p->current, &p->work) ||
      !bsqlAppend(&p->fullkey, zPath, nPath, &p->work.error) ||
      !bsqlAppend(&p->parentPath, zPath, nPath, &p->work.error) ||
      !bsqlAppend(&p->rootPath, zPath, nPath, &p->work.error) ){
    return bsqlCursorError(p);
  }
  p->bEof = 0;
  p->bRootRow = 1;
  p->parent = -1;
  p->bPathValid = 1;
  p->bParentPathValid = 1;
  if( p->bRecursive || !bsqlIterable(p->current) ){
    return SQLITE_OK;
  }
  pFrame = p->aFrame;
  pFrame->id = -1;
  pFrame->nPath = p->fullkey.nData;
  pFrame->bPathValid = 1;
  if( !bsqlIteratorInit(p->current, &pFrame->iterator, &p->work) ){
    return bsqlCursorError(p);
  }
  p->nDepth = 1;
  return bsqlTableNext(pBase);
}

/* Return EOF state without advancing or allocating. */
static int bsqlTableEof(sqlite3_vtab_cursor *pBase){
  return ((BsqlCursor *)pBase)->bEof;
}

/* Return the current row id. The traversal visit limit bounds its growth. */
static int bsqlTableRowid(sqlite3_vtab_cursor *pBase, sqlite3_int64 *pId){
  *pId = ((BsqlCursor *)pBase)->id;
  return SQLITE_OK;
}

/* Publish a virtual-table column. Persistent path buffers are copied, never
** transferred. Temporary type strings and typed values may transfer ownership.
** OOM, malformed values, or traversal limits become SQLite cursor errors.
*/
static int bsqlTableColumn(
  sqlite3_vtab_cursor *pBase, sqlite3_context *pContext, int column
){
  BsqlCursor *p;
  BsqlView view;
  BsqlBuffer out;
  int ok;
  p = (BsqlCursor *)pBase;
  view = p->current;
  memset(&out, 0, sizeof(out));
  ok = 1;
  switch( column ){
    case BSQL_COL_KEY:
      if( !p->bRootRow ){
        if( p->entry.key.bPresent ){
          ok = bsqlScalarResult(pContext, p->entry.key, &p->work);
        }else if( p->entry.name.data ){
          sqlite3_result_text64(pContext, p->entry.name.data,
            p->entry.name.length, SQLITE_TRANSIENT, SQLITE_UTF8);
        }else{
          sqlite3_result_int64(pContext, (sqlite3_int64)p->entry.iIndex);
        }
      }
      break;
    case BSQL_COL_VALUE:
      if( view.pType ){
        ok = bsqlScalarResult(pContext, view, &p->work);
      }else{
        sqlite3_result_blob64(pContext, view.bytes.data, view.bytes.length,
                               SQLITE_TRANSIENT);
      }
      break;
    case BSQL_COL_ATOM:
      if( view.pType && !bsqlIterable(view) ){
        ok = bsqlScalarResult(pContext, view, &p->work);
      }
      break;
    case BSQL_COL_KIND:
      sqlite3_result_text(pContext, bsqlKindText(view), -1, SQLITE_STATIC);
      break;
    case BSQL_COL_TYPE:
      if( view.pType ){
        ok = bsqlTypeText(view.pType, &out, 0, &p->work.error);
        if( ok ){
          bsqlResultTake(pContext, &out, 1);
        }
      }
      break;
    case BSQL_COL_ID:
      sqlite3_result_int64(pContext, p->id);
      break;
    case BSQL_COL_PARENT:
      if( p->parent >= 0 ){
        sqlite3_result_int64(pContext, p->parent);
      }
      break;
    case BSQL_COL_FULLKEY:
      if( p->bPathValid ){
        bsqlResultBuffer(pContext, &p->fullkey, 1);
      }
      break;
    case BSQL_COL_PATH:
      if( p->bParentPathValid ){
        bsqlResultBuffer(pContext, &p->parentPath, 1);
      }
      break;
    case BSQL_COL_INPUT:
      sqlite3_result_blob64(pContext, p->aInput, p->nInput, SQLITE_TRANSIENT);
      break;
    case BSQL_COL_ROOT:
      bsqlResultBuffer(pContext, &p->rootPath, 1);
      break;
    default:
      ok = bsqlFail(&p->work.error, BSQL_INVALID, "invalid table column");
      break;
  }
  bsqlBufferClear(&out);
  if( !ok ){
    bsqlResultError(pContext, &p->work.error);
    return bsqlCursorError(p);
  }
  return SQLITE_OK;
}

/* A positional initializer supports C89 syntax and older sqlite3_module ABIs.
** Fields added after xShadowName are zero-initialized by newer headers.
*/
static const sqlite3_module bsqlTableModule = {
  BSQL_MODULE_VERSION,
  0,                              /* xCreate: eponymous-only. */
  bsqlTableConnect,
  bsqlTableBestIndex,
  bsqlTableDisconnect,
  0,                              /* xDestroy. */
  bsqlTableOpen,
  bsqlTableClose,
  bsqlTableFilter,
  bsqlTableNext,
  bsqlTableEof,
  bsqlTableColumn,
  bsqlTableRowid,
  0,                              /* xUpdate: read-only. */
  0,                              /* xBegin. */
  0,                              /* xSync. */
  0,                              /* xCommit. */
  0,                              /* xRollback. */
  0,                              /* xFindFunction. */
  0,                              /* xRename. */
  0,                              /* xSavepoint. */
  0,                              /* xRelease. */
  0,                              /* xRollbackTo. */
  0,                              /* xShadowName. */
  0                               /* xIntegrity. */
};

#ifdef BSQL_ENABLE_SCHEMA_COMPILER
/* Compile a SQL TEXT schema through the optional Bebop compiler. Reject
** embedded NULs instead of silently compiling a truncated source. Compiler
** allocations use the bounded SQLite allocator. Descriptor bytes are copied
** before compiler cleanup. This callback is registered SQLITE_DIRECTONLY.
*/
static void bsqlSchemaFunction(
  sqlite3_context *pContext, int argc, sqlite3_value **argv
){
  const char *zSource;
  size_t nSource;
  BsqlAllocator allocator;
  BsqlError error;
  bebop_host_t host;
  bebop_context_t *pCompiler;
  bebop_parse_result_t *pParsed;
  bebop_descriptor_t *pDescriptor;
  bebop_status_t rc;
  const uint8_t *aDescriptor;
  size_t nDescriptor;
  (void)argc;
  if( sqlite3_value_type(argv[0]) == SQLITE_NULL ){
    sqlite3_result_null(pContext);
    return;
  }
  if( sqlite3_value_type(argv[0]) != SQLITE_TEXT ){
    sqlite3_result_error(pContext, "schema source must be TEXT", -1);
    return;
  }
  zSource = (const char *)sqlite3_value_text(argv[0]);
  nSource = (size_t)sqlite3_value_bytes(argv[0]);
  if( !zSource ){
    sqlite3_result_error_nomem(pContext);
    return;
  }
  if( memchr(zSource, 0, nSource) ){
    sqlite3_result_error(pContext, "embedded NUL in schema source", -1);
    return;
  }
  memset(&allocator, 0, sizeof(allocator));
  memset(&error, 0, sizeof(error));
  memset(&host, 0, sizeof(host));
  allocator.nLimit = BSQL_MAX_ARENA_BYTES;
  host.allocator.alloc = bsqlArenaAllocate;
  host.allocator.ctx = &allocator;
  pCompiler = bebop_context_create(&host);
  pParsed = 0;
  pDescriptor = 0;
  if( !pCompiler ){
    bsqlArenaFail(&allocator, &error, "cannot allocate schema compiler");
    bsqlResultError(pContext, &error);
    return;
  }
  rc = bebop_parse_source(pCompiler, BEBOP_SOURCE(zSource, "sqlite.bop"),
                           &pParsed);
  if( (rc != BEBOP_OK && rc != BEBOP_OK_WITH_WARNINGS) || !pParsed ||
      bebop_result_error_count(pParsed) ){
    bsqlFail(&error, allocator.eFailure ? allocator.eFailure : BSQL_INVALID,
               "invalid Bebop schema");
  }else if( bebop_descriptor_build(pParsed, BEBOP_DESC_FLAG_NONE,
                                  &pDescriptor) != BEBOP_OK ){
    bsqlFail(&error, allocator.eFailure ? allocator.eFailure : BSQL_INVALID,
               "cannot build descriptor");
  }else if( bebop_descriptor_encode(pDescriptor, &aDescriptor,
                                   &nDescriptor) != BEBOP_OK ){
    bsqlFail(&error, allocator.eFailure ? allocator.eFailure : BSQL_INVALID,
               "cannot encode descriptor");
  }else if( nDescriptor > BSQL_MAX_SPEC_BYTES ){
    bsqlFail(&error, BSQL_LIMIT, "descriptor byte limit exceeded");
  }else{
    sqlite3_result_blob64(pContext, aDescriptor, (sqlite3_uint64)nDescriptor,
                           SQLITE_TRANSIENT);
  }
  if( error.eCode != BSQL_OK ){
    bsqlResultError(pContext, &error);
  }
  if( pDescriptor ){
    bebop_descriptor_free(pDescriptor);
  }
  bebop_context_destroy(pCompiler);
}
#endif

/* Immutable SQL registration metadata; every public overload is listed here. */
typedef struct BsqlRegistration BsqlRegistration;
struct BsqlRegistration {
  const char *zName;              /* SQLite function name. */
  int nArgument;                  /* Fixed registered arity. */
  /* Scalar/aggregate implementation selector. */
  BsqlFunctionKind eKind;
};

static const BsqlRegistration bsqlRegistrations[] = {
  {"bebop_type", 2, BSQL_FN_TYPE},
  {"bebop_pack", 2, BSQL_FN_PACK},
  {"bebop_raw", 1, BSQL_FN_RAW},
  {"bebop_spec", 1, BSQL_FN_SPEC},
  {"bebop_typeof", 1, BSQL_FN_TYPEOF},
  {"bebop_typeof", 2, BSQL_FN_TYPEOF},
  {"bebop_extract", 2, BSQL_FN_EXTRACT},
  {"bebop_get", 2, BSQL_FN_GET},
  {"bebop_kind", 1, BSQL_FN_KIND},
  {"bebop_kind", 2, BSQL_FN_KIND},
  {"bebop_exists", 2, BSQL_FN_EXISTS},
  {"bebop_length", 1, BSQL_FN_LENGTH},
  {"bebop_length", 2, BSQL_FN_LENGTH},
  {"bebop_valid", 1, BSQL_FN_VALID},
  {"bebop_error", 1, BSQL_FN_ERROR},
  {"bebop_branch", 1, BSQL_FN_BRANCH},
  {"bebop_branch", 2, BSQL_FN_BRANCH},
  {"bebop_enum_name", 1, BSQL_FN_ENUM_NAME},
  {"bebop_enum_name", 2, BSQL_FN_ENUM_NAME},
  {"bebop_map_get", 2, BSQL_FN_MAP_GET},
  {"bebop_map_has", 2, BSQL_FN_MAP_HAS},
  {"bebop_from_json", 2, BSQL_FN_FROM_JSON},
  {"bebop_to_json", 1, BSQL_FN_TO_JSON},
  {"bebop_set", 3, BSQL_FN_SET},
  {"bebop_insert", 3, BSQL_FN_INSERT},
  {"bebop_replace", 3, BSQL_FN_REPLACE},
  {"bebop_remove", 2, BSQL_FN_REMOVE},
  {"bebop_append", 3, BSQL_FN_APPEND},
  {"bebop_map_set", 3, BSQL_FN_MAP_SET},
  {"bebop_map_remove", 2, BSQL_FN_MAP_REMOVE},
  {"bebop_group_array", 2, BSQL_FN_GROUP_ARRAY},
  {"bebop_group_map", 3, BSQL_FN_GROUP_MAP}
};

static const BsqlRegistration bsqlOperatorRegistrations[] = {
  {"->", 2, BSQL_FN_ARROW},
  {"->>", 2, BSQL_FN_ARROW_SCALAR}
};

/* Allocate and register one function. SQLite owns its destructor even when
** sqlite3_create_function_v2 fails, so no second manual release is performed.
*/
static int bsqlRegisterFunction(
  sqlite3 *db, BsqlConnection *pConnection, const BsqlRegistration *pRegister
){
  BsqlFunction *p;
  int bAggregate;
  p = sqlite3_malloc64(sizeof(*p));
  if( !p ){
    return SQLITE_NOMEM;
  }
  p->pConnection = pConnection;
  p->eKind = pRegister->eKind;
  assert(pConnection->nRef < UINT_MAX);
  ++pConnection->nRef;
  bAggregate = pRegister->eKind == BSQL_FN_GROUP_ARRAY ||
               pRegister->eKind == BSQL_FN_GROUP_MAP;
  return sqlite3_create_function_v2(db, pRegister->zName,
    pRegister->nArgument, SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS,
    p, bAggregate ? 0 : bsqlSqlFunction,
    bAggregate ? bsqlAggregateStep : 0,
    bAggregate ? bsqlAggregateFinal : 0, bsqlFunctionRelease);
}

/* Remove successfully installed registrations in reverse order during failure
** cleanup. Existing application overrides cannot be reconstructed here.
*/
static void bsqlUnregisterFunctions(
  sqlite3 *db, const BsqlRegistration *a, size_t n
){
  while( n ){
    --n;
    sqlite3_create_function_v2(db, a[n].zName, a[n].nArgument,
      SQLITE_UTF8, 0, 0, 0, 0, 0);
  }
}

/* Create connection-local state with one initializer reference. SQLite OOM
** returns NULL; the caller reports SQLITE_NOMEM without additional allocation.
*/
static BsqlConnection *bsqlConnectionNew(sqlite3 *db){
  BsqlConnection *p;
  p = sqlite3_malloc64(sizeof(*p));
  if( p ){
    memset(p, 0, sizeof(*p));
    p->db = db;
    p->nRef = 1;
  }
  return p;
}

/* Check host API availability before using newer extension entry points. The
** error message is SQLite-owned and follows the extension loader convention.
*/
static int bsqlCheckHost(char **pzError){
  if( sqlite3_libversion_number() < BSQL_MIN_SQLITE_VERSION ){
    if( pzError ){
      *pzError = sqlite3_mprintf("Bebop requires SQLite 3.31.0 or newer");
    }
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

/* Report the failing registration without replacing its SQLite return code. */
static void bsqlRegistrationError(char **pzError, const char *zName, int rc){
  if( pzError ){
    *pzError = sqlite3_mprintf("could not register %s: SQLite %s (%d)",
                               zName, sqlite3_errstr(rc), rc);
  }
}

/* Register all functions and both eponymous tables, optionally including the
** direct-only compiler. Connection/function/module allocations use SQLite.
** Any failed step removes this invocation's earlier registrations and releases
** the initializer reference; loader-owned diagnostic text is returned via
** error.
*/
BSQL_EXPORT int sqlite3_bebopsqlite_init(
  sqlite3 *db, char **pzError, const sqlite3_api_routines *pApi
){
  BsqlConnection *pConnection;
  const char *zName;
  size_t nRegistered;
  int rc;
  int bEach;
  int bTree;
  SQLITE_EXTENSION_INIT2(pApi);
  if( pzError ){
    *pzError = 0;
  }
  rc = bsqlCheckHost(pzError);
  if( rc != SQLITE_OK ){
    return rc;
  }
  pConnection = bsqlConnectionNew(db);
  if( !pConnection ){
    return SQLITE_NOMEM;
  }
  nRegistered = 0;
  bEach = 0;
  bTree = 0;
  zName = 0;
  for(; nRegistered<BSQL_COUNT(bsqlRegistrations); ++nRegistered){
    zName = bsqlRegistrations[nRegistered].zName;
    rc = bsqlRegisterFunction(db, pConnection,
                              bsqlRegistrations + nRegistered);
    if( rc != SQLITE_OK ){
      break;
    }
  }
  if( rc == SQLITE_OK ){
    zName = "bebop_each";
    ++pConnection->nRef;
    rc = sqlite3_create_module_v2(db, zName, &bsqlTableModule, pConnection,
                                   bsqlConnectionRelease);
    bEach = rc == SQLITE_OK;
  }
  if( rc == SQLITE_OK ){
    zName = "bebop_tree";
    ++pConnection->nRef;
    rc = sqlite3_create_module_v2(db, zName, &bsqlTableModule, pConnection,
                                   bsqlConnectionRelease);
    bTree = rc == SQLITE_OK;
  }
#ifdef BSQL_ENABLE_SCHEMA_COMPILER
  if( rc == SQLITE_OK ){
    zName = "bebop_schema";
    rc = sqlite3_create_function_v2(db, zName, 1,
      SQLITE_UTF8 | SQLITE_DIRECTONLY, 0, bsqlSchemaFunction, 0, 0, 0);
  }
#endif
  if( rc != SQLITE_OK ){
    bsqlRegistrationError(pzError, zName, rc);
    if( bTree ){
      sqlite3_create_module_v2(db, "bebop_tree", 0, 0, 0);
    }
    if( bEach ){
      sqlite3_create_module_v2(db, "bebop_each", 0, 0, 0);
    }
    bsqlUnregisterFunctions(db, bsqlRegistrations, nRegistered);
  }
  bsqlConnectionRelease(pConnection);
  return rc;
}

/* Alternate extension-loader name for the same complete registration set. */
BSQL_EXPORT int sqlite3_bebop_init(
  sqlite3 *db, char **pzError, const sqlite3_api_routines *pApi
){
  return sqlite3_bebopsqlite_init(db, pzError, pApi);
}

/* Explicitly install Bebop -> and ->> overloads. The operator-only connection
** cache is separate. Failure removes any earlier operator installed here and
** releases all initializer ownership instead of leaving a partial pair.
*/
BSQL_EXPORT int sqlite3_bebopoperators_init(
  sqlite3 *db, char **pzError, const sqlite3_api_routines *pApi
){
  BsqlConnection *pConnection;
  size_t nRegistered;
  int rc;
  SQLITE_EXTENSION_INIT2(pApi);
  if( pzError ){
    *pzError = 0;
  }
  rc = bsqlCheckHost(pzError);
  if( rc != SQLITE_OK ){
    return rc;
  }
  pConnection = bsqlConnectionNew(db);
  if( !pConnection ){
    return SQLITE_NOMEM;
  }
  for(nRegistered=0;
      nRegistered<BSQL_COUNT(bsqlOperatorRegistrations);
      ++nRegistered){
    rc = bsqlRegisterFunction(db, pConnection,
                              bsqlOperatorRegistrations + nRegistered);
    if( rc != SQLITE_OK ){
      bsqlRegistrationError(pzError,
        bsqlOperatorRegistrations[nRegistered].zName, rc);
      bsqlUnregisterFunctions(db, bsqlOperatorRegistrations, nRegistered);
      break;
    }
  }
  bsqlConnectionRelease(pConnection);
  return rc;
}

/* Install the ordinary interface and explicit operators. If operator setup
** fails, remove the ordinary registrations installed by this invocation too.
*/
BSQL_EXPORT int sqlite3_bebopsqlite_operators_init(
  sqlite3 *db, char **pzError, const sqlite3_api_routines *pApi
){
  int rc;
  rc = sqlite3_bebopsqlite_init(db, pzError, pApi);
  if( rc != SQLITE_OK ){
    return rc;
  }
  rc = sqlite3_bebopoperators_init(db, pzError, pApi);
  if( rc != SQLITE_OK ){
#ifdef BSQL_ENABLE_SCHEMA_COMPILER
    sqlite3_create_function_v2(db, "bebop_schema", 1,
      SQLITE_UTF8, 0, 0, 0, 0, 0);
#endif
    sqlite3_create_module_v2(db, "bebop_tree", 0, 0, 0);
    sqlite3_create_module_v2(db, "bebop_each", 0, 0, 0);
    bsqlUnregisterFunctions(db, bsqlRegistrations,
                             BSQL_COUNT(bsqlRegistrations));
  }
  return rc;
}
