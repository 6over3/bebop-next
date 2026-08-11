#include "generated/descriptor.bb.h"
#include "generated/plugin.bb.h"

typedef Bebop_CodeGeneratorRequest bebop_plugin_request_data_t;
typedef Bebop_CodeGeneratorResponse bebop_plugin_response_data_t;

struct bebop_plugin_request {
  Bebop_Context* ctx;
  bebop_plugin_request_data_t data;
};

struct bebop_plugin_response {
  Bebop_Context* ctx;
  bebop_plugin_response_data_t data;
};

struct bebop_plugin_response_builder {
  Bebop_Context* ctx;
  bebop_plugin_response_data_t data;
  Bebop_GeneratedFile* files;
  uint32_t file_cap;
  Bebop_Diagnostic* diags;
  uint32_t diag_cap;
};

struct bebop_plugin_request_builder {
  Bebop_Context* ctx;
  bebop_plugin_request_data_t data;
  Bebop_String* files;
  uint32_t file_cap;
  bool opts_initialized;
};

static Bebop_Context* bebop__parser_make_ctx(bebop_host_allocator_t* a)
{
  Bebop_ContextOptions opts = bebop_context_options();
  opts.arena_options.allocator =
      (Bebop_Allocator) {.alloc = (Bebop_AllocFn)a->alloc, .ctx = a->ctx};
  return bebop_context_new(&opts);
}

static Bebop_String bebop__parser_dup_str(Bebop_Context* ctx, const char* s)
{
  if (!s) {
    return (Bebop_String) {0};
  }
  const size_t len = strlen(s);
  char* p = bebop_context_alloc(ctx, len + 1);
  if (!p) {
    return (Bebop_String) {0};
  }
  memcpy(p, s, len + 1);
  return (Bebop_String) {.data = p, .length = (uint32_t)len};
}

void bebop_plugin_request_free(bebop_plugin_request_t* r)
{
  if (r && r->ctx) {
    bebop_context_free(r->ctx);
  }
}

bebop_status_t bebop_plugin_request_decode(
    bebop_context_t* ctx, const uint8_t* buf, size_t len, bebop_plugin_request_t** out
)
{
  if (!ctx || !buf || !out) {
    return BEBOP_FATAL;
  }

  Bebop_Context* wctx = bebop__parser_make_ctx(&ctx->host.allocator);
  if (!wctx) {
    return BEBOP_FATAL;
  }

  bebop_plugin_request_t* r = bebop_context_alloc(wctx, sizeof(*r));
  if (!r) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }
  memset(r, 0, sizeof(*r));
  r->ctx = wctx;

  if (Bebop_CodeGeneratorRequest_decode(wctx, bebop_view(buf, len), &r->data) != BEBOP_RESULT_OK) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }

  *out = r;
  return BEBOP_OK;
}

bebop_status_t bebop_plugin_request_encode(
    bebop_context_t* ctx, const bebop_plugin_request_t* r, const uint8_t** out, size_t* len
)
{
  if (!ctx || !r || !out || !len) {
    return BEBOP_FATAL;
  }

  Bebop_Context* wctx = bebop__parser_make_ctx(&ctx->host.allocator);
  if (!wctx) {
    return BEBOP_FATAL;
  }

  Bebop_Writer* w = bebop_context_writer(wctx, 0);
  if (!w) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }

  if (Bebop_CodeGeneratorRequest_encode(w, &r->data) != BEBOP_RESULT_OK) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }

  const Bebop_View tmp_view = bebop_writer_view(w);
  const uint8_t* tmp = tmp_view.data;
  const size_t tmp_len = tmp_view.length;

  uint8_t* buf = bebop_arena_alloc(BEBOP_ARENA(ctx), tmp_len, 1);
  if (!buf) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }
  memcpy(buf, tmp, tmp_len);
  bebop_context_free(wctx);

  *out = buf;
  *len = tmp_len;
  return BEBOP_OK;
}

uint32_t bebop_plugin_request_file_count(const bebop_plugin_request_t* r)
{
  return r && BEBOP_HAS_VALUE(r->data.files_to_generate)
      ? (uint32_t)r->data.files_to_generate.value.length
      : 0;
}

const char* bebop_plugin_request_file_at(const bebop_plugin_request_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.files_to_generate)
      || i >= r->data.files_to_generate.value.length)
  {
    return NULL;
  }
  return r->data.files_to_generate.value.data[i].data;
}

const char* bebop_plugin_request_parameter(const bebop_plugin_request_t* req)
{
  if (req == NULL) {
    return NULL;
  }
  return BEBOP_HAS_VALUE(req->data.parameter) ? req->data.parameter.value.data : NULL;
}

