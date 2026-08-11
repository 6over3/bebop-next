#include <ctype.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "bebop_wire_codegen.h"

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
  Bebop_ArenaOptions options;
  Bebop_AllocFn alloc;
  void* alloc_ctx;
} bebop_wire_arena_impl_t;

struct Bebop_Context {
  bebop_wire_arena_impl_t* arena;
  Bebop_ContextOptions options;
  uint32_t decode_depth;
};

struct Bebop_Writer {
  uint8_t* buffer;
  uint8_t* current;
  uint8_t* end;
  Bebop_Context* context;
};

static uint32_t bebop_wire_popcount32(uint32_t value)
{
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_popcount(value);
#elif defined(_MSC_VER)
  return (uint32_t)__popcnt(value);
#else
  value -= (value >> 1) & UINT32_C(0x55555555);
  value = (value & UINT32_C(0x33333333)) + ((value >> 2) & UINT32_C(0x33333333));
  value = (value + (value >> 4)) & UINT32_C(0x0f0f0f0f);
  return (value * UINT32_C(0x01010101)) >> 24;
#endif
}

static uint32_t bebop_wire_ctz32(uint32_t value)
{
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_ctz(value);
#elif defined(_MSC_VER)
  unsigned long index;
  _BitScanForward(&index, value);
  return (uint32_t)index;
#else
  uint32_t count = 0;
  while ((value & 1u) == 0) {
    value >>= 1;
    count++;
  }
  return count;
#endif
}

static uint32_t bebop_wire_load_le(const uint8_t* data, uint8_t width)
{
  uint32_t value = data[0];
  if (width > 1) {
    value |= (uint32_t)data[1] << 8;
    if (width > 2) {
      value |= (uint32_t)data[2] << 16;
      value |= (uint32_t)data[3] << 24;
    }
  }
  return value;
}

static void bebop_wire_store_le(uint8_t** destination, uint32_t value, uint8_t width)
{
  uint8_t* out = *destination;
  *out++ = (uint8_t)value;
  if (width > 1) {
    *out++ = (uint8_t)(value >> 8);
    if (width > 2) {
      *out++ = (uint8_t)(value >> 16);
      *out++ = (uint8_t)(value >> 24);
    }
  }
  *destination = out;
}

typedef struct {
  uint8_t kind;
  uint8_t size;
  uint8_t block_mask;
} bebop_message_directory_layout_t;

static bool bebop_message_tags_valid(const uint8_t* tags, uint8_t count)
{
  if (count == 0) {
    return true;
  }
  if (!tags || tags[0] == 0) {
    return false;
  }
  for (uint16_t i = 1; i < count; i++) {
    if (tags[i] <= tags[i - 1]) {
      return false;
    }
  }
  return true;
}

static bebop_message_directory_layout_t bebop_message_directory_layout(
    const uint8_t* tags, uint8_t count
)
{
  bebop_message_directory_layout_t layout = {BEBOP_MESSAGE_DIRECTORY_EMPTY, 0, 0};
  if (count == 0) {
    return layout;
  }

  uint8_t block_mask = 0;
  for (uint16_t i = 0; i < count; i++) {
    block_mask |= (uint8_t)(1u << ((tags[i] - 1u) >> 5));
  }
  const uint8_t block_size = (uint8_t)(1u + 5u * bebop_wire_popcount32(block_mask));
  layout =
      (bebop_message_directory_layout_t) {BEBOP_MESSAGE_DIRECTORY_BLOCKS, block_size, block_mask};

  const uint8_t max_tag = tags[count - 1];
  if (max_tag <= 8 && 1 <= layout.size) {
    layout.kind = BEBOP_MESSAGE_DIRECTORY_MASK8;
    layout.size = 1;
  } else if (max_tag <= 16 && 2 <= layout.size) {
    layout.kind = BEBOP_MESSAGE_DIRECTORY_MASK16;
    layout.size = 2;
  } else if (max_tag <= 32 && 4 <= layout.size) {
    layout.kind = BEBOP_MESSAGE_DIRECTORY_MASK32;
    layout.size = 4;
  }

  if (count <= 3 && count < layout.size) {
    layout.kind = count;
    layout.size = count;
  }
  return layout;
}

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

static bebop_wire_arena_impl_t* bebop_wire_arena_create(const Bebop_ArenaOptions* options)
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

  const Bebop_AllocFn alloc = arena->alloc;
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

static void* bebop_default_alloc(void* ptr, size_t old_size, size_t new_size, void* context)
{
  (void)old_size;
  (void)context;
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new_size);
}

Bebop_Context* bebop_context_new(const Bebop_ContextOptions* options)
{
  Bebop_ContextOptions resolved = options ? *options : bebop_context_options();
  if (!resolved.arena_options.allocator.alloc) {
    resolved.arena_options.allocator.alloc = bebop_default_alloc;
  }

  const Bebop_AllocFn alloc_fn = resolved.arena_options.allocator.alloc;
  void* alloc_ctx = resolved.arena_options.allocator.ctx;

  Bebop_Context* context = (Bebop_Context*)alloc_fn(NULL, 0, sizeof(Bebop_Context), alloc_ctx);
  if (!context) {
    return NULL;
  }

  context->arena = bebop_wire_arena_create(&resolved.arena_options);
  if (!context->arena) {
    alloc_fn(context, sizeof(Bebop_Context), 0, alloc_ctx);
    return NULL;
  }

  context->options = resolved;
  context->decode_depth = 0;
  return context;
}

void bebop_context_free(Bebop_Context* context)
{
  if (!context) {
    return;
  }

  const Bebop_AllocFn alloc_fn = context->arena->alloc;
  void* alloc_ctx = context->arena->alloc_ctx;
  bebop_wire_arena_destroy(context->arena);
  alloc_fn(context, sizeof(Bebop_Context), 0, alloc_ctx);
}

void bebop_context_reset(Bebop_Context* context)
{
  if (!context) {
    return;
  }
  bebop_wire_arena_reset(context->arena);
  context->decode_depth = 0;
}

Bebop_Result bebop_context_enter_decode(Bebop_Context* context)
{
  if (BEBOP_WIRE_UNLIKELY(!context)) {
    return BEBOP_RESULT_NULL;
  }
  uint32_t max_depth = context->options.max_decode_depth;
  if (max_depth == 0) {
    max_depth = BEBOP_WIRE_DEFAULT_MAX_DECODE_DEPTH;
  }
  if (BEBOP_WIRE_UNLIKELY(context->decode_depth >= max_depth)) {
    return BEBOP_RESULT_MALFORMED;
  }
  context->decode_depth++;
  return BEBOP_RESULT_OK;
}

void bebop_context_leave_decode(Bebop_Context* context)
{
  if (context && context->decode_depth > 0) {
    context->decode_depth--;
  }
}

size_t bebop_context_allocated(const Bebop_Context* context)
{
  return context ? BEBOP_WIRE_ATOMIC_LOAD(&context->arena->total_allocated) : 0;
}

size_t bebop_context_used(const Bebop_Context* context)
{
  return context ? BEBOP_WIRE_ATOMIC_LOAD(&context->arena->total_used) : 0;
}

Bebop_Reader* bebop_context_reader(Bebop_Context* context, Bebop_View source)
{
  if (!context || !source.data) {
    return NULL;
  }

  Bebop_Reader* reader =
      (Bebop_Reader*)bebop_wire_arena_alloc(context->arena, sizeof(Bebop_Reader));
  if (!reader) {
    return NULL;
  }

  if (bebop_reader_init(reader, context, source) != BEBOP_RESULT_OK) {
    return NULL;
  }
  return reader;
}

Bebop_Result bebop_reader_init(Bebop_Reader* reader, Bebop_Context* context, Bebop_View source)
{
  if (!reader || !source.data) {
    return BEBOP_RESULT_NULL;
  }
  reader->start = source.data;
  reader->current = source.data;
  reader->end = source.data + source.length;
  reader->context = context;
  return BEBOP_RESULT_OK;
}

void bebop_reader_reset(Bebop_Reader* reader, Bebop_View source)
{
  if (!reader || !source.data) {
    return;
  }
  reader->start = source.data;
  reader->current = source.data;
  reader->end = source.data + source.length;
}

void bebop_reader_seek(Bebop_Reader* reader, const uint8_t* position)
{
  if (!reader) {
    return;
  }
  if (position >= reader->start && position <= reader->end) {
    reader->current = position;
  }
}

void bebop_reader_skip(Bebop_Reader* reader, size_t amount)
{
  if (!reader) {
    return;
  }
  if (amount <= (size_t)(reader->end - reader->current)) {
    reader->current += amount;
  }
}

Bebop_Result bebop_reader_read_byte(Bebop_Reader* reader, uint8_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint8_t) > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }

  *out = *reader->current++;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_u16(Bebop_Reader* reader, uint16_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint16_t) > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, sizeof(uint16_t));
  reader->current += sizeof(uint16_t);
