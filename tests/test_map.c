#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bebop_wire.h"
#include "unity.h"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wgnu-statement-expression-from-macro-expansion"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

static void* _alloc(void* ptr, size_t old, size_t new, void* ctx)
{
  (void)ctx;
  (void)old;
  if (new == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new);
}

// ============================================================================
// REAL-WORLD EXAMPLES
// ============================================================================

typedef struct {
  int32_t x, y;
} Vec2;

typedef struct {
  Bebop_String name;
  int32_t health;
  int32_t score;
  Vec2 position;
} Player;

typedef struct {
  Bebop_String key;
  Bebop_String value;
} Header;

static Bebop_Context* ctx;

void setUp(void)
{
  Bebop_ContextOptions opts = bebop_context_options();
  opts.arena_options.allocator.alloc = _alloc;
  ctx = bebop_context_new(&opts);
}

void tearDown(void)
{
  bebop_context_free(ctx);
}

// ============================================================================
// LOW-LEVEL SWISSTABLE MAP TESTS
// ============================================================================

static void test_map_init(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  TEST_ASSERT_EQUAL(0, m.length);
  TEST_ASSERT_EQUAL(0, m.capacity);
  TEST_ASSERT_NULL(m.ctrl);
  TEST_ASSERT_NULL(m.slots);
  TEST_ASSERT_NOT_NULL(m.hash);
  TEST_ASSERT_NOT_NULL(m.eq);
  TEST_ASSERT_EQUAL_PTR(ctx, m.ctx);
}

static void test_map_put_get_single(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  int32_t key = 42;
  int32_t val = 100;
  int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
  int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
  *kp = key;
  *vp = val;

  bool inserted = bebop_map_set(&m, kp, vp);
  TEST_ASSERT_TRUE(inserted);
  TEST_ASSERT_EQUAL(1, m.length);

  int32_t* found = (int32_t*)bebop_map_get(&m, &key);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(100, *found);
}

static void test_map_put_get_multiple(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  for (int32_t i = 0; i < 10; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i * 10;
    bebop_map_set(&m, kp, vp);
  }

  TEST_ASSERT_EQUAL(10, m.length);

  for (int32_t i = 0; i < 10; i++) {
    int32_t* found = (int32_t*)bebop_map_get(&m, &i);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(i * 10, *found);
  }

  int32_t missing = 999;
  TEST_ASSERT_NULL(bebop_map_get(&m, &missing));
}

static void test_map_overwrite(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  int32_t key = 42;
  int32_t* kp1 = bebop_context_alloc(ctx, sizeof(int32_t));
  int32_t* vp1 = bebop_context_alloc(ctx, sizeof(int32_t));
  *kp1 = key;
  *vp1 = 100;
  bebop_map_set(&m, kp1, vp1);
  TEST_ASSERT_EQUAL(1, m.length);

  int32_t* kp2 = bebop_context_alloc(ctx, sizeof(int32_t));
  int32_t* vp2 = bebop_context_alloc(ctx, sizeof(int32_t));
  *kp2 = key;
  *vp2 = 200;
  bool success = bebop_map_set(&m, kp2, vp2);

  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL(1, m.length);  // still only 1 entry

  int32_t* found = (int32_t*)bebop_map_get(&m, &key);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(200, *found);  // value was overwritten
}

static void test_map_delete(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  for (int32_t i = 0; i < 5; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i * 10;
    bebop_map_set(&m, kp, vp);
  }

  TEST_ASSERT_EQUAL(5, m.length);

  int32_t key = 2;
  bool deleted = bebop_map_remove(&m, &key);
  TEST_ASSERT_TRUE(deleted);
  TEST_ASSERT_EQUAL(4, m.length);
  TEST_ASSERT_NULL(bebop_map_get(&m, &key));

  int32_t other = 3;
  TEST_ASSERT_NOT_NULL(bebop_map_get(&m, &other));
}

static void test_map_delete_nonexistent(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
  int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
  *kp = 42;
  *vp = 100;
  bebop_map_set(&m, kp, vp);

  int32_t missing = 999;
  bool deleted = bebop_map_remove(&m, &missing);
  TEST_ASSERT_FALSE(deleted);
  TEST_ASSERT_EQUAL(1, m.length);
}