bebop_version_t bebop_plugin_request_compiler_version(const bebop_plugin_request_t* r)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.compiler_version)) {
    return (bebop_version_t) {0};
  }
  const Bebop_Version* v = &r->data.compiler_version.value;
  return (bebop_version_t) {v->major, v->minor, v->patch, v->suffix.data ? v->suffix.data : ""};
}

uint32_t bebop_plugin_request_schema_count(const bebop_plugin_request_t* req)
{
  return req && BEBOP_HAS_VALUE(req->data.schemas) ? (uint32_t)req->data.schemas.value.length : 0;
}

const bebop_descriptor_schema_t* bebop_plugin_request_schema_at(
    const bebop_plugin_request_t* r, uint32_t i
)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.schemas) || i >= r->data.schemas.value.length) {
    return NULL;
  }
  return (const bebop_descriptor_schema_t*)&r->data.schemas.value.data[i];
}

uint32_t bebop_plugin_request_host_option_count(const bebop_plugin_request_t* r)
{
  return r && BEBOP_HAS_VALUE(r->data.host_options) ? (uint32_t)r->data.host_options.value.length
                                                    : 0;
}

bebop_plugin_option_iterator_t bebop_plugin_request_host_options(const bebop_plugin_request_t* r)
{
  return (bebop_plugin_option_iterator_t) {
      .request = r,
      .cursor = 0,
  };
}

bool bebop_plugin_option_next(
    bebop_plugin_option_iterator_t* iterator, const char** key, const char** value
)
{
  if (!iterator || !iterator->request || !BEBOP_HAS_VALUE(iterator->request->data.host_options)) {
    return false;
  }

  Bebop_MapIter map_iterator = {
      .map = &iterator->request->data.host_options.value,
      .index = iterator->cursor,
  };
  void* map_key = NULL;
  void* map_value = NULL;
  if (!bebop_map_iter_next(&map_iterator, &map_key, &map_value)) {
    iterator->cursor = map_iterator.index;
    return false;
  }
  iterator->cursor = map_iterator.index;
  if (key) {
    *key = map_key ? ((const Bebop_String*)map_key)->data : NULL;
  }
  if (value) {
    *value = map_value ? ((const Bebop_String*)map_value)->data : NULL;
  }
  return true;
}

void bebop_plugin_response_free(bebop_plugin_response_t* r)
{
  if (r && r->ctx) {
    bebop_context_free(r->ctx);
  }
}

bebop_status_t bebop_plugin_response_decode(
    bebop_context_t* ctx, const uint8_t* buf, size_t len, bebop_plugin_response_t** out
)
{
  if (!ctx || !buf || !out) {
    return BEBOP_FATAL;
  }

  Bebop_Context* wctx = bebop__parser_make_ctx(&ctx->host.allocator);
  if (!wctx) {
    return BEBOP_FATAL;
  }

  bebop_plugin_response_t* r = bebop_context_alloc(wctx, sizeof(*r));
  if (!r) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }
  memset(r, 0, sizeof(*r));
  r->ctx = wctx;

  if (Bebop_CodeGeneratorResponse_decode(wctx, bebop_view(buf, len), &r->data) != BEBOP_RESULT_OK) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }

  *out = r;
  return BEBOP_OK;
}

bebop_status_t bebop_plugin_response_encode(
    bebop_context_t* ctx, const bebop_plugin_response_t* r, const uint8_t** out, size_t* len
)
{
  if (!ctx || !r || !out || !len) {
    return BEBOP_FATAL;
  }

  Bebop_Context* wctx = bebop__parser_make_ctx(&ctx->host.allocator);
  if (!wctx) {
    return BEBOP_FATAL;
  }

  Bebop_Writer* w = bebop_context_writer(wctx, 0);
  if (!w) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }

  if (Bebop_CodeGeneratorResponse_encode(w, &r->data) != BEBOP_RESULT_OK) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }

  const Bebop_View tmp_view = bebop_writer_view(w);
  const uint8_t* tmp = tmp_view.data;
  const size_t tmp_len = tmp_view.length;

  uint8_t* buf = bebop_arena_alloc(BEBOP_ARENA(ctx), tmp_len, 1);
  if (!buf) {
    bebop_context_free(wctx);
    return BEBOP_FATAL;
  }
  memcpy(buf, tmp, tmp_len);
  bebop_context_free(wctx);

  *out = buf;
  *len = tmp_len;
  return BEBOP_OK;
}

