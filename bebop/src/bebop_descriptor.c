#include "generated/descriptor.bb.h"

static Bebop_WireCtx* _desc_make_ctx(bebop_host_allocator_t* a)
{
  Bebop_WireCtxOpts opts = Bebop_WireCtx_DefaultOpts();
  opts.arena_options.allocator =
      (Bebop_WireAllocator) {.alloc = (Bebop_WireAllocFn)a->alloc, .ctx = a->ctx};
  return Bebop_WireCtx_New(&opts);
}

static Bebop_Str _desc_dup_str(Bebop_WireCtx* ctx, const char* s)
{
  if (!s) {
    return (Bebop_Str) {0};
  }
  const size_t len = strlen(s);
  char* p = Bebop_WireCtx_Alloc(ctx, len + 1);
  if (!p) {
    return (Bebop_Str) {0};
  }
  memcpy(p, s, len + 1);
  return (Bebop_Str) {.data = p, .length = (uint32_t)len};
}

#define DESC_SET_OPT_STR(field, str) \
  do { \
    if (str) { \
      (field).has_value = true; \
      (field).value = _desc_dup_str(wctx, str); \
    } \
  } while (0)

#define DESC_SET_OPT(field, val) \
  do { \
    (field).has_value = true; \
    (field).value = (val); \
  } while (0)

struct bebop_descriptor {
  Bebop_WireCtx* ctx;
  Bebop_DescriptorSet data;
};

static Bebop_TypeDescriptor* _desc_build_type(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_type_t* src
);
static void _desc_build_def(
    Bebop_WireCtx* wctx,
    bebop_context_t* bctx,
    const bebop_def_t* src,
    Bebop_DefinitionDescriptor* dst
);

static Bebop_TypeDescriptor* _desc_build_type(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_type_t* src
)
{
  if (!src) {
    return NULL;
  }

  Bebop_TypeDescriptor* t = Bebop_WireCtx_Alloc(wctx, sizeof(*t));
  if (!t) {
    return NULL;
  }
  memset(t, 0, sizeof(*t));

  DESC_SET_OPT(t->kind, (Bebop_TypeKind)src->kind);

  switch (src->kind) {
    case BEBOP_TYPE_ARRAY:
      t->array_element.has_value = true;
      t->array_element.value = _desc_build_type(wctx, bctx, src->array.element);
      break;
    case BEBOP_TYPE_FIXED_ARRAY:
      t->fixed_array_element.has_value = true;
      t->fixed_array_element.value = _desc_build_type(wctx, bctx, src->fixed_array.element);
      DESC_SET_OPT(t->fixed_array_size, src->fixed_array.size);
      break;
    case BEBOP_TYPE_MAP:
      t->map_key.has_value = true;
      t->map_key.value = _desc_build_type(wctx, bctx, src->map.key);
      t->map_value.has_value = true;
      t->map_value.value = _desc_build_type(wctx, bctx, src->map.value);
      break;
    case BEBOP_TYPE_DEFINED: {
      const char* name = BEBOP_STR(bctx, src->defined.name);
      if (src->defined.resolved) {
        const char* fqn = BEBOP_STR(bctx, src->defined.resolved->fqn);
        DESC_SET_OPT_STR(t->defined_fqn, fqn ? fqn : name);
      } else {
        DESC_SET_OPT_STR(t->defined_fqn, name);
      }
      break;
    }
    default:
      break;
  }

  return t;
}

static void _desc_build_literal(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_literal_t* src, Bebop_LiteralValue* dst
)
{
  memset(dst, 0, sizeof(*dst));
  if (!src) {
    return;
  }

  DESC_SET_OPT(dst->kind, (Bebop_LiteralKind)src->kind);

  switch (src->kind) {
    case BEBOP_LITERAL_BOOL:
      DESC_SET_OPT(dst->bool_value, src->bool_val);
      break;
    case BEBOP_LITERAL_INT:
      DESC_SET_OPT(dst->int_value, src->int_val);
      break;
    case BEBOP_LITERAL_FLOAT:
      DESC_SET_OPT(dst->float_value, src->float_val);
      break;
    case BEBOP_LITERAL_STRING:
      DESC_SET_OPT_STR(dst->string_value, BEBOP_STR(bctx, src->string_val));
      break;
    case BEBOP_LITERAL_UUID:
      dst->uuid_value.has_value = true;
      memcpy(dst->uuid_value.value.bytes, src->uuid_val, 16);
      break;
    case BEBOP_LITERAL_BYTES:
      if (src->bytes_val.data && src->bytes_val.len > 0) {
        dst->bytes_value.has_value = true;
        dst->bytes_value.value.length = (uint32_t)src->bytes_val.len;
        dst->bytes_value.value.data = Bebop_WireCtx_Alloc(wctx, src->bytes_val.len);
        if (dst->bytes_value.value.data) {
          memcpy(dst->bytes_value.value.data, src->bytes_val.data, src->bytes_val.len);
        }
      }
      break;
    case BEBOP_LITERAL_TIMESTAMP:
      dst->timestamp_value.has_value = true;
      dst->timestamp_value.value.seconds = src->timestamp_val.seconds;
      dst->timestamp_value.value.nanos = src->timestamp_val.nanos;
      dst->timestamp_value.value.offset_ms = src->timestamp_val.offset_ms;
      break;
    case BEBOP_LITERAL_DURATION:
      dst->duration_value.has_value = true;
      dst->duration_value.value.seconds = src->duration_val.seconds;
      dst->duration_value.value.nanos = src->duration_val.nanos;
      break;
    default:
      break;
  }

  if (src->has_env_var) {
    DESC_SET_OPT_STR(dst->raw_value, BEBOP_STR(bctx, src->raw_value));
  }
}

static Bebop_DecoratorUsage_Array _desc_build_decorators(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_decorator_t* chain
)
{
  Bebop_DecoratorUsage_Array result = {0};
  if (!chain) {
    return result;
  }

  uint32_t count = 0;
  for (const bebop_decorator_t* d = chain; d; d = d->next) {
    count++;
  }
  if (count == 0) {
    return result;
  }

  Bebop_DecoratorUsage* arr = Bebop_WireCtx_Alloc(wctx, count * sizeof(*arr));
  if (!arr) {
    return result;
  }

  uint32_t i = 0;
  for (const bebop_decorator_t* d = chain; d; d = d->next, i++) {
    Bebop_DecoratorUsage* u = &arr[i];
    memset(u, 0, sizeof(*u));

    DESC_SET_OPT_STR(u->fqn, BEBOP_STR(bctx, d->name));

    if (d->arg_count > 0 && d->args) {
      Bebop_DecoratorArg* args = Bebop_WireCtx_Alloc(wctx, d->arg_count * sizeof(*args));
      if (args) {
        for (uint32_t j = 0; j < d->arg_count; j++) {
          const bebop_decorator_arg_t* sa = &d->args[j];
          const char* arg_name = BEBOP_STR(bctx, sa->name);
          if (!arg_name && d->resolved && d->resolved->kind == BEBOP_DEF_DECORATOR) {
            if (j < d->resolved->decorator_def.param_count) {
              arg_name = BEBOP_STR(bctx, d->resolved->decorator_def.params[j].name);
            }
          }
          const Bebop_DecoratorArg* da = &args[j];
          memcpy(BEBOP_WIRE_MUTPTR(Bebop_Str, &da->name), &(Bebop_Str) {0}, sizeof(Bebop_Str));
          if (arg_name) {
            Bebop_Str s = _desc_dup_str(wctx, arg_name);
            memcpy(BEBOP_WIRE_MUTPTR(Bebop_Str, &da->name), &s, sizeof(s));
          }
          _desc_build_literal(
              wctx, bctx, &sa->value, BEBOP_WIRE_MUTPTR(Bebop_LiteralValue, &da->value)
          );
        }
        BEBOP_WIRE_SET_SOME(
            u->args, ((Bebop_DecoratorArg_Array) {.data = args, .length = d->arg_count})
        );
      }
    }

    if (d->export_data && d->export_data->count > 0) {
      const bebop_export_data_t* exp = d->export_data;
      u->export_data.has_value = true;
      Bebop_Map_Init(&u->export_data.value, wctx, Bebop_MapHash_Str, Bebop_MapEq_Str);
      for (uint32_t j = 0; j < exp->count; j++) {
        Bebop_Str* key = Bebop_WireCtx_Alloc(wctx, sizeof(Bebop_Str));
        Bebop_LiteralValue* val = Bebop_WireCtx_Alloc(wctx, sizeof(Bebop_LiteralValue));
        if (key && val) {
          *key = _desc_dup_str(wctx, BEBOP_STR(bctx, exp->entries[j].key));
          _desc_build_literal(wctx, bctx, &exp->entries[j].value, val);
          Bebop_Map_Put(&u->export_data.value, key, val);
        }
      }
    }
  }

  result.data = arr;
  result.length = count;
  return result;
}

