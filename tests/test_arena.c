#include "bebop.c"
#include "test_common.h"
#include "unity.h"

static bebop_arena_t arena;
static bebop_host_allocator_t test_alloc;

void setUp(void)
{
  memset(&arena, 0, sizeof(arena));
  test_alloc = test_allocator();
}

void tearDown(void)
{
  bebop_arena_destroy(&arena);
}

void test_arena_create_destroy(void);
void test_arena_create_with_size(void);
void test_arena_reset(void);
void test_arena_alloc_basic(void);
void test_arena_alloc_alignment(void);
void test_arena_alloc_sequential(void);
void test_arena_alloc_zero_size(void);
void test_arena_alloc_large(void);
void test_arena_alloc_grows(void);
void test_arena_new_single(void);
void test_arena_new_array(void);
void test_arena_strdup(void);
void test_arena_strndup(void);
void test_arena_strndup_embedded_null(void);
void test_arena_dup(void);
void test_arena_strdup_null(void);
void test_arena_custom_allocator(void);
void test_arena_custom_allocator_reset(void);
void test_arena_realloc_extends_last_alloc(void);
void test_arena_realloc_copies_when_not_last(void);

void test_arena_create_destroy(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));
  TEST_ASSERT_NOT_NULL(arena.head);
  TEST_ASSERT_NOT_NULL(arena.current);
  TEST_ASSERT_EQUAL_PTR(arena.head, arena.current);
}

void test_arena_create_with_size(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 1024));
  TEST_ASSERT_NOT_NULL(arena.head);
}

void test_arena_reset(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  void* p1 = bebop_arena_alloc(&arena, 100, 8);
  TEST_ASSERT_NOT_NULL(p1);

  bebop_arena_reset(&arena);

  void* p2 = bebop_arena_alloc(&arena, 100, 8);
  TEST_ASSERT_NOT_NULL(p2);
  TEST_ASSERT_EQUAL_PTR(p1, p2);
}

void test_arena_alloc_basic(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  void* p = bebop_arena_alloc(&arena, 64, 8);
  TEST_ASSERT_NOT_NULL(p);

  uint8_t* bytes = (uint8_t*)p;
  for (int i = 0; i < 64; i++) {
    TEST_ASSERT_EQUAL_UINT8(0, bytes[i]);
  }
}

void test_arena_alloc_alignment(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  void* p8 = bebop_arena_alloc(&arena, 1, 8);
  TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)p8 % 8);

  void* p16 = bebop_arena_alloc(&arena, 1, 16);
  TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)p16 % 16);

  void* p32 = bebop_arena_alloc(&arena, 1, 32);
  TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)p32 % 32);

  void* p64 = bebop_arena_alloc(&arena, 1, 64);
  TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)p64 % 64);
}

void test_arena_alloc_sequential(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  void* p1 = bebop_arena_alloc(&arena, 100, 8);
  void* p2 = bebop_arena_alloc(&arena, 100, 8);
  void* p3 = bebop_arena_alloc(&arena, 100, 8);

  TEST_ASSERT_NOT_NULL(p1);
  TEST_ASSERT_NOT_NULL(p2);
  TEST_ASSERT_NOT_NULL(p3);

  TEST_ASSERT_TRUE((char*)p2 >= (char*)p1 + 100);
  TEST_ASSERT_TRUE((char*)p3 >= (char*)p2 + 100);
}

void test_arena_alloc_zero_size(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  void* p = bebop_arena_alloc(&arena, 0, 8);
  TEST_ASSERT_NULL(p);
}

void test_arena_alloc_large(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  size_t large_size = BEBOP_ARENA_CHUNK_SIZE * 2;
  void* p = bebop_arena_alloc(&arena, large_size, 8);
  TEST_ASSERT_NOT_NULL(p);

  TEST_ASSERT_NOT_EQUAL(arena.head, arena.current);
}

void test_arena_alloc_grows(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 1024));

  for (int i = 0; i < 20; i++) {
    void* p = bebop_arena_alloc(&arena, 100, 8);
    TEST_ASSERT_NOT_NULL(p);
  }

  TEST_ASSERT_NOT_EQUAL(arena.head, arena.current);
}

void test_arena_new_single(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  typedef struct {
    int x;
    int64_t y;
    char z;
  } test_struct_t;

  test_struct_t* s = bebop_arena_new1(&arena, test_struct_t);
  TEST_ASSERT_NOT_NULL(s);

  TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)s % _Alignof(test_struct_t));

  TEST_ASSERT_EQUAL_INT(0, s->x);
  TEST_ASSERT_EQUAL_INT64(0, s->y);
  TEST_ASSERT_EQUAL_CHAR('\0', s->z);
}

void test_arena_new_array(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  int* arr = bebop_arena_new(&arena, int, 10);
  TEST_ASSERT_NOT_NULL(arr);

  TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)arr % _Alignof(int));

  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_INT(0, arr[i]);
  }

  for (int i = 0; i < 10; i++) {
    arr[i] = i * 100;
  }
  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_INT(i * 100, arr[i]);
  }
}

void test_arena_strdup(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  const char* original = "hello world";
  char* copy = bebop_arena_strdup(&arena, original);

  TEST_ASSERT_NOT_NULL(copy);
  TEST_ASSERT_NOT_EQUAL(original, copy);
  TEST_ASSERT_EQUAL_STRING(original, copy);
}

void test_arena_strndup(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  const char* original = "hello world";
  char* copy = bebop_arena_strndup(&arena, original, 5);

  TEST_ASSERT_NOT_NULL(copy);
  TEST_ASSERT_EQUAL_STRING("hello", copy);
  TEST_ASSERT_EQUAL_CHAR('\0', copy[5]);
}