const char* bebop_plugin_response_error(const bebop_plugin_response_t* resp)
{
  if (resp == NULL) {
    return NULL;
  }
  return BEBOP_HAS_VALUE(resp->data.error) ? resp->data.error.value.data : NULL;
}

uint32_t bebop_plugin_response_file_count(const bebop_plugin_response_t* r)
{
  return r && BEBOP_HAS_VALUE(r->data.files) ? (uint32_t)r->data.files.value.length : 0;
}

const char* bebop_plugin_response_file_name(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.files) || i >= r->data.files.value.length) {
    return NULL;
  }
  const Bebop_GeneratedFile* f = &r->data.files.value.data[i];
  return BEBOP_HAS_VALUE(f->name) ? f->name.value.data : NULL;
}

const char* bebop_plugin_response_file_insertion_point(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.files) || i >= r->data.files.value.length) {
    return NULL;
  }
  const Bebop_GeneratedFile* f = &r->data.files.value.data[i];
  return BEBOP_HAS_VALUE(f->insertion_point) ? f->insertion_point.value.data : NULL;
}

const char* bebop_plugin_response_file_content(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.files) || i >= r->data.files.value.length) {
    return NULL;
  }
  const Bebop_GeneratedFile* f = &r->data.files.value.data[i];
  return BEBOP_HAS_VALUE(f->content) ? f->content.value.data : NULL;
}

uint32_t bebop_plugin_response_diagnostic_count(const bebop_plugin_response_t* r)
{
  return r && BEBOP_HAS_VALUE(r->data.diagnostics) ? (uint32_t)r->data.diagnostics.value.length : 0;
}

bebop_plugin_severity_t bebop_plugin_response_diagnostic_severity(
    const bebop_plugin_response_t* r, uint32_t i
)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.diagnostics) || i >= r->data.diagnostics.value.length) {
    return BEBOP_PLUGIN_SEV_ERROR;
  }
  const Bebop_Diagnostic* d = &r->data.diagnostics.value.data[i];
  return BEBOP_HAS_VALUE(d->severity) ? (bebop_plugin_severity_t)d->severity.value
                                      : BEBOP_PLUGIN_SEV_ERROR;
}

const char* bebop_plugin_response_diagnostic_text(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.diagnostics) || i >= r->data.diagnostics.value.length) {
    return NULL;
  }
  const Bebop_Diagnostic* d = &r->data.diagnostics.value.data[i];
  return BEBOP_HAS_VALUE(d->text) ? d->text.value.data : NULL;
}

const char* bebop_plugin_response_diagnostic_hint(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.diagnostics) || i >= r->data.diagnostics.value.length) {
    return NULL;
  }
  const Bebop_Diagnostic* d = &r->data.diagnostics.value.data[i];
  return BEBOP_HAS_VALUE(d->hint) ? d->hint.value.data : NULL;
}

const char* bebop_plugin_response_diagnostic_file(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.diagnostics) || i >= r->data.diagnostics.value.length) {
    return NULL;
  }
  const Bebop_Diagnostic* d = &r->data.diagnostics.value.data[i];
  return BEBOP_HAS_VALUE(d->file) ? d->file.value.data : NULL;
}

const int32_t* bebop_plugin_response_diagnostic_span(const bebop_plugin_response_t* r, uint32_t i)
{
  if (!r || !BEBOP_HAS_VALUE(r->data.diagnostics) || i >= r->data.diagnostics.value.length) {
    return NULL;
  }
  const Bebop_Diagnostic* d = &r->data.diagnostics.value.data[i];
  return BEBOP_HAS_VALUE(d->span) ? d->span.value : NULL;
}

bebop_plugin_response_builder_t* bebop_plugin_response_builder_create(bebop_host_allocator_t* a)
{
  if (!a) {
    return NULL;
  }
  Bebop_Context* ctx = bebop__parser_make_ctx(a);
  if (!ctx) {
    return NULL;
  }
  bebop_plugin_response_builder_t* b = bebop_context_alloc(ctx, sizeof(*b));
  if (!b) {
    bebop_context_free(ctx);
    return NULL;
  }
  memset(b, 0, sizeof(*b));
  b->ctx = ctx;
  return b;
}

void bebop_plugin_response_builder_set_error(bebop_plugin_response_builder_t* b, const char* err)
{
  if (!b) {
    return;
  }
  b->data.error.has_value = true;
  b->data.error.value = bebop__parser_dup_str(b->ctx, err);
}