#define DESC_SET_DECORATORS(field, wctx, bctx, chain) \
  do { \
    Bebop_DecoratorUsage_Array _decs = _desc_build_decorators((wctx), (bctx), (chain)); \
    if (_decs.length > 0) { \
      BEBOP_WIRE_SET_SOME((field), _decs); \
    } \
  } while (0)

static uint32_t _desc_count_locations(const bebop_def_t* defs)
{
  uint32_t count = 0;
  for (const bebop_def_t* d = defs; d; d = d->next) {
    count++;
    switch (d->kind) {
      case BEBOP_DEF_STRUCT:
        count += d->struct_def.field_count;
        break;
      case BEBOP_DEF_MESSAGE:
        count += d->message_def.field_count;
        break;
      case BEBOP_DEF_ENUM:
        count += d->enum_def.member_count;
        break;
      case BEBOP_DEF_UNION:
        count += d->union_def.branch_count;
        break;
      case BEBOP_DEF_SERVICE:
        count += d->service_def.method_count;
        break;
      default:
        break;
    }
    count += _desc_count_locations(d->nested_defs);
  }
  return count;
}

static void _desc_set_span(Bebop_Location* loc, bebop_span_t span)
{
  loc->span.has_value = true;
  loc->span.value[0] = (int32_t)span.start_line;
  loc->span.value[1] = (int32_t)span.start_col;
  loc->span.value[2] = (int32_t)span.end_line;
  loc->span.value[3] = (int32_t)span.end_col;
}

static void _desc_set_path(
    Bebop_WireCtx* wctx, Bebop_Location* loc, const int32_t* path, uint32_t path_len
)
{
  if (path_len == 0) {
    return;
  }
  int32_t* p = Bebop_WireCtx_Alloc(wctx, path_len * sizeof(*p));
  if (!p) {
    return;
  }
  memcpy(p, path, path_len * sizeof(*p));
  loc->path.has_value = true;
  loc->path.value.data = p;
  loc->path.value.length = path_len;
}

static const bebop_token_t* _desc_find_token_at(const bebop_schema_t* schema, uint32_t target_off)
{
  if (!schema || schema->tokens.count == 0) {
    return NULL;
  }
  for (uint32_t i = 0; i < schema->tokens.count; i++) {
    const bebop_token_t* tok = &schema->tokens.tokens[i];
    if (tok->span.off == target_off) {
      return tok;
    }
    if (tok->span.off > target_off) {
      return (i > 0) ? &schema->tokens.tokens[i - 1] : tok;
    }
  }
  return &schema->tokens.tokens[schema->tokens.count - 1];
}

static Bebop_Str _desc_extract_comment(
    Bebop_WireCtx* wctx, const bebop_schema_t* schema, const bebop_trivia_t* t
)
{
  const char* src = schema->source + t->span.off;
  size_t len = t->span.len;

  if (t->kind == BEBOP_TRIVIA_DOC_COMMENT && len >= 3 && src[0] == '/' && src[1] == '/'
      && src[2] == '/')
  {
    src += 3;
    len -= 3;
  } else if (t->kind == BEBOP_TRIVIA_LINE_COMMENT && len >= 2 && src[0] == '/' && src[1] == '/') {
    src += 2;
    len -= 2;
  } else if (t->kind == BEBOP_TRIVIA_BLOCK_COMMENT && len >= 4) {
    src += 2;
    len -= 4;
  }

  while (len > 0 && BEBOP_IS_WHITESPACE(*src)) {
    src++;
    len--;
  }

  while (len > 0 && BEBOP_IS_BLANK(src[len - 1])) {
    len--;
  }

  if (len == 0) {
    return (Bebop_Str) {0};
  }

  char* copy = Bebop_WireCtx_Alloc(wctx, len + 1);
  if (!copy) {
    return (Bebop_Str) {0};
  }
  memcpy(copy, src, len);
  copy[len] = '\0';
  return (Bebop_Str) {.data = copy, .length = (uint32_t)len};
}

static void _desc_extract_comments(
    Bebop_WireCtx* wctx,
    const bebop_schema_t* schema,
    const bebop_token_t* tok,
    Bebop_Str* leading,
    Bebop_Str** detached,
    uint32_t* detached_count
)
{
  *leading = (Bebop_Str) {0};
  *detached = NULL;
  *detached_count = 0;

  if (!tok || tok->leading.count == 0) {
    return;
  }

  Bebop_Str groups[32];
  uint32_t group_count = 0;
  int start_new_group = 1;
  int consecutive_newlines = 0;

  for (uint32_t i = 0; i < tok->leading.count; i++) {
    const bebop_trivia_t* t = &tok->leading.items[i];

    if (t->kind == BEBOP_TRIVIA_NEWLINE) {
      consecutive_newlines++;

      if (consecutive_newlines >= 2 && group_count > 0) {
        start_new_group = 1;
      }
    } else if (t->kind == BEBOP_TRIVIA_LINE_COMMENT || t->kind == BEBOP_TRIVIA_DOC_COMMENT
               || t->kind == BEBOP_TRIVIA_BLOCK_COMMENT)
    {
      const Bebop_Str comment = _desc_extract_comment(wctx, schema, t);
      if (comment.length > 0 && group_count < 32) {
        if (start_new_group) {
          groups[group_count++] = comment;
          start_new_group = 0;
        } else {
          groups[group_count - 1] = comment;
        }
      }
      consecutive_newlines = 0;
    } else if (t->kind != BEBOP_TRIVIA_WHITESPACE) {
      consecutive_newlines = 0;
    }
  }

  if (group_count == 0) {
    return;
  }

  *leading = groups[group_count - 1];

  if (group_count > 1) {
    *detached_count = group_count - 1;
    *detached = Bebop_WireCtx_Alloc(wctx, (*detached_count) * sizeof(Bebop_Str));
    if (*detached) {
      for (uint32_t i = 0; i < *detached_count; i++) {
        (*detached)[i] = groups[i];
      }
    } else {
      *detached_count = 0;
    }
  }
}

static Bebop_Str _desc_extract_trailing(
    Bebop_WireCtx* wctx, const bebop_schema_t* schema, const bebop_token_t* tok
)
{
  if (!tok || tok->trailing.count == 0) {
    return (Bebop_Str) {0};
  }

  for (uint32_t i = 0; i < tok->trailing.count; i++) {
    const bebop_trivia_t* t = &tok->trailing.items[i];
    if (t->kind == BEBOP_TRIVIA_LINE_COMMENT || t->kind == BEBOP_TRIVIA_BLOCK_COMMENT) {
      return _desc_extract_comment(wctx, schema, t);
    }
  }
  return (Bebop_Str) {0};
}

static void _desc_set_comments(
    Bebop_WireCtx* wctx, const bebop_schema_t* schema, Bebop_Location* loc, uint32_t span_off
)
{
  const bebop_token_t* tok = _desc_find_token_at(schema, span_off);
  if (!tok) {
    return;
  }

  Bebop_Str leading;
  Bebop_Str* detached;
  uint32_t detached_count;
  _desc_extract_comments(wctx, schema, tok, &leading, &detached, &detached_count);

  if (leading.length > 0) {
    loc->leading_comments.has_value = true;
    loc->leading_comments.value = leading;
  }

  if (detached_count > 0 && detached) {
    loc->detached_comments.has_value = true;
    loc->detached_comments.value.data = detached;
    loc->detached_comments.value.length = detached_count;
  }

  const Bebop_Str trailing = _desc_extract_trailing(wctx, schema, tok);
  if (trailing.length > 0) {
    loc->trailing_comments.has_value = true;
    loc->trailing_comments.value = trailing;
  }
}