static void test_map_clear(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  for (int32_t i = 0; i < 10; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i;
    bebop_map_set(&m, kp, vp);
  }

  TEST_ASSERT_EQUAL(10, m.length);

  bebop_map_clear(&m);
  TEST_ASSERT_EQUAL(0, m.length);

  int32_t key = 5;
  TEST_ASSERT_NULL(bebop_map_get(&m, &key));
}

static void test_map_grow(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  size_t initial_cap = 0;
  for (int32_t i = 0; i < 100; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i * 10;
    bebop_map_set(&m, kp, vp);

    if (i == 0) {
      initial_cap = m.capacity;
    }
  }

  TEST_ASSERT_EQUAL(100, m.length);
  TEST_ASSERT_TRUE(m.capacity > initial_cap);

  for (int32_t i = 0; i < 100; i++) {
    int32_t* found = (int32_t*)bebop_map_get(&m, &i);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(i * 10, *found);
  }
}

static void test_map_many_entries(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  const int count = 1000;
  for (int32_t i = 0; i < count; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i * 2;
    bebop_map_set(&m, kp, vp);
  }

  TEST_ASSERT_EQUAL(count, (int)m.length);

  for (int32_t i = 0; i < count; i++) {
    int32_t* found = (int32_t*)bebop_map_get(&m, &i);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(i * 2, *found);
  }
}

static void test_map_iterator(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  int32_t expected_sum = 0;
  for (int32_t i = 1; i <= 10; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i * 10;
    bebop_map_set(&m, kp, vp);
    expected_sum += i * 10;
  }

  Bebop_MapIter it;
  bebop_map_iter_init(&it, &m);

  int32_t sum = 0;
  int count = 0;
  void* k;
  void* v;
  while (bebop_map_iter_next(&it, &k, &v)) {
    sum += *(int32_t*)v;
    count++;
  }

  TEST_ASSERT_EQUAL(10, count);
  TEST_ASSERT_EQUAL(expected_sum, sum);
}

static void test_map_iterator_empty(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  Bebop_MapIter it;
  bebop_map_iter_init(&it, &m);

  void* k;
  void* v;
  TEST_ASSERT_FALSE(bebop_map_iter_next(&it, &k, &v));
}

static void test_map_string_keys(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, Bebop_String);

  const char* keys[] = {"alpha", "beta", "gamma", "delta", "epsilon"};
  for (int i = 0; i < 5; i++) {
    Bebop_String* kp = bebop_context_alloc(ctx, sizeof(Bebop_String));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    kp->data = keys[i];
    kp->length = (uint32_t)strlen(keys[i]);
    *vp = i + 1;
    bebop_map_set(&m, kp, vp);
  }

  TEST_ASSERT_EQUAL(5, m.length);

  Bebop_String lookup = {.data = "gamma", .length = 5};
  int32_t* found = (int32_t*)bebop_map_get(&m, &lookup);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(3, *found);

  Bebop_String missing = {.data = "omega", .length = 5};
  TEST_ASSERT_NULL(bebop_map_get(&m, &missing));
}

static void test_map_uuid_keys(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, Bebop_UUID);

  Bebop_UUID uuids[3] = {
      {{0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0a,
        0x0b,
        0x0c,
        0x0d,
        0x0e,
        0x0f,
        0x10}},
      {{0x11,
        0x12,
        0x13,
        0x14,
        0x15,
        0x16,
        0x17,
        0x18,
        0x19,
        0x1a,
        0x1b,
        0x1c,
        0x1d,
        0x1e,
        0x1f,
        0x20}},
      {{0x21,
        0x22,
        0x23,
        0x24,
        0x25,
        0x26,
        0x27,
        0x28,
        0x29,
        0x2a,
        0x2b,
        0x2c,
        0x2d,
        0x2e,
        0x2f,
        0x30}},
  };

  for (int i = 0; i < 3; i++) {
    Bebop_UUID* kp = bebop_context_alloc(ctx, sizeof(Bebop_UUID));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = uuids[i];
    *vp = (i + 1) * 100;
    bebop_map_set(&m, kp, vp);
  }

  TEST_ASSERT_EQUAL(3, m.length);

  int32_t* found = (int32_t*)bebop_map_get(&m, &uuids[1]);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(200, *found);

  Bebop_UUID missing = {
      {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
  };
  TEST_ASSERT_NULL(bebop_map_get(&m, &missing));
}