void bebop_plugin_response_builder_add_file(
    bebop_plugin_response_builder_t* b, const char* name, const char* content
)
{
  if (!b) {
    return;
  }
  const uint32_t n = BEBOP_HAS_VALUE(b->data.files) ? (uint32_t)b->data.files.value.length : 0;
  if (n >= b->file_cap) {
    const uint32_t cap = b->file_cap ? b->file_cap * 2 : 8;
    Bebop_GeneratedFile* f = bebop_context_alloc(b->ctx, cap * sizeof(*f));
    if (!f) {
      return;
    }
    if (b->files) {
      memcpy(f, b->files, n * sizeof(*f));
    }
    b->files = f;
    b->file_cap = cap;
  }
  Bebop_GeneratedFile* f = &b->files[n];
  memset(f, 0, sizeof(*f));
  if (name) {
    f->name.has_value = true;
    f->name.value = bebop__parser_dup_str(b->ctx, name);
  }
  if (content) {
    f->content.has_value = true;
    f->content.value = bebop__parser_dup_str(b->ctx, content);
  }
  b->data.files.has_value = true;
  b->data.files.value.data = b->files;
  b->data.files.value.length = n + 1;
}

void bebop_plugin_response_builder_add_insertion(
    bebop_plugin_response_builder_t* b, const char* name, const char* ip, const char* content
)
{
  if (!b) {
    return;
  }
  const uint32_t n = BEBOP_HAS_VALUE(b->data.files) ? (uint32_t)b->data.files.value.length : 0;
  if (n >= b->file_cap) {
    const uint32_t cap = b->file_cap ? b->file_cap * 2 : 8;
    Bebop_GeneratedFile* f = bebop_context_alloc(b->ctx, cap * sizeof(*f));
    if (!f) {
      return;
    }
    if (b->files) {
      memcpy(f, b->files, n * sizeof(*f));
    }
    b->files = f;
    b->file_cap = cap;
  }
  Bebop_GeneratedFile* f = &b->files[n];
  memset(f, 0, sizeof(*f));
  if (name) {
    f->name.has_value = true;
    f->name.value = bebop__parser_dup_str(b->ctx, name);
  }
  if (ip) {
    f->insertion_point.has_value = true;
    f->insertion_point.value = bebop__parser_dup_str(b->ctx, ip);
  }
  if (content) {
    f->content.has_value = true;
    f->content.value = bebop__parser_dup_str(b->ctx, content);
  }
  b->data.files.has_value = true;
  b->data.files.value.data = b->files;
  b->data.files.value.length = n + 1;
}

void bebop_plugin_response_builder_add_diagnostic(
    bebop_plugin_response_builder_t* b,
    bebop_plugin_severity_t sev,
    const char* text,
    const char* hint,
    const char* file,
    const int32_t span[4]
)
{
  if (!b) {
    return;
  }
  const uint32_t n =
      BEBOP_HAS_VALUE(b->data.diagnostics) ? (uint32_t)b->data.diagnostics.value.length : 0;
  if (n >= b->diag_cap) {
    const uint32_t cap = b->diag_cap ? b->diag_cap * 2 : 8;
    Bebop_Diagnostic* d = bebop_context_alloc(b->ctx, cap * sizeof(*d));
    if (!d) {
      return;
    }
    if (b->diags) {
      memcpy(d, b->diags, n * sizeof(*d));
    }
    b->diags = d;
    b->diag_cap = cap;
  }
  Bebop_Diagnostic* d = &b->diags[n];
  memset(d, 0, sizeof(*d));
  d->severity.has_value = true;
  d->severity.value = (Bebop_DiagnosticSeverity)sev;
  if (text) {
    d->text.has_value = true;
    d->text.value = bebop__parser_dup_str(b->ctx, text);
  }
  if (hint) {
    d->hint.has_value = true;
    d->hint.value = bebop__parser_dup_str(b->ctx, hint);
  }
  if (file) {
    d->file.has_value = true;
    d->file.value = bebop__parser_dup_str(b->ctx, file);
  }
  if (span) {
    d->span.has_value = true;
    memcpy(d->span.value, span, 16);
  }
  b->data.diagnostics.has_value = true;
  b->data.diagnostics.value.data = b->diags;
  b->data.diagnostics.value.length = n + 1;
}

bebop_plugin_response_t* bebop_plugin_response_builder_finish(bebop_plugin_response_builder_t* b)
{
  if (!b) {
    return NULL;
  }
  bebop_plugin_response_t* r = bebop_context_alloc(b->ctx, sizeof(*r));
  if (!r) {
    return NULL;
  }
  r->ctx = b->ctx;
  r->data = b->data;
  return r;
}