static uint32_t _desc_build_locations(
    Bebop_WireCtx* wctx,
    const bebop_def_t* defs,
    Bebop_Location* locs,
    uint32_t idx,
    const int32_t* parent_path,
    uint32_t parent_path_len
)
{
  uint32_t def_idx = 0;
  for (const bebop_def_t* d = defs; d; d = d->next, def_idx++) {
    Bebop_Location* loc = &locs[idx++];
    memset(loc, 0, sizeof(*loc));

    const uint32_t path_len = parent_path_len + 2;
    int32_t* path = Bebop_WireCtx_Alloc(wctx, path_len * sizeof(*path));
    if (path) {
      if (parent_path_len > 0) {
        memcpy(path, parent_path, parent_path_len * sizeof(*path));
      }
      path[parent_path_len] = 5;
      path[parent_path_len + 1] = (int32_t)def_idx;
      loc->path.has_value = true;
      loc->path.value.data = path;
      loc->path.value.length = path_len;
    }

    _desc_set_span(loc, d->span);
    _desc_set_comments(wctx, d->schema, loc, d->span.off);

    switch (d->kind) {
      case BEBOP_DEF_STRUCT:
        for (uint32_t i = 0; i < d->struct_def.field_count; i++) {
          const bebop_field_t* f = &d->struct_def.fields[i];
          Bebop_Location* floc = &locs[idx++];
          memset(floc, 0, sizeof(*floc));

          int32_t fpath[6];
          const uint32_t fpath_len = path_len + 2;
          if (path_len <= 4) {
            memcpy(fpath, path, path_len * sizeof(*fpath));
            fpath[path_len] = 1;
            fpath[path_len + 1] = (int32_t)i;
            _desc_set_path(wctx, floc, fpath, fpath_len);
          }
          _desc_set_span(floc, f->span);
          _desc_set_comments(wctx, d->schema, floc, f->span.off);
        }
        break;

      case BEBOP_DEF_MESSAGE:
        for (uint32_t i = 0; i < d->message_def.field_count; i++) {
          const bebop_field_t* f = &d->message_def.fields[i];
          Bebop_Location* floc = &locs[idx++];
          memset(floc, 0, sizeof(*floc));
          int32_t fpath[6];
          const uint32_t fpath_len = path_len + 2;
          if (path_len <= 4) {
            memcpy(fpath, path, path_len * sizeof(*fpath));
            fpath[path_len] = 1;
            fpath[path_len + 1] = (int32_t)i;
            _desc_set_path(wctx, floc, fpath, fpath_len);
          }
          _desc_set_span(floc, f->span);
          _desc_set_comments(wctx, d->schema, floc, f->span.off);
        }
        break;

      case BEBOP_DEF_ENUM:
        for (uint32_t i = 0; i < d->enum_def.member_count; i++) {
          const bebop_enum_member_t* m = &d->enum_def.members[i];
          Bebop_Location* mloc = &locs[idx++];
          memset(mloc, 0, sizeof(*mloc));

          int32_t mpath[6];
          const uint32_t mpath_len = path_len + 2;
          if (path_len <= 4) {
            memcpy(mpath, path, path_len * sizeof(*mpath));
            mpath[path_len] = 2;
            mpath[path_len + 1] = (int32_t)i;
            _desc_set_path(wctx, mloc, mpath, mpath_len);
          }
          _desc_set_span(mloc, m->span);
          _desc_set_comments(wctx, d->schema, mloc, m->span.off);
        }
        break;

      case BEBOP_DEF_UNION:
        for (uint32_t i = 0; i < d->union_def.branch_count; i++) {
          const bebop_union_branch_t* b = &d->union_def.branches[i];
          Bebop_Location* bloc = &locs[idx++];
          memset(bloc, 0, sizeof(*bloc));

          int32_t bpath[6];
          const uint32_t bpath_len = path_len + 2;
          if (path_len <= 4) {
            memcpy(bpath, path, path_len * sizeof(*bpath));
            bpath[path_len] = 1;
            bpath[path_len + 1] = (int32_t)i;
            _desc_set_path(wctx, bloc, bpath, bpath_len);
          }
          _desc_set_span(bloc, b->span);
          _desc_set_comments(wctx, d->schema, bloc, b->span.off);
        }
        break;

      case BEBOP_DEF_SERVICE:
        for (uint32_t i = 0; i < d->service_def.method_count; i++) {
          const bebop_method_t* m = &d->service_def.methods[i];
          Bebop_Location* mloc = &locs[idx++];
          memset(mloc, 0, sizeof(*mloc));

          int32_t mpath[6];
          const uint32_t mpath_len = path_len + 2;
          if (path_len <= 4) {
            memcpy(mpath, path, path_len * sizeof(*mpath));
            mpath[path_len] = 1;
            mpath[path_len + 1] = (int32_t)i;
            _desc_set_path(wctx, mloc, mpath, mpath_len);
          }
          _desc_set_span(mloc, m->span);
          _desc_set_comments(wctx, d->schema, mloc, m->span.off);
        }
        break;

      default:
        break;
    }

    if (d->nested_def_count > 0) {
      idx = _desc_build_locations(wctx, d->nested_defs, locs, idx, path, path_len);
    }
  }
  return idx;
}

static Bebop_SourceCodeInfo* _desc_build_source_code_info(
    Bebop_WireCtx* wctx, const bebop_schema_t* src
)
{
  const uint32_t loc_count = _desc_count_locations(src->definitions);
  if (loc_count == 0) {
    return NULL;
  }

  Bebop_SourceCodeInfo* sci = Bebop_WireCtx_Alloc(wctx, sizeof(*sci));
  if (!sci) {
    return NULL;
  }
  memset(sci, 0, sizeof(*sci));

  Bebop_Location* locs = Bebop_WireCtx_Alloc(wctx, loc_count * sizeof(*locs));
  if (!locs) {
    return NULL;
  }

  _desc_build_locations(wctx, src->definitions, locs, 0, NULL, 0);

  sci->locations.has_value = true;
  sci->locations.value.data = locs;
  sci->locations.value.length = loc_count;

  return sci;
}

static Bebop_FieldDescriptor_Array _desc_build_fields(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_field_t* fields, uint32_t count
)
{
  Bebop_FieldDescriptor_Array result = {0};
  if (count == 0 || !fields) {
    return result;
  }

  Bebop_FieldDescriptor* arr = Bebop_WireCtx_Alloc(wctx, count * sizeof(*arr));
  if (!arr) {
    return result;
  }

  for (uint32_t i = 0; i < count; i++) {
    const bebop_field_t* sf = &fields[i];
    Bebop_FieldDescriptor* df = &arr[i];
    memset(df, 0, sizeof(*df));

    DESC_SET_OPT_STR(df->name, BEBOP_STR(bctx, sf->name));
    DESC_SET_OPT_STR(df->documentation, BEBOP_STR(bctx, sf->documentation));
    BEBOP_WIRE_SET_SOME(df->type, _desc_build_type(wctx, bctx, sf->type));
    DESC_SET_OPT(df->index, sf->index);
    DESC_SET_DECORATORS(df->decorators, wctx, bctx, sf->decorators);
  }

  result.data = arr;
  result.length = count;
  return result;
}

#define DESC_SET_FIELDS(field, wctx, bctx, fields_ptr, cnt) \
  do { \
    Bebop_FieldDescriptor_Array _flds = _desc_build_fields((wctx), (bctx), (fields_ptr), (cnt)); \
    if (_flds.length > 0) { \
      BEBOP_WIRE_SET_SOME((field), _flds); \
    } \
  } while (0)

static Bebop_DefinitionDescriptor_Array _desc_build_nested(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_def_t* parent
)
{
  Bebop_DefinitionDescriptor_Array result = {0};
  if (parent->nested_def_count == 0) {
    return result;
  }

  Bebop_DefinitionDescriptor* arr =
      Bebop_WireCtx_Alloc(wctx, parent->nested_def_count * sizeof(*arr));
  if (!arr) {
    return result;
  }

  uint32_t i = 0;
  for (const bebop_def_t* n = parent->nested_defs; n; n = n->next, i++) {
    _desc_build_def(wctx, bctx, n, &arr[i]);
  }

  result.data = arr;
  result.length = parent->nested_def_count;
  return result;
}

