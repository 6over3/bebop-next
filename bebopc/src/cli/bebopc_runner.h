#ifndef BEBOPC_RUNNER_H
#define BEBOPC_RUNNER_H

#include "../bebopc.h"
#include "bebopc_config.h"

typedef struct {
  bebopc_ctx_t* ctx;
  bebop_context_t* beb_ctx;
  bebop_descriptor_t* desc;
  const char** input_files;
  uint32_t input_file_count;
} bebopc_runner_t;

bebopc_error_code_t bebopc_runner_init(bebopc_runner_t* r,
                                       bebopc_ctx_t* ctx,
                                       bebop_context_t* beb_ctx,
                                       bebop_descriptor_t* desc,
                                       const char** files,
                                       uint32_t file_count);

void bebopc_runner_cleanup(bebopc_runner_t* r);

bebopc_error_code_t bebopc_runner_generate(bebopc_runner_t* r);

char* bebopc_find_plugin(const char* name);

typedef struct bebopc_process bebopc_process_t;

bebopc_process_t* bebopc_process_spawn(const char* exe);

bool bebopc_process_write(bebopc_process_t* p, const void* data, size_t len);

void bebopc_process_close_stdin(bebopc_process_t* p);

uint8_t* bebopc_process_read_all(bebopc_process_t* p, size_t* out_len);

// Write the request and drain stdout concurrently; timeout_ms <= 0 disables
// the deadline. Returns false on timeout or I/O failure (response freed).
bool bebopc_process_exchange(
    bebopc_process_t* p,
    const void* req,
    size_t req_len,
    uint8_t** out_resp,
    size_t* out_len,
    int timeout_ms
);

void bebopc_process_kill(bebopc_process_t* p);

int bebopc_process_wait(bebopc_process_t* p);

// Wait up to timeout_ms (<= 0 waits forever), killing the process on expiry.
int bebopc_process_wait_timeout(bebopc_process_t* p, int timeout_ms);

void bebopc_process_free(bebopc_process_t* p);

#endif
