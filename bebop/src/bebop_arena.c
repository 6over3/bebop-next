struct bebop__chunk {
  bebop__chunk_t* next;
  size_t capacity;
  size_t used;
};

static inline char* bebop__chunk_data(bebop__chunk_t* chunk)
{
  return (char*)(chunk + 1);
}

static void* bebop__alloc(const bebop_host_allocator_t* a, const size_t size)
{
  BEBOP_ASSERT(a->alloc != NULL);
  return a->alloc(NULL, 0, size, a->ctx);
}

static void bebop__free(const bebop_host_allocator_t* a, void* ptr, const size_t size)
{
  BEBOP_ASSERT(a->alloc != NULL);
  a->alloc(ptr, size, 0, a->ctx);
}

static inline size_t bebop__align_up(const size_t val, const size_t align)
{
  return (val + align - 1) & ~(align - 1);
}

static inline char* bebop__align_ptr(const char* ptr, const size_t align)
{
  const uintptr_t addr = (uintptr_t)ptr;
  const uintptr_t aligned = (addr + align - 1) & ~(align - 1);
  return (char*)aligned;
}

static bebop__chunk_t* bebop__chunk_alloc(
    const bebop_arena_t* arena, const size_t min_data_size, const bool use_default
)
{
  const size_t header_size = bebop__align_up(sizeof(bebop__chunk_t), BEBOP_ARENA_MIN_ALIGN);

  size_t data_size = min_data_size;
  const size_t default_data = BEBOP_ARENA_CHUNK_SIZE - header_size;
  if (use_default && data_size < default_data) {
    data_size = default_data;
  }

  if (data_size > SIZE_MAX - header_size) {
    return NULL;
  }
  const size_t total_size = header_size + data_size;

  void* mem = bebop__alloc(&arena->alloc, total_size);
  if (!mem) {
    return NULL;
  }

  bebop__chunk_t* chunk = mem;
  chunk->next = NULL;
  chunk->capacity = data_size;
  chunk->used = 0;

  return chunk;
}

static void bebop__chunk_free(const bebop_arena_t* arena, bebop__chunk_t* chunk)
{
  const size_t header_size = bebop__align_up(sizeof(bebop__chunk_t), BEBOP_ARENA_MIN_ALIGN);
  const size_t total_size = header_size + chunk->capacity;
  bebop__free(&arena->alloc, chunk, total_size);
}

bool bebop_arena_init(
    bebop_arena_t* arena, const bebop_host_allocator_t* alloc, const size_t initial
)
{
  BEBOP_ASSERT(arena != NULL);
  BEBOP_ASSERT(alloc != NULL);
  BEBOP_ASSERT(alloc->alloc != NULL);

  memset(arena, 0, sizeof(*arena));
  arena->alloc = *alloc;

  const size_t init_size = initial > 0 ? initial : BEBOP_ARENA_CHUNK_SIZE;
  bebop__chunk_t* chunk = bebop__chunk_alloc(arena, init_size, initial == 0);
  if (!chunk) {
    return false;
  }

  arena->head = chunk;
  arena->current = chunk;
  return true;
}

void bebop_arena_destroy(bebop_arena_t* arena)
{
  if (!arena) {
    return;
  }

  bebop__chunk_t* chunk = arena->head;
  while (chunk) {
    bebop__chunk_t* next = chunk->next;
    bebop__chunk_free(arena, chunk);
    chunk = next;
  }

  memset(arena, 0, sizeof(*arena));
}

void bebop_arena_reset(bebop_arena_t* arena)
{
  if (!arena || !arena->head) {
    return;
  }

  bebop__chunk_t* chunk = arena->head->next;
  while (chunk) {
    bebop__chunk_t* next = chunk->next;
    bebop__chunk_free(arena, chunk);
    chunk = next;
  }

  arena->head->next = NULL;
  arena->head->used = 0;
  arena->current = arena->head;
}

void* bebop_arena_alloc(bebop_arena_t* arena, size_t size, size_t align)
{
  BEBOP_ASSERT(arena != NULL);
  BEBOP_ASSERT(arena->current != NULL);
  BEBOP_ASSERT(align > 0);
  BEBOP_ASSERT((align & (align - 1)) == 0);

  if (size == 0) {
    return NULL;
  }

  if (align < BEBOP_ARENA_MIN_ALIGN) {
    align = BEBOP_ARENA_MIN_ALIGN;
  }

  if (BEBOP_UNLIKELY(size > SIZE_MAX - align)) {
    return NULL;
  }

  bebop__chunk_t* chunk = arena->current;

  const char* data = bebop__chunk_data(chunk);
  char* ptr = bebop__align_ptr(data + chunk->used, align);
  size_t offset = (size_t)(ptr - data);

  if (BEBOP_UNLIKELY(offset > chunk->capacity || size > chunk->capacity - offset)) {
    const size_t needed = size + align;
    bebop__chunk_t* new_chunk = bebop__chunk_alloc(arena, needed, true);
    if (!new_chunk) {
      return NULL;
    }

    chunk->next = new_chunk;
    arena->current = new_chunk;
    chunk = new_chunk;

    data = bebop__chunk_data(chunk);
    ptr = bebop__align_ptr(data, align);
    offset = (size_t)(ptr - data);
  }

  chunk->used = offset + size;
  memset(ptr, 0, size);

  return ptr;
}

void* bebop_arena_dup(bebop_arena_t* arena, const void* src, size_t size)
{
  if (!src || size == 0) {
    return NULL;
  }

  void* dst = bebop_arena_alloc(arena, size, BEBOP_ARENA_MIN_ALIGN);
  if (dst) {
    memcpy(dst, src, size);
  }
  return dst;
}

char* bebop_arena_strdup(bebop_arena_t* arena, const char* str)
{
  if (!str) {
    return NULL;
  }
  return bebop_arena_strndup(arena, str, strlen(str));
}

char* bebop_arena_strndup(bebop_arena_t* arena, const char* str, size_t len)
{
  if (!str || len == SIZE_MAX) {
    return NULL;
  }

  char* dst = bebop_arena_alloc(arena, len + 1, 1);
  if (dst) {
    memcpy(dst, str, len);
    dst[len] = '\0';
  }
  return dst;
}

void* bebop_arena_malloc(bebop_arena_t* arena, const size_t size)
{
  return bebop_arena_alloc(arena, size, BEBOP_MAX_ALIGN);
}

void* bebop_arena_realloc(
    bebop_arena_t* arena, const void* ptr, const size_t old_size, const size_t new_size
)
{
  if (new_size == 0) {
    return NULL;
  }

  void* new_ptr = bebop_arena_malloc(arena, new_size);
  if (!new_ptr) {
    return NULL;
  }

  if (ptr && old_size > 0) {
    const size_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
  }

  return new_ptr;
}

void bebop_arena_free(const bebop_arena_t* arena, const void* ptr)
{
  (void)arena;
  (void)ptr;
}