static void _desc_build_def(
    Bebop_WireCtx* wctx,
    bebop_context_t* bctx,
    const bebop_def_t* src,
    Bebop_DefinitionDescriptor* dst
)
{
  memset(dst, 0, sizeof(*dst));

  BEBOP_WIRE_SET_SOME(dst->kind, (Bebop_DefinitionKind)src->kind);
  BEBOP_WIRE_SET_SOME(dst->visibility, (Bebop_Visibility)src->visibility);
  DESC_SET_OPT_STR(dst->name, BEBOP_STR(bctx, src->name));
  DESC_SET_OPT_STR(dst->fqn, BEBOP_STR(bctx, src->fqn));
  DESC_SET_OPT_STR(dst->documentation, BEBOP_STR(bctx, src->documentation));
  DESC_SET_DECORATORS(dst->decorators, wctx, bctx, src->decorators);
  {
    const Bebop_DefinitionDescriptor_Array nested = _desc_build_nested(wctx, bctx, src);
    if (nested.length > 0) {
      BEBOP_WIRE_SET_SOME(dst->nested, nested);
    }
  }

  switch (src->kind) {
    case BEBOP_DEF_ENUM: {
      Bebop_EnumDef* ed = Bebop_WireCtx_Alloc(wctx, sizeof(*ed));
      if (!ed) {
        break;
      }
      memset(ed, 0, sizeof(*ed));

      DESC_SET_OPT(ed->base_type, (Bebop_TypeKind)src->enum_def.base_type);

      bool is_flags = false;
      for (const bebop_decorator_t* d = src->decorators; d; d = d->next) {
        const char* dname = BEBOP_STR(bctx, d->name);
        if (dname && strcmp(dname, "flags") == 0) {
          is_flags = true;
          break;
        }
      }
      DESC_SET_OPT(ed->is_flags, is_flags);

      if (src->enum_def.member_count > 0) {
        Bebop_EnumMemberDescriptor* members =
            Bebop_WireCtx_Alloc(wctx, src->enum_def.member_count * sizeof(*members));
        if (members) {
          for (uint32_t i = 0; i < src->enum_def.member_count; i++) {
            const bebop_enum_member_t* sm = &src->enum_def.members[i];
            Bebop_EnumMemberDescriptor* dm = &members[i];
            memset(dm, 0, sizeof(*dm));
            DESC_SET_OPT_STR(dm->name, BEBOP_STR(bctx, sm->name));
            DESC_SET_OPT_STR(dm->documentation, BEBOP_STR(bctx, sm->documentation));
            DESC_SET_OPT(dm->value, sm->value);
            DESC_SET_DECORATORS(dm->decorators, wctx, bctx, sm->decorators);
          }
          ed->members.has_value = true;
          ed->members.value.data = members;
          ed->members.value.length = src->enum_def.member_count;
        }
      }

      dst->enum_def.has_value = true;
      dst->enum_def.value = ed;
      break;
    }

    case BEBOP_DEF_STRUCT: {
      Bebop_StructDef* sd = Bebop_WireCtx_Alloc(wctx, sizeof(*sd));
      if (!sd) {
        break;
      }
      memset(sd, 0, sizeof(*sd));

      DESC_SET_OPT(sd->is_mutable, src->struct_def.is_mutable);
      DESC_SET_OPT(sd->fixed_size, src->struct_def.fixed_size);
      DESC_SET_FIELDS(sd->fields, wctx, bctx, src->struct_def.fields, src->struct_def.field_count);

      dst->struct_def.has_value = true;
      dst->struct_def.value = sd;
      break;
    }

    case BEBOP_DEF_MESSAGE: {
      Bebop_MessageDef* md = Bebop_WireCtx_Alloc(wctx, sizeof(*md));
      if (!md) {
        break;
      }
      memset(md, 0, sizeof(*md));

      DESC_SET_FIELDS(
          md->fields, wctx, bctx, src->message_def.fields, src->message_def.field_count
      );

      dst->message_def.has_value = true;
      dst->message_def.value = md;
      break;
    }

    case BEBOP_DEF_UNION: {
      Bebop_UnionDef* ud = Bebop_WireCtx_Alloc(wctx, sizeof(*ud));
      if (!ud) {
        break;
      }
      memset(ud, 0, sizeof(*ud));

      if (src->union_def.branch_count > 0) {
        Bebop_UnionBranchDescriptor* branches =
            Bebop_WireCtx_Alloc(wctx, src->union_def.branch_count * sizeof(*branches));
        if (branches) {
          for (uint32_t i = 0; i < src->union_def.branch_count; i++) {
            const bebop_union_branch_t* sb = &src->union_def.branches[i];
            Bebop_UnionBranchDescriptor* db = &branches[i];
            memset(db, 0, sizeof(*db));

            DESC_SET_OPT(db->discriminator, sb->discriminator);
            DESC_SET_OPT_STR(db->documentation, BEBOP_STR(bctx, sb->documentation));
            if (sb->def) {
              DESC_SET_OPT_STR(db->inline_fqn, BEBOP_STR(bctx, sb->def->fqn));
            } else if (sb->type_ref) {
              const char* ref_fqn = sb->type_ref->defined.resolved
                  ? BEBOP_STR(bctx, sb->type_ref->defined.resolved->fqn)
                  : BEBOP_STR(bctx, sb->type_ref->defined.name);
              DESC_SET_OPT_STR(db->type_ref_fqn, ref_fqn);
              DESC_SET_OPT_STR(db->name, BEBOP_STR(bctx, sb->name));
            }
            DESC_SET_DECORATORS(db->decorators, wctx, bctx, sb->decorators);
          }
          ud->branches.has_value = true;
          ud->branches.value.data = branches;
          ud->branches.value.length = src->union_def.branch_count;
        }
      }

      dst->union_def.has_value = true;
      dst->union_def.value = ud;
      break;
    }

    case BEBOP_DEF_SERVICE: {
      Bebop_ServiceDef* svd = Bebop_WireCtx_Alloc(wctx, sizeof(*svd));
      if (!svd) {
        break;
      }
      memset(svd, 0, sizeof(*svd));

      if (src->service_def.method_count > 0) {
        Bebop_MethodDescriptor* methods =
            Bebop_WireCtx_Alloc(wctx, src->service_def.method_count * sizeof(*methods));
        if (methods) {
          for (uint32_t i = 0; i < src->service_def.method_count; i++) {
            const bebop_method_t* sm = &src->service_def.methods[i];
            Bebop_MethodDescriptor* dm = &methods[i];
            memset(dm, 0, sizeof(*dm));

            DESC_SET_OPT_STR(dm->name, BEBOP_STR(bctx, sm->name));
            DESC_SET_OPT_STR(dm->documentation, BEBOP_STR(bctx, sm->documentation));
            dm->request_type.has_value = true;
            dm->request_type.value = _desc_build_type(wctx, bctx, sm->request_type);
            dm->response_type.has_value = true;
            dm->response_type.value = _desc_build_type(wctx, bctx, sm->response_type);
            DESC_SET_OPT(dm->method_type, (Bebop_MethodType)sm->method_type);
            DESC_SET_OPT(dm->id, sm->id);
            DESC_SET_DECORATORS(dm->decorators, wctx, bctx, sm->decorators);
          }
          svd->methods.has_value = true;
          svd->methods.value.data = methods;
          svd->methods.value.length = src->service_def.method_count;
        }
      }

      dst->service_def.has_value = true;
      dst->service_def.value = svd;
      break;
    }

    case BEBOP_DEF_CONST: {
      Bebop_ConstDef* cd = Bebop_WireCtx_Alloc(wctx, sizeof(*cd));
      if (!cd) {
        break;
      }
      memset(cd, 0, sizeof(*cd));

      cd->type.has_value = true;
      cd->type.value = _desc_build_type(wctx, bctx, src->const_def.type);

      Bebop_LiteralValue* val = Bebop_WireCtx_Alloc(wctx, sizeof(*val));
      if (val) {
        _desc_build_literal(wctx, bctx, &src->const_def.value, val);
        cd->value.has_value = true;
        cd->value.value = val;
      }

      dst->const_def.has_value = true;
      dst->const_def.value = cd;
      break;
    }

    case BEBOP_DEF_DECORATOR: {
      Bebop_DecoratorDef* dd = Bebop_WireCtx_Alloc(wctx, sizeof(*dd));
      if (!dd) {
        break;
      }
      memset(dd, 0, sizeof(*dd));

      DESC_SET_OPT(dd->targets, (Bebop_DecoratorTarget)src->decorator_def.targets);
      DESC_SET_OPT(dd->allow_multiple, src->decorator_def.allow_multiple);

      if (src->decorator_def.validate_span.len > 0 && src->schema && src->schema->source) {
        const char* lua_src = src->schema->source + src->decorator_def.validate_span.off;
        const size_t lua_len = src->decorator_def.validate_span.len;
        char* copy = Bebop_WireCtx_Alloc(wctx, lua_len + 1);
        if (copy) {
          memcpy(copy, lua_src, lua_len);
          copy[lua_len] = '\0';
          dd->validate_source.has_value = true;
          dd->validate_source.value = (Bebop_Str) {.data = copy, .length = (uint32_t)lua_len};
        }
      }

      if (src->decorator_def.export_span.len > 0 && src->schema && src->schema->source) {
        const char* lua_src = src->schema->source + src->decorator_def.export_span.off;
        const size_t lua_len = src->decorator_def.export_span.len;
        char* copy = Bebop_WireCtx_Alloc(wctx, lua_len + 1);
        if (copy) {
          memcpy(copy, lua_src, lua_len);
          copy[lua_len] = '\0';
          dd->export_source.has_value = true;
          dd->export_source.value = (Bebop_Str) {.data = copy, .length = (uint32_t)lua_len};
        }
      }

      if (src->decorator_def.param_count > 0) {
        Bebop_DecoratorParamDef* params =
            Bebop_WireCtx_Alloc(wctx, src->decorator_def.param_count * sizeof(*params));
        if (params) {
          for (uint32_t i = 0; i < src->decorator_def.param_count; i++) {
            const bebop_macro_param_def_t* sp = &src->decorator_def.params[i];
            Bebop_DecoratorParamDef* dp = &params[i];
            memset(dp, 0, sizeof(*dp));

            DESC_SET_OPT_STR(dp->name, BEBOP_STR(bctx, sp->name));
            DESC_SET_OPT_STR(dp->description, BEBOP_STR(bctx, sp->description));
            DESC_SET_OPT(dp->type, (Bebop_TypeKind)sp->type);
            DESC_SET_OPT(dp->required, sp->required);

            if (sp->default_value) {
              Bebop_LiteralValue* dv = Bebop_WireCtx_Alloc(wctx, sizeof(*dv));
              if (dv) {
                _desc_build_literal(wctx, bctx, sp->default_value, dv);
                dp->default_value.has_value = true;
                dp->default_value.value = dv;
              }
            }

            if (sp->allowed_value_count > 0 && sp->allowed_values) {
              Bebop_LiteralValue* allowed =
                  Bebop_WireCtx_Alloc(wctx, sp->allowed_value_count * sizeof(*allowed));
              if (allowed) {
                for (uint32_t j = 0; j < sp->allowed_value_count; j++) {
                  _desc_build_literal(wctx, bctx, &sp->allowed_values[j], &allowed[j]);
                }
                dp->allowed_values.has_value = true;
                dp->allowed_values.value.data = allowed;
                dp->allowed_values.value.length = sp->allowed_value_count;
              }
            }
          }
          dd->params.has_value = true;
          dd->params.value.data = params;
          dd->params.value.length = src->decorator_def.param_count;
        }
      }

      dst->decorator_def.has_value = true;
      dst->decorator_def.value = dd;
      break;
    }

    default:
      break;
  }
}