void test_arena_strndup_embedded_null(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  const char data[] = {'a', 'b', '\0', 'c', 'd'};
  char* copy = bebop_arena_strndup(&arena, data, 5);

  TEST_ASSERT_NOT_NULL(copy);
  TEST_ASSERT_EQUAL_CHAR('a', copy[0]);
  TEST_ASSERT_EQUAL_CHAR('b', copy[1]);
  TEST_ASSERT_EQUAL_CHAR('\0', copy[2]);
  TEST_ASSERT_EQUAL_CHAR('c', copy[3]);
  TEST_ASSERT_EQUAL_CHAR('d', copy[4]);
  TEST_ASSERT_EQUAL_CHAR('\0', copy[5]);
}

void test_arena_dup(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  uint8_t original[] = {0x01, 0x02, 0x03, 0x04, 0x00, 0xFF};
  uint8_t* copy = (uint8_t*)bebop_arena_dup(&arena, original, sizeof(original));

  TEST_ASSERT_NOT_NULL(copy);
  TEST_ASSERT_NOT_EQUAL(original, copy);
  TEST_ASSERT_EQUAL_MEMORY(original, copy, sizeof(original));
}

void test_arena_strdup_null(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  char* copy = bebop_arena_strdup(&arena, NULL);
  TEST_ASSERT_NULL(copy);
}

static size_t custom_alloc_count = 0;
static size_t custom_free_count = 0;
static size_t custom_total_allocated = 0;

static void* custom_alloc(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
  (void)ctx;
  if (new_size == 0) {
    custom_free_count++;
    custom_total_allocated -= old_size;
    free(ptr);
    return NULL;
  }
  custom_alloc_count++;
  custom_total_allocated += new_size;
  return realloc(ptr, new_size);
}

void test_arena_custom_allocator(void)
{
  custom_alloc_count = 0;
  custom_free_count = 0;
  custom_total_allocated = 0;

  bebop_host_allocator_t alloc = {.alloc = custom_alloc, .ctx = NULL};

  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &alloc, 0));
  TEST_ASSERT_EQUAL_UINT(1, custom_alloc_count);

  for (int i = 0; i < 1000; i++) {
    bebop_arena_alloc(&arena, 100, 8);
  }
  TEST_ASSERT_TRUE(custom_alloc_count > 1);

  size_t allocs_before_destroy = custom_alloc_count;
  bebop_arena_destroy(&arena);

  TEST_ASSERT_EQUAL_UINT(allocs_before_destroy, custom_free_count);
  TEST_ASSERT_EQUAL_UINT(0, custom_total_allocated);

  memset(&arena, 0, sizeof(arena));
}

void test_arena_custom_allocator_reset(void)
{
  custom_alloc_count = 0;
  custom_free_count = 0;

  bebop_host_allocator_t alloc = {.alloc = custom_alloc, .ctx = NULL};

  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &alloc, 1024));

  for (int i = 0; i < 100; i++) {
    bebop_arena_alloc(&arena, 100, 8);
  }
  size_t chunks_allocated = custom_alloc_count;
  TEST_ASSERT_TRUE(chunks_allocated > 1);

  bebop_arena_reset(&arena);
  TEST_ASSERT_EQUAL_UINT(chunks_allocated - 1, custom_free_count);
}

void test_arena_realloc_extends_last_alloc(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  char* p = bebop_arena_alloc(&arena, 64, 8);
  TEST_ASSERT_NOT_NULL(p);
  memset(p, 0xAB, 64);

  char* grown = bebop_arena_realloc(&arena, p, 64, 256);
  TEST_ASSERT_EQUAL_PTR(p, grown);
  for (int i = 0; i < 64; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xAB, (uint8_t)grown[i]);
  }
  for (int i = 64; i < 256; i++) {
    TEST_ASSERT_EQUAL_HEX8(0, (uint8_t)grown[i]);
  }
}

void test_arena_realloc_copies_when_not_last(void)
{
  TEST_ASSERT_TRUE(bebop_arena_init(&arena, &test_alloc, 0));

  char* p = bebop_arena_alloc(&arena, 64, 8);
  TEST_ASSERT_NOT_NULL(p);
  memset(p, 0xCD, 64);

  TEST_ASSERT_NOT_NULL(bebop_arena_alloc(&arena, 16, 8));

  char* grown = bebop_arena_realloc(&arena, p, 64, 256);
  TEST_ASSERT_NOT_NULL(grown);
  TEST_ASSERT_TRUE(grown != p);
  for (int i = 0; i < 64; i++) {
    TEST_ASSERT_EQUAL_HEX8(0xCD, (uint8_t)grown[i]);
  }
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_arena_create_destroy);
  RUN_TEST(test_arena_create_with_size);
  RUN_TEST(test_arena_reset);

  RUN_TEST(test_arena_alloc_basic);
  RUN_TEST(test_arena_alloc_alignment);
  RUN_TEST(test_arena_alloc_sequential);
  RUN_TEST(test_arena_alloc_zero_size);
  RUN_TEST(test_arena_alloc_large);
  RUN_TEST(test_arena_alloc_grows);

  RUN_TEST(test_arena_new_single);
  RUN_TEST(test_arena_new_array);

  RUN_TEST(test_arena_strdup);
  RUN_TEST(test_arena_strndup);
  RUN_TEST(test_arena_strndup_embedded_null);
  RUN_TEST(test_arena_dup);
  RUN_TEST(test_arena_strdup_null);

  RUN_TEST(test_arena_custom_allocator);
  RUN_TEST(test_arena_custom_allocator_reset);

  RUN_TEST(test_arena_realloc_extends_last_alloc);
  RUN_TEST(test_arena_realloc_copies_when_not_last);

  return UNITY_END();
}