#else
  const uint16_t b0 = *reader->current++;
  const uint16_t b1 = *reader->current++;
  *out = (b1 << 8) | b0;
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_u32(Bebop_Reader* reader, uint32_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint32_t) > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_u64(Bebop_Reader* reader, uint64_t* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint64_t) > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_i8(Bebop_Reader* reader, int8_t* out)
{
  return bebop_reader_read_byte(reader, (uint8_t*)out);
}

Bebop_Result bebop_reader_read_i16(Bebop_Reader* reader, int16_t* out)
{
  return bebop_reader_read_u16(reader, (uint16_t*)out);
}

Bebop_Result bebop_reader_read_i32(Bebop_Reader* reader, int32_t* out)
{
  return bebop_reader_read_u32(reader, (uint32_t*)out);
}

Bebop_Result bebop_reader_read_i64(Bebop_Reader* reader, int64_t* out)
{
  return bebop_reader_read_u64(reader, (uint64_t*)out);
}

Bebop_Result bebop_reader_read_bool(Bebop_Reader* reader, bool* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }

  uint8_t byte;
  const Bebop_Result result = bebop_reader_read_byte(reader, &byte);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_RESULT_OK)) {
    *out = byte != 0;
  }
  return result;
}

Bebop_Result bebop_reader_read_f16(Bebop_Reader* reader, Bebop_Float16* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint16_t) > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_bf16(Bebop_Reader* reader, Bebop_BFloat16* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint16_t) > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_f32(Bebop_Reader* reader, float* out)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }

  uint32_t bits;
  const Bebop_Result result = bebop_reader_read_u32(reader, &bits);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_RESULT_OK)) {
    memcpy(out, &bits, sizeof(float));
  }
  return result;
}

Bebop_Result bebop_reader_read_f64(Bebop_Reader* reader, double* out)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }

  uint64_t bits;
  const Bebop_Result result = bebop_reader_read_u64(reader, &bits);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_RESULT_OK)) {
    memcpy(out, &bits, sizeof(double));
  }
  return result;
}