static Bebop_SchemaDescriptor* _desc_build_schema(
    Bebop_WireCtx* wctx, bebop_context_t* bctx, const bebop_schema_t* src, bebop_desc_flags_t flags
)
{
  Bebop_SchemaDescriptor* s = Bebop_WireCtx_Alloc(wctx, sizeof(*s));
  if (!s) {
    return NULL;
  }
  memset(s, 0, sizeof(*s));

  DESC_SET_OPT_STR(s->path, src->path);
  DESC_SET_OPT_STR(s->package, BEBOP_STR(bctx, src->package));
  DESC_SET_OPT(s->edition, (Bebop_Edition)src->edition);

  if (src->import_count > 0 && src->imports) {
    Bebop_Str* imports = Bebop_WireCtx_Alloc(wctx, src->import_count * sizeof(*imports));
    if (imports) {
      for (uint32_t i = 0; i < src->import_count; i++) {
        imports[i] = _desc_dup_str(wctx, BEBOP_STR(bctx, src->imports[i].path));
      }
      s->imports.has_value = true;
      s->imports.value.data = imports;
      s->imports.value.length = src->import_count;
    }
  }

  if (src->definition_count > 0) {
    Bebop_DefinitionDescriptor* defs =
        Bebop_WireCtx_Alloc(wctx, src->definition_count * sizeof(*defs));
    if (defs) {
      uint32_t i = 0;
      for (const bebop_def_t* d = src->definitions; d; d = d->next, i++) {
        _desc_build_def(wctx, bctx, d, &defs[i]);
      }
      s->definitions.has_value = true;
      s->definitions.value.data = defs;
      s->definitions.value.length = src->definition_count;
    }
  }

  if (flags & BEBOP_DESC_FLAG_SOURCE_INFO) {
    Bebop_SourceCodeInfo* sci = _desc_build_source_code_info(wctx, src);
    if (sci) {
      s->source_code_info.has_value = true;
      s->source_code_info.value = sci;
    }
  }

  return s;
}

bebop_status_t bebop_descriptor_build(
    const bebop_parse_result_t* result, bebop_desc_flags_t flags, bebop_descriptor_t** out
)
{
  if (!result || !out) {
    return BEBOP_FATAL;
  }

  bebop_context_t* bctx = result->ctx;
  bebop_host_allocator_t alloc = bctx->host.allocator;

  Bebop_WireCtx* wctx = _desc_make_ctx(&alloc);
  if (!wctx) {
    return BEBOP_FATAL;
  }

  bebop_descriptor_t* desc = Bebop_WireCtx_Alloc(wctx, sizeof(*desc));
  if (!desc) {
    Bebop_WireCtx_Free(wctx);
    return BEBOP_FATAL;
  }
  memset(desc, 0, sizeof(*desc));
  desc->ctx = wctx;

  const uint32_t schema_count = bebop_result_schema_count(result);
  if (schema_count > 0) {
    Bebop_SchemaDescriptor* schemas = Bebop_WireCtx_Alloc(wctx, schema_count * sizeof(*schemas));
    if (schemas) {
      for (uint32_t i = 0; i < schema_count; i++) {
        const bebop_schema_t* src = bebop_result_schema_at(result, i);
        const Bebop_SchemaDescriptor* built = _desc_build_schema(wctx, bctx, src, flags);
        if (built) {
          memcpy(&schemas[i], built, sizeof(schemas[i]));
        }
      }
      desc->data.schemas.has_value = true;
      desc->data.schemas.value.data = schemas;
      desc->data.schemas.value.length = schema_count;
    }
  }

  *out = desc;
  return BEBOP_OK;
}

bebop_status_t bebop_descriptor_encode(
    const bebop_descriptor_t* desc, const uint8_t** out_buf, size_t* out_len
)
{
  if (!desc || !out_buf || !out_len) {
    return BEBOP_FATAL;
  }

  Bebop_Writer* w;
  if (Bebop_WireCtx_Writer(desc->ctx, &w) != BEBOP_WIRE_OK) {
    return BEBOP_FATAL;
  }

  if (Bebop_DescriptorSet_Encode(w, &desc->data) != BEBOP_WIRE_OK) {
    return BEBOP_FATAL;
  }

  uint8_t* buf = NULL;
  size_t len = 0;
  Bebop_Writer_Buf(w, &buf, &len);

  *out_buf = buf;
  *out_len = len;
  return BEBOP_OK;
}

bebop_status_t bebop_descriptor_decode(
    bebop_context_t* ctx, const uint8_t* buf, size_t len, bebop_descriptor_t** out
)
{
  if (!ctx || !buf || !out) {
    return BEBOP_FATAL;
  }

  Bebop_WireCtx* wctx = _desc_make_ctx(&ctx->host.allocator);
  if (!wctx) {
    return BEBOP_FATAL;
  }

  bebop_descriptor_t* desc = Bebop_WireCtx_Alloc(wctx, sizeof(*desc));
  if (!desc) {
    Bebop_WireCtx_Free(wctx);
    return BEBOP_FATAL;
  }
  memset(desc, 0, sizeof(*desc));
  desc->ctx = wctx;

  Bebop_Reader* rd;
  if (Bebop_WireCtx_Reader(wctx, buf, len, &rd) != BEBOP_WIRE_OK) {
    Bebop_WireCtx_Free(wctx);
    return BEBOP_FATAL;
  }

  if (Bebop_DescriptorSet_Decode(wctx, rd, &desc->data) != BEBOP_WIRE_OK) {
    Bebop_WireCtx_Free(wctx);
    return BEBOP_FATAL;
  }

  *out = desc;
  return BEBOP_OK;
}

void bebop_descriptor_free(bebop_descriptor_t* desc)
{
  if (desc && desc->ctx) {
    Bebop_WireCtx_Free(desc->ctx);
  }
}

uint32_t bebop_descriptor_schema_count(const bebop_descriptor_t* desc)
{
  return desc && BEBOP_WIRE_IS_SOME(desc->data.schemas) ? (uint32_t)desc->data.schemas.value.length
                                                        : 0;
}

const bebop_descriptor_schema_t* bebop_descriptor_schema_at(
    const bebop_descriptor_t* desc, uint32_t idx
)
{
  if (!desc || !BEBOP_WIRE_IS_SOME(desc->data.schemas) || idx >= desc->data.schemas.value.length) {
    return NULL;
  }
  return &desc->data.schemas.value.data[idx];
}

const char* bebop_descriptor_schema_path(const bebop_descriptor_schema_t* s)
{
  return s && BEBOP_WIRE_IS_SOME(s->path) ? s->path.value.data : NULL;
}

const char* bebop_descriptor_schema_package(const bebop_descriptor_schema_t* s)
{
  return s && BEBOP_WIRE_IS_SOME(s->package) ? s->package.value.data : NULL;
}

bebop_edition_t bebop_descriptor_schema_edition(const bebop_descriptor_schema_t* s)
{
  return s && BEBOP_WIRE_IS_SOME(s->edition) ? (bebop_edition_t)s->edition.value : BEBOP_ED_UNKNOWN;
}

uint32_t bebop_descriptor_schema_import_count(const bebop_descriptor_schema_t* s)
{
  return s && BEBOP_WIRE_IS_SOME(s->imports) ? (uint32_t)s->imports.value.length : 0;
}

