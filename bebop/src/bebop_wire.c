#include "bebop_wire.h"

typedef struct bebop_wire_arena_block_impl {
  struct bebop_wire_arena_block_impl* next;
#ifdef BEBOP_WIRE_SINGLE_THREADED
  size_t used;
#else
  _Atomic size_t used;
#endif
  size_t capacity;
} bebop_wire_arena_block_impl_t;

typedef struct {
#ifdef BEBOP_WIRE_SINGLE_THREADED
  bebop_wire_arena_block_impl_t* current_block;
  size_t total_allocated;
  size_t total_used;
#else
  _Atomic(bebop_wire_arena_block_impl_t*) current_block;
  _Atomic size_t total_allocated;
  _Atomic size_t total_used;
#endif
  Bebop_ArenaOpts options;
  Bebop_WireAllocFn alloc;
  void* alloc_ctx;
} bebop_wire_arena_impl_t;

struct Bebop_WireCtx {
  bebop_wire_arena_impl_t* arena;
  Bebop_WireCtxOpts options;
};

struct Bebop_Reader {
  const uint8_t* start;
  const uint8_t* current;
  const uint8_t* end;
  Bebop_WireCtx* context;
};

struct Bebop_Writer {
  uint8_t* buffer;
  uint8_t* current;
  uint8_t* end;
  Bebop_WireCtx* context;
};

#define BEBOP_ARENA_OVERHEAD (sizeof(bebop_wire_arena_block_impl_t))
#if defined(_MSC_VER)
#define BEBOP_ARENA_ALIGN __alignof(max_align_t)
#else
#define BEBOP_ARENA_ALIGN _Alignof(max_align_t)
#endif

static size_t bebop_wire_align_size(size_t size, size_t alignment)
{
  return (size + alignment - 1) & ~(alignment - 1);
}

static bebop_wire_arena_block_impl_t* bebop_wire_arena_allocate_block(
    const bebop_wire_arena_impl_t* arena, size_t min_size
)
{
  size_t capacity = arena->options.initial_block_size;
  const size_t required = bebop_wire_align_size(min_size, BEBOP_ARENA_ALIGN);

  if (required > arena->options.max_block_size) {
    return NULL;
  }
  if (capacity < required) {
    capacity = required;
  }
  if (capacity > arena->options.max_block_size) {
    capacity = arena->options.max_block_size;
  }

  const size_t total_size = sizeof(bebop_wire_arena_block_impl_t) + capacity;

  bebop_wire_arena_block_impl_t* block =
      (bebop_wire_arena_block_impl_t*)arena->alloc(NULL, 0, total_size, arena->alloc_ctx);
  if (!block) {
    return NULL;
  }

  block->next = NULL;
  BEBOP_WIRE_ATOMIC_INIT(&block->used, 0);
  block->capacity = capacity;
  return block;
}

static bebop_wire_arena_impl_t* bebop_wire_arena_create(const Bebop_ArenaOpts* options)
{
  if (!options) {
    return NULL;
  }
  assert(options->allocator.alloc);

  void* alloc_ctx = options->allocator.ctx;
  bebop_wire_arena_impl_t* arena = (bebop_wire_arena_impl_t*)options->allocator.alloc(
      NULL, 0, sizeof(bebop_wire_arena_impl_t), alloc_ctx
  );
  if (!arena) {
    return NULL;
  }

  arena->options = *options;
  arena->alloc = options->allocator.alloc;
  arena->alloc_ctx = alloc_ctx;
  BEBOP_WIRE_ATOMIC_INIT(&arena->current_block, NULL);
  BEBOP_WIRE_ATOMIC_INIT(&arena->total_allocated, 0);
  BEBOP_WIRE_ATOMIC_INIT(&arena->total_used, 0);

  return arena;
}

static void bebop_wire_arena_destroy(bebop_wire_arena_impl_t* arena)
{
  if (!arena) {
    return;
  }

  const Bebop_WireAllocFn alloc = arena->alloc;
  void* ctx = arena->alloc_ctx;

  bebop_wire_arena_block_impl_t* block = BEBOP_WIRE_ATOMIC_LOAD(&arena->current_block);
  while (block) {
    bebop_wire_arena_block_impl_t* next = block->next;
    alloc(block, sizeof(bebop_wire_arena_block_impl_t) + block->capacity, 0, ctx);
    block = next;
  }

  alloc(arena, sizeof(bebop_wire_arena_impl_t), 0, ctx);
}

static void bebop_wire_arena_reset(bebop_wire_arena_impl_t* arena)
{
  if (!arena) {
    return;
  }

  bebop_wire_arena_block_impl_t* block = BEBOP_WIRE_ATOMIC_LOAD(&arena->current_block);
  while (block) {
    bebop_wire_arena_block_impl_t* next = block->next;
    arena->alloc(
        block, sizeof(bebop_wire_arena_block_impl_t) + block->capacity, 0, arena->alloc_ctx
    );
    block = next;
  }

  BEBOP_WIRE_ATOMIC_STORE(&arena->current_block, NULL);
  BEBOP_WIRE_ATOMIC_STORE(&arena->total_allocated, 0);
  BEBOP_WIRE_ATOMIC_STORE(&arena->total_used, 0);
}

static void* bebop_wire_arena_alloc(bebop_wire_arena_impl_t* arena, size_t size)
{
  if (!arena || size == 0) {
    return NULL;
  }

  size_t aligned_size = bebop_wire_align_size(size, BEBOP_ARENA_ALIGN);

  while (true) {
    bebop_wire_arena_block_impl_t* current = BEBOP_WIRE_ATOMIC_LOAD(&arena->current_block);

    if (!current || BEBOP_WIRE_ATOMIC_LOAD(&current->used) + aligned_size > current->capacity) {
      bebop_wire_arena_block_impl_t* new_block =
          bebop_wire_arena_allocate_block(arena, aligned_size);
      if (!new_block) {
        return NULL;
      }

      new_block->next = current;

      if (BEBOP_WIRE_ATOMIC_CAS_WEAK(&arena->current_block, &current, new_block)) {
        current = new_block;
        BEBOP_WIRE_ATOMIC_FETCH_ADD(
            &arena->total_allocated, sizeof(bebop_wire_arena_block_impl_t) + new_block->capacity
        );
      } else {
        arena->alloc(
            new_block,
            sizeof(bebop_wire_arena_block_impl_t) + new_block->capacity,
            0,
            arena->alloc_ctx
        );
        continue;
      }
    }

    size_t old_used = BEBOP_WIRE_ATOMIC_LOAD(&current->used);
    if (old_used + aligned_size <= current->capacity) {
      if (BEBOP_WIRE_ATOMIC_CAS_WEAK(&current->used, &old_used, old_used + aligned_size)) {
        BEBOP_WIRE_ATOMIC_FETCH_ADD(&arena->total_used, aligned_size);
        return (uint8_t*)(current + 1) + old_used;
      }
    } else {
      continue;
    }
  }
}

static void* bebop_wire_arena_realloc(
    bebop_wire_arena_impl_t* arena, void* ptr, size_t old_size, size_t new_size
)
{
  if (!ptr) {
    return bebop_wire_arena_alloc(arena, new_size);
  }
  if (new_size == 0) {
    return NULL;
  }
  if (new_size <= old_size) {
    return ptr;
  }

  const size_t aligned_old = bebop_wire_align_size(old_size, BEBOP_ARENA_ALIGN);
  const size_t aligned_new = bebop_wire_align_size(new_size, BEBOP_ARENA_ALIGN);

  bebop_wire_arena_block_impl_t* current = BEBOP_WIRE_ATOMIC_LOAD(&arena->current_block);
  if (current) {
    const uint8_t* block_data = (uint8_t*)(current + 1);
    size_t used = BEBOP_WIRE_ATOMIC_LOAD(&current->used);

    // Check if ptr is the topmost allocation - can extend in place
    if ((uint8_t*)ptr + aligned_old == block_data + used) {
      const size_t extra = aligned_new - aligned_old;
      if (used + extra <= current->capacity) {
        size_t new_used = used + extra;
        if (BEBOP_WIRE_ATOMIC_CAS_WEAK(&current->used, &used, new_used)) {
          BEBOP_WIRE_ATOMIC_FETCH_ADD(&arena->total_used, (ptrdiff_t)extra);
          return ptr;
        }
      }
    }
  }

  // Not topmost or doesn't fit - alloc new and copy
  void* new_ptr = bebop_wire_arena_alloc(arena, new_size);
  if (new_ptr) {
    memcpy(new_ptr, ptr, old_size);
  }
  return new_ptr;
}