static void test_map_delete_and_reinsert(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  int32_t key = 42;
  int32_t* kp1 = bebop_context_alloc(ctx, sizeof(int32_t));
  int32_t* vp1 = bebop_context_alloc(ctx, sizeof(int32_t));
  *kp1 = key;
  *vp1 = 100;
  bebop_map_set(&m, kp1, vp1);

  TEST_ASSERT_EQUAL(100, *(int32_t*)bebop_map_get(&m, &key));

  bebop_map_remove(&m, &key);
  TEST_ASSERT_NULL(bebop_map_get(&m, &key));
  TEST_ASSERT_EQUAL(0, m.length);

  int32_t* kp2 = bebop_context_alloc(ctx, sizeof(int32_t));
  int32_t* vp2 = bebop_context_alloc(ctx, sizeof(int32_t));
  *kp2 = key;
  *vp2 = 200;
  bebop_map_set(&m, kp2, vp2);

  TEST_ASSERT_EQUAL(1, m.length);
  TEST_ASSERT_EQUAL(200, *(int32_t*)bebop_map_get(&m, &key));
}

static void test_map_delete_during_probe(void)
{
  Bebop_Map m;
  BEBOP_MAP_INIT(&m, ctx, int32_t);

  for (int32_t i = 0; i < 20; i++) {
    int32_t* kp = bebop_context_alloc(ctx, sizeof(int32_t));
    int32_t* vp = bebop_context_alloc(ctx, sizeof(int32_t));
    *kp = i;
    *vp = i;
    bebop_map_set(&m, kp, vp);
  }

  for (int32_t i = 0; i < 20; i += 2) {
    bebop_map_remove(&m, &i);
  }

  TEST_ASSERT_EQUAL(10, m.length);

  for (int32_t i = 1; i < 20; i += 2) {
    int32_t* found = (int32_t*)bebop_map_get(&m, &i);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(i, *found);
  }
}

static void test_map_null_safety(void)
{
  Bebop_Map m = {0};

  bebop_map_init(NULL, ctx, BEBOP_MAP_KEY_I32);
  TEST_ASSERT_NULL(bebop_map_get(NULL, NULL));
  TEST_ASSERT_NULL(bebop_map_get(&m, NULL));
  TEST_ASSERT_FALSE(bebop_map_set(NULL, NULL, NULL));
  TEST_ASSERT_FALSE(bebop_map_remove(NULL, NULL));

  Bebop_MapIter it;
  bebop_map_iter_init(&it, NULL);
  void* k;
  void* v;
  TEST_ASSERT_FALSE(bebop_map_iter_next(&it, &k, &v));
}

// ============================================================================
// MAIN
// ============================================================================

int main(void)
{
  UNITY_BEGIN();

  // Low-level SwissTable map tests
  RUN_TEST(test_map_init);
  RUN_TEST(test_map_put_get_single);
  RUN_TEST(test_map_put_get_multiple);
  RUN_TEST(test_map_overwrite);
  RUN_TEST(test_map_delete);
  RUN_TEST(test_map_delete_nonexistent);
  RUN_TEST(test_map_clear);
  RUN_TEST(test_map_grow);
  RUN_TEST(test_map_many_entries);
  RUN_TEST(test_map_iterator);
  RUN_TEST(test_map_iterator_empty);
  RUN_TEST(test_map_string_keys);
  RUN_TEST(test_map_uuid_keys);
  RUN_TEST(test_map_delete_and_reinsert);
  RUN_TEST(test_map_delete_during_probe);
  RUN_TEST(test_map_null_safety);

  return UNITY_END();
}
