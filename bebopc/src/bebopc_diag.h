#ifndef BEBOPC_DIAG_H
#define BEBOPC_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  DIAG_ERROR,
  DIAG_WARNING,
  DIAG_INFO,
} diag_severity_t;

typedef enum {
  DIAG_FMT_TERMINAL,
  DIAG_FMT_JSON,
  DIAG_FMT_MSBUILD,
  DIAG_FMT_XCODE,
} diag_format_t;

typedef struct {
  const char* path;
  const char* text;
  uint32_t text_len;
  uint32_t* line_offsets;
  uint32_t line_count;
} diag_source_t;

typedef struct {
  uint32_t start;
  uint32_t end;
  const char* message;
  int priority;
} diag_label_t;

typedef struct {
  diag_severity_t severity;
  const char* code;
  const char* message;
  const char* note;
  const diag_source_t* source;
  diag_label_t* labels;
  uint32_t label_count;
} diag_t;

typedef struct {
  const char* vert;
  const char* horiz;
  const char* tl;
  const char* bl;
  const char* dot;
  const char* anchor;
  const char* corner;
  const char* tr;
  const char* br;
  const char* arrow;
} diag_box_t;

typedef struct {
  diag_format_t format;
  bool color;
  bool unicode;
  diag_box_t box;
} diag_ctx_t;

typedef struct {
  char* data;
  size_t len;
  size_t cap;
} diag_buf_t;

void diag_init(diag_ctx_t* ctx);

void diag_buf_init(diag_buf_t* buf);
void diag_buf_cleanup(diag_buf_t* buf);

void diag_source_init(diag_source_t* src,
                      const char* path,
                      const char* text,
                      uint32_t text_len);
void diag_source_cleanup(diag_source_t* src);

void diag_source_loc(const diag_source_t* src,
                     uint32_t offset,
                     uint32_t* line,
                     uint32_t* col);

void diag_render(diag_ctx_t* ctx, diag_buf_t* buf, const diag_t* diag);

void diag_set_format(diag_ctx_t* ctx, diag_format_t fmt);
void diag_set_color(diag_ctx_t* ctx, bool enabled);
void diag_set_unicode(diag_ctx_t* ctx, bool enabled);

#endif
