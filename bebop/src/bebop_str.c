#define BEBOP_INTERN_DEFAULT_CAPACITY 256

#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

uint64_t bebop_hash_fnv1a(const char* str, const size_t len)
{
  uint64_t hash = FNV_OFFSET_BASIS;
  for (size_t i = 0; i < len; i++) {
    hash ^= (uint8_t)str[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

bool bebop_intern_init(bebop_intern_t* intern, bebop_arena_t* arena, uint32_t capacity)
{
  BEBOP_ASSERT(intern != NULL);
  BEBOP_ASSERT(arena != NULL);

  memset(intern, 0, sizeof(*intern));
  intern->arena = arena;

  if (capacity == 0) {
    capacity = BEBOP_INTERN_DEFAULT_CAPACITY;
  }

  intern->strings = bebop_arena_new(arena, char*, capacity);
  intern->hashes = bebop_arena_new(arena, uint64_t, capacity);
  intern->lengths = bebop_arena_new(arena, uint32_t, capacity);
  intern->next = bebop_arena_new(arena, uint32_t, capacity);

  if (!intern->strings || !intern->hashes || !intern->lengths || !intern->next) {
    return false;
  }

  intern->lookup = bebop_internmap_new(capacity, arena);

  intern->capacity = capacity;
  intern->count = 1;

  intern->strings[0] = NULL;
  intern->hashes[0] = 0;
  intern->lengths[0] = 0;
  intern->next[0] = 0;

  return true;
}

static bool bebop__intern_grow(bebop_intern_t* intern)
{
  const uint32_t new_capacity = intern->capacity * 2;

  char** new_strings = bebop_arena_new(intern->arena, char*, new_capacity);
  uint64_t* new_hashes = bebop_arena_new(intern->arena, uint64_t, new_capacity);
  uint32_t* new_lengths = bebop_arena_new(intern->arena, uint32_t, new_capacity);
  uint32_t* new_next = bebop_arena_new(intern->arena, uint32_t, new_capacity);

  if (!new_strings || !new_hashes || !new_lengths || !new_next) {
    return false;
  }

  memcpy(new_strings, intern->strings, intern->count * sizeof(char*));
  memcpy(new_hashes, intern->hashes, intern->count * sizeof(uint64_t));
  memcpy(new_lengths, intern->lengths, intern->count * sizeof(uint32_t));
  memcpy(new_next, intern->next, intern->count * sizeof(uint32_t));

  intern->strings = new_strings;
  intern->hashes = new_hashes;
  intern->lengths = new_lengths;
  intern->next = new_next;
  intern->capacity = new_capacity;

  return true;
}

bebop_str_t bebop_intern_n(bebop_intern_t* intern, const char* str, const size_t len)
{
  BEBOP_ASSERT(intern != NULL);

  if (!str || len > UINT32_MAX) {
    return BEBOP_STR_NULL;
  }

  const uint64_t hash = bebop_hash_fnv1a(str, len);

  // The lookup map is keyed on the hash alone, so distinct strings can
  // collide; verify content and chain collisions through next[].
  uint32_t chain_tail = 0;
  const bebop_internmap_Iter it = bebop_internmap_find(&intern->lookup, &hash);
  const bebop_internmap_Entry* entry = bebop_internmap_Iter_get(&it);
  if (entry) {
    uint32_t idx = entry->val;
    for (;;) {
      if (intern->lengths[idx] == len && memcmp(intern->strings[idx], str, len) == 0) {
        return (bebop_str_t) {idx};
      }
      if (intern->next[idx] == 0) {
        chain_tail = idx;
        break;
      }
      idx = intern->next[idx];
    }
  }

  if (intern->count >= intern->capacity) {
    if (!bebop__intern_grow(intern)) {
      return BEBOP_STR_NULL;
    }
  }

  char* copy = bebop_arena_strndup(intern->arena, str, len);
  if (!copy) {
    return BEBOP_STR_NULL;
  }

  const uint32_t idx = intern->count++;
  intern->strings[idx] = copy;
  intern->hashes[idx] = hash;
  intern->lengths[idx] = (uint32_t)len;
  intern->next[idx] = 0;

  if (chain_tail != 0) {
    intern->next[chain_tail] = idx;
  } else {
    const bebop_internmap_Entry new_entry = {hash, idx};
    bebop_internmap_insert(&intern->lookup, &new_entry);
  }

  return (bebop_str_t) {idx};
}

bebop_str_t bebop_intern(bebop_intern_t* intern, const char* str)
{
  if (!str) {
    return BEBOP_STR_NULL;
  }
  return bebop_intern_n(intern, str, strlen(str));
}

const char* bebop_str_get(const bebop_intern_t* intern, const bebop_str_t handle)
{
  BEBOP_ASSERT(intern != NULL);

  if (handle.idx == 0 || handle.idx >= intern->count) {
    return NULL;
  }
  return intern->strings[handle.idx];
}

size_t bebop_str_len(const bebop_intern_t* intern, const bebop_str_t handle)
{
  BEBOP_ASSERT(intern != NULL);

  if (handle.idx == 0 || handle.idx >= intern->count) {
    return 0;
  }
  return intern->lengths[handle.idx];
}