const char* bebop_descriptor_schema_import_at(const bebop_descriptor_schema_t* s, uint32_t idx)
{
  if (!s || !BEBOP_WIRE_IS_SOME(s->imports) || idx >= s->imports.value.length) {
    return NULL;
  }
  return s->imports.value.data[idx].data;
}

uint32_t bebop_descriptor_schema_def_count(const bebop_descriptor_schema_t* s)
{
  return s && BEBOP_WIRE_IS_SOME(s->definitions) ? (uint32_t)s->definitions.value.length : 0;
}

const bebop_descriptor_def_t* bebop_descriptor_schema_def_at(
    const bebop_descriptor_schema_t* s, uint32_t idx
)
{
  if (!s || !BEBOP_WIRE_IS_SOME(s->definitions) || idx >= s->definitions.value.length) {
    return NULL;
  }
  return &s->definitions.value.data[idx];
}

const bebop_descriptor_source_code_info_t* bebop_descriptor_schema_source_code_info(
    const bebop_descriptor_schema_t* s
)
{
  return s && BEBOP_WIRE_IS_SOME(s->source_code_info) ? s->source_code_info.value : NULL;
}

bebop_def_kind_t bebop_descriptor_def_kind(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->kind) ? (bebop_def_kind_t)d->kind.value : BEBOP_DEF_UNKNOWN;
}

const char* bebop_descriptor_def_name(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->name) ? d->name.value.data : NULL;
}

const char* bebop_descriptor_def_fqn(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->fqn) ? d->fqn.value.data : NULL;
}

const char* bebop_descriptor_def_documentation(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->documentation) ? d->documentation.value.data : NULL;
}

bebop_visibility_t bebop_descriptor_def_visibility(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->visibility) ? (bebop_visibility_t)d->visibility.value
                                                : BEBOP_VIS_DEFAULT;
}

uint32_t bebop_descriptor_def_decorator_count(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->decorators) ? (uint32_t)d->decorators.value.length : 0;
}

const bebop_descriptor_usage_t* bebop_descriptor_def_decorator_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d || !BEBOP_WIRE_IS_SOME(d->decorators) || idx >= d->decorators.value.length) {
    return NULL;
  }
  return &d->decorators.value.data[idx];
}

uint32_t bebop_descriptor_def_nested_count(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->nested) ? (uint32_t)d->nested.value.length : 0;
}

const bebop_descriptor_def_t* bebop_descriptor_def_nested_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d || !BEBOP_WIRE_IS_SOME(d->nested) || idx >= d->nested.value.length) {
    return NULL;
  }
  return &d->nested.value.data[idx];
}

uint32_t bebop_descriptor_def_field_count(const bebop_descriptor_def_t* d)
{
  if (!d) {
    return 0;
  }
  if (BEBOP_WIRE_IS_SOME(d->struct_def) && d->struct_def.value
      && BEBOP_WIRE_IS_SOME(d->struct_def.value->fields))
  {
    return (uint32_t)d->struct_def.value->fields.value.length;
  }
  if (BEBOP_WIRE_IS_SOME(d->message_def) && d->message_def.value
      && BEBOP_WIRE_IS_SOME(d->message_def.value->fields))
  {
    return (uint32_t)d->message_def.value->fields.value.length;
  }
  return 0;
}

const bebop_descriptor_field_t* bebop_descriptor_def_field_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d) {
    return NULL;
  }
  if (BEBOP_WIRE_IS_SOME(d->struct_def) && d->struct_def.value
      && BEBOP_WIRE_IS_SOME(d->struct_def.value->fields))
  {
    if (idx < d->struct_def.value->fields.value.length) {
      return &d->struct_def.value->fields.value.data[idx];
    }
  }
  if (BEBOP_WIRE_IS_SOME(d->message_def) && d->message_def.value
      && BEBOP_WIRE_IS_SOME(d->message_def.value->fields))
  {
    if (idx < d->message_def.value->fields.value.length) {
      return &d->message_def.value->fields.value.data[idx];
    }
  }
  return NULL;
}

bool bebop_descriptor_def_is_mutable(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->struct_def) && d->struct_def.value
          && BEBOP_WIRE_IS_SOME(d->struct_def.value->is_mutable)
      ? d->struct_def.value->is_mutable.value
      : false;
}

uint32_t bebop_descriptor_def_fixed_size(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->struct_def) && d->struct_def.value
          && BEBOP_WIRE_IS_SOME(d->struct_def.value->fixed_size)
      ? d->struct_def.value->fixed_size.value
      : 0;
}

uint32_t bebop_descriptor_def_member_count(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->enum_def) && d->enum_def.value
          && BEBOP_WIRE_IS_SOME(d->enum_def.value->members)
      ? (uint32_t)d->enum_def.value->members.value.length
      : 0;
}

const bebop_descriptor_member_t* bebop_descriptor_def_member_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d || !BEBOP_WIRE_IS_SOME(d->enum_def) || !d->enum_def.value
      || !BEBOP_WIRE_IS_SOME(d->enum_def.value->members))
  {
    return NULL;
  }
  if (idx >= d->enum_def.value->members.value.length) {
    return NULL;
  }
  return &d->enum_def.value->members.value.data[idx];
}

bebop_type_kind_t bebop_descriptor_def_base_type(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->enum_def) && d->enum_def.value
          && BEBOP_WIRE_IS_SOME(d->enum_def.value->base_type)
      ? (bebop_type_kind_t)d->enum_def.value->base_type.value
      : BEBOP_TYPE_UNKNOWN;
}

bool bebop_descriptor_def_is_flags(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->enum_def) && d->enum_def.value
          && BEBOP_WIRE_IS_SOME(d->enum_def.value->is_flags)
      ? d->enum_def.value->is_flags.value
      : false;
}

uint32_t bebop_descriptor_def_branch_count(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->union_def) && d->union_def.value
          && BEBOP_WIRE_IS_SOME(d->union_def.value->branches)
      ? (uint32_t)d->union_def.value->branches.value.length
      : 0;
}

const bebop_descriptor_branch_t* bebop_descriptor_def_branch_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d || !BEBOP_WIRE_IS_SOME(d->union_def) || !d->union_def.value
      || !BEBOP_WIRE_IS_SOME(d->union_def.value->branches))
  {
    return NULL;
  }
  if (idx >= d->union_def.value->branches.value.length) {
    return NULL;
  }
  return &d->union_def.value->branches.value.data[idx];
}

uint32_t bebop_descriptor_def_method_count(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->service_def) && d->service_def.value
          && BEBOP_WIRE_IS_SOME(d->service_def.value->methods)
      ? (uint32_t)d->service_def.value->methods.value.length
      : 0;
}

const bebop_descriptor_method_t* bebop_descriptor_def_method_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d || !BEBOP_WIRE_IS_SOME(d->service_def) || !d->service_def.value
      || !BEBOP_WIRE_IS_SOME(d->service_def.value->methods))
  {
    return NULL;
  }
  if (idx >= d->service_def.value->methods.value.length) {
    return NULL;
  }
  return &d->service_def.value->methods.value.data[idx];
}

const bebop_descriptor_type_t* bebop_descriptor_def_const_type(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->const_def) && d->const_def.value
          && BEBOP_WIRE_IS_SOME(d->const_def.value->type)
      ? d->const_def.value->type.value
      : NULL;
}

const bebop_descriptor_literal_t* bebop_descriptor_def_const_value(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->const_def) && d->const_def.value
          && BEBOP_WIRE_IS_SOME(d->const_def.value->value)
      ? d->const_def.value->value.value
      : NULL;
}

bebop_decorator_target_t bebop_descriptor_def_targets(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->decorator_def) && d->decorator_def.value
          && BEBOP_WIRE_IS_SOME(d->decorator_def.value->targets)
      ? (bebop_decorator_target_t)d->decorator_def.value->targets.value
      : BEBOP_TARGET_NONE;
}

bool bebop_descriptor_def_allow_multiple(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->decorator_def) && d->decorator_def.value
          && BEBOP_WIRE_IS_SOME(d->decorator_def.value->allow_multiple)
      ? d->decorator_def.value->allow_multiple.value
      : false;
}

uint32_t bebop_descriptor_def_param_count(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->decorator_def) && d->decorator_def.value
          && BEBOP_WIRE_IS_SOME(d->decorator_def.value->params)
      ? (uint32_t)d->decorator_def.value->params.value.length
      : 0;
}

const bebop_descriptor_param_t* bebop_descriptor_def_param_at(
    const bebop_descriptor_def_t* d, uint32_t idx
)
{
  if (!d || !BEBOP_WIRE_IS_SOME(d->decorator_def) || !d->decorator_def.value
      || !BEBOP_WIRE_IS_SOME(d->decorator_def.value->params))
  {
    return NULL;
  }
  if (idx >= d->decorator_def.value->params.value.length) {
    return NULL;
  }
  return &d->decorator_def.value->params.value.data[idx];
}