Bebop_Result bebop_reader_read_i128(Bebop_Reader* reader, Bebop_Int128* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_u128(Bebop_Reader* reader, Bebop_UInt128* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_uuid(Bebop_Reader* reader, Bebop_UUID* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }
  memcpy(out->bytes, reader->current, 16);
  reader->current += 16;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_timestamp(Bebop_Reader* reader, Bebop_Timestamp* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }

  memcpy(out, reader->current, 16);
  reader->current += 16;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_duration(Bebop_Reader* reader, Bebop_Duration* out)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !out)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(12 > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }

  memcpy(out, reader->current, 12);
  reader->current += 12;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_length(Bebop_Reader* reader, uint32_t* out)
{
  const Bebop_Result result = bebop_reader_read_u32(reader, out);
  if (BEBOP_WIRE_LIKELY(result == BEBOP_RESULT_OK)) {
    if (BEBOP_WIRE_UNLIKELY(*out > (size_t)(reader->end - reader->current))) {
      return BEBOP_RESULT_MALFORMED;
    }
  }
  return result;
}

Bebop_Result bebop_reader_read_string(Bebop_Reader* reader, Bebop_String* out)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }

  uint32_t length;
  const Bebop_Result result = bebop_reader_read_u32(reader, &length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  // length + 1 wraps to 0 on 32-bit size_t when length == UINT32_MAX
  const size_t remaining = (size_t)(reader->end - reader->current);
  if (BEBOP_WIRE_UNLIKELY((size_t)length >= remaining)) {
    return BEBOP_RESULT_MALFORMED;
  }
  if (BEBOP_WIRE_UNLIKELY(reader->current[length] != 0)) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total = (size_t)length + 1;

  out->data = (const char*)reader->current;
  out->length = length;
  reader->current += total;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_bytes(Bebop_Reader* reader, Bebop_Bytes* out)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }

  uint32_t length;
  const Bebop_Result result = bebop_reader_read_length(reader, &length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  out->data = reader->current;
  out->length = length;
  reader->current += length;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_bytes(
    Bebop_Reader* reader, size_t byte_count, Bebop_Bytes* out
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(byte_count > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }
  out->data = reader->current;
  out->length = byte_count;
  reader->current += byte_count;
  return BEBOP_RESULT_OK;
}

static Bebop_Result bebop_message_index_parse(Bebop_MessageIndex* out, Bebop_View encoded)
{
  if (!out || !encoded.data) {
    return BEBOP_RESULT_NULL;
  }
  if (encoded.length < BEBOP_WIRE_SIZE_LEN + 1) {
    return BEBOP_RESULT_MALFORMED;
  }

  const uint32_t body_length = bebop_wire_load_le(encoded.data, 4);
  if ((size_t)body_length != encoded.length - BEBOP_WIRE_SIZE_LEN || body_length == 0) {
    return BEBOP_RESULT_MALFORMED;
  }

  const uint8_t* body = encoded.data + BEBOP_WIRE_SIZE_LEN;
  const uint8_t* end = encoded.data + encoded.length;
  const uint8_t control = end[-1];
  const uint8_t width_code = control & 3u;
  const uint8_t kind = (control >> 2) & 7u;
  if ((control & 0xe0u) != 0 || width_code == 3) {
    return BEBOP_RESULT_MALFORMED;
  }
  const uint8_t width = (uint8_t)(1u << width_code);

  size_t directory_size;
  uint16_t field_count = 0;
  switch (kind) {
    case BEBOP_MESSAGE_DIRECTORY_EMPTY:
      directory_size = 0;
      break;
    case BEBOP_MESSAGE_DIRECTORY_TINY1:
    case BEBOP_MESSAGE_DIRECTORY_TINY2:
    case BEBOP_MESSAGE_DIRECTORY_TINY3:
      directory_size = kind;
      field_count = kind;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK8:
      directory_size = 1;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK16:
      directory_size = 2;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK32:
      directory_size = 4;
      break;
    case BEBOP_MESSAGE_DIRECTORY_BLOCKS:
      if (body_length < 2) {
        return BEBOP_RESULT_MALFORMED;
      }
      directory_size = 1u + 5u * bebop_wire_popcount32(end[-2]);
      break;
    default:
      return BEBOP_RESULT_MALFORMED;
  }
  if (directory_size + 1 > body_length) {
    return BEBOP_RESULT_MALFORMED;
  }

  const uint8_t* directory = end - 1 - directory_size;
  if (kind >= BEBOP_MESSAGE_DIRECTORY_MASK8 && kind <= BEBOP_MESSAGE_DIRECTORY_MASK32) {
    field_count =
        (uint16_t)bebop_wire_popcount32(bebop_wire_load_le(directory, (uint8_t)directory_size));
  } else if (kind == BEBOP_MESSAGE_DIRECTORY_BLOCKS) {
    const uint8_t block_mask = directory[directory_size - 1];
    if (block_mask == 0) {
      return BEBOP_RESULT_MALFORMED;
    }
    const uint8_t* entry = directory;
    uint16_t rank = 0;
    for (uint8_t block = 0; block < 8; block++) {
      if ((block_mask & (uint8_t)(1u << block)) == 0) {
        continue;
      }
      if (entry[0] != rank) {
        return BEBOP_RESULT_MALFORMED;
      }
      const uint32_t mask = bebop_wire_load_le(entry + 1, 4);
      if (mask == 0 || (block == 7 && (mask & UINT32_C(0x80000000)) != 0)) {
        return BEBOP_RESULT_MALFORMED;
      }
      rank += (uint16_t)bebop_wire_popcount32(mask);
      entry += 5;
    }
    field_count = rank;
  }

  if (field_count == 0 && kind != BEBOP_MESSAGE_DIRECTORY_EMPTY) {
    return BEBOP_RESULT_MALFORMED;
  }
  if (field_count != 0 && kind == BEBOP_MESSAGE_DIRECTORY_EMPTY) {
    return BEBOP_RESULT_MALFORMED;
  }

  if (kind >= BEBOP_MESSAGE_DIRECTORY_TINY1 && kind <= BEBOP_MESSAGE_DIRECTORY_TINY3) {
    if (directory[0] == 0) {
      return BEBOP_RESULT_MALFORMED;
    }
    for (uint16_t i = 1; i < field_count; i++) {
      if (directory[i] <= directory[i - 1]) {
        return BEBOP_RESULT_MALFORMED;
      }
    }
  }

  const size_t boundary_count = field_count == 0 ? 0 : field_count - 1;
  if (boundary_count > (SIZE_MAX - directory_size - 1) / width) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t boundary_size = boundary_count * width;
  if (boundary_size + directory_size + 1 > body_length) {
    return BEBOP_RESULT_MALFORMED;
  }

  const uint8_t* boundaries = directory - boundary_size;
  const size_t payload_size = (size_t)(boundaries - body);
  uint32_t previous = 0;
  for (size_t i = 0; i < boundary_count; i++) {
    const uint32_t boundary = bebop_wire_load_le(boundaries + i * width, width);
    if (boundary < previous || boundary > payload_size) {
      return BEBOP_RESULT_MALFORMED;
    }
    previous = boundary;
  }
  if (field_count == 0 && payload_size != 0) {
    return BEBOP_RESULT_MALFORMED;
  }

  *out = (Bebop_MessageIndex) {.encoded = encoded,
                               .boundaries = boundaries,
                               .directory = directory,
                               .field_count = (uint8_t)field_count,
                               .offset_width = width,
                               .directory_kind = kind};
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_message_index_init(Bebop_MessageIndex* out, Bebop_View encoded)
{
  return bebop_message_index_parse(out, encoded);
}

Bebop_Result bebop_message_index_validate(const Bebop_MessageIndex* index)
{
  if (!index) {
    return BEBOP_RESULT_NULL;
  }
  Bebop_MessageIndex validated;
  return bebop_message_index_parse(&validated, index->encoded);
}

Bebop_Result bebop_message_index_begin(Bebop_MessageIndex* out, Bebop_View encoded)
{
  if (!out || !encoded.data) {
    return BEBOP_RESULT_NULL;
  }
  if (encoded.length < BEBOP_WIRE_SIZE_LEN + 1) {
    return BEBOP_RESULT_MALFORMED;
  }
  const uint32_t body_length = bebop_wire_load_le(encoded.data, 4);
  if ((size_t)body_length != encoded.length - BEBOP_WIRE_SIZE_LEN || body_length == 0) {
    return BEBOP_RESULT_MALFORMED;
  }

  const uint8_t* end = encoded.data + encoded.length;
  const uint8_t control = end[-1];
  const uint8_t width_code = control & 3u;
  const uint8_t kind = (control >> 2) & 7u;
  if ((control & 0xe0u) != 0 || width_code == 3) {
    return BEBOP_RESULT_MALFORMED;
  }

  const uint8_t width = (uint8_t)(1u << width_code);
  size_t directory_size;
  uint16_t field_count = 0;
  switch (kind) {
    case BEBOP_MESSAGE_DIRECTORY_EMPTY:
      directory_size = 0;
      break;
    case BEBOP_MESSAGE_DIRECTORY_TINY1:
    case BEBOP_MESSAGE_DIRECTORY_TINY2:
    case BEBOP_MESSAGE_DIRECTORY_TINY3:
      directory_size = kind;
      field_count = kind;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK8:
      directory_size = 1;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK16:
      directory_size = 2;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK32:
      directory_size = 4;
      break;
    case BEBOP_MESSAGE_DIRECTORY_BLOCKS:
      if (body_length < 2 || end[-2] == 0) {
        return BEBOP_RESULT_MALFORMED;
      }
      directory_size = 1u + 5u * bebop_wire_popcount32(end[-2]);
      break;
    default:
      return BEBOP_RESULT_MALFORMED;
  }
  if ((size_t)directory_size + 1u > body_length) {
    return BEBOP_RESULT_MALFORMED;
  }
  const uint8_t* directory = end - 1 - directory_size;
  if (kind >= BEBOP_MESSAGE_DIRECTORY_MASK8 && kind <= BEBOP_MESSAGE_DIRECTORY_MASK32) {
    field_count =
        (uint16_t)bebop_wire_popcount32(bebop_wire_load_le(directory, (uint8_t)directory_size));
  } else if (kind == BEBOP_MESSAGE_DIRECTORY_BLOCKS) {
    const uint8_t block_mask = directory[directory_size - 1u];
    const uint8_t* entry = directory;
    uint16_t rank = 0;
    for (uint8_t block = 0; block < 8; block++) {
      if ((block_mask & (uint8_t)(1u << block)) == 0) {
        continue;
      }
      const uint32_t mask = bebop_wire_load_le(entry + 1, 4);
      if (entry[0] != rank || mask == 0 || (block == 7 && (mask & UINT32_C(0x80000000)) != 0)) {
        return BEBOP_RESULT_MALFORMED;
      }
      rank += (uint16_t)bebop_wire_popcount32(mask);
      entry += 5;
    }
    field_count = rank;
  }
  if (field_count == 0 && kind != BEBOP_MESSAGE_DIRECTORY_EMPTY) {
    return BEBOP_RESULT_MALFORMED;
  }
  if (kind >= BEBOP_MESSAGE_DIRECTORY_TINY1 && kind <= BEBOP_MESSAGE_DIRECTORY_TINY3) {
    if (directory[0] == 0) {
      return BEBOP_RESULT_MALFORMED;
    }
    for (uint16_t i = 1; i < field_count; i++) {
      if (directory[i] <= directory[i - 1]) {
        return BEBOP_RESULT_MALFORMED;
      }
    }
  }
  const size_t boundary_size = field_count == 0 ? 0 : ((size_t)field_count - 1u) * width;
  if (field_count == 0) {
    if (body_length != 1) {
      return BEBOP_RESULT_MALFORMED;
    }
  } else if (boundary_size + directory_size + 1u > body_length) {
    return BEBOP_RESULT_MALFORMED;
  }

  *out = (Bebop_MessageIndex) {.encoded = encoded,
                               .boundaries = directory - boundary_size,
                               .directory = directory,
                               .field_count = (uint8_t)field_count,
                               .offset_width = width,
                               .directory_kind = kind};
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_message_index(Bebop_Reader* reader, Bebop_MessageIndex* out)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  const uint8_t* start = reader->current;
  uint32_t body_length;
  Bebop_Result result = bebop_reader_read_length(reader, &body_length);
  if (result != BEBOP_RESULT_OK) {
    reader->current = start;
    return result;
  }
  if ((size_t)body_length > bebop_reader_remaining(reader)) {
    reader->current = start;
    return BEBOP_RESULT_MALFORMED;
  }
  const Bebop_View encoded = {start, (size_t)body_length + BEBOP_WIRE_SIZE_LEN};
  result = bebop_message_index_parse(out, encoded);
  if (result == BEBOP_RESULT_OK) {
    reader->current = start + encoded.length;
  }
  return result;
}

Bebop_Result bebop_reader_begin_message_index(Bebop_Reader* reader, Bebop_MessageIndex* out)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  const uint8_t* start = reader->current;
  uint32_t body_length;
  Bebop_Result result = bebop_reader_read_length(reader, &body_length);
  if (result != BEBOP_RESULT_OK) {
    reader->current = start;
    return result;
  }
  if ((size_t)body_length > bebop_reader_remaining(reader)) {
    reader->current = start;
    return BEBOP_RESULT_MALFORMED;
  }
  const Bebop_View encoded = {start, (size_t)body_length + BEBOP_WIRE_SIZE_LEN};
  result = bebop_message_index_begin(out, encoded);
  if (result == BEBOP_RESULT_OK) {
    reader->current = start + encoded.length;
  }
  return result;
}

static Bebop_Result bebop_message_field_at_rank(
    const Bebop_MessageIndex* index, uint8_t rank, Bebop_View* field
)
{
  if (rank >= index->field_count) {
    return BEBOP_RESULT_INVALID;
  }
  const uint8_t* body = index->encoded.data + BEBOP_WIRE_SIZE_LEN;
  const uint32_t start = rank == 0
      ? 0
      : bebop_wire_load_le(
            index->boundaries + ((size_t)rank - 1u) * index->offset_width, index->offset_width
        );
  const uint32_t end = (uint16_t)rank + 1u == index->field_count
      ? (uint32_t)(index->boundaries - body)
      : bebop_wire_load_le(
            index->boundaries + (size_t)rank * index->offset_width, index->offset_width
        );
  if (end < start || end > (size_t)(index->boundaries - body)) {
    return BEBOP_RESULT_MALFORMED;
  }
  *field = (Bebop_View) {body + start, end - start};
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_message_index_field_at(
    const Bebop_MessageIndex* index, uint8_t rank, Bebop_View* field
)
{
  if (!index || !field) {
    return BEBOP_RESULT_NULL;
  }
  return bebop_message_field_at_rank(index, rank, field);
}

Bebop_Result bebop_message_index_field(
    const Bebop_MessageIndex* index, uint8_t tag, Bebop_View* field, bool* present
)
{
  if (!index || !field || !present) {
    return BEBOP_RESULT_NULL;
  }
  *field = (Bebop_View) {NULL, 0};
  *present = false;
  if (tag == 0 || index->field_count == 0) {
    return BEBOP_RESULT_OK;
  }

  uint32_t rank;
  switch (index->directory_kind) {
    case BEBOP_MESSAGE_DIRECTORY_TINY1:
    case BEBOP_MESSAGE_DIRECTORY_TINY2:
    case BEBOP_MESSAGE_DIRECTORY_TINY3:
      for (rank = 0; rank < index->field_count; rank++) {
        if (index->directory[rank] == tag) {
          break;
        }
        if (index->directory[rank] > tag) {
          return BEBOP_RESULT_OK;
        }
      }
      if (rank == index->field_count) {
        return BEBOP_RESULT_OK;
      }
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK8:
    case BEBOP_MESSAGE_DIRECTORY_MASK16:
    case BEBOP_MESSAGE_DIRECTORY_MASK32: {
      const uint8_t mask_width =
          (uint8_t)(1u << (index->directory_kind - BEBOP_MESSAGE_DIRECTORY_MASK8));
      if (tag > mask_width * 8u) {
        return BEBOP_RESULT_OK;
      }
      const uint32_t mask = bebop_wire_load_le(index->directory, mask_width);
      const uint32_t bit = UINT32_C(1) << (tag - 1u);
      if ((mask & bit) == 0) {
        return BEBOP_RESULT_OK;
      }
      rank = bebop_wire_popcount32(mask & (bit - 1u));
      break;
    }
    case BEBOP_MESSAGE_DIRECTORY_BLOCKS: {
      const uint8_t block = (uint8_t)((tag - 1u) >> 5);
      const uint8_t block_bit = (uint8_t)(1u << block);
      const uint8_t top_mask = index->encoded.data[index->encoded.length - 2];
      if ((top_mask & block_bit) == 0) {
        return BEBOP_RESULT_OK;
      }
      const uint8_t* entry =
          index->directory + 5u * bebop_wire_popcount32(top_mask & (block_bit - 1u));
      const uint32_t mask = bebop_wire_load_le(entry + 1, 4);
      const uint32_t bit = UINT32_C(1) << ((tag - 1u) & 31u);
      if ((mask & bit) == 0) {
        return BEBOP_RESULT_OK;
      }
      rank = entry[0] + bebop_wire_popcount32(mask & (bit - 1u));
      break;
    }
    default:
      return BEBOP_RESULT_MALFORMED;
  }

  Bebop_Result result = bebop_message_field_at_rank(index, (uint8_t)rank, field);
  if (result != BEBOP_RESULT_OK) {
    return result;
  }
  *present = true;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_message_index_block_masks(const Bebop_MessageIndex* index, uint32_t masks[8])
{
  if (!index || !masks) {
    return BEBOP_RESULT_NULL;
  }
  if (index->directory_kind != BEBOP_MESSAGE_DIRECTORY_BLOCKS) {
    return BEBOP_RESULT_INVALID;
  }
  memset(masks, 0, 8u * sizeof(*masks));
  const uint8_t top_mask = index->encoded.data[index->encoded.length - 2u];
  const uint8_t* entry = index->directory;
  for (uint8_t block = 0; block < 8; block++) {
    if ((top_mask & (uint8_t)(1u << block)) != 0) {
      masks[block] = bebop_wire_load_le(entry + 1, 4);
      entry += 5;
    }
  }
  return BEBOP_RESULT_OK;
}

void bebop_message_field_iterator_init(
    Bebop_MessageFieldIterator* iterator, const Bebop_MessageIndex* index
)
{
  if (!iterator) {
    return;
  }
  *iterator = (Bebop_MessageFieldIterator) {.index = index,
                                            .block_entry = index ? index->directory : NULL,
                                            .pending_tags = 0,
                                            .rank = 0,
                                            .block = 0,
                                            .tag_base = 0};
  if (!index) {
    return;
  }
  if (index->directory_kind >= BEBOP_MESSAGE_DIRECTORY_MASK8
      && index->directory_kind <= BEBOP_MESSAGE_DIRECTORY_MASK32)
  {
    const uint8_t width = (uint8_t)(1u << (index->directory_kind - BEBOP_MESSAGE_DIRECTORY_MASK8));
    iterator->pending_tags = bebop_wire_load_le(index->directory, width);
  }
}

Bebop_Result bebop_message_field_iterator_next_tag(
    Bebop_MessageFieldIterator* iterator, uint8_t* tag, bool* present
)
{
  if (!iterator || !iterator->index || !tag || !present) {
    return BEBOP_RESULT_NULL;
  }
  const Bebop_MessageIndex* index = iterator->index;
  *present = false;
  if (iterator->rank == index->field_count) {
    return BEBOP_RESULT_OK;
  }

  if (index->directory_kind >= BEBOP_MESSAGE_DIRECTORY_TINY1
      && index->directory_kind <= BEBOP_MESSAGE_DIRECTORY_TINY3)
  {
    *tag = index->directory[iterator->rank];
  } else if (index->directory_kind >= BEBOP_MESSAGE_DIRECTORY_MASK8
             && index->directory_kind <= BEBOP_MESSAGE_DIRECTORY_MASK32)
  {
    const uint32_t bit = bebop_wire_ctz32(iterator->pending_tags);
    *tag = (uint8_t)(bit + 1u);
    iterator->pending_tags &= iterator->pending_tags - 1u;
  } else if (index->directory_kind == BEBOP_MESSAGE_DIRECTORY_BLOCKS) {
    while (iterator->pending_tags == 0) {
      const uint8_t top_mask = index->encoded.data[index->encoded.length - 2];
      while (iterator->block < 8 && (top_mask & (uint8_t)(1u << iterator->block)) == 0) {
        iterator->block++;
      }
      if (iterator->block == 8) {
        return BEBOP_RESULT_MALFORMED;
      }
      iterator->tag_base = (uint8_t)(iterator->block * 32u);
      iterator->pending_tags = bebop_wire_load_le(iterator->block_entry + 1, 4);
      iterator->block_entry += 5;
      iterator->block++;
    }
    const uint32_t bit = bebop_wire_ctz32(iterator->pending_tags);
    *tag = (uint8_t)(iterator->tag_base + bit + 1u);
    iterator->pending_tags &= iterator->pending_tags - 1u;
  } else {
    return BEBOP_RESULT_MALFORMED;
  }

  iterator->rank++;
  *present = true;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_message_field_iterator_next(
    Bebop_MessageFieldIterator* iterator, uint8_t* tag, Bebop_View* field, bool* present
)
{
  if (!iterator || !tag || !field || !present) {
    return BEBOP_RESULT_NULL;
  }
  *field = (Bebop_View) {NULL, 0};
  Bebop_Result result = bebop_message_field_iterator_next_tag(iterator, tag, present);
  if (result != BEBOP_RESULT_OK || !*present) {
    return result;
  }
  return bebop_message_field_at_rank(iterator->index, (uint8_t)(iterator->rank - 1u), field);
}

Bebop_Writer* bebop_context_writer(Bebop_Context* context, size_t capacity)
{
  if (!context) {
    return NULL;
  }

  Bebop_Writer* writer =
      (Bebop_Writer*)bebop_wire_arena_alloc(context->arena, sizeof(Bebop_Writer));
  if (!writer) {
    return NULL;
  }

  size_t buffer_size = capacity > context->options.initial_writer_size
      ? capacity
      : context->options.initial_writer_size;
  if (buffer_size == 0) {
    buffer_size = 1;
  }
  uint8_t* buffer = (uint8_t*)bebop_wire_arena_alloc(context->arena, buffer_size);
  if (!buffer) {
    return NULL;
  }

  writer->buffer = buffer;
  writer->current = buffer;
  writer->end = buffer + buffer_size;
  writer->context = context;
  return writer;
}

void bebop_writer_reset(Bebop_Writer* writer)
{
  if (!writer) {
    return;
  }
  writer->current = writer->buffer;
}

Bebop_Result bebop_writer_reserve(Bebop_Writer* writer, size_t additional_bytes)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_LIKELY(additional_bytes <= (size_t)(writer->end - writer->current))) {
    return BEBOP_RESULT_OK;
  }

  const size_t current_size = (size_t)(writer->end - writer->buffer);
  const size_t used_size = (size_t)(writer->current - writer->buffer);
  if (BEBOP_WIRE_UNLIKELY(additional_bytes > SIZE_MAX - used_size)) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t target_size = used_size + additional_bytes;
  size_t new_size = current_size > 0 ? current_size : 16;

  while (new_size < target_size) {
    if (BEBOP_WIRE_UNLIKELY(new_size > SIZE_MAX / 2)) {
      new_size = target_size;
      break;
    }
    new_size *= 2;
  }

  uint8_t* new_buffer = (uint8_t*)bebop_wire_arena_alloc(writer->context->arena, new_size);
  if (!new_buffer) {
    return BEBOP_RESULT_OOM;
  }

  memcpy(new_buffer, writer->buffer, used_size);
  writer->buffer = new_buffer;
  writer->current = new_buffer + used_size;
  writer->end = new_buffer + new_size;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_byte(Bebop_Writer* writer, uint8_t value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(1 > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, 1);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  *writer->current++ = value;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_u16(Bebop_Writer* writer, uint16_t value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint16_t) > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, sizeof(uint16_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_u32(Bebop_Writer* writer, uint32_t value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint32_t) > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, sizeof(uint32_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_u64(Bebop_Writer* writer, uint64_t value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint64_t) > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, sizeof(uint64_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_i8(Bebop_Writer* writer, int8_t value)
{
  return bebop_writer_write_byte(writer, (uint8_t)value);
}

Bebop_Result bebop_writer_write_i16(Bebop_Writer* writer, int16_t value)
{
  return bebop_writer_write_u16(writer, (uint16_t)value);
}

Bebop_Result bebop_writer_write_i32(Bebop_Writer* writer, int32_t value)
{
  return bebop_writer_write_u32(writer, (uint32_t)value);
}

Bebop_Result bebop_writer_write_i64(Bebop_Writer* writer, int64_t value)
{
  return bebop_writer_write_u64(writer, (uint64_t)value);
}

Bebop_Result bebop_writer_write_bool(Bebop_Writer* writer, bool value)
{
  return bebop_writer_write_byte(writer, value ? 1 : 0);
}

Bebop_Result bebop_writer_write_f16(Bebop_Writer* writer, Bebop_Float16 value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint16_t) > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, sizeof(uint16_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_bf16(Bebop_Writer* writer, Bebop_BFloat16 value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(sizeof(uint16_t) > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, sizeof(uint16_t));
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_f32(Bebop_Writer* writer, float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(float));
  return bebop_writer_write_u32(writer, bits);
}

Bebop_Result bebop_writer_write_f64(Bebop_Writer* writer, double value)
{
  uint64_t bits;
  memcpy(&bits, &value, sizeof(double));
  return bebop_writer_write_u64(writer, bits);
}

Bebop_Result bebop_writer_write_i128(Bebop_Writer* writer, Bebop_Int128 value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_u128(Bebop_Writer* writer, Bebop_UInt128 value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
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
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_uuid(Bebop_Writer* writer, Bebop_UUID value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
  memcpy(writer->current, value.bytes, 16);
  writer->current += 16;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_timestamp(Bebop_Writer* writer, Bebop_Timestamp value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(16 > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, 16);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, &value, 16);
  writer->current += 16;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_duration(Bebop_Writer* writer, Bebop_Duration value)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY(12 > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, 12);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, &value, 12);
  writer->current += 12;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_string(Bebop_Writer* writer, Bebop_String value)
{
  if (!writer || !value.data) {
    return BEBOP_RESULT_NULL;
  }

  // >= so the NUL terminator below never wraps length + 1 on 32-bit size_t
  if (BEBOP_WIRE_UNLIKELY(value.length >= UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)value.length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  const size_t total = value.length + 1;
  if (BEBOP_WIRE_UNLIKELY(total > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, value.data, value.length);
  writer->current[value.length] = '\0';
  writer->current += total;

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_bytes(Bebop_Writer* writer, Bebop_Bytes value)
{
  if (!writer || !value.data) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(value.length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)value.length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (value.length > 0) {
    if (BEBOP_WIRE_UNLIKELY(value.length > (size_t)(writer->end - writer->current))) {
      result = bebop_writer_reserve(writer, value.length);
      if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
        return result;
      }
    }

    memcpy(writer->current, value.data, value.length);
    writer->current += value.length;
  }

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_bytes(Bebop_Writer* writer, Bebop_Bytes value)
{
  if (!writer || !value.data) {
    return BEBOP_RESULT_NULL;
  }
  if (value.length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(value.length > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, value.length);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, value.data, value.length);
  writer->current += value.length;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_f32_array(Bebop_Writer* writer, const float* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(float))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(float);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_f32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_f64_array(Bebop_Writer* writer, const double* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(double))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(double);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_f64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_u16_array(Bebop_Writer* writer, const uint16_t* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(uint16_t))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(uint16_t);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_u16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_i16_array(Bebop_Writer* writer, const int16_t* data, size_t length)
{
  return bebop_writer_write_u16_array(writer, (const uint16_t*)data, length);
}

Bebop_Result bebop_writer_write_u32_array(Bebop_Writer* writer, const uint32_t* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(uint32_t))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(uint32_t);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_u32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_i32_array(Bebop_Writer* writer, const int32_t* data, size_t length)
{
  return bebop_writer_write_u32_array(writer, (const uint32_t*)data, length);
}

Bebop_Result bebop_writer_write_u64_array(Bebop_Writer* writer, const uint64_t* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(uint64_t))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(uint64_t);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_u64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_i64_array(Bebop_Writer* writer, const int64_t* data, size_t length)
{
  return bebop_writer_write_u64_array(writer, (const uint64_t*)data, length);
}

Bebop_Result bebop_writer_write_i8_array(Bebop_Writer* writer, const int8_t* data, size_t length)
{
  return bebop_writer_write_bytes(writer, bebop_bytes(data, length));
}

Bebop_Result bebop_writer_write_u8_array(Bebop_Writer* writer, const uint8_t* data, size_t length)
{
  return bebop_writer_write_bytes(writer, bebop_bytes(data, length));
}

Bebop_Result bebop_writer_write_f16_array(
    Bebop_Writer* writer, const Bebop_Float16* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(Bebop_Float16))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(Bebop_Float16);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_f16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_bf16_array(
    Bebop_Writer* writer, const Bebop_BFloat16* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(Bebop_BFloat16))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(Bebop_BFloat16);

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_bf16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_i128_array(
    Bebop_Writer* writer, const Bebop_Int128* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  const size_t total_bytes = length * 16;

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_i128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_u128_array(
    Bebop_Writer* writer, const Bebop_UInt128* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  const size_t total_bytes = length * 16;

  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < length; i++) {
    result = bebop_writer_write_u128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_bool_array(Bebop_Writer* writer, const bool* data, size_t length)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, length);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  for (size_t i = 0; i < length; i++) {
    *writer->current++ = data[i] ? 1 : 0;
  }

  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_uuid_array(
    Bebop_Writer* writer, const Bebop_UUID* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(Bebop_UUID))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(Bebop_UUID);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_timestamp_array(
    Bebop_Writer* writer, const Bebop_Timestamp* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(Bebop_Timestamp))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(Bebop_Timestamp);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_duration_array(
    Bebop_Writer* writer, const Bebop_Duration* data, size_t length
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }

  if (BEBOP_WIRE_UNLIKELY(length > UINT32_MAX)) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_write_u32(writer, (uint32_t)length);
  if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
    return result;
  }

  if (length == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(length > SIZE_MAX / sizeof(Bebop_Duration))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = length * sizeof(Bebop_Duration);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_u8_array(
    Bebop_Writer* writer, const uint8_t* data, size_t count
)
{
  return bebop_writer_write_fixed_bytes(writer, bebop_bytes(data, count));
}

Bebop_Result bebop_writer_write_fixed_i8_array(
    Bebop_Writer* writer, const int8_t* data, size_t count
)
{
  return bebop_writer_write_fixed_bytes(writer, bebop_bytes(data, count));
}

Bebop_Result bebop_writer_write_fixed_bool_array(
    Bebop_Writer* writer, const bool* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, count);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, count);
  writer->current += count;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_u16_array(
    Bebop_Writer* writer, const uint16_t* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(uint16_t))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(uint16_t);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_u16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_i16_array(
    Bebop_Writer* writer, const int16_t* data, size_t count
)
{
  return bebop_writer_write_fixed_u16_array(writer, (const uint16_t*)data, count);
}

Bebop_Result bebop_writer_write_fixed_u32_array(
    Bebop_Writer* writer, const uint32_t* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(uint32_t))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(uint32_t);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_u32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_i32_array(
    Bebop_Writer* writer, const int32_t* data, size_t count
)
{
  return bebop_writer_write_fixed_u32_array(writer, (const uint32_t*)data, count);
}

Bebop_Result bebop_writer_write_fixed_u64_array(
    Bebop_Writer* writer, const uint64_t* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(uint64_t))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(uint64_t);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_u64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_i64_array(
    Bebop_Writer* writer, const int64_t* data, size_t count
)
{
  return bebop_writer_write_fixed_u64_array(writer, (const uint64_t*)data, count);
}

Bebop_Result bebop_writer_write_fixed_f16_array(
    Bebop_Writer* writer, const Bebop_Float16* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_Float16))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_Float16);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_f16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_bf16_array(
    Bebop_Writer* writer, const Bebop_BFloat16* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_BFloat16))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_BFloat16);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_bf16(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_f32_array(
    Bebop_Writer* writer, const float* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(float))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(float);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_f32(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_f64_array(
    Bebop_Writer* writer, const double* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(double))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(double);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_f64(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_i128_array(
    Bebop_Writer* writer, const Bebop_Int128* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_Int128))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_Int128);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_i128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_u128_array(
    Bebop_Writer* writer, const Bebop_UInt128* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_UInt128))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_UInt128);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_writer_write_u128(writer, data[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_uuid_array(
    Bebop_Writer* writer, const Bebop_UUID* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_UUID))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_UUID);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_timestamp_array(
    Bebop_Writer* writer, const Bebop_Timestamp* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_Timestamp))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_Timestamp);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_write_fixed_duration_array(
    Bebop_Writer* writer, const Bebop_Duration* data, size_t count
)
{
  if (BEBOP_WIRE_UNLIKELY(!writer || !data)) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }

  if (BEBOP_WIRE_UNLIKELY(count > SIZE_MAX / sizeof(Bebop_Duration))) {
    return BEBOP_RESULT_OVERFLOW;
  }
  const size_t total_bytes = count * sizeof(Bebop_Duration);
  if (BEBOP_WIRE_UNLIKELY(total_bytes > (size_t)(writer->end - writer->current))) {
    const Bebop_Result result = bebop_writer_reserve(writer, total_bytes);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }

  memcpy(writer->current, data, total_bytes);
  writer->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_u8_array(Bebop_Reader* reader, uint8_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }
  memcpy(out, reader->current, count);
  reader->current += count;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_i8_array(Bebop_Reader* reader, int8_t* out, size_t count)
{
  return bebop_reader_read_fixed_u8_array(reader, (uint8_t*)out, count);
}

Bebop_Result bebop_reader_read_fixed_bool_array(Bebop_Reader* reader, bool* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }
  memcpy(out, reader->current, count);
  reader->current += count;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_u16_array(Bebop_Reader* reader, uint16_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(uint16_t))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(uint16_t);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_u16(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_i16_array(Bebop_Reader* reader, int16_t* out, size_t count)
{
  return bebop_reader_read_fixed_u16_array(reader, (uint16_t*)out, count);
}

Bebop_Result bebop_reader_read_fixed_u32_array(Bebop_Reader* reader, uint32_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(uint32_t))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(uint32_t);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_u32(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_i32_array(Bebop_Reader* reader, int32_t* out, size_t count)
{
  return bebop_reader_read_fixed_u32_array(reader, (uint32_t*)out, count);
}

Bebop_Result bebop_reader_read_fixed_u64_array(Bebop_Reader* reader, uint64_t* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(uint64_t))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(uint64_t);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_u64(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_i64_array(Bebop_Reader* reader, int64_t* out, size_t count)
{
  return bebop_reader_read_fixed_u64_array(reader, (uint64_t*)out, count);
}

Bebop_Result bebop_reader_read_fixed_f16_array(
    Bebop_Reader* reader, Bebop_Float16* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_Float16)))
  {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_Float16);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_f16(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_bf16_array(
    Bebop_Reader* reader, Bebop_BFloat16* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_BFloat16)))
  {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_BFloat16);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_bf16(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_f32_array(Bebop_Reader* reader, float* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(float))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(float);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_f32(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_f64_array(Bebop_Reader* reader, double* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(double))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(double);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_f64(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_i128_array(
    Bebop_Reader* reader, Bebop_Int128* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_Int128))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_Int128);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_i128(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_u128_array(
    Bebop_Reader* reader, Bebop_UInt128* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_UInt128)))
  {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_UInt128);

#if BEBOP_WIRE_ASSUME_LE
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
#else
  for (size_t i = 0; i < count; i++) {
    Bebop_Result result = bebop_reader_read_u128(reader, &out[i]);
    if (BEBOP_WIRE_UNLIKELY(result != BEBOP_RESULT_OK)) {
      return result;
    }
  }
#endif
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_uuid_array(Bebop_Reader* reader, Bebop_UUID* out, size_t count)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_UUID))) {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_UUID);
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_timestamp_array(
    Bebop_Reader* reader, Bebop_Timestamp* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_Timestamp)))
  {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_Timestamp);
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_reader_read_fixed_duration_array(
    Bebop_Reader* reader, Bebop_Duration* out, size_t count
)
{
  if (!reader || !out) {
    return BEBOP_RESULT_NULL;
  }
  if (count == 0) {
    return BEBOP_RESULT_OK;
  }
  if (BEBOP_WIRE_UNLIKELY(count > (size_t)(reader->end - reader->current) / sizeof(Bebop_Duration)))
  {
    return BEBOP_RESULT_MALFORMED;
  }
  const size_t total_bytes = count * sizeof(Bebop_Duration);
  memcpy(out, reader->current, total_bytes);
  reader->current += total_bytes;
  return BEBOP_RESULT_OK;
}

Bebop_Result bebop_writer_begin_length(Bebop_Writer* writer, size_t* position)
{
  if (!writer || !position) {
    return BEBOP_RESULT_NULL;
  }

  *position = bebop_writer_length(writer);
  return bebop_writer_write_u32(writer, 0);
}

Bebop_Result bebop_writer_end_length(Bebop_Writer* writer, size_t position, uint32_t length)
{
  if (!writer) {
    return BEBOP_RESULT_NULL;
  }
  const size_t written = bebop_writer_length(writer);
  if (BEBOP_WIRE_UNLIKELY(written < sizeof(uint32_t) || position > written - sizeof(uint32_t))) {
    return BEBOP_RESULT_MALFORMED;
  }

#if BEBOP_WIRE_ASSUME_LE
  memcpy(writer->buffer + position, &length, sizeof(uint32_t));
#else
  writer->buffer[position++] = length;
  writer->buffer[position++] = length >> 8;
  writer->buffer[position++] = length >> 16;
  writer->buffer[position++] = length >> 24;
#endif
  return BEBOP_RESULT_OK;
}

size_t bebop_indexed_message_size(
    size_t payload_size, const uint8_t* sorted_tags, uint8_t field_count
)
{
  if (!bebop_message_tags_valid(sorted_tags, field_count) || payload_size > UINT32_MAX) {
    return SIZE_MAX;
  }
  const uint8_t width = payload_size <= UINT8_MAX ? 1 : payload_size <= UINT16_MAX ? 2 : 4;
  const bebop_message_directory_layout_t directory =
      bebop_message_directory_layout(sorted_tags, field_count);
  const size_t boundary_count = field_count == 0 ? 0 : (size_t)field_count - 1;
  const size_t metadata_size = 1u + directory.size + boundary_count * width;
  if (payload_size > UINT32_MAX - metadata_size
      || payload_size > SIZE_MAX - BEBOP_WIRE_SIZE_LEN - metadata_size)
  {
    return SIZE_MAX;
  }
  return BEBOP_WIRE_SIZE_LEN + payload_size + metadata_size;
}

Bebop_Result bebop_writer_end_indexed_message(
    Bebop_Writer* writer,
    size_t length_position,
    size_t payload_start,
    const uint8_t* sorted_tags,
    const uint32_t* payload_offsets,
    uint8_t field_count
)
{
  if (!writer || (field_count != 0 && (!sorted_tags || !payload_offsets))) {
    return BEBOP_RESULT_NULL;
  }
  if (!bebop_message_tags_valid(sorted_tags, field_count)) {
    return BEBOP_RESULT_INVALID;
  }

  const size_t written = bebop_writer_length(writer);
  if (length_position > SIZE_MAX - BEBOP_WIRE_SIZE_LEN
      || length_position + BEBOP_WIRE_SIZE_LEN != payload_start || payload_start > written)
  {
    return BEBOP_RESULT_INVALID;
  }
  const size_t payload_size = written - payload_start;
  if (payload_size > UINT32_MAX) {
    return BEBOP_RESULT_OVERFLOW;
  }
  if (field_count == 0 && payload_size != 0) {
    return BEBOP_RESULT_INVALID;
  }
  if (field_count != 0 && payload_offsets[0] != 0) {
    return BEBOP_RESULT_INVALID;
  }
  for (uint16_t i = 0; i < field_count; i++) {
    if (payload_offsets[i] > payload_size
        || (i != 0 && payload_offsets[i] < payload_offsets[i - 1]))
    {
      return BEBOP_RESULT_INVALID;
    }
  }

  const uint8_t width = payload_size <= UINT8_MAX ? 1 : payload_size <= UINT16_MAX ? 2 : 4;
  const uint8_t width_code = width == 1 ? 0 : width == 2 ? 1 : 2;
  const bebop_message_directory_layout_t directory =
      bebop_message_directory_layout(sorted_tags, field_count);
  const size_t boundary_count = field_count == 0 ? 0 : (size_t)field_count - 1;
  const size_t metadata_size = 1u + directory.size + boundary_count * width;
  if (payload_size > UINT32_MAX - metadata_size) {
    return BEBOP_RESULT_OVERFLOW;
  }

  Bebop_Result result = bebop_writer_reserve(writer, metadata_size);
  if (result != BEBOP_RESULT_OK) {
    return result;
  }
  uint8_t* output = writer->current;
  for (uint16_t i = 1; i < field_count; i++) {
    bebop_wire_store_le(&output, payload_offsets[i], width);
  }

  switch (directory.kind) {
    case BEBOP_MESSAGE_DIRECTORY_EMPTY:
      break;
    case BEBOP_MESSAGE_DIRECTORY_TINY1:
    case BEBOP_MESSAGE_DIRECTORY_TINY2:
    case BEBOP_MESSAGE_DIRECTORY_TINY3:
      memcpy(output, sorted_tags, field_count);
      output += field_count;
      break;
    case BEBOP_MESSAGE_DIRECTORY_MASK8:
    case BEBOP_MESSAGE_DIRECTORY_MASK16:
    case BEBOP_MESSAGE_DIRECTORY_MASK32: {
      uint32_t mask = 0;
      for (uint16_t i = 0; i < field_count; i++) {
        mask |= UINT32_C(1) << (sorted_tags[i] - 1u);
      }
      bebop_wire_store_le(&output, mask, directory.size);
      break;
    }
    case BEBOP_MESSAGE_DIRECTORY_BLOCKS: {
      uint16_t next = 0;
      uint8_t rank = 0;
      for (uint8_t block = 0; block < 8; block++) {
        if ((directory.block_mask & (uint8_t)(1u << block)) == 0) {
          continue;
        }
        uint32_t mask = 0;
        const uint8_t first_tag = (uint8_t)(block * 32u + 1u);
        const uint16_t block_limit = (uint16_t)first_tag + 32u;
        while (next < field_count && sorted_tags[next] < block_limit) {
          mask |= UINT32_C(1) << (sorted_tags[next] - first_tag);
          next++;
        }
        *output++ = rank;
        bebop_wire_store_le(&output, mask, 4);
        rank = (uint8_t)(rank + bebop_wire_popcount32(mask));
      }
      *output++ = directory.block_mask;
      break;
    }
    default:
      return BEBOP_RESULT_INVALID;
  }
  *output++ = (uint8_t)((directory.kind << 2) | width_code);
  writer->current = output;
  return bebop_writer_end_length(writer, length_position, (uint32_t)(payload_size + metadata_size));
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

Bebop_UUID bebop_uuid_parse(const char* str)
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
    // The lookup table maps invalid characters to 0; reject them instead of
    // silently producing a wrong UUID.
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) {
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

size_t bebop_uuid_format(Bebop_UUID uuid, char* buf, size_t len)
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

Bebop_ContextOptions bebop_context_options(void)
{
  const Bebop_ContextOptions options = {
      .arena_options =
          {.initial_block_size = 4096,
           .max_block_size = 1048576,
           .allocator = {.alloc = bebop_default_alloc, .ctx = NULL}},
      .initial_writer_size = 1024,
      .max_decode_depth = BEBOP_WIRE_DEFAULT_MAX_DECODE_DEPTH
  };
  return options;
}

size_t bebop_reader_position(const Bebop_Reader* reader)
{
  return reader ? (size_t)(reader->current - reader->start) : 0;
}

const uint8_t* bebop_reader_data(const Bebop_Reader* reader)
{
  return reader ? reader->current : NULL;
}

size_t bebop_reader_remaining(const Bebop_Reader* reader)
{
  return reader ? (size_t)(reader->end - reader->current) : 0;
}

Bebop_Result bebop_reader_push_limit(Bebop_Reader* reader, uint32_t len, const uint8_t** old_end)
{
  if (BEBOP_WIRE_UNLIKELY(!reader || !old_end)) {
    return BEBOP_RESULT_NULL;
  }
  if (BEBOP_WIRE_UNLIKELY((size_t)len > (size_t)(reader->end - reader->current))) {
    return BEBOP_RESULT_MALFORMED;
  }
  *old_end = reader->end;
  reader->end = reader->current + len;
  return BEBOP_RESULT_OK;
}

void bebop_reader_pop_limit(Bebop_Reader* reader, const uint8_t* old_end)
{
  if (reader && old_end && old_end >= reader->end) {
    reader->end = old_end;
  }
}

size_t bebop_writer_length(const Bebop_Writer* writer)
{
  return writer ? (size_t)(writer->current - writer->buffer) : 0;
}

Bebop_View bebop_writer_view(const Bebop_Writer* writer)
{
  return writer ? (Bebop_View) {writer->buffer, (size_t)(writer->current - writer->buffer)}
                : (Bebop_View) {NULL, 0};
}

size_t bebop_writer_available(const Bebop_Writer* writer)
{
  return writer ? (size_t)(writer->end - writer->current) : 0;
}

Bebop_String bebop_string(const char* str)
{
  const Bebop_String view = {str, str ? strlen(str) : 0};
  return view;
}

bool bebop_string_equal(Bebop_String a, Bebop_String b)
{
  return a.length == b.length && (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

bool bebop_uuid_equal(Bebop_UUID a, Bebop_UUID b)
{
  return memcmp(&a, &b, sizeof(Bebop_UUID)) == 0;
}

void* bebop_context_alloc(Bebop_Context* context, size_t size)
{
  return context ? bebop_wire_arena_alloc(context->arena, size) : NULL;
}

void* bebop_context_realloc(Bebop_Context* context, void* ptr, size_t old_size, size_t new_size)
{
  return context ? bebop_wire_arena_realloc(context->arena, ptr, old_size, new_size) : NULL;
}

void* bebop_context_alloc_array(Bebop_Context* context, size_t count, size_t elem_size)
{
  if (!context || (elem_size != 0 && count > SIZE_MAX / elem_size)) {
    return NULL;
  }
  return bebop_wire_arena_alloc(context->arena, count * elem_size);
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

void* bebop_map_get(const Bebop_Map* m, const void* key)
{
  if (!m || !m->ctrl || m->length == 0) {
    return NULL;
  }

  const uint64_t h = m->hash(key);
  const int8_t h2 = H2(h);
  const size_t mask = m->capacity - 1;
  const size_t start = H1(h) & mask;
  const size_t max_probes = m->capacity / BEBOP_MAP_GROUP_SIZE + 2;

  for (size_t probe = 0; probe < max_probes; probe++) {
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
  return NULL;
}

bool bebop_map_set(Bebop_Map* m, void* key, void* value)
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
  const size_t max_probes = m->capacity / BEBOP_MAP_GROUP_SIZE + 2;

  size_t insert_idx = SIZE_MAX;

  for (size_t probe = 0; probe < max_probes; probe++) {
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

bool bebop_map_remove(Bebop_Map* m, const void* key)
{
  if (!m || !m->ctrl || m->length == 0) {
    return false;
  }

  const uint64_t h = m->hash(key);
  const int8_t h2 = H2(h);
  const size_t mask = m->capacity - 1;
  const size_t start = H1(h) & mask;
  const size_t max_probes = m->capacity / BEBOP_MAP_GROUP_SIZE + 2;

  for (size_t probe = 0; probe < max_probes; probe++) {
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
  return false;
}

void bebop_map_clear(Bebop_Map* m)
{
  if (m && m->ctrl) {
    memset(m->ctrl, CTRL_EMPTY, m->capacity);
    m->length = 0;
    m->growth_left = (m->capacity * BEBOP_MAP_LOAD_FACTOR) / 8;
  }
}

void bebop_map_iter_init(Bebop_MapIter* it, const Bebop_Map* m)
{
  it->map = m;
  it->index = 0;
}

bool bebop_map_iter_next(Bebop_MapIter* it, void** key, void** value)
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

  int8_t* new_ctrl = bebop_context_alloc(m->ctx, ctrl_size);
  if (!new_ctrl) {
    return false;
  }
  memset(new_ctrl, CTRL_EMPTY, ctrl_size);

  Bebop_MapSlot* new_slots = bebop_context_alloc(m->ctx, slots_size);
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
      if (!bebop_map_set(m, old_slots[i].key, old_slots[i].value)) {
        return false;
      }
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
static uint64_t bebop_map_hash_bool(const void* key)
{
  return bebop__wyhash64(*(const bool*)key, 0);
}

static uint64_t bebop_map_hash_i8(const void* key)
{
  return bebop__wyhash64((uint64_t)*(const int8_t*)key, 0);
}

static uint64_t bebop_map_hash_u8(const void* key)
{
  return bebop__wyhash64(*(const uint8_t*)key, 0);
}

static uint64_t bebop_map_hash_i16(const void* key)
{
  return bebop__wyhash64((uint64_t)*(const int16_t*)key, 0);
}

static uint64_t bebop_map_hash_u16(const void* key)
{
  return bebop__wyhash64(*(const uint16_t*)key, 0);
}

static uint64_t bebop_map_hash_i32(const void* key)
{
  return bebop__wyhash64((uint64_t)*(const int32_t*)key, 0);
}

static uint64_t bebop_map_hash_u32(const void* key)
{
  return bebop__wyhash64(*(const uint32_t*)key, 0);
}

static uint64_t bebop_map_hash_i64(const void* key)
{
  return bebop__wyhash64(*(const uint64_t*)key, 0);
}

static uint64_t bebop_map_hash_u64(const void* key)
{
  return bebop__wyhash64(*(const uint64_t*)key, 0);
}

static uint64_t bebop_map_hash_i128(const void* key)
{
  uint64_t lo, hi;
  memcpy(&lo, key, sizeof(lo));
  memcpy(&hi, (const uint8_t*)key + sizeof(lo), sizeof(hi));
  return bebop__wyhash64(lo, hi);
}

static uint64_t bebop_map_hash_u128(const void* key)
{
  uint64_t lo, hi;
  memcpy(&lo, key, sizeof(lo));
  memcpy(&hi, (const uint8_t*)key + sizeof(lo), sizeof(hi));
  return bebop__wyhash64(lo, hi);
}

static uint64_t bebop_map_hash_uuid(const void* key)
{
  uint64_t lo, hi;
  memcpy(&lo, key, sizeof(lo));
  memcpy(&hi, (const uint8_t*)key + sizeof(lo), sizeof(hi));
  return bebop__wyhash64(lo, hi);
}

static uint64_t bebop_map_hash_string(const void* key)
{
  const Bebop_String* s = (const Bebop_String*)key;
  return bebop__wyhash(s->data, s->length, 0);
}

// Map equality functions for all valid map key types
static bool bebop_map_equal_bool(const void* a, const void* b)
{
  return *(const bool*)a == *(const bool*)b;
}

static bool bebop_map_equal_i8(const void* a, const void* b)
{
  return *(const int8_t*)a == *(const int8_t*)b;
}

static bool bebop_map_equal_u8(const void* a, const void* b)
{
  return *(const uint8_t*)a == *(const uint8_t*)b;
}

static bool bebop_map_equal_i16(const void* a, const void* b)
{
  return *(const int16_t*)a == *(const int16_t*)b;
}

static bool bebop_map_equal_u16(const void* a, const void* b)
{
  return *(const uint16_t*)a == *(const uint16_t*)b;
}

static bool bebop_map_equal_i32(const void* a, const void* b)
{
  return *(const int32_t*)a == *(const int32_t*)b;
}

static bool bebop_map_equal_u32(const void* a, const void* b)
{
  return *(const uint32_t*)a == *(const uint32_t*)b;
}

static bool bebop_map_equal_i64(const void* a, const void* b)
{
  return *(const int64_t*)a == *(const int64_t*)b;
}

static bool bebop_map_equal_u64(const void* a, const void* b)
{
  return *(const uint64_t*)a == *(const uint64_t*)b;
}

static bool bebop_map_equal_i128(const void* a, const void* b)
{
  return memcmp(a, b, 16) == 0;
}

static bool bebop_map_equal_u128(const void* a, const void* b)
{
  return memcmp(a, b, 16) == 0;
}

static bool bebop_map_equal_uuid(const void* a, const void* b)
{
  return memcmp(a, b, 16) == 0;
}

static bool bebop_map_equal_string(const void* a, const void* b)
{
  const Bebop_String* sa = (const Bebop_String*)a;
  const Bebop_String* sb = (const Bebop_String*)b;
  return sa->length == sb->length && memcmp(sa->data, sb->data, sa->length) == 0;
}

void bebop_map_init(Bebop_Map* map, Bebop_Context* context, Bebop_MapKeyKind key_kind)
{
  static const Bebop_MapHashFn hashes[] = {
      bebop_map_hash_bool,
      bebop_map_hash_i8,
      bebop_map_hash_u8,
      bebop_map_hash_i16,
      bebop_map_hash_u16,
      bebop_map_hash_i32,
      bebop_map_hash_u32,
      bebop_map_hash_i64,
      bebop_map_hash_u64,
      bebop_map_hash_i128,
      bebop_map_hash_u128,
      bebop_map_hash_string,
      bebop_map_hash_uuid,
  };
  static const Bebop_MapEqFn equals[] = {
      bebop_map_equal_bool,
      bebop_map_equal_i8,
      bebop_map_equal_u8,
      bebop_map_equal_i16,
      bebop_map_equal_u16,
      bebop_map_equal_i32,
      bebop_map_equal_u32,
      bebop_map_equal_i64,
      bebop_map_equal_u64,
      bebop_map_equal_i128,
      bebop_map_equal_u128,
      bebop_map_equal_string,
      bebop_map_equal_uuid,
  };

  if (!map) {
    return;
  }
  *map = (Bebop_Map) {0};
  if (!context || key_kind < BEBOP_MAP_KEY_BOOL || key_kind > BEBOP_MAP_KEY_UUID) {
    assert(context && key_kind >= BEBOP_MAP_KEY_BOOL && key_kind <= BEBOP_MAP_KEY_UUID);
    return;
  }
  *map = (Bebop_Map) {
      .hash = hashes[key_kind],
      .eq = equals[key_kind],
      .ctx = context,
  };
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
  Bebop_String type_url;
  Bebop_U8_Array value;
} bebop_any_layout;

static size_t bebop__cstrlen(const char* s)
{
  const char* p = s;
  while (*p) {
    p++;
  }
  return (size_t)(p - s);
}

BEBOP_API Bebop_Result bebop_any_pack(
    Bebop_Context* ctx, Bebop_Any* any, const void* record, const Bebop_TypeInfo* type_info
)
{
  if (!ctx || !any || !record || !type_info || !type_info->type_fqn || !type_info->size_fn
      || !type_info->encode_fn)
  {
    return BEBOP_RESULT_NULL;
  }

  bebop_any_layout* a = (bebop_any_layout*)any;

  const char* prefix = type_info->prefix ? type_info->prefix : BEBOP_TYPE_URL_PREFIX;
  const size_t prefix_len = bebop__cstrlen(prefix);
  const size_t fqn_len = bebop__cstrlen(type_info->type_fqn);
  const size_t url_len = prefix_len + fqn_len;

  char* url_buf = (char*)bebop_context_alloc(ctx, url_len + 1);
  if (!url_buf) {
    return BEBOP_RESULT_OOM;
  }
  memcpy(url_buf, prefix, prefix_len);
  memcpy(url_buf + prefix_len, type_info->type_fqn, fqn_len);
  url_buf[url_len] = '\0';

  a->type_url.data = url_buf;
  a->type_url.length = url_len;

  const size_t encoded_size = type_info->size_fn(record);

  Bebop_Writer* w = bebop_context_writer(ctx, encoded_size);
  if (!w) {
    return BEBOP_RESULT_OOM;
  }

  Bebop_Result r = type_info->encode_fn(w, record);
  if (r != BEBOP_RESULT_OK) {
    return r;
  }

  a->value.data = w->buffer;
  a->value.length = bebop_writer_length(w);
  a->value.capacity = 0;

  return BEBOP_RESULT_OK;
}

BEBOP_API bool bebop_any_is(const Bebop_Any* any, const char* type_fqn)
{
  if (!any || !type_fqn) {
    return false;
  }

  const char* name = bebop_any_type_name(any);
  if (!name) {
    return false;
  }

  const bebop_any_layout* a = (const bebop_any_layout*)any;
  const char* url_end = a->type_url.data + a->type_url.length;
  const size_t name_len = (size_t)(url_end - name);
  const size_t fqn_len = bebop__cstrlen(type_fqn);

  if (name_len != fqn_len) {
    return false;
  }

  return memcmp(name, type_fqn, fqn_len) == 0;
}

BEBOP_API const char* bebop_any_type_name(const Bebop_Any* any)
{
  if (!any) {
    return NULL;
  }

  const bebop_any_layout* a = (const bebop_any_layout*)any;

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

BEBOP_API Bebop_Result bebop_any_unpack(
    Bebop_Context* ctx, const Bebop_Any* any, void* record, const Bebop_TypeInfo* type_info
)
{
  if (!ctx || !any || !record || !type_info || !type_info->decode_fn) {
    return BEBOP_RESULT_NULL;
  }

  const bebop_any_layout* a = (const bebop_any_layout*)any;

  if (!a->value.data) {
    return BEBOP_RESULT_NULL;
  }

  Bebop_Reader* rd = bebop_context_reader(ctx, bebop_view(a->value.data, a->value.length));
  if (!rd) {
    return BEBOP_RESULT_OOM;
  }

  return type_info->decode_fn(ctx, rd, record);
}

// #endregion