Bebop_WireCtx* Bebop_WireCtx_New(const Bebop_WireCtxOpts* options)
{
  if (!options) {
    return NULL;
  }

  assert(options->arena_options.allocator.alloc);

  const Bebop_WireAllocFn alloc_fn = options->arena_options.allocator.alloc;
  void* alloc_ctx = options->arena_options.allocator.ctx;

  Bebop_WireCtx* context = (Bebop_WireCtx*)alloc_fn(NULL, 0, sizeof(Bebop_WireCtx), alloc_ctx);
  if (!context) {
    return NULL;
  }

  context->arena = bebop_wire_arena_create(&options->arena_options);
  if (!context->arena) {
    alloc_fn(context, sizeof(Bebop_WireCtx), 0, alloc_ctx);
    return NULL;
  }

  context->options = *options;
  return context;
}

void Bebop_WireCtx_Free(Bebop_WireCtx* context)
{
  if (!context) {
    return;
  }

  const Bebop_WireAllocFn alloc_fn = context->arena->alloc;
  void* alloc_ctx = context->arena->alloc_ctx;
  bebop_wire_arena_destroy(context->arena);
  alloc_fn(context, sizeof(Bebop_WireCtx), 0, alloc_ctx);
}

void Bebop_WireCtx_Reset(Bebop_WireCtx* context)
{
  if (!context) {
    return;
  }
  bebop_wire_arena_reset(context->arena);
}

size_t Bebop_WireCtx_Allocated(const Bebop_WireCtx* context)
{
  return context ? BEBOP_WIRE_ATOMIC_LOAD(&context->arena->total_allocated) : 0;
}

size_t Bebop_WireCtx_Used(const Bebop_WireCtx* context)
{
  return context ? BEBOP_WIRE_ATOMIC_LOAD(&context->arena->total_used) : 0;
}

Bebop_WireResult Bebop_WireCtx_Reader(
    Bebop_WireCtx* context, const uint8_t* buffer, size_t buffer_length, Bebop_Reader** out
)
{
  if (!context || !buffer || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_Reader* reader =
      (Bebop_Reader*)bebop_wire_arena_alloc(context->arena, sizeof(Bebop_Reader));
  if (!reader) {
    return BEBOP_WIRE_ERR_OOM;
  }

  reader->start = buffer;
  reader->current = buffer;
  reader->end = buffer + buffer_length;
  reader->context = context;
  *out = reader;
  return BEBOP_WIRE_OK;
}

void Bebop_Reader_Reset(Bebop_Reader* reader, const uint8_t* buffer, size_t buffer_length)
{
  if (!reader || !buffer) {
    return;
  }
  reader->start = buffer;
  reader->current = buffer;
  reader->end = buffer + buffer_length;
}

void Bebop_Reader_Seek(Bebop_Reader* reader, const uint8_t* position)
{
  if (!reader) {
    return;
  }
  if (position >= reader->start && position <= reader->end) {
    reader->current = position;
  }
}

void Bebop_Reader_Skip(Bebop_Reader* reader, size_t amount)
{
  if (!reader) {
    return;
  }
  if (reader->current + amount <= reader->end) {
    reader->current += amount;
  }
}

Bebop_WireResult Bebop_Reader_GetByte(Bebop_Reader* reader, uint8_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + sizeof(uint8_t) > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

  *out = *reader->current++;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetU16(Bebop_Reader* reader, uint16_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + sizeof(uint16_t) > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, sizeof(uint16_t));
  reader->current += sizeof(uint16_t);
#else
  const uint16_t b0 = *reader->current++;
  const uint16_t b1 = *reader->current++;
  *out = (b1 << 8) | b0;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetU32(Bebop_Reader* reader, uint32_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + sizeof(uint32_t) > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, sizeof(uint32_t));
  reader->current += sizeof(uint32_t);
#else
  const uint32_t b0 = *reader->current++;
  const uint32_t b1 = *reader->current++;
  const uint32_t b2 = *reader->current++;
  const uint32_t b3 = *reader->current++;
  *out = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetU64(Bebop_Reader* reader, uint64_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + sizeof(uint64_t) > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, sizeof(uint64_t));
  reader->current += sizeof(uint64_t);
#else
  const uint64_t b0 = *reader->current++;
  const uint64_t b1 = *reader->current++;
  const uint64_t b2 = *reader->current++;
  const uint64_t b3 = *reader->current++;
  const uint64_t b4 = *reader->current++;
  const uint64_t b5 = *reader->current++;
  const uint64_t b6 = *reader->current++;
  const uint64_t b7 = *reader->current++;
  *out = (b7 << 0x38) | (b6 << 0x30) | (b5 << 0x28) | (b4 << 0x20) | (b3 << 0x18) | (b2 << 0x10)
      | (b1 << 0x08) | b0;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetI8(Bebop_Reader* reader, int8_t* out)
{
  return Bebop_Reader_GetByte(reader, (uint8_t*)out);
}

Bebop_WireResult Bebop_Reader_GetI16(Bebop_Reader* reader, int16_t* out)
{
  return Bebop_Reader_GetU16(reader, (uint16_t*)out);
}

Bebop_WireResult Bebop_Reader_GetI32(Bebop_Reader* reader, int32_t* out)
{
  return Bebop_Reader_GetU32(reader, (uint32_t*)out);
}

Bebop_WireResult Bebop_Reader_GetI64(Bebop_Reader* reader, int64_t* out)
{
  return Bebop_Reader_GetU64(reader, (uint64_t*)out);
}

Bebop_WireResult Bebop_Reader_GetBool(Bebop_Reader* reader, bool* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  uint8_t byte;
  const Bebop_WireResult result = Bebop_Reader_GetByte(reader, &byte);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_WIRE_OK)) {
    *out = byte != 0;
  }
  return result;
}

Bebop_WireResult Bebop_Reader_GetF16(Bebop_Reader* reader, Bebop_Float16* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + sizeof(uint16_t) > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, sizeof(uint16_t));
  reader->current += sizeof(uint16_t);
#else
  uint16_t bits;
  const uint16_t b0 = *reader->current++;
  const uint16_t b1 = *reader->current++;
  bits = (b1 << 8) | b0;
  memcpy(out, &bits, sizeof(uint16_t));
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetBF16(Bebop_Reader* reader, Bebop_BFloat16* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + sizeof(uint16_t) > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, sizeof(uint16_t));
  reader->current += sizeof(uint16_t);
#else
  uint16_t bits;
  const uint16_t b0 = *reader->current++;
  const uint16_t b1 = *reader->current++;
  bits = (b1 << 8) | b0;
  memcpy(out, &bits, sizeof(uint16_t));
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetF32(Bebop_Reader* reader, float* out)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  uint32_t bits;
  const Bebop_WireResult result = Bebop_Reader_GetU32(reader, &bits);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_WIRE_OK)) {
    memcpy(out, &bits, sizeof(float));
  }
  return result;
}

Bebop_WireResult Bebop_Reader_GetF64(Bebop_Reader* reader, double* out)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  uint64_t bits;
  const Bebop_WireResult result = Bebop_Reader_GetU64(reader, &bits);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_WIRE_OK)) {
    memcpy(out, &bits, sizeof(double));
  }
  return result;
}