const char* bebop_descriptor_def_validate_source(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->decorator_def) && d->decorator_def.value
          && BEBOP_WIRE_IS_SOME(d->decorator_def.value->validate_source)
      ? d->decorator_def.value->validate_source.value.data
      : NULL;
}

const char* bebop_descriptor_def_export_source(const bebop_descriptor_def_t* d)
{
  return d && BEBOP_WIRE_IS_SOME(d->decorator_def) && d->decorator_def.value
          && BEBOP_WIRE_IS_SOME(d->decorator_def.value->export_source)
      ? d->decorator_def.value->export_source.value.data
      : NULL;
}

const char* bebop_descriptor_param_name(const bebop_descriptor_param_t* p)
{
  return p && BEBOP_WIRE_IS_SOME(p->name) ? p->name.value.data : NULL;
}

const char* bebop_descriptor_param_description(const bebop_descriptor_param_t* p)
{
  return p && BEBOP_WIRE_IS_SOME(p->description) ? p->description.value.data : NULL;
}

bebop_type_kind_t bebop_descriptor_param_type(const bebop_descriptor_param_t* p)
{
  return p && BEBOP_WIRE_IS_SOME(p->type) ? (bebop_type_kind_t)p->type.value : BEBOP_TYPE_UNKNOWN;
}

bool bebop_descriptor_param_required(const bebop_descriptor_param_t* p)
{
  return p && BEBOP_WIRE_IS_SOME(p->required) ? p->required.value : false;
}

const bebop_descriptor_literal_t* bebop_descriptor_param_default_value(
    const bebop_descriptor_param_t* p
)
{
  return p && BEBOP_WIRE_IS_SOME(p->default_value) ? p->default_value.value : NULL;
}

uint32_t bebop_descriptor_param_allowed_count(const bebop_descriptor_param_t* p)
{
  return p && BEBOP_WIRE_IS_SOME(p->allowed_values) ? (uint32_t)p->allowed_values.value.length : 0;
}

const bebop_descriptor_literal_t* bebop_descriptor_param_allowed_at(
    const bebop_descriptor_param_t* p, uint32_t idx
)
{
  if (!p || !BEBOP_WIRE_IS_SOME(p->allowed_values) || idx >= p->allowed_values.value.length) {
    return NULL;
  }
  return &p->allowed_values.value.data[idx];
}

const char* bebop_descriptor_field_name(const bebop_descriptor_field_t* f)
{
  return f && BEBOP_WIRE_IS_SOME(f->name) ? f->name.value.data : NULL;
}

const char* bebop_descriptor_field_documentation(const bebop_descriptor_field_t* f)
{
  return f && BEBOP_WIRE_IS_SOME(f->documentation) ? f->documentation.value.data : NULL;
}

const bebop_descriptor_type_t* bebop_descriptor_field_type(const bebop_descriptor_field_t* f)
{
  return f && BEBOP_WIRE_IS_SOME(f->type) ? f->type.value : NULL;
}

uint32_t bebop_descriptor_field_index(const bebop_descriptor_field_t* f)
{
  return f && BEBOP_WIRE_IS_SOME(f->index) ? f->index.value : 0;
}

uint32_t bebop_descriptor_field_decorator_count(const bebop_descriptor_field_t* f)
{
  return f && BEBOP_WIRE_IS_SOME(f->decorators) ? (uint32_t)f->decorators.value.length : 0;
}

const bebop_descriptor_usage_t* bebop_descriptor_field_decorator_at(
    const bebop_descriptor_field_t* f, uint32_t idx
)
{
  if (!f || !BEBOP_WIRE_IS_SOME(f->decorators) || idx >= f->decorators.value.length) {
    return NULL;
  }
  return &f->decorators.value.data[idx];
}

const char* bebop_descriptor_member_name(const bebop_descriptor_member_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->name) ? m->name.value.data : NULL;
}

const char* bebop_descriptor_member_documentation(const bebop_descriptor_member_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->documentation) ? m->documentation.value.data : NULL;
}

uint64_t bebop_descriptor_member_value(const bebop_descriptor_member_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->value) ? m->value.value : 0;
}

uint32_t bebop_descriptor_member_decorator_count(const bebop_descriptor_member_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->decorators) ? (uint32_t)m->decorators.value.length : 0;
}

const bebop_descriptor_usage_t* bebop_descriptor_member_decorator_at(
    const bebop_descriptor_member_t* m, uint32_t idx
)
{
  if (!m || !BEBOP_WIRE_IS_SOME(m->decorators) || idx >= m->decorators.value.length) {
    return NULL;
  }
  return &m->decorators.value.data[idx];
}

uint8_t bebop_descriptor_branch_discriminator(const bebop_descriptor_branch_t* b)
{
  return b && BEBOP_WIRE_IS_SOME(b->discriminator) ? b->discriminator.value : 0;
}

const char* bebop_descriptor_branch_documentation(const bebop_descriptor_branch_t* b)
{
  return b && BEBOP_WIRE_IS_SOME(b->documentation) ? b->documentation.value.data : NULL;
}

const char* bebop_descriptor_branch_inline_fqn(const bebop_descriptor_branch_t* b)
{
  return b && BEBOP_WIRE_IS_SOME(b->inline_fqn) ? b->inline_fqn.value.data : NULL;
}

const char* bebop_descriptor_branch_type_ref_fqn(const bebop_descriptor_branch_t* b)
{
  return b && BEBOP_WIRE_IS_SOME(b->type_ref_fqn) ? b->type_ref_fqn.value.data : NULL;
}

const char* bebop_descriptor_branch_name(const bebop_descriptor_branch_t* b)
{
  if (!b) {
    return NULL;
  }
  // Explicit name (type-reference branches)
  if (BEBOP_WIRE_IS_SOME(b->name)) {
    return b->name.value.data;
  }
  // For inline branches, extract name from inline_fqn (last component after dot)
  if (BEBOP_WIRE_IS_SOME(b->inline_fqn)) {
    const char* fqn = b->inline_fqn.value.data;
    const char* last_dot = strrchr(fqn, '.');
    return last_dot ? last_dot + 1 : fqn;
  }
  return NULL;
}

uint32_t bebop_descriptor_branch_decorator_count(const bebop_descriptor_branch_t* b)
{
  return b && BEBOP_WIRE_IS_SOME(b->decorators) ? (uint32_t)b->decorators.value.length : 0;
}

const bebop_descriptor_usage_t* bebop_descriptor_branch_decorator_at(
    const bebop_descriptor_branch_t* b, uint32_t idx
)
{
  if (!b || !BEBOP_WIRE_IS_SOME(b->decorators) || idx >= b->decorators.value.length) {
    return NULL;
  }
  return &b->decorators.value.data[idx];
}

const char* bebop_descriptor_method_name(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->name) ? m->name.value.data : NULL;
}

const char* bebop_descriptor_method_documentation(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->documentation) ? m->documentation.value.data : NULL;
}

const bebop_descriptor_type_t* bebop_descriptor_method_request(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->request_type) ? m->request_type.value : NULL;
}

const bebop_descriptor_type_t* bebop_descriptor_method_response(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->response_type) ? m->response_type.value : NULL;
}

bebop_method_type_t bebop_descriptor_method_type(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->method_type) ? (bebop_method_type_t)m->method_type.value
                                                 : BEBOP_METHOD_UNKNOWN;
}

uint32_t bebop_descriptor_method_id(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->id) ? m->id.value : 0;
}

uint32_t bebop_descriptor_method_decorator_count(const bebop_descriptor_method_t* m)
{
  return m && BEBOP_WIRE_IS_SOME(m->decorators) ? (uint32_t)m->decorators.value.length : 0;
}

const bebop_descriptor_usage_t* bebop_descriptor_method_decorator_at(
    const bebop_descriptor_method_t* m, uint32_t idx
)
{
  if (!m || !BEBOP_WIRE_IS_SOME(m->decorators) || idx >= m->decorators.value.length) {
    return NULL;
  }
  return &m->decorators.value.data[idx];
}

bebop_type_kind_t bebop_descriptor_type_kind(const bebop_descriptor_type_t* t)
{
  return t && BEBOP_WIRE_IS_SOME(t->kind) ? (bebop_type_kind_t)t->kind.value : BEBOP_TYPE_UNKNOWN;
}

const bebop_descriptor_type_t* bebop_descriptor_type_element(const bebop_descriptor_type_t* t)
{
  if (!t) {
    return NULL;
  }
  if (BEBOP_WIRE_IS_SOME(t->array_element)) {
    return t->array_element.value;
  }
  if (BEBOP_WIRE_IS_SOME(t->fixed_array_element)) {
    return t->fixed_array_element.value;
  }
  return NULL;
}

