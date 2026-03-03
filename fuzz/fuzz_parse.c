#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bebop.c"
#include "fuzz_common.h"

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

int main(void)
{
  __AFL_INIT();

  unsigned char* buf = __AFL_FUZZ_TESTCASE_BUF;

  while (__AFL_LOOP(10000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;
    if (len <= 0) {
      continue;
    }

    bebop_context_t* ctx = fuzz_context_create();
    if (!ctx) {
      continue;
    }

    bebop_parse_result_t* result = NULL;
    bebop_parse_source(ctx, &(bebop_source_t) {(const char*)buf, (size_t)len, "fuzz.bop"}, &result);

    bebop_context_destroy(ctx);
  }

  return 0;
}
