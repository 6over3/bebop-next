#ifndef BEBOP_FUZZ_COMMON_H
#define BEBOP_FUZZ_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bebop.h"

static void* fuzz_alloc_fn(size_t size, void* ctx)
{
  (void)ctx;
  return malloc(size);
}

static void* fuzz_realloc_fn(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
  (void)old_size;
  (void)ctx;
  return realloc(ptr, new_size);
}

static void fuzz_free_fn(void* ptr, size_t size, void* ctx)
{
  (void)size;
  (void)ctx;
  free(ptr);
}

static bebop_file_result_t fuzz_file_reader_fn(const char* path, void* ctx)
{
  (void)ctx;
  FILE* f = fopen(path, "rb");
  if (!f) {
    return (bebop_file_result_t) {.content = NULL, .content_len = 0, .error = "File not found"};
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* content = (char*)malloc((size_t)len + 1);
  if (!content) {
    fclose(f);
    return (bebop_file_result_t) {.content = NULL, .content_len = 0, .error = "Out of memory"};
  }
  size_t read = fread(content, 1, (size_t)len, f);
  fclose(f);
  content[read] = '\0';
  return (bebop_file_result_t) {.content = content, .content_len = read, .error = NULL};
}

static bool fuzz_file_exists_fn(const char* path, void* ctx)
{
  (void)ctx;
  FILE* f = fopen(path, "rb");
  if (f) {
    fclose(f);
    return true;
  }
  return false;
}

static inline bebop_host_t fuzz_host(void)
{
  bebop_host_t host = {0};
  host.allocator.alloc = fuzz_alloc_fn;
  host.allocator.realloc = fuzz_realloc_fn;
  host.allocator.free = fuzz_free_fn;
  host.allocator.ctx = NULL;
  host.file_reader.read = fuzz_file_reader_fn;
  host.file_reader.exists = fuzz_file_exists_fn;
  host.file_reader.ctx = NULL;
  return host;
}

static inline bebop_context_t* fuzz_context_create(void)
{
  bebop_host_t host = fuzz_host();
  return bebop_context_create(&host);
}

#endif
