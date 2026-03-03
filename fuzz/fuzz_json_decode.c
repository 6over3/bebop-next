#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// clang-format off
#include "bebop_wire.c"
#include "../tests/generated/json.bb.c"
// clang-format on

#ifndef __AFL_FUZZ_TESTCASE_LEN
#define __AFL_FUZZ_INIT()
#define __AFL_INIT()
#define __AFL_LOOP(n) (fuzz_stdin_read())
#define __AFL_FUZZ_TESTCASE_BUF fuzz_buf
#define __AFL_FUZZ_TESTCASE_LEN fuzz_len

static unsigned char fuzz_buf[1024 * 1024];
static int fuzz_len;
static int fuzz_done;

static int fuzz_stdin_read(void)
{
  if (fuzz_done) {
    return 0;
  }
  fuzz_len = (int)fread(fuzz_buf, 1, sizeof(fuzz_buf), stdin);
  fuzz_done = 1;
  return fuzz_len > 0;
}
#endif

__AFL_FUZZ_INIT();

static void* fuzz_alloc(void* ptr, size_t old, size_t new, void* ctx)
{
  (void)ctx;
  (void)old;
  if (new == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new);
}

static bebop_wire_ctx_t* fuzz_ctx_new(void)
{
  bebop_wire_ctx_opts_t opts = bebop_wire_ctx_default_opts();
  opts.arena_options.allocator.alloc = fuzz_alloc;
  return bebop_wire_ctx_new_with_opts(&opts);
}

int main(void)
{
  __AFL_INIT();

  unsigned char* buf = __AFL_FUZZ_TESTCASE_BUF;

  while (__AFL_LOOP(10000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;
    if (len <= 0) {
      continue;
    }

    bebop_wire_ctx_t* ctx = fuzz_ctx_new();
    if (!ctx) {
      continue;
    }

    bebop_wire_reader_t* rd = NULL;
    if (bebop_wire_ctx_reader(ctx, (const uint8_t*)buf, (size_t)len, &rd) == BEBOP_WIRE_OK) {
      json_JsonValue value = {0};
      json_JsonValue_decode(ctx, rd, &value);
    }

    bebop_wire_ctx_free(ctx);
  }

  return 0;
}