Bebop_WireResult Bebop_Reader_GetI128(Bebop_Reader* reader, Bebop_Int128* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + 16 > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_HAS_I128
  uint64_t low, high;
  memcpy(&low, reader->current, sizeof(uint64_t));
  memcpy(&high, reader->current + 8, sizeof(uint64_t));
  *out = (Bebop_Int128)((Bebop_UInt128)low | ((Bebop_UInt128)high << 64));
  reader->current += 16;
#else
  memcpy(out, reader->current, 16);
  reader->current += 16;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetU128(Bebop_Reader* reader, Bebop_UInt128* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + 16 > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_HAS_I128
  uint64_t low, high;
  memcpy(&low, reader->current, sizeof(uint64_t));
  memcpy(&high, reader->current + 8, sizeof(uint64_t));
  *out = (Bebop_UInt128)low | ((Bebop_UInt128)high << 64);
  reader->current += 16;
#else
  memcpy(out, reader->current, 16);
  reader->current += 16;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetUUID(Bebop_Reader* reader, Bebop_UUID* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + 16 > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  memcpy(out->bytes, reader->current, 16);
  reader->current += 16;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetTimestamp(Bebop_Reader* reader, Bebop_Timestamp* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + 16 > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

  memcpy(out, reader->current, 16);
  reader->current += 16;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetDuration(Bebop_Reader* reader, Bebop_Duration* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + 12 > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

  memcpy(out, reader->current, 12);
  reader->current += 12;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetLen(Bebop_Reader* reader, uint32_t* out)
{
  const Bebop_WireResult result = Bebop_Reader_GetU32(reader, out);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_WIRE_OK)) {
    if (BEBOP_WIRE_UNLIKELY(reader->current + *out > reader->end)) {
      return BEBOP_WIRE_ERR_MALFORMED;
    }
  }
  return result;
}

Bebop_WireResult Bebop_Reader_GetStr(Bebop_Reader* reader, Bebop_Str* out)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  uint32_t length;
  const Bebop_WireResult result = Bebop_Reader_GetU32(reader, &length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  const size_t total = length + 1;
  if (BEBOP_WIRE_UNLIKELY(reader->current + total > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

  out->data = (const char*)reader->current;
  out->length = length;
  reader->current += total;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetByteArray(Bebop_Reader* reader, Bebop_Bytes* out)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  uint32_t length;
  const Bebop_WireResult result = Bebop_Reader_GetLen(reader, &length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  out->data = reader->current;
  out->length = length;
  reader->current += length;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedBytes(
    Bebop_Reader* reader, size_t byte_count, Bebop_Bytes* out
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + byte_count > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  out->data = reader->current;
  out->length = byte_count;
  reader->current += byte_count;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_WireCtx_Writer(Bebop_WireCtx* context, Bebop_Writer** out)
{
  if (!context || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_Writer* writer =
      (Bebop_Writer*)bebop_wire_arena_alloc(context->arena, sizeof(Bebop_Writer));
  if (!writer) {
    return BEBOP_WIRE_ERR_OOM;
  }

  uint8_t* buffer =
      (uint8_t*)bebop_wire_arena_alloc(context->arena, context->options.initial_writer_size);
  if (!buffer) {
    return BEBOP_WIRE_ERR_OOM;
  }

  writer->buffer = buffer;
  writer->current = buffer;
  writer->end = buffer + context->options.initial_writer_size;
  writer->context = context;
  *out = writer;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_WireCtx_WriterHint(
    Bebop_WireCtx* context, size_t size_hint, Bebop_Writer** out
)
{
  if (!context || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_Writer* writer =
      (Bebop_Writer*)bebop_wire_arena_alloc(context->arena, sizeof(Bebop_Writer));
  if (!writer) {
    return BEBOP_WIRE_ERR_OOM;
  }

  const size_t buffer_size = size_hint > context->options.initial_writer_size
      ? size_hint
      : context->options.initial_writer_size;

  uint8_t* buffer = (uint8_t*)bebop_wire_arena_alloc(context->arena, buffer_size);
  if (!buffer) {
    return BEBOP_WIRE_ERR_OOM;
  }

  writer->buffer = buffer;
  writer->current = buffer;
  writer->end = buffer + buffer_size;
  writer->context = context;
  *out = writer;
  return BEBOP_WIRE_OK;
}

void Bebop_Writer_Reset(Bebop_Writer* writer)
{
  if (!writer) {
    return;
  }
  writer->current = writer->buffer;
}

Bebop_WireResult Bebop_Writer_Ensure(Bebop_Writer* writer, size_t additional_bytes)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }

  if (BEBOP_WIRE_LIKELY(writer->current + additional_bytes <= writer->end)) {
    return BEBOP_WIRE_OK;
  }

  const size_t current_size = (size_t)(writer->end - writer->buffer);
  const size_t used_size = (size_t)(writer->current - writer->buffer);
  size_t new_size = current_size * 2;

  while (new_size < used_size + additional_bytes) {
    new_size *= 2;
  }

  uint8_t* new_buffer = (uint8_t*)bebop_wire_arena_alloc(writer->context->arena, new_size);
  if (!new_buffer) {
    return BEBOP_WIRE_ERR_OOM;
  }

  memcpy(new_buffer, writer->buffer, used_size);
  writer->buffer = new_buffer;
  writer->current = new_buffer + used_size;
  writer->end = new_buffer + new_size;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetByte(Bebop_Writer* writer, uint8_t value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + 1 > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, 1);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  *writer->current++ = value;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetU16(Bebop_Writer* writer, uint16_t value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + sizeof(uint16_t) > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, sizeof(uint16_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, &value, sizeof(uint16_t));
  writer->current += sizeof(uint16_t);
#else
  *writer->current++ = value;
  *writer->current++ = value >> 8;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetU32(Bebop_Writer* writer, uint32_t value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + sizeof(uint32_t) > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, sizeof(uint32_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, &value, sizeof(uint32_t));
  writer->current += sizeof(uint32_t);
#else
  *writer->current++ = value;
  *writer->current++ = value >> 8;
  *writer->current++ = value >> 16;
  *writer->current++ = value >> 24;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetU64(Bebop_Writer* writer, uint64_t value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + sizeof(uint64_t) > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, sizeof(uint64_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, &value, sizeof(uint64_t));
  writer->current += sizeof(uint64_t);
#else
  *writer->current++ = value;
  *writer->current++ = value >> 0x08;
  *writer->current++ = value >> 0x10;
  *writer->current++ = value >> 0x18;
  *writer->current++ = value >> 0x20;
  *writer->current++ = value >> 0x28;
  *writer->current++ = value >> 0x30;
  *writer->current++ = value >> 0x38;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetI8(Bebop_Writer* writer, int8_t value)
{
  return Bebop_Writer_SetByte(writer, (uint8_t)value);
}

Bebop_WireResult Bebop_Writer_SetI16(Bebop_Writer* writer, int16_t value)
{
  return Bebop_Writer_SetU16(writer, (uint16_t)value);
}

Bebop_WireResult Bebop_Writer_SetI32(Bebop_Writer* writer, int32_t value)
{
  return Bebop_Writer_SetU32(writer, (uint32_t)value);
}

Bebop_WireResult Bebop_Writer_SetI64(Bebop_Writer* writer, int64_t value)
{
  return Bebop_Writer_SetU64(writer, (uint64_t)value);
}

Bebop_WireResult Bebop_Writer_SetBool(Bebop_Writer* writer, bool value)
{
  return Bebop_Writer_SetByte(writer, value ? 1 : 0);
}

Bebop_WireResult Bebop_Writer_SetF16(Bebop_Writer* writer, Bebop_Float16 value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + sizeof(uint16_t) > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, sizeof(uint16_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, &value, sizeof(uint16_t));
  writer->current += sizeof(uint16_t);
#else
  uint16_t bits;
  memcpy(&bits, &value, sizeof(uint16_t));
  *writer->current++ = (uint8_t)bits;
  *writer->current++ = (uint8_t)(bits >> 8);
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetBF16(Bebop_Writer* writer, Bebop_BFloat16 value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + sizeof(uint16_t) > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, sizeof(uint16_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, &value, sizeof(uint16_t));
  writer->current += sizeof(uint16_t);
#else
  uint16_t bits;
  memcpy(&bits, &value, sizeof(uint16_t));
  *writer->current++ = (uint8_t)bits;
  *writer->current++ = (uint8_t)(bits >> 8);
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetF32(Bebop_Writer* writer, float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(float));
  return Bebop_Writer_SetU32(writer, bits);
}

Bebop_WireResult Bebop_Writer_SetF64(Bebop_Writer* writer, double value)
{
  uint64_t bits;
  memcpy(&bits, &value, sizeof(double));
  return Bebop_Writer_SetU64(writer, bits);
}

Bebop_WireResult Bebop_Writer_SetI128(Bebop_Writer* writer, Bebop_Int128 value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + 16 > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_HAS_I128
  const Bebop_UInt128 uval = (Bebop_UInt128)value;
  const uint64_t low = (uint64_t)uval;
  const uint64_t high = (uint64_t)(uval >> 64);
  memcpy(writer->current, &low, sizeof(uint64_t));
  memcpy(writer->current + 8, &high, sizeof(uint64_t));
#else
  memcpy(writer->current, &value, 16);
#endif
  writer->current += 16;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetU128(Bebop_Writer* writer, Bebop_UInt128 value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + 16 > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_HAS_I128
  const uint64_t low = (uint64_t)value;
  const uint64_t high = (uint64_t)(value >> 64);
  memcpy(writer->current, &low, sizeof(uint64_t));
  memcpy(writer->current + 8, &high, sizeof(uint64_t));
#else
  memcpy(writer->current, &value, 16);
#endif
  writer->current += 16;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetUUID(Bebop_Writer* writer, Bebop_UUID value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + 16 > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
  memcpy(writer->current, value.bytes, 16);
  writer->current += 16;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetTimestamp(Bebop_Writer* writer, Bebop_Timestamp value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + 16 > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, &value, 16);
  writer->current += 16;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetDuration(Bebop_Writer* writer, Bebop_Duration value)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(writer->current + 12 > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, 12);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, &value, 12);
  writer->current += 12;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetStr(Bebop_Writer* writer, const char* data, size_t length)
{
  if (!writer || !data) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  const size_t total = length + 1;
  if (BEBOP_WIRE_UNLIKELY(writer->current + total > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, length);
  writer->current[length] = '\0';
  writer->current += total;

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetStrView(Bebop_Writer* writer, Bebop_Str view)
{
  return Bebop_Writer_SetStr(writer, view.data, view.length);
}

Bebop_WireResult Bebop_Writer_SetByteArray(Bebop_Writer* writer, const uint8_t* data, size_t length)
{
  if (!writer || !data) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length > 0) {
    if (BEBOP_WIRE_UNLIKELY(writer->current + length > writer->end)) {
      result = Bebop_Writer_Ensure(writer, length);
      if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
        return result;
      }
    }

    memcpy(writer->current, data, length);
    writer->current += length;
  }

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetByteArrayView(Bebop_Writer* writer, Bebop_Bytes view)
{
  return Bebop_Writer_SetByteArray(writer, view.data, view.length);
}

Bebop_WireResult Bebop_Writer_SetFixedBytes(
    Bebop_Writer* writer, const uint8_t* data, size_t byte_count
)
{
  if (!writer || !data) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (byte_count == 0) {
    return BEBOP_WIRE_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(writer->current + byte_count > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, byte_count);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, byte_count);
  writer->current += byte_count;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetF32Array(Bebop_Writer* writer, const float* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(float);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetF32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetF64Array(Bebop_Writer* writer, const double* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(double);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetF64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetU16Array(Bebop_Writer* writer, const uint16_t* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(uint16_t);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetU16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetI16Array(Bebop_Writer* writer, const int16_t* data, size_t length)
{
  return Bebop_Writer_SetU16Array(writer, (const uint16_t*)data, length);
}

Bebop_WireResult Bebop_Writer_SetU32Array(Bebop_Writer* writer, const uint32_t* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(uint32_t);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetU32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetI32Array(Bebop_Writer* writer, const int32_t* data, size_t length)
{
  return Bebop_Writer_SetU32Array(writer, (const uint32_t*)data, length);
}

Bebop_WireResult Bebop_Writer_SetU64Array(Bebop_Writer* writer, const uint64_t* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(uint64_t);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetU64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetI64Array(Bebop_Writer* writer, const int64_t* data, size_t length)
{
  return Bebop_Writer_SetU64Array(writer, (const uint64_t*)data, length);
}

Bebop_WireResult Bebop_Writer_SetI8Array(Bebop_Writer* writer, const int8_t* data, size_t length)
{
  return Bebop_Writer_SetByteArray(writer, (const uint8_t*)data, length);
}

Bebop_WireResult Bebop_Writer_SetU8Array(Bebop_Writer* writer, const uint8_t* data, size_t length)
{
  return Bebop_Writer_SetByteArray(writer, data, length);
}

Bebop_WireResult Bebop_Writer_SetF16Array(
    Bebop_Writer* writer, const Bebop_Float16* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(Bebop_Float16);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetF16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetBF16Array(
    Bebop_Writer* writer, const Bebop_BFloat16* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(Bebop_BFloat16);

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetBF16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetI128Array(
    Bebop_Writer* writer, const Bebop_Int128* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * 16;

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetI128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetU128Array(
    Bebop_Writer* writer, const Bebop_UInt128* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * 16;

  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = Bebop_Writer_SetU128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetBoolArray(Bebop_Writer* writer, const bool* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(writer->current + length > writer->end)) {
    result = Bebop_Writer_Ensure(writer, length);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  for (size_t i = 0; i < length; i++) {
    *writer->current++ = data[i] ? 1 : 0;
  }

  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetUUIDArray(
    Bebop_Writer* writer, const Bebop_UUID* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(Bebop_UUID);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetTimestampArray(
    Bebop_Writer* writer, const Bebop_Timestamp* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(Bebop_Timestamp);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetDurationArray(
    Bebop_Writer* writer, const Bebop_Duration* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_WireResult result = Bebop_Writer_SetU32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = length * sizeof(Bebop_Duration);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedU8Array(
    Bebop_Writer* writer, const uint8_t* data, size_t count
)
{
  return Bebop_Writer_SetFixedBytes(writer, data, count);
}

Bebop_WireResult Bebop_Writer_SetFixedI8Array(
    Bebop_Writer* writer, const int8_t* data, size_t count
)
{
  return Bebop_Writer_SetFixedBytes(writer, (const uint8_t*)data, count);
}

Bebop_WireResult Bebop_Writer_SetFixedBoolArray(
    Bebop_Writer* writer, const bool* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(writer->current + count > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, count);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, count);
  writer->current += count;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedU16Array(
    Bebop_Writer* writer, const uint16_t* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(uint16_t);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetU16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedI16Array(
    Bebop_Writer* writer, const int16_t* data, size_t count
)
{
  return Bebop_Writer_SetFixedU16Array(writer, (const uint16_t*)data, count);
}

Bebop_WireResult Bebop_Writer_SetFixedU32Array(
    Bebop_Writer* writer, const uint32_t* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(uint32_t);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetU32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedI32Array(
    Bebop_Writer* writer, const int32_t* data, size_t count
)
{
  return Bebop_Writer_SetFixedU32Array(writer, (const uint32_t*)data, count);
}

Bebop_WireResult Bebop_Writer_SetFixedU64Array(
    Bebop_Writer* writer, const uint64_t* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(uint64_t);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetU64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedI64Array(
    Bebop_Writer* writer, const int64_t* data, size_t count
)
{
  return Bebop_Writer_SetFixedU64Array(writer, (const uint64_t*)data, count);
}

Bebop_WireResult Bebop_Writer_SetFixedF16Array(
    Bebop_Writer* writer, const Bebop_Float16* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_Float16);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetF16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedBF16Array(
    Bebop_Writer* writer, const Bebop_BFloat16* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_BFloat16);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetBF16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedF32Array(
    Bebop_Writer* writer, const float* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(float);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetF32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedF64Array(
    Bebop_Writer* writer, const double* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(double);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetF64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedI128Array(
    Bebop_Writer* writer, const Bebop_Int128* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_Int128);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetI128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedU128Array(
    Bebop_Writer* writer, const Bebop_UInt128* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_UInt128);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Writer_SetU128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedUUIDArray(
    Bebop_Writer* writer, const Bebop_UUID* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_UUID);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedTimestampArray(
    Bebop_Writer* writer, const Bebop_Timestamp* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_Timestamp);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetFixedDurationArray(
    Bebop_Writer* writer, const Bebop_Duration* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }

  const size_t total_bytes = count * sizeof(Bebop_Duration);
  if (BEBOP_WIRE_UNLIKELY(writer->current + total_bytes > writer->end)) {
    const Bebop_WireResult result = Bebop_Writer_Ensure(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedU8Array(Bebop_Reader* reader, uint8_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + count > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  memcpy(out, reader->current, count);
  reader->current += count;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedI8Array(Bebop_Reader* reader, int8_t* out, size_t count)
{
  return Bebop_Reader_GetFixedU8Array(reader, (uint8_t*)out, count);
}

Bebop_WireResult Bebop_Reader_GetFixedBoolArray(Bebop_Reader* reader, bool* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current + count > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  memcpy(out, reader->current, count);
  reader->current += count;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedU16Array(Bebop_Reader* reader, uint16_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(uint16_t);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetU16(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedI16Array(Bebop_Reader* reader, int16_t* out, size_t count)
{
  return Bebop_Reader_GetFixedU16Array(reader, (uint16_t*)out, count);
}

Bebop_WireResult Bebop_Reader_GetFixedU32Array(Bebop_Reader* reader, uint32_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(uint32_t);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetU32(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedI32Array(Bebop_Reader* reader, int32_t* out, size_t count)
{
  return Bebop_Reader_GetFixedU32Array(reader, (uint32_t*)out, count);
}

Bebop_WireResult Bebop_Reader_GetFixedU64Array(Bebop_Reader* reader, uint64_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(uint64_t);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetU64(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedI64Array(Bebop_Reader* reader, int64_t* out, size_t count)
{
  return Bebop_Reader_GetFixedU64Array(reader, (uint64_t*)out, count);
}

Bebop_WireResult Bebop_Reader_GetFixedF16Array(
    Bebop_Reader* reader, Bebop_Float16* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_Float16);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetF16(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedBF16Array(
    Bebop_Reader* reader, Bebop_BFloat16* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_BFloat16);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetBF16(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedF32Array(Bebop_Reader* reader, float* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(float);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetF32(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedF64Array(Bebop_Reader* reader, double* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(double);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetF64(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedI128Array(
    Bebop_Reader* reader, Bebop_Int128* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_Int128);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetI128(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedU128Array(
    Bebop_Reader* reader, Bebop_UInt128* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_UInt128);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_WireResult result = Bebop_Reader_GetU128(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_WIRE_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedUUIDArray(Bebop_Reader* reader, Bebop_UUID* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_UUID);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedTimestampArray(
    Bebop_Reader* reader, Bebop_Timestamp* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_Timestamp);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Reader_GetFixedDurationArray(
    Bebop_Reader* reader, Bebop_Duration* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (count == 0) {
    return BEBOP_WIRE_OK;
  }
  const size_t total_bytes = count * sizeof(Bebop_Duration);
  if (BEBOP_WIRE_UNLIKELY(reader->current + total_bytes > reader->end)) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_SetLen(Bebop_Writer* writer, size_t* position)
{
  if (!writer || !position) {
    return BEBOP_WIRE_ERR_NULL;
  }

  *position = Bebop_Writer_Len(writer);
  return Bebop_Writer_SetU32(writer, 0);
}

Bebop_WireResult Bebop_Writer_FillLen(Bebop_Writer* writer, size_t position, uint32_t length)
{
  if (!writer) {
    return BEBOP_WIRE_ERR_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(position + sizeof(uint32_t) > Bebop_Writer_Len(writer))) {
    return BEBOP_WIRE_ERR_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->buffer + position, &length, sizeof(uint32_t));
#else
  writer->buffer[position++] = length;
  writer->buffer[position++] = length >> 8;
  writer->buffer[position++] = length >> 16;
  writer->buffer[position++] = length >> 24;
#endif
  return BEBOP_WIRE_OK;
}

Bebop_WireResult Bebop_Writer_Buf(Bebop_Writer* writer, uint8_t** buffer, size_t* length)
{
  if (!writer || !buffer || !length) {
    return BEBOP_WIRE_ERR_NULL;
  }

  *buffer = writer->buffer;
  *length = Bebop_Writer_Len(writer);
  return BEBOP_WIRE_OK;
}

static const uint8_t bebop__wire_ascii_to_hex[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,
    4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0, 10, 11, 12, 13, 14, 15, 0,  0,  0,  0,  0,  0,  0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  10, 11, 12, 13, 14, 15, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    /* rest are zeros */
};

static const char bebop__wire_hex_chars[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

Bebop_UUID Bebop_UUID_FromString(const char* str)
{
  Bebop_UUID uuid = {0};
  if (!str) {
    return uuid;
  }

  const char* s = str;
  int byte_idx = 0;

  while (*s && byte_idx < 16) {
    if (*s == '-') {
      s++;
      continue;
    }
    if (!*(s + 1)) {
      return (Bebop_UUID) {0};
    }

    const uint8_t high = bebop__wire_ascii_to_hex[(uint8_t)*s++];
    const uint8_t low = bebop__wire_ascii_to_hex[(uint8_t)*s++];
    uuid.bytes[byte_idx++] = (uint8_t)((high << 4) | low);
  }

  if (byte_idx != 16) {
    return (Bebop_UUID) {0};
  }
  return uuid;
}

size_t Bebop_UUID_ToString(Bebop_UUID uuid, char* buf, size_t len)
{
  if (!buf || len < BEBOP_WIRE_UUID_STR_LEN + 1) {
    return 0;
  }

  char* p = buf;
  for (int i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      *p++ = '-';
    }
    *p++ = bebop__wire_hex_chars[(uuid.bytes[i] >> 4) & 0xF];
    *p++ = bebop__wire_hex_chars[uuid.bytes[i] & 0xF];
  }
  *p = '\0';

  return BEBOP_WIRE_UUID_STR_LEN;
}

Bebop_WireCtxOpts Bebop_WireCtx_DefaultOpts(void)
{
  const Bebop_WireCtxOpts options = {
      .arena_options =
          {.initial_block_size = 4096,
           .max_block_size = 1048576,
           .allocator = {.alloc = NULL, .ctx = NULL}},
      .initial_writer_size = 1024
  };
  return options;
}

size_t Bebop_Reader_Pos(const Bebop_Reader* reader)
{
  return reader ? (size_t)(reader->current - reader->start) : 0;
}

const uint8_t* Bebop_Reader_Ptr(const Bebop_Reader* reader)
{
  return reader ? reader->current : NULL;
}

size_t Bebop_Writer_Len(const Bebop_Writer* writer)
{
  return writer ? (size_t)(writer->current - writer->buffer) : 0;
}

size_t Bebop_Writer_Remaining(const Bebop_Writer* writer)
{
  return writer ? (size_t)(writer->end - writer->current) : 0;
}

Bebop_Str Bebop_Str_FromCStr(const char* str)
{
  const Bebop_Str view = {str, str ? strlen(str) : 0};
  return view;
}

bool Bebop_Str_Equal(Bebop_Str a, Bebop_Str b)
{
  return a.length == b.length && (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

bool Bebop_UUID_Equal(Bebop_UUID a, Bebop_UUID b)
{
  return memcmp(&a, &b, sizeof(Bebop_UUID)) == 0;
}

void* Bebop_WireCtx_Alloc(Bebop_WireCtx* context, size_t size)
{
  return context ? bebop_wire_arena_alloc(context->arena, size) : NULL;
}

void* Bebop_WireCtx_Realloc(Bebop_WireCtx* context, void* ptr, size_t old_size, size_t new_size)
{
  return context ? bebop_wire_arena_realloc(context->arena, ptr, old_size, new_size) : NULL;
}

// #region Map Implementation (SwissTable)
//
// Based on Google's SwissTable / Abseil flat_hash_map
// Reference: CppCon 2017 - Matt Kulukundis
// https://abseil.io/about/design/swisstables

#define BEBOP_MAP_GROUP_SIZE 8
#define BEBOP_MAP_INITIAL_CAPACITY 8
#define BEBOP_MAP_LOAD_FACTOR 7  // 7/8 = 87.5%

// Control byte values
#define CTRL_EMPTY ((int8_t)-128)  // 0b10000000 - slot never used
#define CTRL_DELETED ((int8_t)-2)  // 0b11111110 - tombstone

// Extract H1 (upper 57 bits) and H2 (lower 7 bits) from hash
#define H1(hash) ((hash) >> 7)
#define H2(hash) ((int8_t)((hash) & 0x7F))

// Check if control byte is full (has a key)
#define CTRL_IS_FULL(c) ((c) >= 0)
#define CTRL_IS_EMPTY_OR_DELETED(c) ((c) < 0)

// Portable SWAR (SIMD Within A Register) for 8 bytes at a time
// Find bytes matching h2 in a 64-bit word
static inline uint64_t bebop__map_match_h2(uint64_t ctrl_word, int8_t h2)
{
  const uint64_t broadcast = 0x0101010101010101ULL * (uint8_t)h2;
  const uint64_t diff = ctrl_word ^ broadcast;
  // Find zero bytes using the null-byte detection trick
  return (diff - 0x0101010101010101ULL) & ~diff & 0x8080808080808080ULL;
}

// Find empty slots (0x80) in a 64-bit control word
static inline uint64_t bebop__map_match_empty(uint64_t ctrl_word)
{
  // Empty = 0x80, Deleted = 0xFE, Full = 0x00-0x7F
  // Empty has bit pattern 10000000, only one with bit 7 set and bit 0 clear
  return ctrl_word & ~(ctrl_word << 1) & 0x8080808080808080ULL;
}

// Find empty or deleted slots
static inline uint64_t bebop__map_match_empty_or_deleted(uint64_t ctrl_word)
{
  // Both empty (0x80) and deleted (0xFE) have high bit set
  return ctrl_word & 0x8080808080808080ULL;
}

// Count trailing zeros in match result, divided by 8 to get slot index
static inline size_t bebop__map_match_first(uint64_t match)
{
  if (match == 0) {
    return BEBOP_MAP_GROUP_SIZE;
  }
#if defined(__GNUC__) || defined(__clang__)
  return (size_t)__builtin_ctzll(match) / 8;
#elif defined(_MSC_VER)
  unsigned long idx;
  _BitScanForward64(&idx, match);
  return idx / 8;
#else
  size_t n = 0;
  while (!(match & 0xFF)) {
    match >>= 8;
    n++;
  }
  return n;
#endif
}

// Triangular probing sequence: 0, 1, 3, 6, 10, 15, ...
// probe(i) = i * (i + 1) / 2
static inline size_t bebop__map_probe_offset(size_t i)
{
  return (i * (i + 1)) / 2;
}

static bool bebop__map_grow(Bebop_Map* m);

// Clear bits up to and including the matched position (to advance to next
// match)
static inline uint64_t bebop__map_clear_match(uint64_t match, size_t offset)
{
  const size_t shift = (offset + 1) * 8;
  return (shift < 64) ? (match & ~((1ULL << shift) - 1)) : 0;
}

void Bebop_Map_Init(Bebop_Map* m, Bebop_WireCtx* ctx, Bebop_MapHashFn hash, Bebop_MapEqFn eq)
{
  m->ctrl = NULL;
  m->slots = NULL;
  m->length = 0;
  m->capacity = 0;
  m->growth_left = 0;
  m->hash = hash;
  m->eq = eq;
  m->ctx = ctx;
}

void* Bebop_Map_Get(const Bebop_Map* m, const void* key)
{
  if (!m || !m->ctrl || m->length == 0) {
    return NULL;
  }

  const uint64_t h = m->hash(key);
  const int8_t h2 = H2(h);
  const size_t mask = m->capacity - 1;
  const size_t start = H1(h) & mask;

  for (size_t probe = 0;; probe++) {
    size_t group_start = (start + bebop__map_probe_offset(probe)) & mask;
    // Align to group boundary
    group_start &= ~(size_t)(BEBOP_MAP_GROUP_SIZE - 1);

    uint64_t ctrl_word;
    memcpy(&ctrl_word, m->ctrl + group_start, sizeof(ctrl_word));

    // Find slots matching H2
    uint64_t match = bebop__map_match_h2(ctrl_word, h2);
    while (match) {
      const size_t offset = bebop__map_match_first(match);
      const size_t idx = group_start + offset;
      if (m->eq(m->slots[idx].key, key)) {
        return m->slots[idx].value;
      }
      match &= match - 1;  // clear lowest set bit-group
      match = bebop__map_clear_match(match, offset);
    }

    // If any empty slot found, key doesn't exist
    if (bebop__map_match_empty(ctrl_word)) {
      return NULL;
    }
  }
}

bool Bebop_Map_Put(Bebop_Map* m, void* key, void* value)
{
  if (!m) {
    return false;
  }

  if (m->growth_left == 0) {
    if (!bebop__map_grow(m)) {
      return false;
    }
  }

  const uint64_t h = m->hash(key);
  const int8_t h2 = H2(h);
  const size_t mask = m->capacity - 1;
  const size_t start = H1(h) & mask;

  size_t insert_idx = SIZE_MAX;

  for (size_t probe = 0;; probe++) {
    size_t group_start = (start + bebop__map_probe_offset(probe)) & mask;
    group_start &= ~(size_t)(BEBOP_MAP_GROUP_SIZE - 1);

    uint64_t ctrl_word;
    memcpy(&ctrl_word, m->ctrl + group_start, sizeof(ctrl_word));

    // Check for existing key
    uint64_t match = bebop__map_match_h2(ctrl_word, h2);
    while (match) {
      const size_t offset = bebop__map_match_first(match);
      const size_t idx = group_start + offset;
      if (m->eq(m->slots[idx].key, key)) {
        m->slots[idx].value = value;
        return true;
      }
      match = bebop__map_clear_match(match, offset);
    }

    // Find insertion point (first empty or deleted)
    if (insert_idx == SIZE_MAX) {
      const uint64_t empty_match = bebop__map_match_empty_or_deleted(ctrl_word);
      if (empty_match) {
        const size_t offset = bebop__map_match_first(empty_match);
        insert_idx = group_start + offset;
      }
    }

    // If we found an empty slot, no need to keep searching for duplicates
    if (bebop__map_match_empty(ctrl_word)) {
      break;
    }
  }

  // Insert at found position
  if (insert_idx != SIZE_MAX) {
    if (m->ctrl[insert_idx] == CTRL_EMPTY) {
      m->growth_left--;
    }
    m->ctrl[insert_idx] = h2;
    m->slots[insert_idx].key = key;
    m->slots[insert_idx].value = value;
    m->length++;
    return true;
  }

  return false;
}

bool Bebop_Map_Del(Bebop_Map* m, const void* key)
{
  if (!m || !m->ctrl || m->length == 0) {
    return false;
  }

  const uint64_t h = m->hash(key);
  const int8_t h2 = H2(h);
  const size_t mask = m->capacity - 1;
  const size_t start = H1(h) & mask;

  for (size_t probe = 0;; probe++) {
    size_t group_start = (start + bebop__map_probe_offset(probe)) & mask;
    group_start &= ~(size_t)(BEBOP_MAP_GROUP_SIZE - 1);

    uint64_t ctrl_word;
    memcpy(&ctrl_word, m->ctrl + group_start, sizeof(ctrl_word));

    uint64_t match = bebop__map_match_h2(ctrl_word, h2);
    while (match) {
      const size_t offset = bebop__map_match_first(match);
      const size_t idx = group_start + offset;
      if (m->eq(m->slots[idx].key, key)) {
        m->ctrl[idx] = CTRL_DELETED;
        m->length--;
        return true;
      }
      match = bebop__map_clear_match(match, offset);
    }

    if (bebop__map_match_empty(ctrl_word)) {
      return false;
    }
  }
}

void Bebop_Map_Clear(Bebop_Map* m)
{
  if (m && m->ctrl) {
    memset(m->ctrl, CTRL_EMPTY, m->capacity);
    m->length = 0;
    m->growth_left = (m->capacity * BEBOP_MAP_LOAD_FACTOR) / 8;
  }
}

void Bebop_MapIter_Init(Bebop_MapIter* it, const Bebop_Map* m)
{
  it->map = m;
  it->index = 0;
}

bool Bebop_MapIter_Next(Bebop_MapIter* it, void** key, void** value)
{
  if (!it || !it->map || !it->map->ctrl) {
    return false;
  }

  while (it->index < it->map->capacity) {
    if (CTRL_IS_FULL(it->map->ctrl[it->index])) {
      if (key) {
        *key = it->map->slots[it->index].key;
      }
      if (value) {
        *value = it->map->slots[it->index].value;
      }
      it->index++;
      return true;
    }
    it->index++;
  }
  return false;
}

static bool bebop__map_grow(Bebop_Map* m)
{
  const size_t new_cap = m->capacity ? m->capacity * 2 : BEBOP_MAP_INITIAL_CAPACITY;

  // Allocate control bytes + sentinel group + slots
  const size_t ctrl_size = new_cap + BEBOP_MAP_GROUP_SIZE;  // extra group for probing wraparound
  const size_t slots_size = new_cap * sizeof(Bebop_MapSlot);

  int8_t* new_ctrl = Bebop_WireCtx_Alloc(m->ctx, ctrl_size);
  if (!new_ctrl) {
    return false;
  }
  memset(new_ctrl, CTRL_EMPTY, ctrl_size);

  Bebop_MapSlot* new_slots = Bebop_WireCtx_Alloc(m->ctx, slots_size);
  if (!new_slots) {
    return false;
  }

  const int8_t* old_ctrl = m->ctrl;
  const Bebop_MapSlot* old_slots = m->slots;
  const size_t old_cap = m->capacity;

  m->ctrl = new_ctrl;
  m->slots = new_slots;
  m->capacity = new_cap;
  m->length = 0;
  m->growth_left = (new_cap * BEBOP_MAP_LOAD_FACTOR) / 8;

  // Rehash all entries
  for (size_t i = 0; i < old_cap; i++) {
    if (old_ctrl && CTRL_IS_FULL(old_ctrl[i])) {
      Bebop_Map_Put(m, old_slots[i].key, old_slots[i].value);
    }
  }

  return true;
}

// wyhash - fast portable hash (public domain, Wang Yi)
// Standalone implementation for bebop

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#pragma intrinsic(_umul128)
#endif

static inline uint64_t bebop__wyrot(uint64_t x)
{
  return (x >> 32) | (x << 32);
}

static inline void bebop__wymum(uint64_t* a, uint64_t* b)
{
#if defined(__SIZEOF_INT128__)
  const __uint128_t r = (__uint128_t)*a * *b;
  *a = (uint64_t)r;
  *b = (uint64_t)(r >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
  *a = _umul128(*a, *b, b);
#else
  uint64_t ha = *a >> 32, la = (uint32_t)*a;
  uint64_t hb = *b >> 32, lb = (uint32_t)*b;
  uint64_t rh = ha * hb, rl = la * lb;
  uint64_t rm0 = ha * lb, rm1 = hb * la;
  uint64_t t = rl + (rm0 << 32);
  uint64_t c = t < rl;
  uint64_t lo = t + (rm1 << 32);
  c += lo < t;
  uint64_t hi = rh + (rm0 >> 32) + (rm1 >> 32) + c;
  *a = lo;
  *b = hi;
#endif
}

static inline uint64_t bebop__wymix(uint64_t a, uint64_t b)
{
  bebop__wymum(&a, &b);
  return a ^ b;
}

#if BEBOP_WIRE_ASSUME_LE
static inline uint64_t bebop__wyr8(const uint8_t* p)
{
  uint64_t v;
  memcpy(&v, p, 8);
  return v;
}

static inline uint64_t bebop__wyr4(const uint8_t* p)
{
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
#elif defined(__GNUC__) || defined(__clang__)
static inline uint64_t bebop__wyr8(const uint8_t* p)
{
  uint64_t v;
  memcpy(&v, p, 8);
  return __builtin_bswap64(v);
}

static inline uint64_t bebop__wyr4(const uint8_t* p)
{
  uint32_t v;
  memcpy(&v, p, 4);
  return __builtin_bswap32(v);
}
#elif defined(_MSC_VER)
static inline uint64_t bebop__wyr8(const uint8_t* p)
{
  uint64_t v;
  memcpy(&v, p, 8);
  return _byteswap_uint64(v);
}

static inline uint64_t bebop__wyr4(const uint8_t* p)
{
  uint32_t v;
  memcpy(&v, p, 4);
  return _byteswap_ulong(v);
}
#else
static inline uint64_t bebop__wyr8(const uint8_t* p)
{
  uint64_t v;
  memcpy(&v, p, 8);
  return (
      ((v >> 56) & 0xff) | ((v >> 40) & 0xff00) | ((v >> 24) & 0xff0000) | ((v >> 8) & 0xff000000)
      | ((v << 8) & 0xff00000000ull) | ((v << 24) & 0xff0000000000ull)
      | ((v << 40) & 0xff000000000000ull) | ((v << 56) & 0xff00000000000000ull)
  );
}

static inline uint64_t bebop__wyr4(const uint8_t* p)
{
  uint32_t v;
  memcpy(&v, p, 4);
  return (
      ((v >> 24) & 0xff) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | ((v << 24) & 0xff000000)
  );
}
#endif

static inline uint64_t bebop__wyr3(const uint8_t* p, size_t k)
{
  return ((uint64_t)p[0] << 16) | ((uint64_t)p[k >> 1] << 8) | p[k - 1];
}

static const uint64_t bebop__wyp[4] = {
    0x2d358dccaa6c78a5ull, 0x8bb84b93962eacc9ull, 0x4b33a62ed433d4a3ull, 0x4d5a2da51de1aa47ull
};

static inline uint64_t bebop__wyhash(const void* key, size_t len, uint64_t seed)
{
  const uint8_t* p = (const uint8_t*)key;
  seed ^= bebop__wymix(seed ^ bebop__wyp[0], bebop__wyp[1]);
  uint64_t a, b;
  if (BEBOP_WIRE_LIKELY(len <= 16)) {
    if (BEBOP_WIRE_LIKELY(len >= 4)) {
      a = (bebop__wyr4(p) << 32) | bebop__wyr4(p + ((len >> 3) << 2));
      b = (bebop__wyr4(p + len - 4) << 32) | bebop__wyr4(p + len - 4 - ((len >> 3) << 2));
    } else if (BEBOP_WIRE_LIKELY(len > 0)) {
      a = bebop__wyr3(p, len);
      b = 0;
    } else {
      a = b = 0;
    }
  } else {
    size_t i = len;
    if (BEBOP_WIRE_UNLIKELY(i >= 48)) {
      uint64_t see1 = seed, see2 = seed;
      do {
        seed = bebop__wymix(bebop__wyr8(p) ^ bebop__wyp[1], bebop__wyr8(p + 8) ^ seed);
        see1 = bebop__wymix(bebop__wyr8(p + 16) ^ bebop__wyp[2], bebop__wyr8(p + 24) ^ see1);
        see2 = bebop__wymix(bebop__wyr8(p + 32) ^ bebop__wyp[3], bebop__wyr8(p + 40) ^ see2);
        p += 48;
        i -= 48;
      } while (BEBOP_WIRE_LIKELY(i >= 48));
      seed ^= see1 ^ see2;
    }
    while (BEBOP_WIRE_UNLIKELY(i > 16)) {
      seed = bebop__wymix(bebop__wyr8(p) ^ bebop__wyp[1], bebop__wyr8(p + 8) ^ seed);
      i -= 16;
      p += 16;
    }
    a = bebop__wyr8(p + i - 16);
    b = bebop__wyr8(p + i - 8);
  }
  a ^= bebop__wyp[1];
  b ^= seed;
  bebop__wymum(&a, &b);
  return bebop__wymix(a ^ bebop__wyp[0] ^ len, b ^ bebop__wyp[1]);
}

static inline uint64_t bebop__wyhash64(uint64_t a, uint64_t b)
{
  a ^= 0x2d358dccaa6c78a5ull;
  b ^= 0x8bb84b93962eacc9ull;
  bebop__wymum(&a, &b);
  return bebop__wymix(a ^ 0x2d358dccaa6c78a5ull, b ^ 0x8bb84b93962eacc9ull);
}

// Map hash functions for all valid map key types
uint64_t Bebop_MapHash_Bool(const void* key)
{
  return bebop__wyhash64(*(const bool*)key, 0);
}

uint64_t Bebop_MapHash_Byte(const void* key)
{
  return bebop__wyhash64(*(const uint8_t*)key, 0);
}

uint64_t Bebop_MapHash_I8(const void* key)
{
  return bebop__wyhash64((uint64_t)*(const int8_t*)key, 0);
}

uint64_t Bebop_MapHash_U8(const void* key)
{
  return bebop__wyhash64(*(const uint8_t*)key, 0);
}

uint64_t Bebop_MapHash_I16(const void* key)
{
  return bebop__wyhash64((uint64_t)*(const int16_t*)key, 0);
}

uint64_t Bebop_MapHash_U16(const void* key)
{
  return bebop__wyhash64(*(const uint16_t*)key, 0);
}

uint64_t Bebop_MapHash_I32(const void* key)
{
  return bebop__wyhash64((uint64_t)*(const int32_t*)key, 0);
}

uint64_t Bebop_MapHash_U32(const void* key)
{
  return bebop__wyhash64(*(const uint32_t*)key, 0);
}

uint64_t Bebop_MapHash_I64(const void* key)
{
  return bebop__wyhash64(*(const uint64_t*)key, 0);
}

uint64_t Bebop_MapHash_U64(const void* key)
{
  return bebop__wyhash64(*(const uint64_t*)key, 0);
}

uint64_t Bebop_MapHash_I128(const void* key)
{
  const uint64_t* p = (const uint64_t*)key;
  return bebop__wyhash64(p[0], p[1]);
}

uint64_t Bebop_MapHash_U128(const void* key)
{
  const uint64_t* p = (const uint64_t*)key;
  return bebop__wyhash64(p[0], p[1]);
}

uint64_t Bebop_MapHash_UUID(const void* key)
{
  const uint64_t* p = (const uint64_t*)key;
  return bebop__wyhash64(p[0], p[1]);
}

uint64_t Bebop_MapHash_Str(const void* key)
{
  const Bebop_Str* s = (const Bebop_Str*)key;
  return bebop__wyhash(s->data, s->length, 0);
}

// Map equality functions for all valid map key types
bool Bebop_MapEq_Bool(const void* a, const void* b)
{
  return *(const bool*)a == *(const bool*)b;
}

bool Bebop_MapEq_Byte(const void* a, const void* b)
{
  return *(const uint8_t*)a == *(const uint8_t*)b;
}

bool Bebop_MapEq_I8(const void* a, const void* b)
{
  return *(const int8_t*)a == *(const int8_t*)b;
}

bool Bebop_MapEq_U8(const void* a, const void* b)
{
  return *(const uint8_t*)a == *(const uint8_t*)b;
}

bool Bebop_MapEq_I16(const void* a, const void* b)
{
  return *(const int16_t*)a == *(const int16_t*)b;
}

bool Bebop_MapEq_U16(const void* a, const void* b)
{
  return *(const uint16_t*)a == *(const uint16_t*)b;
}

bool Bebop_MapEq_I32(const void* a, const void* b)
{
  return *(const int32_t*)a == *(const int32_t*)b;
}

bool Bebop_MapEq_U32(const void* a, const void* b)
{
  return *(const uint32_t*)a == *(const uint32_t*)b;
}

bool Bebop_MapEq_I64(const void* a, const void* b)
{
  return *(const int64_t*)a == *(const int64_t*)b;
}

bool Bebop_MapEq_U64(const void* a, const void* b)
{
  return *(const uint64_t*)a == *(const uint64_t*)b;
}

bool Bebop_MapEq_I128(const void* a, const void* b)
{
  return memcmp(a, b, 16) == 0;
}

bool Bebop_MapEq_U128(const void* a, const void* b)
{
  return memcmp(a, b, 16) == 0;
}

bool Bebop_MapEq_UUID(const void* a, const void* b)
{
  return memcmp(a, b, 16) == 0;
}

bool Bebop_MapEq_Str(const void* a, const void* b)
{
  const Bebop_Str* sa = (const Bebop_Str*)a;
  const Bebop_Str* sb = (const Bebop_Str*)b;
  return sa->length == sb->length && memcmp(sa->data, sb->data, sa->length) == 0;
}

// #endregion

// #region Reflection Type Descriptors

const BebopReflection_TypeDescriptor BebopReflection_Type_Bool = {
    BEBOP_REFLECTION_TYPE_BOOL, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Byte = {
    BEBOP_REFLECTION_TYPE_BYTE, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Int8 = {
    BEBOP_REFLECTION_TYPE_INT8, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Int16 = {
    BEBOP_REFLECTION_TYPE_INT16, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_UInt16 = {
    BEBOP_REFLECTION_TYPE_UINT16, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Int32 = {
    BEBOP_REFLECTION_TYPE_INT32, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_UInt32 = {
    BEBOP_REFLECTION_TYPE_UINT32, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Int64 = {
    BEBOP_REFLECTION_TYPE_INT64, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_UInt64 = {
    BEBOP_REFLECTION_TYPE_UINT64, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Int128 = {
    BEBOP_REFLECTION_TYPE_INT128, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_UInt128 = {
    BEBOP_REFLECTION_TYPE_UINT128, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Float16 = {
    BEBOP_REFLECTION_TYPE_FLOAT16, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Float32 = {
    BEBOP_REFLECTION_TYPE_FLOAT32, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Float64 = {
    BEBOP_REFLECTION_TYPE_FLOAT64, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_BFloat16 = {
    BEBOP_REFLECTION_TYPE_BFLOAT16, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_String = {
    BEBOP_REFLECTION_TYPE_STRING, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_UUID = {
    BEBOP_REFLECTION_TYPE_UUID, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Timestamp = {
    BEBOP_REFLECTION_TYPE_TIMESTAMP, NULL, NULL, NULL, 0, NULL
};
const BebopReflection_TypeDescriptor BebopReflection_Type_Duration = {
    BEBOP_REFLECTION_TYPE_DURATION, NULL, NULL, NULL, 0, NULL
};

// #endregion

// #region Any Type Helpers

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
const char BEBOP_TYPE_URL_PREFIX[] = "type.bebop.sh/";

typedef struct {
  Bebop_Str type_url;
  Bebop_U8_Array value;
} Bebop_Any_Layout;

static size_t bebop__cstrlen(const char* s)
{
  const char* p = s;
  while (*p) {
    p++;
  }
  return (size_t)(p - s);
}

BEBOP_API Bebop_WireResult Bebop_Any_Pack(
    Bebop_WireCtx* ctx, Bebop_Any* any, const void* record, const Bebop_TypeInfo* type_info
)
{
  if (!ctx || !any || !record || !type_info || !type_info->type_fqn || !type_info->size_fn
      || !type_info->encode_fn)
  {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_Any_Layout* a = (Bebop_Any_Layout*)any;

  const char* prefix = type_info->prefix ? type_info->prefix : BEBOP_TYPE_URL_PREFIX;
  const size_t prefix_len = bebop__cstrlen(prefix);
  const size_t fqn_len = bebop__cstrlen(type_info->type_fqn);
  const size_t url_len = prefix_len + fqn_len;

  char* url_buf = (char*)Bebop_WireCtx_Alloc(ctx, url_len + 1);
  if (!url_buf) {
    return BEBOP_WIRE_ERR_OOM;
  }
  memcpy(url_buf, prefix, prefix_len);
  memcpy(url_buf + prefix_len, type_info->type_fqn, fqn_len);
  url_buf[url_len] = '\0';

  a->type_url.data = url_buf;
  a->type_url.length = url_len;

  const size_t encoded_size = type_info->size_fn(record);

  Bebop_Writer* w;
  Bebop_WireResult r = Bebop_WireCtx_WriterHint(ctx, encoded_size, &w);
  if (r != BEBOP_WIRE_OK) {
    return r;
  }

  r = type_info->encode_fn(w, record);
  if (r != BEBOP_WIRE_OK) {
    return r;
  }

  uint8_t* buf;
  size_t len;
  r = Bebop_Writer_Buf(w, &buf, &len);
  if (r != BEBOP_WIRE_OK) {
    return r;
  }

  a->value.data = buf;
  a->value.length = len;
  a->value.capacity = 0;

  return BEBOP_WIRE_OK;
}

BEBOP_API bool Bebop_Any_Is(const Bebop_Any* any, const char* type_fqn)
{
  if (!any || !type_fqn) {
    return false;
  }

  const char* name = Bebop_Any_TypeName(any);
  if (!name) {
    return false;
  }

  const Bebop_Any_Layout* a = (const Bebop_Any_Layout*)any;
  const char* url_end = a->type_url.data + a->type_url.length;
  const size_t name_len = (size_t)(url_end - name);
  const size_t fqn_len = bebop__cstrlen(type_fqn);

  if (name_len != fqn_len) {
    return false;
  }

  return memcmp(name, type_fqn, fqn_len) == 0;
}

BEBOP_API const char* Bebop_Any_TypeName(const Bebop_Any* any)
{
  if (!any) {
    return NULL;
  }

  const Bebop_Any_Layout* a = (const Bebop_Any_Layout*)any;

  if (!a->type_url.data || a->type_url.length == 0) {
    return NULL;
  }

  const char* last_slash = NULL;
  for (size_t i = 0; i < a->type_url.length; i++) {
    if (a->type_url.data[i] == '/') {
      last_slash = a->type_url.data + i;
    }
  }

  if (!last_slash) {
    return NULL;
  }

  return last_slash + 1;
}

BEBOP_API Bebop_WireResult Bebop_Any_Unpack(
    Bebop_WireCtx* ctx, const Bebop_Any* any, void* record, const Bebop_TypeInfo* type_info
)
{
  if (!ctx || !any || !record || !type_info || !type_info->decode_fn) {
    return BEBOP_WIRE_ERR_NULL;
  }

  const Bebop_Any_Layout* a = (const Bebop_Any_Layout*)any;

  if (!a->value.data) {
    return BEBOP_WIRE_ERR_NULL;
  }

  Bebop_Reader* rd;
  const Bebop_WireResult r = Bebop_WireCtx_Reader(ctx, a->value.data, a->value.length, &rd);
  if (r != BEBOP_WIRE_OK) {
    return r;
  }

  return type_info->decode_fn(ctx, rd, record);
}

// #endregion
