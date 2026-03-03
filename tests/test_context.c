#include "bebop.c"
#include "test_common.h"
#include "unity.h"

static bebop_context_t* ctx;

void setUp(void)
{
  ctx = NULL;
}

void tearDown(void)
{
  if (ctx) {
    bebop_context_destroy(ctx);
    ctx = NULL;
  }
}

void test_context_create_destroy(void);
void test_context_error_initial_state(void);
void test_context_error_set_and_get(void);
void test_context_error_clear(void);
void test_context_intern_works(void);
void test_context_custom_allocator(void);

void test_context_create_destroy(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);

  bebop_context_destroy(ctx);
  ctx = NULL;
}

void test_context_error_initial_state(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);

  TEST_ASSERT_EQUAL(BEBOP_ERR_NONE, bebop_context_last_error(ctx));
  TEST_ASSERT_NULL(bebop_context_error_message(ctx));
}

void test_context_error_set_and_get(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);

  bebop__context_set_error(ctx, BEBOP_ERR_FILE_NOT_FOUND, "test.bop not found");

  TEST_ASSERT_EQUAL(BEBOP_ERR_FILE_NOT_FOUND, bebop_context_last_error(ctx));
  TEST_ASSERT_EQUAL_STRING("test.bop not found", bebop_context_error_message(ctx));
}

void test_context_error_clear(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);

  bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "allocation failed");
  TEST_ASSERT_EQUAL(BEBOP_ERR_OUT_OF_MEMORY, bebop_context_last_error(ctx));

  bebop_context_clear_error(ctx);
  TEST_ASSERT_EQUAL(BEBOP_ERR_NONE, bebop_context_last_error(ctx));
  TEST_ASSERT_NULL(bebop_context_error_message(ctx));
}

void test_context_intern_works(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);

  bebop_str_t s1 = bebop_intern(&ctx->intern, "hello");
  bebop_str_t s2 = bebop_intern(&ctx->intern, "world");
  bebop_str_t s3 = bebop_intern(&ctx->intern, "hello");

  TEST_ASSERT_FALSE(bebop_str_is_null(s1));
  TEST_ASSERT_FALSE(bebop_str_is_null(s2));
  TEST_ASSERT_TRUE(bebop_str_eq(s1, s3));
  TEST_ASSERT_FALSE(bebop_str_eq(s1, s2));

  TEST_ASSERT_EQUAL_STRING("hello", bebop_str_get(&ctx->intern, s1));
  TEST_ASSERT_EQUAL_STRING("world", bebop_str_get(&ctx->intern, s2));
}

#define MAX_TRACKED_ALLOCS 64

typedef struct {
  void* ptr;
  size_t size;
} tracked_alloc_t;

typedef struct {
  tracked_alloc_t allocs[MAX_TRACKED_ALLOCS];
  size_t count;
  size_t total_bytes;
} alloc_tracker_t;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)  // unreachable code
#endif
static void* tracking_alloc(void* ptr, size_t old_size, size_t new_size, void* user_ctx)
{
  alloc_tracker_t* tracker = (alloc_tracker_t*)user_ctx;

  // Free case
  if (new_size == 0) {
    for (size_t i = 0; i < tracker->count; i++) {
      if (tracker->allocs[i].ptr == ptr) {
        TEST_ASSERT_EQUAL_UINT(tracker->allocs[i].size, old_size);
        tracker->total_bytes -= old_size;
        tracker->allocs[i] = tracker->allocs[tracker->count - 1];
        tracker->count--;
        free(ptr);
        return NULL;
      }
    }
    TEST_FAIL_MESSAGE("free called with untracked pointer");
    BEBOP_UNREACHABLE();
  }

  // Malloc case
  if (ptr == NULL) {
    void* new_ptr = malloc(new_size);
    if (new_ptr && tracker->count < MAX_TRACKED_ALLOCS) {
      tracker->allocs[tracker->count].ptr = new_ptr;
      tracker->allocs[tracker->count].size = new_size;
      tracker->count++;
      tracker->total_bytes += new_size;
    }
    return new_ptr;
  }

  // Realloc case
  for (size_t i = 0; i < tracker->count; i++) {
    if (tracker->allocs[i].ptr == ptr) {
      TEST_ASSERT_EQUAL_UINT(tracker->allocs[i].size, old_size);
      void* new_ptr = realloc(ptr, new_size);
      if (new_ptr) {
        tracker->allocs[i].ptr = new_ptr;
        tracker->total_bytes -= old_size;
        tracker->total_bytes += new_size;
        tracker->allocs[i].size = new_size;
      }
      return new_ptr;
    }
  }
  TEST_FAIL_MESSAGE("realloc called with untracked pointer");
  BEBOP_UNREACHABLE();
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

void test_context_custom_allocator(void)
{
  alloc_tracker_t tracker = {0};

  bebop_host_t host = test_host(NULL, 0);
  host.allocator.alloc = tracking_alloc;
  host.allocator.ctx = &tracker;

  ctx = bebop_context_create(&host);
  TEST_ASSERT_NOT_NULL(ctx);

  TEST_ASSERT_EQUAL_UINT(2, tracker.count);

  bebop_context_destroy(ctx);
  ctx = NULL;

  TEST_ASSERT_EQUAL_UINT(0, tracker.count);
  TEST_ASSERT_EQUAL_UINT(0, tracker.total_bytes);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_context_create_destroy);

  RUN_TEST(test_context_error_initial_state);
  RUN_TEST(test_context_error_set_and_get);
  RUN_TEST(test_context_error_clear);

  RUN_TEST(test_context_intern_works);

  RUN_TEST(test_context_custom_allocator);

  return UNITY_END();
}