void bebop_plugin_response_builder_free(bebop_plugin_response_builder_t* b)
{
  if (b && b->ctx) {
    bebop_context_free(b->ctx);
  }
}

bebop_plugin_request_builder_t* bebop_plugin_request_builder_create(bebop_host_allocator_t* a)
{
  if (!a) {
    return NULL;
  }
  Bebop_Context* ctx = bebop__parser_make_ctx(a);
  if (!ctx) {
    return NULL;
  }
  bebop_plugin_request_builder_t* b = bebop_context_alloc(ctx, sizeof(*b));
  if (!b) {
    bebop_context_free(ctx);
    return NULL;
  }
  memset(b, 0, sizeof(*b));
  b->ctx = ctx;
  return b;
}

void bebop_plugin_request_builder_set_version(bebop_plugin_request_builder_t* b, bebop_version_t v)
{
  if (!b) {
    return;
  }
  const Bebop_Version ver = {
      .major = v.major,
      .minor = v.minor,
      .patch = v.patch,
      .suffix = bebop__parser_dup_str(b->ctx, v.suffix ? v.suffix : "")
  };
  b->data.compiler_version.has_value = true;
  memcpy(BEBOP_WIRE_MUTPTR(Bebop_Version, &b->data.compiler_version.value), &ver, sizeof(ver));
}

void bebop_plugin_request_builder_set_parameter(bebop_plugin_request_builder_t* b, const char* p)
{
  if (!b) {
    return;
  }
  b->data.parameter.has_value = true;
  b->data.parameter.value = bebop__parser_dup_str(b->ctx, p);
}

void bebop_plugin_request_builder_add_file(bebop_plugin_request_builder_t* b, const char* path)
{
  if (!b) {
    return;
  }
  const uint32_t n = BEBOP_HAS_VALUE(b->data.files_to_generate)
      ? (uint32_t)b->data.files_to_generate.value.length
      : 0;
  if (n >= b->file_cap) {
    const uint32_t cap = b->file_cap ? b->file_cap * 2 : 8;
    Bebop_String* f = bebop_context_alloc(b->ctx, cap * sizeof(*f));
    if (!f) {
      return;
    }
    if (b->files) {
      memcpy(f, b->files, n * sizeof(*f));
    }
    b->files = f;
    b->file_cap = cap;
  }
  b->files[n] = bebop__parser_dup_str(b->ctx, path);
  b->data.files_to_generate.has_value = true;
  b->data.files_to_generate.value.data = b->files;
  b->data.files_to_generate.value.length = n + 1;
}

void bebop_plugin_request_builder_add_option(
    bebop_plugin_request_builder_t* b, const char* k, const char* v
)
{
  if (!b) {
    return;
  }
  if (!b->opts_initialized) {
    b->data.host_options.has_value = true;
    bebop_map_init(&b->data.host_options.value, b->ctx, BEBOP_MAP_KEY_STRING);
    b->opts_initialized = true;
  }
  Bebop_String* key = bebop_context_alloc(b->ctx, sizeof(Bebop_String));
  Bebop_String* val = bebop_context_alloc(b->ctx, sizeof(Bebop_String));
  if (!key || !val) {
    return;
  }
  *key = bebop__parser_dup_str(b->ctx, k);
  *val = bebop__parser_dup_str(b->ctx, v);
  bebop_map_set(&b->data.host_options.value, key, val);
}

void bebop_plugin_request_builder_set_descriptor(
    bebop_plugin_request_builder_t* b, const bebop_descriptor_t* d
)
{
  if (!b || !d) {
    return;
  }
  const uint32_t n = bebop_descriptor_schema_count(d);
  if (n == 0) {
    return;
  }
  b->data.schemas.has_value = true;
  b->data.schemas.value.data =
      BEBOP_WIRE_MUTPTR(Bebop_SchemaDescriptor, bebop_descriptor_schema_at(d, 0));
  b->data.schemas.value.length = n;
}

bebop_plugin_request_t* bebop_plugin_request_builder_finish(bebop_plugin_request_builder_t* b)
{
  if (!b) {
    return NULL;
  }
  bebop_plugin_request_t* r = bebop_context_alloc(b->ctx, sizeof(*r));
  if (!r) {
    return NULL;
  }
  r->ctx = b->ctx;
  memcpy(&r->data, &b->data, sizeof(r->data));
  return r;
}

void bebop_plugin_request_builder_free(bebop_plugin_request_builder_t* b)
{
  if (b && b->ctx) {
    bebop_context_free(b->ctx);
  }
}
