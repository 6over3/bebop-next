#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// clang-format off
#include "bebop_wire.c"
#include "../tests/generated/bebop/json.bb.c"
// clang-format on

#ifndef __AFL_FUZZ_TESTCASE_LEN
#define __AFL_FUZZ_INIT()
#define __AFL_INIT()
#define __AFL_LOOP(n) (fuzz_stdin_read())
#define __AFL_FUZZ_TESTCASE_BUF fuzz_buffer
#define __AFL_FUZZ_TESTCASE_LEN fuzz_length

static unsigned char fuzz_buffer[1024 * 1024];
static int fuzz_length;
static bool fuzz_done;

static bool fuzz_stdin_read(void)
{
  if (fuzz_done) {
    return false;
  }
  fuzz_length = (int)fread(fuzz_buffer, 1, sizeof(fuzz_buffer), stdin);
  fuzz_done = true;
  return fuzz_length > 0;
}
#endif

__AFL_FUZZ_INIT();

static void* fuzz_alloc(void* pointer, size_t old_size, size_t new_size, void* context)
{
  (void)context;
  (void)old_size;
  if (new_size == 0) {
    free(pointer);
    return NULL;
  }
  return realloc(pointer, new_size);
}

static Bebop_Context* fuzz_context(void)
{
  Bebop_ContextOptions options = bebop_context_options();
  options.arena_options.allocator.alloc = fuzz_alloc;
  return bebop_context_new(&options);
}

int main(void)
{
  __AFL_INIT();

  const unsigned char* buffer = __AFL_FUZZ_TESTCASE_BUF;
  while (__AFL_LOOP(10000)) {
    const int length = __AFL_FUZZ_TESTCASE_LEN;
    if (length <= 0) {
      continue;
    }

    Bebop_Context* context = fuzz_context();
    if (!context) {
      continue;
    }

    Bebop_Value value = {0};
    (void)Bebop_Value_decode(context, bebop_view(buffer, (size_t)length), &value);
    bebop_context_free(context);
  }

  return 0;
}