uint32_t bebop_descriptor_type_fixed_size(const bebop_descriptor_type_t* t)
{
  return t && BEBOP_WIRE_IS_SOME(t->fixed_array_size) ? t->fixed_array_size.value : 0;
}

const bebop_descriptor_type_t* bebop_descriptor_type_key(const bebop_descriptor_type_t* t)
{
  return t && BEBOP_WIRE_IS_SOME(t->map_key) ? t->map_key.value : NULL;
}

const bebop_descriptor_type_t* bebop_descriptor_type_value(const bebop_descriptor_type_t* t)
{
  return t && BEBOP_WIRE_IS_SOME(t->map_value) ? t->map_value.value : NULL;
}

const char* bebop_descriptor_type_fqn(const bebop_descriptor_type_t* t)
{
  return t && BEBOP_WIRE_IS_SOME(t->defined_fqn) ? t->defined_fqn.value.data : NULL;
}

bebop_literal_kind_t bebop_descriptor_literal_kind(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->kind) ? (bebop_literal_kind_t)l->kind.value
                                          : BEBOP_LITERAL_UNKNOWN;
}

bool bebop_descriptor_literal_as_bool(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->bool_value) ? l->bool_value.value : false;
}

int64_t bebop_descriptor_literal_as_int(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->int_value) ? l->int_value.value : 0;
}

double bebop_descriptor_literal_as_float(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->float_value) ? l->float_value.value : 0.0;
}

const char* bebop_descriptor_literal_as_string(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->string_value) ? l->string_value.value.data : NULL;
}

const uint8_t* bebop_descriptor_literal_as_uuid(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->uuid_value) ? l->uuid_value.value.bytes : NULL;
}

const char* bebop_descriptor_literal_raw_value(const bebop_descriptor_literal_t* l)
{
  return l && BEBOP_WIRE_IS_SOME(l->raw_value) ? l->raw_value.value.data : NULL;
}

const uint8_t* bebop_descriptor_literal_as_bytes(
    const bebop_descriptor_literal_t* l, size_t* out_len
)
{
  if (!l || !BEBOP_WIRE_IS_SOME(l->bytes_value)) {
    if (out_len) {
      *out_len = 0;
    }
    return NULL;
  }
  if (out_len) {
    *out_len = l->bytes_value.value.length;
  }
  return l->bytes_value.value.data;
}

void bebop_descriptor_literal_as_timestamp(
    const bebop_descriptor_literal_t* l, int64_t* out_seconds, int32_t* out_nanos, int32_t* out_offset_ms
)
{
  if (!l || !BEBOP_WIRE_IS_SOME(l->timestamp_value)) {
    if (out_seconds) {
      *out_seconds = 0;
    }
    if (out_nanos) {
      *out_nanos = 0;
    }
    if (out_offset_ms) {
      *out_offset_ms = 0;
    }
    return;
  }
  if (out_seconds) {
    *out_seconds = l->timestamp_value.value.seconds;
  }
  if (out_nanos) {
    *out_nanos = l->timestamp_value.value.nanos;
  }
  if (out_offset_ms) {
    *out_offset_ms = l->timestamp_value.value.offset_ms;
  }
}

void bebop_descriptor_literal_as_duration(
    const bebop_descriptor_literal_t* l, int64_t* out_seconds, int32_t* out_nanos
)
{
  if (!l || !BEBOP_WIRE_IS_SOME(l->duration_value)) {
    if (out_seconds) {
      *out_seconds = 0;
    }
    if (out_nanos) {
      *out_nanos = 0;
    }
    return;
  }
  if (out_seconds) {
    *out_seconds = l->duration_value.value.seconds;
  }
  if (out_nanos) {
    *out_nanos = l->duration_value.value.nanos;
  }
}

const char* bebop_descriptor_usage_fqn(const bebop_descriptor_usage_t* u)
{
  return u && BEBOP_WIRE_IS_SOME(u->fqn) ? u->fqn.value.data : NULL;
}

uint32_t bebop_descriptor_usage_arg_count(const bebop_descriptor_usage_t* u)
{
  return u && BEBOP_WIRE_IS_SOME(u->args) ? (uint32_t)u->args.value.length : 0;
}

const char* bebop_descriptor_usage_arg_name(const bebop_descriptor_usage_t* u, uint32_t idx)
{
  if (!u || !BEBOP_WIRE_IS_SOME(u->args) || idx >= u->args.value.length) {
    return NULL;
  }
  return u->args.value.data[idx].name.data;
}

const bebop_descriptor_literal_t* bebop_descriptor_usage_arg_value(
    const bebop_descriptor_usage_t* u, uint32_t idx
)
{
  if (!u || !BEBOP_WIRE_IS_SOME(u->args) || idx >= u->args.value.length) {
    return NULL;
  }
  return &u->args.value.data[idx].value;
}

uint32_t bebop_descriptor_usage_export_count(const bebop_descriptor_usage_t* u)
{
  return u && BEBOP_WIRE_IS_SOME(u->export_data) ? (uint32_t)u->export_data.value.length : 0;
}

const char* bebop_descriptor_usage_export_key_at(const bebop_descriptor_usage_t* u, uint32_t idx)
{
  if (!u || !BEBOP_WIRE_IS_SOME(u->export_data) || idx >= u->export_data.value.length) {
    return NULL;
  }
  Bebop_MapIter it;
  Bebop_MapIter_Init(&it, &u->export_data.value);
  void* key = NULL;
  for (uint32_t i = 0; i <= idx; i++) {
    if (!Bebop_MapIter_Next(&it, &key, NULL)) {
      return NULL;
    }
  }
  return key ? ((Bebop_Str*)key)->data : NULL;
}

const bebop_descriptor_literal_t* bebop_descriptor_usage_export_value_at(
    const bebop_descriptor_usage_t* u, uint32_t idx
)
{
  if (!u || !BEBOP_WIRE_IS_SOME(u->export_data) || idx >= u->export_data.value.length) {
    return NULL;
  }
  Bebop_MapIter it;
  Bebop_MapIter_Init(&it, &u->export_data.value);
  void* val = NULL;
  for (uint32_t i = 0; i <= idx; i++) {
    if (!Bebop_MapIter_Next(&it, NULL, &val)) {
      return NULL;
    }
  }
  return (Bebop_LiteralValue*)val;
}

uint32_t bebop_descriptor_location_count(const bebop_descriptor_source_code_info_t* sci)
{
  return sci && BEBOP_WIRE_IS_SOME(sci->locations) ? (uint32_t)sci->locations.value.length : 0;
}

const bebop_descriptor_location_t* bebop_descriptor_location_at(
    const bebop_descriptor_source_code_info_t* sci, uint32_t idx
)
{
  if (!sci || !BEBOP_WIRE_IS_SOME(sci->locations) || idx >= sci->locations.value.length) {
    return NULL;
  }
  return &sci->locations.value.data[idx];
}

const int32_t* bebop_descriptor_location_path(
    const bebop_descriptor_location_t* loc, uint32_t* out_count
)
{
  if (!loc || !BEBOP_WIRE_IS_SOME(loc->path)) {
    if (out_count) {
      *out_count = 0;
    }
    return NULL;
  }
  if (out_count) {
    *out_count = (uint32_t)loc->path.value.length;
  }
  return loc->path.value.data;
}

const int32_t* bebop_descriptor_location_span(const bebop_descriptor_location_t* loc)
{
  return loc && BEBOP_WIRE_IS_SOME(loc->span) ? loc->span.value : NULL;
}

const char* bebop_descriptor_location_leading(const bebop_descriptor_location_t* loc)
{
  return loc && BEBOP_WIRE_IS_SOME(loc->leading_comments) ? loc->leading_comments.value.data : NULL;
}

const char* bebop_descriptor_location_trailing(const bebop_descriptor_location_t* loc)
{
  return loc && BEBOP_WIRE_IS_SOME(loc->trailing_comments) ? loc->trailing_comments.value.data
                                                           : NULL;
}

uint32_t bebop_descriptor_location_detached_count(const bebop_descriptor_location_t* loc)
{
  return loc && BEBOP_WIRE_IS_SOME(loc->detached_comments)
      ? (uint32_t)loc->detached_comments.value.length
      : 0;
}

const char* bebop_descriptor_location_detached_at(
    const bebop_descriptor_location_t* loc, uint32_t idx
)
{
  if (!loc || !BEBOP_WIRE_IS_SOME(loc->detached_comments)
      || idx >= loc->detached_comments.value.length)
  {
    return NULL;
  }
  return loc->detached_comments.value.data[idx].data;
}
