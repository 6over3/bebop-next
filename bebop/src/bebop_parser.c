#define BEBOP_PARSE_CURRENT(p) (&(p)->stream.tokens[(p)->current])
#define BEBOP_PARSE_PREVIOUS(p) (&(p)->stream.tokens[(p)->current - 1])
#define BEBOP_PARSE_CHECK(p, k) (BEBOP_PARSE_CURRENT(p)->kind == (k))
#define BEBOP_PARSE_AT_END(p) BEBOP_PARSE_CHECK((p), BEBOP_TOKEN_EOF)
#define BEBOP_PARSE_ADVANCE(p) \
  (BEBOP_PARSE_AT_END(p) ? BEBOP_PARSE_PREVIOUS(p) : ((p)->current++, BEBOP_PARSE_PREVIOUS(p)))

static inline bebop_token_t* bebop__parse_peek(const bebop_parser_t* p, const uint32_t n)
{
  if (BEBOP_UNLIKELY(p->stream.count == 0)) {
    return NULL;
  }
  const uint32_t idx = n > UINT32_MAX - p->current ? p->stream.count - 1
      : p->current + n >= p->stream.count          ? p->stream.count - 1
                                                   : p->current + n;
  return &p->stream.tokens[idx];
}

static void bebop__parse_fatal(bebop_parser_t* p, const bebop_error_t err, const char* msg)
{
  p->flags |= BEBOP_PARSER_FATAL;
  bebop__context_set_error(p->ctx, err, msg);
}

#define BEBOP_PARSE_IS_FATAL(p) ((p)->flags & BEBOP_PARSER_FATAL)

#define bebop__PARSE_ERROR_FMT(p, tok, code, ...) \
  do { \
    char bebop__msg[512]; \
    snprintf(bebop__msg, sizeof(bebop__msg), __VA_ARGS__); \
    bebop__parse_error_at(p, tok, code, bebop__msg); \
  } while (0)

#define bebop__PARSE_ERROR_HINT_FMT(p, tok, code, hint, ...) \
  do { \
    if (!((p)->flags & (BEBOP_PARSER_PANIC_MODE | BEBOP_PARSER_FATAL))) { \
      (p)->flags |= BEBOP_PARSER_HAD_ERROR | BEBOP_PARSER_PANIC_MODE; \
      char bebop__msg[512]; \
      snprintf(bebop__msg, sizeof(bebop__msg), __VA_ARGS__); \
      bebop__schema_add_diagnostic( \
          (p)->schema, \
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, (code), (tok)->span}, \
          bebop__msg, \
          (hint) \
      ); \
    } \
  } while (0)

static void bebop__parse_error_at(
    bebop_parser_t* p, const bebop_token_t* tok, const uint32_t code, const char* msg
)
{
  if (p->flags & (BEBOP_PARSER_PANIC_MODE | BEBOP_PARSER_FATAL)) {
    return;
  }

  p->flags |= BEBOP_PARSER_HAD_ERROR | BEBOP_PARSER_PANIC_MODE;

  bebop__schema_add_diagnostic(
      p->schema, (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, code, tok->span}, msg, NULL
  );
}

#define BEBOP_PARSE_ERROR_CURRENT(p, code, msg) \
  bebop__parse_error_at((p), BEBOP_PARSE_CURRENT(p), (code), (msg))

#define BEBOP_PARSE_ERROR(p, code, msg) \
  bebop__parse_error_at((p), BEBOP_PARSE_PREVIOUS(p), (code), (msg))

#define BEBOP_PARSE_WARNING(p, tok, code, msg) \
  bebop__schema_add_diagnostic( \
      (p)->schema, (bebop__diag_loc_t) {BEBOP_DIAG_WARNING, (code), (tok)->span}, (msg), NULL \
  )

#define BEBOP_PARSE_CONSUME(p, k, msg) \
  (BEBOP_PARSE_CHECK((p), (k)) \
       ? (BEBOP_PARSE_ADVANCE(p), true) \
       : (BEBOP_PARSE_ERROR_CURRENT((p), BEBOP_DIAG_UNEXPECTED_TOKEN, (msg)), false))

#define BEBOP_PARSE_CONSUME_AFTER(p, k, msg) \
  (BEBOP_PARSE_CHECK((p), (k)) \
       ? (BEBOP_PARSE_ADVANCE(p), true) \
       : (BEBOP_PARSE_ERROR((p), BEBOP_DIAG_UNEXPECTED_TOKEN, (msg)), false))

#define BEBOP_PARSE_MATCH(p, k) \
  (BEBOP_PARSE_CHECK((p), (k)) ? (BEBOP_PARSE_ADVANCE(p), true) : false)

static inline bool bebop__token_is_ident_or_keyword(bebop_token_kind_t kind)
{
  return kind == BEBOP_TOKEN_IDENTIFIER || (kind >= BEBOP_TOKEN_ENUM && kind <= BEBOP_TOKEN_CONST);
}

#define BEBOP_PARSE_CONSUME_NAME(p, msg) \
  (bebop__token_is_ident_or_keyword(BEBOP_PARSE_CURRENT(p)->kind) \
       ? (BEBOP_PARSE_ADVANCE(p), true) \
       : (BEBOP_PARSE_ERROR_CURRENT((p), BEBOP_DIAG_UNEXPECTED_TOKEN, (msg)), false))

static void bebop__parse_synchronize(bebop_parser_t* p)
{
  p->flags &= ~(uint32_t)BEBOP_PARSER_PANIC_MODE;

  BEBOP_PARSE_ADVANCE(p);

  while (!BEBOP_PARSE_AT_END(p)) {
    if (BEBOP_PARSE_PREVIOUS(p)->kind == BEBOP_TOKEN_SEMICOLON) {
      return;
    }

    switch (BEBOP_PARSE_CURRENT(p)->kind) {
      case BEBOP_TOKEN_ENUM:
      case BEBOP_TOKEN_STRUCT:
      case BEBOP_TOKEN_MESSAGE:
      case BEBOP_TOKEN_UNION:
      case BEBOP_TOKEN_SERVICE:
      case BEBOP_TOKEN_CONST:
      case BEBOP_TOKEN_MUT:
      case BEBOP_TOKEN_READONLY:
      case BEBOP_TOKEN_IMPORT:
      case BEBOP_TOKEN_PACKAGE:
      case BEBOP_TOKEN_EXPORT:
      case BEBOP_TOKEN_LOCAL:
      case BEBOP_TOKEN_AT:
        return;
      default:
        break;
    }

    BEBOP_PARSE_ADVANCE(p);
  }
}

static void bebop__parse_synchronize_in_block(bebop_parser_t* p)
{
  p->flags &= ~(uint32_t)BEBOP_PARSER_PANIC_MODE;

  BEBOP_PARSE_ADVANCE(p);

  while (!BEBOP_PARSE_AT_END(p)) {
    if (BEBOP_PARSE_PREVIOUS(p)->kind == BEBOP_TOKEN_SEMICOLON) {
      return;
    }
    if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE)) {
      return;
    }
    if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_AT)) {
      return;
    }
    BEBOP_PARSE_ADVANCE(p);
  }
}

static const char* const bebop__fuzzy_keywords[] = {
#define X(N, s) s,
    BEBOP_KEYWORDS(X)
#undef X
};
#define bebop__FUZZY_KEYWORD_COUNT \
  (sizeof(bebop__fuzzy_keywords) / sizeof(bebop__fuzzy_keywords[0]))

static void bebop__suggest_keyword(
    bebop_parser_t* p, const char* input, size_t input_len, bebop_span_t span
)
{
  const char* suggestion = bebop_util_fuzzy_match(
      input, input_len, bebop__fuzzy_keywords, bebop__FUZZY_KEYWORD_COUNT, 3
  );
  if (suggestion) {
    char buf[64];
    snprintf(buf, sizeof(buf), "did you mean '%s'?", suggestion);
    bebop__schema_diag_add_label(p->schema, span, buf);
  }
}

typedef struct {
  const char* name;
  const char* suggestion;
} bebop__reserved_ident_t;

static const bebop__reserved_ident_t bebop__reserved_identifiers[] = {
#define X(N, s, sug) {s, sug},
    BEBOP_RESERVED_IDENTIFIERS(X)
#undef X
};
#define bebop__RESERVED_IDENT_COUNT BEBOP_COUNTOF(bebop__reserved_identifiers)

static const char* bebop__check_reserved_identifier(const char* name)
{
  for (size_t i = 0; i < bebop__RESERVED_IDENT_COUNT; i++) {
    if (strcmp(name, bebop__reserved_identifiers[i].name) == 0) {
      return bebop__reserved_identifiers[i].suggestion;
    }
  }
  return NULL;
}

static void bebop__check_reserved_name(
    bebop_parser_t* p, const bebop_token_t* tok, bebop_str_t name
)
{
  const char* suggestion = bebop__check_reserved_identifier(BEBOP_STR(p->ctx, name));
  if (suggestion) {
    bebop__PARSE_ERROR_FMT(
        p,
        tok,
        BEBOP_DIAG_RESERVED_IDENTIFIER,
        "'%s' is a reserved identifier",
        BEBOP_STR(p->ctx, name)
    );
    char label[64];
    snprintf(label, sizeof(label), "did you mean '%s'?", suggestion);
    bebop__schema_diag_add_label(p->schema, tok->span, label);
  }
}

#define BEBOP_DECORATOR_BODY_ITEMS(X) \
  X(targets) \
  X(multiple) \
  X(param) \
  X(validate) \
  X(export)

static const char* const bebop__decorator_body_items[] = {
#define X(name) #name,
    BEBOP_DECORATOR_BODY_ITEMS(X)
#undef X
};
#define bebop__DECORATOR_BODY_ITEM_COUNT BEBOP_COUNTOF(bebop__decorator_body_items)

static bool bebop__is_decorator_body_item(bebop_parser_t* p)
{
  if (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_IDENTIFIER)) {
    return false;
  }
  const char* name = BEBOP_STR(p->ctx, BEBOP_PARSE_CURRENT(p)->lexeme);
  const size_t len = BEBOP_STR_LEN(p->ctx, BEBOP_PARSE_CURRENT(p)->lexeme);
  for (size_t i = 0; i < bebop__DECORATOR_BODY_ITEM_COUNT; i++) {
    const char* item = bebop__decorator_body_items[i];
    if (len == strlen(item) && memcmp(name, item, len) == 0) {
      return true;
    }
  }
  return false;
}

static void bebop__parse_synchronize_in_decorator(bebop_parser_t* p)
{
  p->flags &= ~(uint32_t)BEBOP_PARSER_PANIC_MODE;

  int depth = 0;
  while (!BEBOP_PARSE_AT_END(p)) {
    if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_LBRACE)) {
      depth++;
    }
    if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE)) {
      if (depth > 0) {
        depth--;
      } else {
        break;
      }
    }

    if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACKET)) {
      BEBOP_PARSE_ADVANCE(p);
      if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACKET)) {
        BEBOP_PARSE_ADVANCE(p);
        break;
      }
      continue;
    }
    if (depth == 0 && bebop__is_decorator_body_item(p)) {
      break;
    }
    BEBOP_PARSE_ADVANCE(p);
  }
}

static void bebop__suggest_decorator_item(
    bebop_parser_t* p, const char* input, size_t input_len, bebop_span_t span
)
{
  const char* suggestion = bebop_util_fuzzy_match(
      input, input_len, bebop__decorator_body_items, bebop__DECORATOR_BODY_ITEM_COUNT, 3
  );
  if (suggestion) {
    char buf[64];
    snprintf(buf, sizeof(buf), "did you mean '%s'?", suggestion);
    bebop__schema_diag_add_label(p->schema, span, buf);
  }
}

static bebop_span_t bebop__parse_span_from_tokens(
    const bebop_token_t* first, const bebop_token_t* last
)
{
  if (!first) {
    return BEBOP_SPAN_INVALID;
  }
  if (!last) {
    return first->span;
  }

  const uint32_t last_end = last->span.off + last->span.len;
  if (last_end < last->span.off) {
    return first->span;
  }
  const uint32_t len = last_end >= first->span.off ? last_end - first->span.off : first->span.len;
  return (bebop_span_t) {
      .off = first->span.off,
      .len = len,
      .start_line = first->span.start_line,
      .start_col = first->span.start_col,
      .end_line = last->span.end_line,
      .end_col = last->span.end_col,
  };
}

#define BEBOP_PARSE_SET_DEF_SPAN(def, keyword, end) \
  ((def)->span = bebop__parse_span_from_tokens( \
       (keyword), (end) ? (end) : &(bebop_token_t) {.span = (def)->name_span} \
   ))

static bebop_str_t bebop__parse_extract_doc(
    const bebop_parser_t* p, const bebop_trivia_list_t* trivia
)
{
  if (!trivia || trivia->count == 0) {
    return BEBOP_STR_NULL;
  }

  int32_t doc_end = -1;
  int32_t doc_start = -1;

  for (int32_t i = (int32_t)trivia->count - 1; i >= 0; i--) {
    const bebop_trivia_t* t = &trivia->items[i];
    if (t->kind == BEBOP_TRIVIA_DOC_COMMENT) {
      if (doc_end < 0) {
        doc_end = i;
      }
      doc_start = i;
    } else if (t->kind == BEBOP_TRIVIA_NEWLINE || t->kind == BEBOP_TRIVIA_WHITESPACE) {
      if (doc_end < 0) {
        continue;
      }
    } else {
      break;
    }
  }

  if (doc_end < 0) {
    return BEBOP_STR_NULL;
  }

  const bebop_trivia_t* first = &trivia->items[doc_start];
  const char* src = p->source + first->span.off;

  if (first->span.len >= 3 && src[0] == '/' && src[1] == '*' && src[2] == '*') {
    size_t src_len = first->span.len;
    if (src_len < 5) {
      return BEBOP_STR_NULL;
    }

    src += 3;
    src_len -= 5;

    char* out = bebop_arena_new(BEBOP_ARENA(p->ctx), char, src_len + 1);
    if (!out) {
      return BEBOP_STR_NULL;
    }

    size_t out_len = 0;
    const char* end = src + src_len;
    bool at_line_start = true;

    while (src < end) {
      if (at_line_start) {
        while (src < end && BEBOP_IS_WHITESPACE(*src)) {
          src++;
        }
        if (src < end && *src == '*') {
          src++;
          if (src < end && *src == ' ') {
            src++;
          }
        }
        at_line_start = false;
      }
      if (src < end) {
        if (BEBOP_IS_NEWLINE(*src)) {
          out[out_len++] = '\n';
          at_line_start = true;
        } else {
          out[out_len++] = *src;
        }
        src++;
      }
    }

    while (out_len > 0 && BEBOP_IS_BLANK(out[out_len - 1])) {
      out_len--;
    }
    return out_len > 0 ? bebop_intern_n(BEBOP_INTERN(p->ctx), out, out_len) : BEBOP_STR_NULL;
  }

  size_t total_len = 0;
  for (int32_t i = doc_start; i <= doc_end; i++) {
    if (trivia->items[i].kind == BEBOP_TRIVIA_DOC_COMMENT) {
      total_len += trivia->items[i].span.len + 1;
    }
  }

  char* out = bebop_arena_new(BEBOP_ARENA(p->ctx), char, total_len + 1);
  if (!out) {
    return BEBOP_STR_NULL;
  }

  size_t out_len = 0;
  for (int32_t i = doc_start; i <= doc_end; i++) {
    const bebop_trivia_t* t = &trivia->items[i];
    if (t->kind != BEBOP_TRIVIA_DOC_COMMENT) {
      continue;
    }

    src = p->source + t->span.off;
    size_t len = t->span.len;

    if (len >= 3 && src[0] == '/' && src[1] == '/' && src[2] == '/') {
      src += 3;
      len -= 3;
      if (len > 0 && *src == ' ') {
        src++;
        len--;
      }
    }

    while (len > 0 && BEBOP_IS_WHITESPACE(src[len - 1])) {
      len--;
    }
    if (len > 0) {
      if (out_len > 0) {
        out[out_len++] = '\n';
      }
      memcpy(out + out_len, src, len);
      out_len += len;
    }
  }

  return out_len > 0 ? bebop_intern_n(BEBOP_INTERN(p->ctx), out, out_len) : BEBOP_STR_NULL;
}

typedef struct {
  const char* name;
  bebop_type_kind_t kind;
} bebop__scalar_entry_t;

static const bebop__scalar_entry_t bebop__scalar_types[] = {
#define X(name, str, size, is_int) {str, BEBOP_TYPE_##name},
    BEBOP_SCALAR_TYPES(X)
#undef X
#define X(str, kind) {str, kind},
        BEBOP_TYPE_ALIASES(X)
#undef X
};

#define bebop__SCALAR_COUNT BEBOP_COUNTOF(bebop__scalar_types)

static bool bebop__parse_lookup_scalar(
    const char* name, const size_t len, bebop_type_kind_t* out_kind
)
{
  for (size_t i = 0; i < bebop__SCALAR_COUNT; i++) {
    if (bebop_streqn(bebop__scalar_types[i].name, name, len)) {
      *out_kind = bebop__scalar_types[i].kind;
      return true;
    }
  }
  return false;
}

static bool bebop__parse_is_integer_type(const bebop_type_kind_t kind)
{
  switch (kind) {
#define X(name, str, size, is_int) \
  case BEBOP_TYPE_##name: \
    return is_int;
    BEBOP_SCALAR_TYPES(X)
#undef X
    default:
      return false;
  }
}

BEBOP_MAYBE_UNUSED static uint32_t bebop__parse_scalar_wire_size(const bebop_type_kind_t kind)
{
  switch (kind) {
#define X(name, str, size, is_int) \
  case BEBOP_TYPE_##name: \
    return size;
    BEBOP_SCALAR_TYPES(X)
#undef X
    default:
      return 0;
  }
}

static const char* const bebop__param_type_names[] = {
#define X(name, str, size, is_int) str,
    BEBOP_SCALAR_TYPES(X)
#undef X
#define X(str, kind) str,
        BEBOP_TYPE_ALIASES(X)
#undef X
            "type",
};
#define bebop__PARAM_TYPE_NAME_COUNT BEBOP_COUNTOF(bebop__param_type_names)

static void bebop__suggest_param_type(
    bebop_parser_t* p, const char* input, size_t input_len, bebop_span_t span
)
{
  const char* suggestion = bebop_util_fuzzy_match(
      input, input_len, bebop__param_type_names, bebop__PARAM_TYPE_NAME_COUNT, 3
  );
  if (suggestion) {
    char buf[64];
    snprintf(buf, sizeof(buf), "did you mean '%s'?", suggestion);
    bebop__schema_diag_add_label(p->schema, span, buf);
  }
}

static bebop_type_t* bebop__parse_make_scalar_type(
    bebop_parser_t* p, const bebop_type_kind_t kind, const bebop_span_t span
)
{
  bebop_type_t* type = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_type_t, 1);
  if (!type) {
    bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate type");
    return NULL;
  }
  type->kind = kind;
  type->span = span;
  return type;
}

static bebop_type_t* bebop__parse_make_array_type(
    bebop_parser_t* p, bebop_type_t* element, const bebop_span_t span
)
{
  bebop_type_t* type = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_type_t, 1);
  if (!type) {
    bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate array type");
    return NULL;
  }
  type->kind = BEBOP_TYPE_ARRAY;
  type->span = span;
  type->array.element = element;
  return type;
}

static bebop_type_t* bebop__parse_make_fixed_array_type(
    bebop_parser_t* p, bebop_type_t* element, const uint32_t size, const bebop_span_t span
)
{
  bebop_type_t* type = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_type_t, 1);
  if (!type) {
    bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate fixed array type");
    return NULL;
  }
  type->kind = BEBOP_TYPE_FIXED_ARRAY;
  type->span = span;
  type->fixed_array.element = element;
  type->fixed_array.size = size;
  return type;
}

static bebop_type_t* bebop__parse_make_map_type(
    bebop_parser_t* p, bebop_type_t* key, bebop_type_t* value, const bebop_span_t span
)
{
  bebop_type_t* type = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_type_t, 1);
  if (!type) {
    bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate map type");
    return NULL;
  }
  type->kind = BEBOP_TYPE_MAP;
  type->span = span;
  type->map.key = key;
  type->map.value = value;
  return type;
}

static bebop_type_t* bebop__parse_make_defined_type(
    bebop_parser_t* p, const bebop_str_t name, const bebop_span_t span
)
{
  bebop_type_t* type = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_type_t, 1);
  if (!type) {
    bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate defined type");
    return NULL;
  }
  type->kind = BEBOP_TYPE_DEFINED;
  type->span = span;
  type->defined.name = name;
  type->defined.resolved = NULL;
  return type;
}

#define BEBOP_TYPE_STACK_SIZE 128

typedef enum {
  TYPE_STATE_BASE,
  TYPE_STATE_MAP_KEY,
  TYPE_STATE_MAP_VALUE,
  TYPE_STATE_ARRAY_SUFFIX,
  TYPE_STATE_DONE,
} bebop_type_state_t;

typedef struct {
  bebop_type_state_t state;
  bebop_type_t* result;
  bebop_type_t* map_key;
  bebop_token_t* start_tok;
  uint32_t depth;
} bebop_type_frame_t;

static bebop_type_t* bebop__parse_type(bebop_parser_t* p)
{
  bebop_type_frame_t stack[BEBOP_TYPE_STACK_SIZE];
  int sp = 0;

  stack[0] = (bebop_type_frame_t) {
      .state = TYPE_STATE_BASE,
      .result = NULL,
      .map_key = NULL,
      .start_tok = BEBOP_PARSE_CURRENT(p),
      .depth = 0,
  };

  while (sp >= 0) {
    bebop_type_frame_t* frame = &stack[sp];

    if (frame->depth > BEBOP_MAX_TYPE_DEPTH) {
      BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_FIELD, "Type nesting too deep (maximum 64 levels)");
      return NULL;
    }

    switch (frame->state) {
      case TYPE_STATE_BASE: {
        const bebop_token_t* tok = BEBOP_PARSE_CURRENT(p);

        if (tok->kind == BEBOP_TOKEN_MAP) {
          BEBOP_PARSE_ADVANCE(p);
          if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACKET, "Expected '[' after 'map'")) {
            return NULL;
          }

          if (sp + 1 >= BEBOP_TYPE_STACK_SIZE) {
            BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_FIELD, "Type nesting too deep");
            return NULL;
          }
          frame->state = TYPE_STATE_MAP_KEY;
          sp++;
          stack[sp] = (bebop_type_frame_t) {
              .state = TYPE_STATE_BASE,
              .result = NULL,
              .map_key = NULL,
              .start_tok = BEBOP_PARSE_CURRENT(p),
              .depth = frame->depth + 1,
          };
        } else if (tok->kind == BEBOP_TOKEN_ARRAY) {
          BEBOP_PARSE_ERROR_CURRENT(
              p, BEBOP_DIAG_UNEXPECTED_TOKEN, "Use postfix array syntax 'T[]' instead of 'array[T]'"
          );
          return NULL;
        } else if (tok->kind == BEBOP_TOKEN_IDENTIFIER) {
          const char* lex = BEBOP_STR(p->ctx, tok->lexeme);
          if (BEBOP_UNLIKELY(!lex)) {
            BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_UNRECOGNIZED_TYPE, "Invalid type name");
            return NULL;
          }
          const size_t len = BEBOP_STR_LEN(p->ctx, tok->lexeme);

          bebop_type_kind_t kind;
          if (bebop__parse_lookup_scalar(lex, len, &kind)) {
            BEBOP_PARSE_ADVANCE(p);
            frame->result = bebop__parse_make_scalar_type(p, kind, tok->span);
          } else {
            const bebop_token_t* first_tok = BEBOP_PARSE_ADVANCE(p);
            const bebop_token_t* last_tok = first_tok;

            if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_DOT)) {
              char name_buf[512];
              const char* first_str = BEBOP_STR(p->ctx, first_tok->lexeme);
              const size_t first_len = BEBOP_STR_LEN(p->ctx, first_tok->lexeme);
              size_t name_len = first_len;

              if (name_len >= sizeof(name_buf)) {
                BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_QUALIFIED_NAME, "Type name too long");
                return NULL;
              }
              memcpy(name_buf, first_str, name_len);

              while (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_DOT)) {
                if (!BEBOP_PARSE_CONSUME_AFTER(
                        p, BEBOP_TOKEN_IDENTIFIER, "Expected identifier after '.'"
                    )) {
                  return NULL;
                }
                bebop_token_t* part = BEBOP_PARSE_PREVIOUS(p);

                const char* part_str = BEBOP_STR(p->ctx, part->lexeme);
                const size_t part_len = BEBOP_STR_LEN(p->ctx, part->lexeme);

                if (name_len + 1 + part_len >= sizeof(name_buf)) {
                  BEBOP_PARSE_ERROR(
                      p, BEBOP_DIAG_INVALID_QUALIFIED_NAME, "Qualified type name too long"
                  );
                  return NULL;
                }

                name_buf[name_len++] = '.';
                memcpy(name_buf + name_len, part_str, part_len);
                name_len += part_len;
                last_tok = part;
              }

              const bebop_str_t qualified_name =
                  bebop_intern_n(BEBOP_INTERN(p->ctx), name_buf, name_len);
              const bebop_span_t span = bebop__parse_span_from_tokens(first_tok, last_tok);
              frame->result = bebop__parse_make_defined_type(p, qualified_name, span);
            } else {
              frame->result = bebop__parse_make_defined_type(p, first_tok->lexeme, first_tok->span);
            }
          }
          frame->state = TYPE_STATE_ARRAY_SUFFIX;
        } else {
          BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_UNRECOGNIZED_TYPE, "Expected type");
          return NULL;
        }
        break;
      }

      case TYPE_STATE_MAP_KEY: {
        const bebop_type_frame_t* child = &stack[sp + 1];
        frame->map_key = child->result;
        bebop_sema_check_map_key_type(p->sema, frame->map_key, frame->map_key->span);

        if (!BEBOP_PARSE_CONSUME(
                p, BEBOP_TOKEN_COMMA, "Expected ',' between map key and value types"
            ))
        {
          return NULL;
        }

        if (sp + 1 >= BEBOP_TYPE_STACK_SIZE) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_FIELD, "Type nesting too deep");
          return NULL;
        }
        frame->state = TYPE_STATE_MAP_VALUE;
        sp++;
        stack[sp] = (bebop_type_frame_t) {
            .state = TYPE_STATE_BASE,
            .result = NULL,
            .map_key = NULL,
            .start_tok = BEBOP_PARSE_CURRENT(p),
            .depth = frame->depth + 1,
        };
        break;
      }

      case TYPE_STATE_MAP_VALUE: {
        const bebop_type_frame_t* child = &stack[sp + 1];
        bebop_type_t* value = child->result;

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACKET, "Expected ']' after map value type"))
        {
          return NULL;
        }

        const bebop_span_t span =
            bebop__parse_span_from_tokens(frame->start_tok, BEBOP_PARSE_PREVIOUS(p));
        frame->result = bebop__parse_make_map_type(p, frame->map_key, value, span);
        frame->state = TYPE_STATE_ARRAY_SUFFIX;
        break;
      }

      case TYPE_STATE_ARRAY_SUFFIX: {
        if (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_LBRACKET)) {
          frame->depth++;
          if (frame->depth > BEBOP_MAX_TYPE_DEPTH) {
            BEBOP_PARSE_ERROR(
                p, BEBOP_DIAG_INVALID_FIELD, "Type nesting too deep (maximum 64 levels)"
            );
            return NULL;
          }

          if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_NUMBER)) {
            const bebop_token_t* size_tok = BEBOP_PARSE_ADVANCE(p);
            const char* str = BEBOP_STR(p->ctx, size_tok->lexeme);
            const size_t len = BEBOP_STR_LEN(p->ctx, size_tok->lexeme);
            int64_t size_val;

            if (!bebop_util_parse_int(str, len, &size_val) || size_val < 1
                || size_val > BEBOP_MAX_FIXED_ARRAY_SIZE)
            {
              bebop__PARSE_ERROR_FMT(
                  p,
                  size_tok,
                  BEBOP_DIAG_INVALID_FIELD,
                  "Fixed array size must be 1 to %d",
                  BEBOP_MAX_FIXED_ARRAY_SIZE
              );
              return NULL;
            }

            if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_RBRACKET, "Expected ']'")) {
              return NULL;
            }

            const bebop_span_t span =
                bebop__parse_span_from_tokens(frame->start_tok, BEBOP_PARSE_PREVIOUS(p));
            frame->result =
                bebop__parse_make_fixed_array_type(p, frame->result, (uint32_t)size_val, span);
          } else {
            if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_RBRACKET, "Expected ']' for array type")) {
              return NULL;
            }
            const bebop_span_t span =
                bebop__parse_span_from_tokens(frame->start_tok, BEBOP_PARSE_PREVIOUS(p));
            frame->result = bebop__parse_make_array_type(p, frame->result, span);
          }

        } else {
          frame->state = TYPE_STATE_DONE;
        }
        break;
      }

      case TYPE_STATE_DONE: {
        if (sp == 0) {
          return frame->result;
        }

        sp--;
        break;
      }
    }
  }

  return NULL;
}

#define BEBOP_MAX_ENV_VAR_NAME 256

static bebop_str_t bebop__parse_substitute_env_vars(
    bebop_parser_t* p, const bebop_str_t src, bool* had_substitution
)
{
  const char* str = BEBOP_STR(p->ctx, src);
  const size_t len = BEBOP_STR_LEN(p->ctx, src);

  *had_substitution = false;
  if (!str || len == 0) {
    return src;
  }

  const char* dollar = memchr(str, '$', len);
  while (dollar && (size_t)(dollar - str) + 1 < len) {
    if (dollar[1] == '(') {
      break;
    }
    dollar = memchr(dollar + 1, '$', len - (size_t)(dollar - str) - 1);
  }
  if (!dollar || (size_t)(dollar - str) + 1 >= len) {
    return src;
  }

  if (len > SIZE_MAX - BEBOP_MAX_ENV_VAR_NAME) {
    return src;
  }
  size_t result_cap = len + BEBOP_MAX_ENV_VAR_NAME;
  char* result = bebop_arena_alloc(BEBOP_ARENA(p->ctx), result_cap, 1);
  if (!result) {
    return src;
  }
  size_t result_len = 0;

#define NEED_SPACE(n) \
  do { \
    size_t _need = (n); \
    if (_need > SIZE_MAX - result_len) \
      goto fail; \
    while (result_len + _need >= result_cap) { \
      if (result_cap > SIZE_MAX / 2) \
        goto fail; \
      result_cap *= 2; \
      result = bebop_arena_realloc(BEBOP_ARENA(p->ctx), result, result_len, result_cap); \
      if (!result) \
        goto fail; \
    } \
  } while (0)

  size_t i = 0;
  while (i < len) {
    if (str[i] == '$' && i + 1 < len && str[i + 1] == '(') {
      const size_t var_start = i + 2;
      size_t var_end = var_start;

      while (var_end < len && str[var_end] != ')') {
        var_end++;
      }

      const size_t var_len = var_end - var_start;
      if (var_end >= len) {
        BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Unclosed environment variable reference");
        goto fail;
      }
      if (var_len >= BEBOP_MAX_ENV_VAR_NAME) {
        BEBOP_PARSE_ERROR(
            p, BEBOP_DIAG_INVALID_LITERAL, "Environment variable name too long (max 255 characters)"
        );
        goto fail;
      }

      char var_name[BEBOP_MAX_ENV_VAR_NAME];
      memcpy(var_name, str + var_start, var_len);
      var_name[var_len] = '\0';

      const char* value = NULL;
      const bebop_env_t* env = &p->ctx->host.env;
      for (uint32_t ei = 0; ei < env->count; ei++) {
        if (bebop__strcmp(env->entries[ei].key, var_name) == 0) {
          value = env->entries[ei].value;
          break;
        }
      }

      if (value) {
        const size_t value_len = strlen(value);
        NEED_SPACE(value_len);
        memcpy(result + result_len, value, value_len);
        result_len += value_len;
        *had_substitution = true;
      } else {
        bebop__PARSE_ERROR_FMT(
            p,
            BEBOP_PARSE_PREVIOUS(p),
            BEBOP_DIAG_ENV_VAR_NOT_FOUND,
            "Environment variable '%s' not found",
            var_name
        );
        const size_t orig_len = var_end - i + 1;
        NEED_SPACE(orig_len);
        memcpy(result + result_len, str + i, orig_len);
        result_len += orig_len;
      }
      i = var_end + 1;
    } else {
      NEED_SPACE(1);
      result[result_len++] = str[i++];
    }
  }

#undef NEED_SPACE

  return *had_substitution ? bebop_intern_n(BEBOP_INTERN(p->ctx), result, result_len) : src;

fail:
  return src;
}

static bool bebop__parse_number(
    bebop_parser_t* p,
    const bebop__str_view_t str,
    const bebop_type_kind_t expected_type,
    bebop_literal_t* out
)
{
  if (expected_type == BEBOP_TYPE_FLOAT32 || expected_type == BEBOP_TYPE_FLOAT64) {
    double val;
    if (bebop_util_parse_float(str.data, str.len, &val)) {
      out->kind = BEBOP_LITERAL_FLOAT;
      out->float_val = val;
      return true;
    }
  }

  int64_t ival;
  if (bebop_util_parse_int(str.data, str.len, &ival)) {
    out->kind = BEBOP_LITERAL_INT;
    out->int_val = ival;
    return true;
  }

  double fval;
  if (bebop_util_parse_float(str.data, str.len, &fval)) {
    out->kind = BEBOP_LITERAL_FLOAT;
    out->float_val = fval;
    return true;
  }

  BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_NUMBER, "Invalid number literal");
  return false;
}

static bool bebop__parse_literal(
    bebop_parser_t* p, const bebop_type_kind_t expected_type, bebop_literal_t* out
)
{
  const bebop_token_t* tok = BEBOP_PARSE_CURRENT(p);
  memset(out, 0, sizeof(*out));
  out->ctx = p->ctx;
  out->span = tok->span;

  switch (tok->kind) {
    case BEBOP_TOKEN_TRUE:
      BEBOP_PARSE_ADVANCE(p);
      out->kind = BEBOP_LITERAL_BOOL;
      out->bool_val = true;
      return true;

    case BEBOP_TOKEN_FALSE:
      BEBOP_PARSE_ADVANCE(p);
      out->kind = BEBOP_LITERAL_BOOL;
      out->bool_val = false;
      return true;

    case BEBOP_TOKEN_STRING: {
      BEBOP_PARSE_ADVANCE(p);
      out->kind = BEBOP_LITERAL_STRING;
      out->raw_value = tok->lexeme;

      bool had_subst = false;
      out->string_val = bebop__parse_substitute_env_vars(p, tok->lexeme, &had_subst);
      out->has_env_var = had_subst;

      if (expected_type == BEBOP_TYPE_UUID) {
        const char* str = BEBOP_STR(p->ctx, out->string_val);
        const size_t len = BEBOP_STR_LEN(p->ctx, out->string_val);
        if (bebop_util_parse_uuid(str, len, out->uuid_val)) {
          out->kind = BEBOP_LITERAL_UUID;
        } else {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_UUID, "Invalid UUID format");
          return false;
        }
      } else if (expected_type == BEBOP_TYPE_TIMESTAMP) {
        const char* str = BEBOP_STR(p->ctx, out->string_val);
        const size_t len = BEBOP_STR_LEN(p->ctx, out->string_val);
        if (bebop_util_parse_timestamp(
                str, len, &out->timestamp_val.seconds, &out->timestamp_val.nanos
            ))
        {
          out->kind = BEBOP_LITERAL_TIMESTAMP;
        } else {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Invalid timestamp format");
          return false;
        }
      } else if (expected_type == BEBOP_TYPE_DURATION) {
        const char* str = BEBOP_STR(p->ctx, out->string_val);
        const size_t len = BEBOP_STR_LEN(p->ctx, out->string_val);
        if (bebop_util_parse_duration(
                str, len, &out->duration_val.seconds, &out->duration_val.nanos
            ))
        {
          out->kind = BEBOP_LITERAL_DURATION;
        } else {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Invalid duration format");
          return false;
        }
      } else if (expected_type == BEBOP_TYPE_BYTE) {
        const char* str = BEBOP_STR(p->ctx, out->string_val);
        const size_t len = BEBOP_STR_LEN(p->ctx, out->string_val);
        uint8_t* data = bebop_arena_new(BEBOP_ARENA(p->ctx), uint8_t, len);
        if (!data && len > 0) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Failed to allocate bytes literal");
          return false;
        }
        if (len > 0) {
          memcpy(data, str, len);
        }
        out->bytes_val.data = data;
        out->bytes_val.len = len;
        out->kind = BEBOP_LITERAL_BYTES;
      }
      return true;
    }

    case BEBOP_TOKEN_BYTES: {
      BEBOP_PARSE_ADVANCE(p);
      const char* str = BEBOP_STR(p->ctx, tok->lexeme);
      const size_t len = BEBOP_STR_LEN(p->ctx, tok->lexeme);
      uint8_t* data = bebop_arena_new(BEBOP_ARENA(p->ctx), uint8_t, len);
      if (!data && len > 0) {
        BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Failed to allocate bytes literal");
        return false;
      }
      if (len > 0) {
        memcpy(data, str, len);
      }
      out->bytes_val.data = data;
      out->bytes_val.len = len;
      out->kind = BEBOP_LITERAL_BYTES;
      out->span = tok->span;
      return true;
    }

    case BEBOP_TOKEN_NUMBER: {
      BEBOP_PARSE_ADVANCE(p);
      const char* str = BEBOP_STR(p->ctx, tok->lexeme);
      if (BEBOP_UNLIKELY(!str)) {
        BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_NUMBER, "Invalid number literal");
        return false;
      }
      const size_t len = BEBOP_STR_LEN(p->ctx, tok->lexeme);
      out->raw_value = tok->lexeme;
      return bebop__parse_number(p, (bebop__str_view_t) {str, len}, expected_type, out);
    }

    case BEBOP_TOKEN_IDENTIFIER: {
      const char* lex = BEBOP_STR(p->ctx, tok->lexeme);
      if (lex) {
        const size_t len = BEBOP_STR_LEN(p->ctx, tok->lexeme);

        if (bebop_streqni("inf", lex, len) || bebop_streqni("nan", lex, len)) {
          BEBOP_PARSE_ADVANCE(p);
          double val;
          if (bebop_util_parse_float(lex, len, &val)) {
            out->kind = BEBOP_LITERAL_FLOAT;
            out->float_val = val;
            out->raw_value = tok->lexeme;
            return true;
          }
        }
      }
      break;
    }

    case BEBOP_TOKEN_MINUS: {
      BEBOP_PARSE_ADVANCE(p);
      const bebop_token_t* num_tok = BEBOP_PARSE_CURRENT(p);

      if (num_tok->kind == BEBOP_TOKEN_IDENTIFIER) {
        const char* lex = BEBOP_STR(p->ctx, num_tok->lexeme);
        const size_t len = BEBOP_STR_LEN(p->ctx, num_tok->lexeme);

        if (lex && bebop_streqni("inf", lex, len)) {
          BEBOP_PARSE_ADVANCE(p);
          out->kind = BEBOP_LITERAL_FLOAT;
          out->float_val = (double)-INFINITY;
          out->span = bebop__parse_span_from_tokens(tok, BEBOP_PARSE_PREVIOUS(p));
          out->raw_value = bebop_intern_n(&p->ctx->intern, "-inf", 4);
          return true;
        }
      }

      if (num_tok->kind == BEBOP_TOKEN_NUMBER) {
        BEBOP_PARSE_ADVANCE(p);
        const char* str = BEBOP_STR(p->ctx, num_tok->lexeme);
        const size_t len = BEBOP_STR_LEN(p->ctx, num_tok->lexeme);

        if (!str) {
          bebop__context_set_error(p->ctx, BEBOP_ERR_INTERNAL, "Null string in number token");
          return false;
        }

        char buf[BEBOP_SMALL_BUF_SIZE];
        if (len > sizeof(buf) - 2) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_NUMBER, "Number too long");
          return false;
        }
        buf[0] = '-';
        memcpy(buf + 1, str, len);
        buf[len + 1] = '\0';
        out->span = bebop__parse_span_from_tokens(tok, BEBOP_PARSE_PREVIOUS(p));
        out->raw_value = bebop_intern_n(&p->ctx->intern, buf, len + 1);
        return bebop__parse_number(p, (bebop__str_view_t) {buf, len + 1}, expected_type, out);
      }

      BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_INVALID_LITERAL, "Expected number after '-'");
      return false;
    }

    default:
      break;
  }

  BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_INVALID_LITERAL, "Expected literal value");
  return false;
}

static bebop_decorator_arg_t* bebop__parse_decorator_args(bebop_parser_t* p, uint32_t* out_count)
{
  bebop_decorator_arg_t* args = NULL;
  uint32_t count = 0;
  uint32_t capacity = 0;

  while (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RPAREN) && !BEBOP_PARSE_AT_END(p)) {
    if (count > 0) {
      if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_COMMA, "Expected ',' between decorator arguments")) {
        break;
      }
    }

    bebop_str_t arg_name = BEBOP_STR_NULL;
    bebop_span_t name_span = BEBOP_SPAN_INVALID;

    const bebop_token_t* peek_tok = bebop__parse_peek(p, 1);
    if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_IDENTIFIER)
        && peek_tok && peek_tok->kind == BEBOP_TOKEN_COLON)
    {
      const bebop_token_t* name_tok = BEBOP_PARSE_ADVANCE(p);
      arg_name = name_tok->lexeme;
      name_span = name_tok->span;
      BEBOP_PARSE_ADVANCE(p);
    }

    bebop_literal_t value;
    if (!bebop__parse_literal(p, BEBOP_TYPE_STRING, &value)) {
      break;
    }

    bebop_decorator_arg_t* arg =
        BEBOP_ARRAY_PUSH(BEBOP_ARENA(p->ctx), args, count, capacity, bebop_decorator_arg_t);
    if (!arg) {
      break;
    }

    arg->name = arg_name;
    arg->span = bebop_str_is_null(arg_name)
        ? value.span
        : bebop__parse_span_from_tokens(
              &(bebop_token_t) {.span = name_span}, &(bebop_token_t) {.span = value.span}
          );
    arg->value = value;
  }

  *out_count = count;
  return args;
}

static bebop_decorator_t* bebop__parse_decorators(bebop_parser_t* p)
{
  bebop_decorator_t* head = NULL;
  bebop_decorator_t** tail = &head;

  while (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_AT)) {
    const bebop_token_t* at = BEBOP_PARSE_ADVANCE(p);
    if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_IDENTIFIER, "Expected decorator name")) {
      while (!BEBOP_PARSE_AT_END(p) && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_AT)
             && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_STRUCT)
             && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_MESSAGE)
             && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_ENUM) && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_UNION)
             && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_SERVICE)
             && !BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_CONST))
      {
        BEBOP_PARSE_ADVANCE(p);
      }
      continue;
    }
    bebop_token_t* name = BEBOP_PARSE_PREVIOUS(p);

    bebop_str_t dec_name = name->lexeme;

    while (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_DOT)) {
      BEBOP_PARSE_ADVANCE(p);
      if (!BEBOP_PARSE_CONSUME_AFTER(
              p, BEBOP_TOKEN_IDENTIFIER, "Expected identifier after '.'")) {
        break;
      }
      bebop_token_t* next = BEBOP_PARSE_PREVIOUS(p);

      const char* left = BEBOP_STR(p->ctx, dec_name);
      const size_t left_len = BEBOP_STR_LEN(p->ctx, dec_name);
      const char* right = BEBOP_STR(p->ctx, next->lexeme);
      const size_t right_len = BEBOP_STR_LEN(p->ctx, next->lexeme);
      char* buf = bebop__join_dotted(
          BEBOP_ARENA(p->ctx),
          (bebop__str_view_t) {left, left_len},
          (bebop__str_view_t) {right, right_len}
      );
      if (!buf) {
        break;
      }
      dec_name = bebop_intern_n(BEBOP_INTERN(p->ctx), buf, left_len + 1 + right_len);
    }

    bebop_decorator_arg_t* args = NULL;
    uint32_t arg_count = 0;

    if (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_LPAREN)) {
      args = bebop__parse_decorator_args(p, &arg_count);
      if (!BEBOP_PARSE_CONSUME_AFTER(
              p, BEBOP_TOKEN_RPAREN, "Expected ')' after decorator arguments"
          ))
      {
        return head;
      }
    }

    bebop_decorator_t* dec = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_decorator_t, 1);
    if (!dec) {
      bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate decorator");
      return head;
    }

    dec->name = dec_name;
    dec->span = bebop__parse_span_from_tokens(at, BEBOP_PARSE_PREVIOUS(p));
    dec->args = args;
    dec->arg_count = arg_count;
    dec->next = NULL;
    dec->schema = p->schema;
    dec->resolved = NULL;

    for (uint32_t i = 0; i < arg_count; i++) {
      args[i].decorator = dec;
    }

    *tail = dec;
    tail = &dec->next;
  }

  return head;
}

typedef enum {
  bebop__OP_NONE = 0,
  bebop__OP_OR = 1,
  bebop__OP_AND = 2,
  bebop__OP_SHIFT = 3,
} bebop__op_t;

static bool bebop__parse_lookup_enum_member(
    const bebop_parser_t* p, const bebop_def_t* enum_def, const bebop_str_t name, int64_t* out_val
)
{
  if (enum_def && enum_def->kind == BEBOP_DEF_ENUM) {
    for (uint32_t i = 0; i < enum_def->enum_def.member_count; i++) {
      if (bebop_str_eq(enum_def->enum_def.members[i].name, name)) {
        *out_val = (int64_t)enum_def->enum_def.members[i].value;
        return true;
      }
    }
  }

  for (const bebop_def_t* def = p->schema->definitions; def != NULL; def = def->next) {
    if (def->kind != BEBOP_DEF_ENUM) {
      continue;
    }

    for (uint32_t j = 0; j < def->enum_def.member_count; j++) {
      if (bebop_str_eq(def->enum_def.members[j].name, name)) {
        *out_val = (int64_t)def->enum_def.members[j].value;
        return true;
      }
    }
  }

  return false;
}

#define bebop__EXPR_STACK_SIZE 32

typedef enum {
  EXPR_STATE_OPERAND,
  EXPR_STATE_OPERATOR,
} bebop__expr_state_t;

typedef struct {
  int64_t value_stack[bebop__EXPR_STACK_SIZE];
  bebop__op_t op_stack[bebop__EXPR_STACK_SIZE];
  int value_top;
  int op_top;
  bool negate_next;
} bebop__expr_frame_t;

static int64_t bebop__parse_apply_op(const int64_t a, const int64_t b, const bebop__op_t op)
{
  switch (op) {
    case bebop__OP_OR:
      return a | b;
    case bebop__OP_AND:
      return a & b;
    case bebop__OP_SHIFT:
      if (b < 0 || b >= 64) {
        return 0;
      }
      return a << b;
    default:
      return 0;
  }
}

static int64_t bebop__parse_expression(bebop_parser_t* p, bebop_def_t* enum_def)
{
  bebop__expr_frame_t frames[bebop__EXPR_STACK_SIZE];
  int frame_top = 0;

  frames[0].value_top = -1;
  frames[0].op_top = -1;
  frames[0].negate_next = false;

  bebop__expr_state_t state = EXPR_STATE_OPERAND;

  while (frame_top >= 0) {
    bebop__expr_frame_t* f = &frames[frame_top];

    if (state == EXPR_STATE_OPERAND) {
      bool negate = f->negate_next;
      f->negate_next = false;

      if (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_MINUS)) {
        negate = !negate;
      }

      if (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_LPAREN)) {
        if (frame_top + 1 >= bebop__EXPR_STACK_SIZE) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Expression too deeply nested");
          return 0;
        }
        f->negate_next = negate;
        frame_top++;
        frames[frame_top].value_top = -1;
        frames[frame_top].op_top = -1;
        frames[frame_top].negate_next = false;
        continue;
      }

      int64_t val = 0;
      if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_NUMBER)) {
        const bebop_token_t* tok = BEBOP_PARSE_ADVANCE(p);
        const char* str = BEBOP_STR(p->ctx, tok->lexeme);
        if (BEBOP_UNLIKELY(!str)) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_NUMBER, "Invalid integer literal");
          return 0;
        }
        const size_t len = BEBOP_STR_LEN(p->ctx, tok->lexeme);
        if (!bebop_util_parse_int(str, len, &val)) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_NUMBER, "Invalid integer literal");
          return 0;
        }
      } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_IDENTIFIER)) {
        const bebop_token_t* tok = BEBOP_PARSE_ADVANCE(p);
        if (!bebop__parse_lookup_enum_member(p, enum_def, tok->lexeme, &val)) {
          const char* name = BEBOP_STR(p->ctx, tok->lexeme);
          bebop__PARSE_ERROR_FMT(
              p, tok, BEBOP_DIAG_INVALID_LITERAL, "Unknown enum member '%s'", name ? name : ""
          );
          return 0;
        }
      } else {
        BEBOP_PARSE_ERROR_CURRENT(
            p, BEBOP_DIAG_INVALID_LITERAL, "Expected number, identifier, or '(' in expression"
        );
        return 0;
      }

      if (negate) {
        val = -val;
      }

      if (f->value_top >= bebop__EXPR_STACK_SIZE - 1) {
        BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Expression too complex");
        return 0;
      }
      f->value_stack[++f->value_top] = val;
      state = EXPR_STATE_OPERATOR;

    } else {
      bebop__op_t op = bebop__OP_NONE;
      if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_PIPE)) {
        op = bebop__OP_OR;
      } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_AMPERSAND)) {
        op = bebop__OP_AND;
      } else {
        const bebop_token_t* peek_tok = bebop__parse_peek(p, 1);
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_LANGLE)
            && peek_tok && peek_tok->kind == BEBOP_TOKEN_LANGLE)
        {
          op = bebop__OP_SHIFT;
        }
      }

      if (op == bebop__OP_NONE) {
        while (f->op_top >= 0) {
          if (f->value_top < 1) {
            BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Invalid expression: missing operand");
            return 0;
          }
          const int64_t b = f->value_stack[f->value_top--];
          const int64_t a = f->value_stack[f->value_top--];
          f->value_stack[++f->value_top] = bebop__parse_apply_op(a, b, f->op_stack[f->op_top--]);
        }

        int64_t result = f->value_top >= 0 ? f->value_stack[0] : 0;

        if (frame_top == 0) {
          return result;
        }

        BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_RPAREN, "Expected ')'");
        frame_top--;
        f = &frames[frame_top];

        if (f->negate_next) {
          result = -result;
          f->negate_next = false;
        }

        if (f->value_top >= bebop__EXPR_STACK_SIZE - 1) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Expression too complex");
          return 0;
        }
        f->value_stack[++f->value_top] = result;
        continue;
      }

      while (f->op_top >= 0 && f->op_stack[f->op_top] >= op) {
        if (f->value_top < 1) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Invalid expression: missing operand");
          return 0;
        }
        const int64_t b = f->value_stack[f->value_top--];
        const int64_t a = f->value_stack[f->value_top--];
        f->value_stack[++f->value_top] = bebop__parse_apply_op(a, b, f->op_stack[f->op_top--]);
      }

      if (f->op_top >= bebop__EXPR_STACK_SIZE - 1) {
        BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_LITERAL, "Expression too complex");
        return 0;
      }
      f->op_stack[++f->op_top] = op;

      if (op == bebop__OP_SHIFT) {
        BEBOP_PARSE_ADVANCE(p);
        BEBOP_PARSE_ADVANCE(p);
      } else {
        BEBOP_PARSE_ADVANCE(p);
      }
      state = EXPR_STATE_OPERAND;
    }
  }

  return 0;
}

typedef enum {
  PARSE_STATE_FILE,
  PARSE_STATE_DEFINITION,
  PARSE_STATE_IMPORT,
  PARSE_STATE_EDITION,
  PARSE_STATE_PACKAGE,

  PARSE_STATE_ENUM_START,
  PARSE_STATE_ENUM_BODY,
  PARSE_STATE_ENUM_MEMBER,

  PARSE_STATE_STRUCT_START,
  PARSE_STATE_STRUCT_BODY,
  PARSE_STATE_STRUCT_FIELD,

  PARSE_STATE_MESSAGE_START,
  PARSE_STATE_MESSAGE_BODY,
  PARSE_STATE_MESSAGE_FIELD,

  PARSE_STATE_UNION_START,
  PARSE_STATE_UNION_BODY,
  PARSE_STATE_UNION_BRANCH,

  PARSE_STATE_SERVICE_START,
  PARSE_STATE_SERVICE_BODY,
  PARSE_STATE_SERVICE_METHOD,

  PARSE_STATE_CONST_START,
} bebop_parse_state_t;

typedef struct {
  bebop_parse_state_t state;
  bebop_def_t* def;
  bebop_token_t* keyword;
  bebop_decorator_t* decorators;
  uint32_t count;
  uint32_t capacity;
  bool is_mutable;
  bebop_visibility_t visibility;
  bebop_def_t* nested_parent;
} bebop_parse_frame_t;

#define BEBOP_PARSE_STACK_SIZE 64

static bool bebop__is_def_keyword(const bebop_token_kind_t kind)
{
  return kind == BEBOP_TOKEN_STRUCT || kind == BEBOP_TOKEN_MESSAGE || kind == BEBOP_TOKEN_ENUM
      || kind == BEBOP_TOKEN_UNION;
}

static bool bebop__parse_is_nested_def_start(bebop_parser_t* p)
{
  const bebop_token_kind_t kind = BEBOP_PARSE_CURRENT(p)->kind;

  if (bebop__is_def_keyword(kind)) {
    return true;
  }

  if (kind == BEBOP_TOKEN_EXPORT || kind == BEBOP_TOKEN_LOCAL) {
    if (p->current + 1 < p->stream.count) {
      const bebop_token_kind_t next = p->stream.tokens[p->current + 1].kind;
      if (bebop__is_def_keyword(next) || next == BEBOP_TOKEN_MUT) {
        return true;
      }
    }
    return false;
  }

  if (kind == BEBOP_TOKEN_MUT) {
    return true;
  }

  if (kind == BEBOP_TOKEN_AT) {
    const uint32_t save_pos = p->current;
    int depth = 0;
    bool after_at = false;

    while (!BEBOP_PARSE_AT_END(p)) {
      const bebop_token_t* tok = BEBOP_PARSE_CURRENT(p);

      if (tok->kind == BEBOP_TOKEN_AT && depth == 0) {
        BEBOP_PARSE_ADVANCE(p);
        after_at = true;
        continue;
      }

      if (after_at && tok->kind == BEBOP_TOKEN_IDENTIFIER && depth == 0) {
        BEBOP_PARSE_ADVANCE(p);
        continue;
      }

      if (tok->kind == BEBOP_TOKEN_DOT && depth == 0) {
        BEBOP_PARSE_ADVANCE(p);
        after_at = true;
        continue;
      }

      if (tok->kind == BEBOP_TOKEN_LPAREN) {
        depth++;
        after_at = false;
        BEBOP_PARSE_ADVANCE(p);
        continue;
      }

      if (tok->kind == BEBOP_TOKEN_RPAREN) {
        depth--;
        BEBOP_PARSE_ADVANCE(p);
        if (depth == 0) {
          after_at = false;
          continue;
        }
        continue;
      }

      if (depth > 0) {
        BEBOP_PARSE_ADVANCE(p);
        continue;
      }

      after_at = false;
      const bool is_def = bebop__is_def_keyword(tok->kind) || tok->kind == BEBOP_TOKEN_EXPORT
          || tok->kind == BEBOP_TOKEN_LOCAL || tok->kind == BEBOP_TOKEN_MUT;

      p->current = save_pos;
      return is_def;
    }

    p->current = save_pos;
  }

  return false;
}

static bebop_token_t* bebop__parse_def_name(bebop_parser_t* p, const char* expected_context)
{
  if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_IDENTIFIER, expected_context)) {
    return NULL;
  }
  return BEBOP_PARSE_PREVIOUS(p);
}

static void bebop__parse_check_duplicate_def(bebop_parser_t* p, bebop_def_t* def)
{
  if (!def || bebop_str_is_null(def->name)) {
    return;
  }

  const bebop_def_t* existing = bebop__schema_find_def(p->schema, def->name);
  if (existing) {
    const char* name = BEBOP_STR(p->ctx, def->name);
    bebop__PARSE_ERROR_FMT(
        p,
        &(bebop_token_t) {.span = def->name_span},
        BEBOP_DIAG_MULTIPLE_DEFINITIONS,
        "A definition named '%s' already exists",
        name ? name : ""
    );
    bebop__schema_diag_add_label(p->schema, existing->name_span, "first defined here");
  }
}

static bebop_enum_member_t* bebop__parse_enum_member_inline(
    bebop_parser_t* p, bebop_def_t* enum_def, uint32_t* count, uint32_t* capacity
)
{
  bebop_decorator_t* decorators = bebop__parse_decorators(p);

  if (!BEBOP_PARSE_CONSUME_NAME(p, "Expected enum member name")) {
    return NULL;
  }
  bebop_token_t* name = BEBOP_PARSE_PREVIOUS(p);

  bebop__check_reserved_name(p, name, name->lexeme);
  bebop_sema_check_duplicate_name(p->sema, name->lexeme, name->span);

  const bebop_str_t doc = bebop__parse_extract_doc(p, &name->leading);
  const bebop_span_t name_span = name->span;

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_EQUALS, "Expected '=' after enum member name")) {
    return NULL;
  }

  const bebop_token_t* value_start = BEBOP_PARSE_CURRENT(p);
  const int64_t value = bebop__parse_expression(p, enum_def);
  bebop_span_t value_span;
  if (BEBOP_PARSE_PREVIOUS(p) < value_start) {
    value_span = value_start->span;
  } else {
    value_span = bebop__parse_span_from_tokens(value_start, BEBOP_PARSE_PREVIOUS(p));
  }

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_SEMICOLON, "Expected ';' after enum member value"))
  {
    return NULL;
  }

  bebop_enum_member_t* member = BEBOP_ARRAY_PUSH(
      BEBOP_ARENA(p->ctx), enum_def->enum_def.members, *count, *capacity, bebop_enum_member_t
  );
  if (!member) {
    return NULL;
  }

  member->name = name->lexeme;
  member->span = bebop__parse_span_from_tokens(
      decorators ? &(bebop_token_t) {.span = decorators->span} : name, BEBOP_PARSE_PREVIOUS(p)
  );
  member->name_span = name_span;
  member->value_span = value_span;
  member->value_expr =
      bebop_intern_n(&p->ctx->intern, p->schema->source + value_span.off, value_span.len);
  member->documentation = doc;
  member->decorators = decorators;
  member->value = (uint64_t)value;
  member->parent = enum_def;

  enum_def->enum_def.member_count = *count;

  bebop_sema_check_enum_member(p->sema, member, enum_def);

  return member;
}

typedef struct {
  uint32_t* count;
  uint32_t* capacity;
} bebop__field_state_t;

static bebop_field_t* bebop__parse_field_inline(
    bebop_parser_t* p, bebop_def_t* parent, bebop__field_state_t* fields, const bool is_message
)
{
  bebop_decorator_t* decorators = bebop__parse_decorators(p);
  const bebop_token_t* first_tok =
      decorators ? &(bebop_token_t) {.span = decorators->span} : BEBOP_PARSE_CURRENT(p);

  if (!BEBOP_PARSE_CONSUME_NAME(p, "Expected field name")) {
    return NULL;
  }
  bebop_token_t* name = BEBOP_PARSE_PREVIOUS(p);

  uint32_t index = 0;
  if (is_message) {
    if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LPAREN, "Expected '(' after field name")) {
      return NULL;
    }

    if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_NUMBER, "Expected field index")) {
      return NULL;
    }
    bebop_token_t* idx_tok = BEBOP_PARSE_PREVIOUS(p);

    const char* idx_str = BEBOP_STR(p->ctx, idx_tok->lexeme);
    const size_t idx_len = BEBOP_STR_LEN(p->ctx, idx_tok->lexeme);
    int64_t idx_val;
    if (!bebop_util_parse_int(idx_str, idx_len, &idx_val)) {
      BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_FIELD_INDEX, "Invalid field index");
      return NULL;
    }
    if (idx_val < 1 || idx_val > BEBOP_MAX_FIELD_INDEX) {
      BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_FIELD_INDEX, "Field index must be 1-255");
      return NULL;
    }
    index = (uint32_t)idx_val;

    bebop_sema_check_field_index(p->sema, index, idx_tok->span);

    if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RPAREN, "Expected ')' after field index")) {
      return NULL;
    }
  }

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_COLON, "Expected ':' after field name")) {
    return NULL;
  }

  bebop_type_t* type = bebop__parse_type(p);
  if (!type) {
    return NULL;
  }

  if (!is_message && parent->kind == BEBOP_DEF_STRUCT) {
    bebop_sema_check_self_reference(p->sema, type, type->span);
  }

  if (bebop_str_eq(name->lexeme, parent->name)) {
    bebop__parse_error_at(
        p,
        name,
        BEBOP_DIAG_RESERVED_IDENTIFIER,
        "Field name cannot be the same as the containing definition name"
    );
  }

  bebop__check_reserved_name(p, name, name->lexeme);
  bebop_sema_check_duplicate_name(p->sema, name->lexeme, name->span);

  bebop_str_t doc = BEBOP_STR_NULL;
  if (!decorators) {
    doc = bebop__parse_extract_doc(p, &first_tok->leading);
  }
  if (bebop_str_is_null(doc)) {
    doc = bebop__parse_extract_doc(p, &name->leading);
  }

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_SEMICOLON, "Expected ';' after field")) {
    return NULL;
  }

  bebop_field_t** fields_ptr =
      is_message ? &parent->message_def.fields : &parent->struct_def.fields;
  bebop_field_t* field = BEBOP_ARRAY_PUSH(
      BEBOP_ARENA(p->ctx), *fields_ptr, *fields->count, *fields->capacity, bebop_field_t
  );
  if (!field) {
    return NULL;
  }

  field->name = name->lexeme;
  field->span = bebop__parse_span_from_tokens(
      decorators ? &(bebop_token_t) {.span = decorators->span} : first_tok, BEBOP_PARSE_PREVIOUS(p)
  );
  field->name_span = name->span;
  field->documentation = doc;
  field->decorators = decorators;
  field->type = type;
  field->index = index;
  field->parent = parent;

  return field;
}

static bebop_method_t* bebop__parse_method_inline(
    bebop_parser_t* p, bebop_def_t* service_def, uint32_t* count, uint32_t* capacity
)
{
  bebop_decorator_t* decorators = bebop__parse_decorators(p);
  if (!BEBOP_PARSE_CONSUME_NAME(p, "Expected method name")) {
    return NULL;
  }
  bebop_token_t* name = BEBOP_PARSE_PREVIOUS(p);

  bebop__check_reserved_name(p, name, name->lexeme);
  bebop_sema_check_duplicate_name(p->sema, name->lexeme, name->span);

  const bebop_str_t doc = bebop__parse_extract_doc(p, &name->leading);

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LPAREN, "Expected '(' after method name")) {
    return NULL;
  }

  const bool request_stream = BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_STREAM);
  bebop_type_t* request_type = bebop__parse_type(p);
  if (!request_type) {
    return NULL;
  }

  bebop_sema_check_service_type(p->sema, request_type, request_type->span);

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RPAREN, "Expected ')' after request type")) {
    return NULL;
  }
  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_COLON, "Expected ':' after ')'")) {
    return NULL;
  }

  const bool response_stream = BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_STREAM);
  bebop_type_t* response_type = bebop__parse_type(p);
  if (!response_type) {
    return NULL;
  }

  bebop_sema_check_service_type(p->sema, response_type, response_type->span);

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_SEMICOLON, "Expected ';' after method")) {
    return NULL;
  }

  bebop_method_type_t method_type;
  if (!request_stream && !response_stream) {
    method_type = BEBOP_METHOD_UNARY;
  } else if (!request_stream && response_stream) {
    method_type = BEBOP_METHOD_SERVER_STREAM;
  } else if (request_stream && !response_stream) {
    method_type = BEBOP_METHOD_CLIENT_STREAM;
  } else {
    method_type = BEBOP_METHOD_DUPLEX_STREAM;
  }

  const char* svc_name = BEBOP_STR(p->ctx, service_def->name);
  const char* mth_name = BEBOP_STR(p->ctx, name->lexeme);
  char path[512];
  const int len =
      snprintf(path, sizeof(path), "/%s/%s", svc_name ? svc_name : "", mth_name ? mth_name : "");
  uint32_t method_id = 0;
  if (len > 0 && (size_t)len < sizeof(path)) {
    method_id = bebop_util_hash_method_id(path, (size_t)len);
  } else if (len >= (int)sizeof(path)) {
    bebop__PARSE_ERROR_FMT(
        p,
        name,
        BEBOP_DIAG_INVALID_FIELD,
        "Method path too long (truncated): '%s.%s'",
        svc_name ? svc_name : "",
        mth_name ? mth_name : ""
    );
  }

  for (uint32_t i = 0; i < *count; i++) {
    if (service_def->service_def.methods[i].id == method_id) {
      const char* other_name = BEBOP_STR(p->ctx, service_def->service_def.methods[i].name);
      bebop__PARSE_ERROR_FMT(
          p,
          name,
          BEBOP_DIAG_DUPLICATE_METHOD_ID,
          "Method '%s' has same computed ID (0x%08x) as '%s'",
          mth_name ? mth_name : "",
          method_id,
          other_name ? other_name : ""
      );
      bebop__schema_diag_add_label(
          p->schema, service_def->service_def.methods[i].name_span, "conflicts with this method"
      );
      return NULL;
    }
  }

  bebop_method_t* method = BEBOP_ARRAY_PUSH(
      BEBOP_ARENA(p->ctx), service_def->service_def.methods, *count, *capacity, bebop_method_t
  );
  if (!method) {
    return NULL;
  }

  method->name = name->lexeme;
  method->span = bebop__parse_span_from_tokens(
      decorators ? &(bebop_token_t) {.span = decorators->span} : name, BEBOP_PARSE_PREVIOUS(p)
  );
  method->name_span = name->span;
  method->documentation = doc;
  method->decorators = decorators;
  method->request_type = request_type;
  method->response_type = response_type;
  method->method_type = method_type;
  method->id = method_id;
  method->parent = service_def;

  return method;
}

static bool bebop__is_keyword_kind(const bebop_token_kind_t kind)
{
  switch (kind) {
#define X(NAME, str) case BEBOP_TOKEN_##NAME:
    BEBOP_KEYWORDS(X)
#undef X
    return true;
    default:
      return false;
  }
}

static bool bebop__is_name_token(const bebop_token_t* tok)
{
  return tok->kind == BEBOP_TOKEN_IDENTIFIER || bebop__is_keyword_kind(tok->kind);
}

typedef struct {
  const char* name;
  bebop_decorator_target_t target;
} bebop__target_entry_t;

static const bebop__target_entry_t bebop__target_constants[] = {
#define X(NAME, bit) {#NAME, (bebop_decorator_target_t)(1 << (bit))},
    BEBOP_DECORATOR_TARGETS(X)
#undef X
        {"ALL", BEBOP_TARGET_ALL},
};

#define bebop__TARGET_COUNT BEBOP_COUNTOF(bebop__target_constants)

static const char* const bebop__target_names[] = {
#define X(NAME, bit) #NAME,
    BEBOP_DECORATOR_TARGETS(X)
#undef X
        "ALL",
};
#define bebop__TARGET_NAME_COUNT BEBOP_COUNTOF(bebop__target_names)

static void bebop__suggest_target(
    bebop_parser_t* p, const char* input, size_t input_len, bebop_span_t span
)
{
  const char* suggestion =
      bebop_util_fuzzy_match(input, input_len, bebop__target_names, bebop__TARGET_NAME_COUNT, 3);
  if (suggestion) {
    char buf[64];
    snprintf(buf, sizeof(buf), "did you mean '%s'?", suggestion);
    bebop__schema_diag_add_label(p->schema, span, buf);
  }
}

static bebop_decorator_target_t bebop__parse_target_constant(const char* name, const size_t len)
{
  for (size_t i = 0; i < bebop__TARGET_COUNT; i++) {
    if (bebop_streqn(bebop__target_constants[i].name, name, len)) {
      return bebop__target_constants[i].target;
    }
  }
  return BEBOP_TARGET_NONE;
}

static bebop_decorator_target_t bebop__parse_target_expr(bebop_parser_t* p)
{
  bebop_decorator_target_t result = BEBOP_TARGET_NONE;

  if (!BEBOP_PARSE_CONSUME(p,
                           BEBOP_TOKEN_IDENTIFIER,
                           "Expected target constant (STRUCT, MESSAGE, ENUM, "
                           "UNION, FIELD, SERVICE, METHOD, BRANCH, or ALL)")) {
    return BEBOP_TARGET_NONE;
  }
  bebop_token_t* tok = BEBOP_PARSE_PREVIOUS(p);

  const char* name = BEBOP_STR(p->ctx, tok->lexeme);
  size_t len = BEBOP_STR_LEN(p->ctx, tok->lexeme);
  bebop_decorator_target_t target = bebop__parse_target_constant(name, len);
  if (target == BEBOP_TARGET_NONE) {
    bebop__PARSE_ERROR_FMT(p, tok, BEBOP_DIAG_INVALID_MACRO, "Unknown target constant '%s'", name);
    bebop__suggest_target(p, name, len, tok->span);
    return BEBOP_TARGET_NONE;
  }
  result = target;

  while (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_PIPE)) {
    if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_IDENTIFIER, "Expected target constant after '|'"))
    {
      return result;
    }
    tok = BEBOP_PARSE_PREVIOUS(p);

    name = BEBOP_STR(p->ctx, tok->lexeme);
    len = BEBOP_STR_LEN(p->ctx, tok->lexeme);
    target = bebop__parse_target_constant(name, len);
    if (target == BEBOP_TARGET_NONE) {
      bebop__PARSE_ERROR_FMT(
          p, tok, BEBOP_DIAG_INVALID_MACRO, "Unknown target constant '%s'", name
      );
      bebop__suggest_target(p, name, len, tok->span);
      return result;
    }
    result |= target;
  }

  return result;
}

static const char* bebop__parse_lua_block(
    bebop_parser_t* p, size_t* len_out, bebop_span_t* span_out, const char* block_name
)
{
  const bebop_token_t* tok = BEBOP_PARSE_CURRENT(p);
  if (tok->kind != BEBOP_TOKEN_RAW_BLOCK) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Expected '[[' to start %s block", block_name);
    BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_INVALID_MACRO, msg);
    return NULL;
  }
  BEBOP_PARSE_ADVANCE(p);

  const char* content = BEBOP_STR(p->ctx, tok->lexeme);
  const size_t content_len = BEBOP_STR_LEN(p->ctx, tok->lexeme);

  *span_out = (bebop_span_t) {
      .off = tok->span.off + 2,
      .len = (uint32_t)content_len,
      .start_line = tok->span.start_line,
      .start_col = tok->span.start_col + 2,
  };
  *len_out = content_len;

  return content;
}

static bool bebop__parse_macro_param(bebop_parser_t* p, bebop_macro_param_def_t* param)
{
  memset(param, 0, sizeof(*param));

  const bebop_token_t* param_keyword = BEBOP_PARSE_PREVIOUS(p);
  param->description = bebop__parse_extract_doc(p, &param_keyword->leading);
  param->span = param_keyword->span;

  const bebop_token_t* name = BEBOP_PARSE_CURRENT(p);
  if (!bebop__is_name_token(name)) {
    BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_INVALID_MACRO, "Expected parameter name after 'param'");
    return false;
  }
  BEBOP_PARSE_ADVANCE(p);
  param->name = name->lexeme;

  if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_BANG)) {
    BEBOP_PARSE_ADVANCE(p);
    param->required = true;
  } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_QUESTION)) {
    BEBOP_PARSE_ADVANCE(p);
    param->required = false;
  } else {
    BEBOP_PARSE_ERROR_CURRENT(
        p,
        BEBOP_DIAG_INVALID_MACRO,
        "Expected '!' (required) or '?' (optional) after parameter name"
    );
    return false;
  }

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_COLON, "Expected ':' after parameter modifier")) {
    return false;
  }

  if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_IDENTIFIER, "Expected parameter type")) {
    return false;
  }
  bebop_token_t* type_tok = BEBOP_PARSE_PREVIOUS(p);

  const char* type_name = BEBOP_STR(p->ctx, type_tok->lexeme);
  const size_t type_len = BEBOP_STR_LEN(p->ctx, type_tok->lexeme);

  if (BEBOP_STREQ(type_name, type_len, "type")) {
    param->type = BEBOP_TYPE_DEFINED;
  } else {
    bebop_type_kind_t kind;
    if (!bebop__parse_lookup_scalar(type_name, type_len, &kind)) {
      bebop__PARSE_ERROR_FMT(
          p, type_tok, BEBOP_DIAG_INVALID_MACRO, "Unknown parameter type '%s'", type_name
      );
      bebop__suggest_param_type(p, type_name, type_len, type_tok->span);
      return false;
    }
    param->type = kind;
  }

  if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_EQUALS)) {
    BEBOP_PARSE_ADVANCE(p);
    param->default_value = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_literal_t, 1);
    if (!param->default_value) {
      return false;
    }
    if (!bebop__parse_literal(p, param->type, param->default_value)) {
      bebop__parse_error_at(
          p, BEBOP_PARSE_CURRENT(p), BEBOP_DIAG_INVALID_MACRO, "Invalid default value for parameter"
      );
      return false;
    }
  }

  if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_IDENTIFIER)) {
    const char* kw = BEBOP_STR(p->ctx, BEBOP_PARSE_CURRENT(p)->lexeme);
    const size_t kw_len = BEBOP_STR_LEN(p->ctx, BEBOP_PARSE_CURRENT(p)->lexeme);
    if (BEBOP_STREQ(kw, kw_len, "in")) {
      BEBOP_PARSE_ADVANCE(p);

      if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACKET, "Expected '[' after 'in'")) {
        return false;
      }

      uint32_t capacity = 8;
      param->allowed_values = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_literal_t, capacity);
      if (!param->allowed_values) {
        return false;
      }
      param->allowed_value_count = 0;

      while (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACKET) && !BEBOP_PARSE_AT_END(p)) {
        if (param->allowed_value_count >= capacity) {
          const uint32_t new_cap = capacity * 2;
          bebop_literal_t* new_arr = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_literal_t, new_cap);
          if (!new_arr) {
            return false;
          }
          memcpy(
              new_arr, param->allowed_values, sizeof(bebop_literal_t) * param->allowed_value_count
          );
          param->allowed_values = new_arr;
          capacity = new_cap;
        }

        if (!bebop__parse_literal(
                p, param->type, &param->allowed_values[param->allowed_value_count]
            ))
        {
          BEBOP_PARSE_ERROR_CURRENT(
              p, BEBOP_DIAG_INVALID_MACRO, "Invalid value in 'in [...]' constraint list"
          );
          return false;
        }
        param->allowed_value_count++;

        if (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACKET)) {
          if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_COMMA, "Expected ',' or ']' in value list")) {
            return false;
          }
        }
      }

      if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_RBRACKET, "Expected ']' to close value list")) {
        return false;
      }
    }
  }

  return true;
}

static void bebop__parse_macro_decorator(bebop_parser_t* p)
{
  const bebop_token_t* hash_tok = BEBOP_PARSE_ADVANCE(p);

  const bebop_str_t description = bebop__parse_extract_doc(p, &hash_tok->leading);

  const bebop_token_t* decorator_kw = BEBOP_PARSE_CURRENT(p);
  if (decorator_kw->kind != BEBOP_TOKEN_IDENTIFIER) {
    BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_INVALID_MACRO, "Expected 'decorator' after '#'");
    bebop__parse_synchronize(p);
    return;
  }
  const char* kw_str = BEBOP_STR(p->ctx, decorator_kw->lexeme);
  const size_t kw_len = BEBOP_STR_LEN(p->ctx, decorator_kw->lexeme);
  if (!BEBOP_STREQ(kw_str, kw_len, "decorator")) {
    bebop__PARSE_ERROR_FMT(
        p,
        decorator_kw,
        BEBOP_DIAG_INVALID_MACRO,
        "Expected 'decorator' after '#', got '%s'",
        kw_str ? kw_str : ""
    );
    bebop__parse_synchronize(p);
    return;
  }
  BEBOP_PARSE_ADVANCE(p);

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LPAREN, "Expected '(' after '#decorator'")) {
    bebop__parse_synchronize(p);
    return;
  }

  if (!BEBOP_PARSE_CONSUME(
          p, BEBOP_TOKEN_IDENTIFIER, "Expected decorator name inside '#decorator(...)'")) {
    bebop__parse_synchronize(p);
    return;
  }
  bebop_token_t* name_tok = BEBOP_PARSE_PREVIOUS(p);

  const bebop_def_t* existing = bebop__schema_find_def(p->schema, name_tok->lexeme);
  if (existing) {
    bebop__PARSE_ERROR_FMT(
        p,
        name_tok,
        BEBOP_DIAG_DUPLICATE_MACRO_DECORATOR,
        "Decorator '%s' is already defined",
        BEBOP_STR(p->ctx, name_tok->lexeme)
    );
    bebop__schema_diag_add_label(p->schema, existing->name_span, "first defined here");
    bebop__parse_synchronize(p);
    return;
  }

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RPAREN, "Expected ')' after decorator name")) {
    bebop__parse_synchronize(p);
    return;
  }

  if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{' to begin decorator body")) {
    bebop__parse_synchronize(p);
    return;
  }

  bebop_decorator_target_t targets = BEBOP_TARGET_NONE;
  bool has_targets = false;
  bool allow_multiple = false;
  bebop_span_t validate_span = BEBOP_SPAN_INVALID;
  bebop_span_t export_span = BEBOP_SPAN_INVALID;

  bebop_macro_param_def_t params[32];
  uint32_t param_count = 0;

  while (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) && !BEBOP_PARSE_AT_END(p)) {
    const bebop_token_t* item_tok = BEBOP_PARSE_CURRENT(p);

    if (!bebop__is_name_token(item_tok)) {
      bebop__PARSE_ERROR_FMT(
          p,
          item_tok,
          BEBOP_DIAG_INVALID_MACRO,
          "Unexpected token '%s' in decorator body",
          bebop_token_kind_name(item_tok->kind)
      );
      bebop__parse_synchronize_in_decorator(p);
      continue;
    }

    const char* item_name = BEBOP_STR(p->ctx, item_tok->lexeme);
    const size_t item_len = BEBOP_STR_LEN(p->ctx, item_tok->lexeme);

    if (BEBOP_STREQ(item_name, item_len, "targets")) {
      BEBOP_PARSE_ADVANCE(p);
      if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_EQUALS, "Expected '=' after 'targets'")) {
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }
      targets = bebop__parse_target_expr(p);
      has_targets = true;

    } else if (BEBOP_STREQ(item_name, item_len, "multiple")) {
      BEBOP_PARSE_ADVANCE(p);
      if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_EQUALS, "Expected '=' after 'multiple'")) {
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }
      if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_TRUE)) {
        BEBOP_PARSE_ADVANCE(p);
        allow_multiple = true;
      } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_FALSE)) {
        BEBOP_PARSE_ADVANCE(p);
        allow_multiple = false;
      } else {
        BEBOP_PARSE_ERROR_CURRENT(
            p, BEBOP_DIAG_INVALID_MACRO, "Expected 'true' or 'false' after 'multiple ='"
        );
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }

    } else if (BEBOP_STREQ(item_name, item_len, "param")) {
      BEBOP_PARSE_ADVANCE(p);
      if (param_count >= 32) {
        BEBOP_PARSE_ERROR_CURRENT(p, BEBOP_DIAG_INVALID_MACRO, "Too many parameters (maximum 32)");
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }
      if (!bebop__parse_macro_param(p, &params[param_count])) {
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }
      param_count++;

    } else if (BEBOP_STREQ(item_name, item_len, "validate")) {
      BEBOP_PARSE_ADVANCE(p);
      size_t validate_len;
      const char* validate_src =
          bebop__parse_lua_block(p, &validate_len, &validate_span, "validate");
      if (!validate_src) {
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }

    } else if (BEBOP_STREQ(item_name, item_len, "export")) {
      BEBOP_PARSE_ADVANCE(p);
      size_t export_len;
      const char* export_src = bebop__parse_lua_block(p, &export_len, &export_span, "export");
      if (!export_src) {
        bebop__parse_synchronize_in_decorator(p);
        continue;
      }

    } else {
      bebop__PARSE_ERROR_FMT(
          p, item_tok, BEBOP_DIAG_INVALID_MACRO, "Unknown decorator body item '%s'", item_name
      );
      bebop__suggest_decorator_item(p, item_name, item_len, item_tok->span);
      bebop__parse_synchronize_in_decorator(p);
      continue;
    }
  }

  if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_RBRACE, "Expected '}' to close decorator body")) {
    return;
  }
  bebop_token_t* close_brace = BEBOP_PARSE_PREVIOUS(p);

  if (!has_targets) {
    bebop__parse_error_at(
        p,
        name_tok,
        BEBOP_DIAG_INVALID_MACRO,
        "Decorator definition missing required 'targets' field"
    );
    return;
  }

  bebop_def_t* def = bebop_arena_new1(BEBOP_ARENA(p->ctx), bebop_def_t);
  if (!def) {
    bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate decorator def");
    return;
  }

  def->kind = BEBOP_DEF_DECORATOR;
  def->name = name_tok->lexeme;
  def->name_span = name_tok->span;
  def->span = bebop__parse_span_from_tokens(hash_tok, close_brace);
  def->documentation = description;
  def->decorator_def.targets = targets;
  def->decorator_def.allow_multiple = allow_multiple;
  def->decorator_def.validate_span = validate_span;
  def->decorator_def.export_span = export_span;
  def->decorator_def.validate_ref = BEBOP_LUA_NOREF;
  def->decorator_def.export_ref = BEBOP_LUA_NOREF;

  if (param_count > 0) {
    def->decorator_def.params =
        bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_macro_param_def_t, param_count);
    if (!def->decorator_def.params) {
      return;
    }
    memcpy(def->decorator_def.params, params, sizeof(bebop_macro_param_def_t) * param_count);
    def->decorator_def.param_count = param_count;
  }

  bebop__schema_add_def(p->schema, def);
}

static void bebop__parse_file(bebop_parser_t* p)
{
  bebop_parse_frame_t stack[BEBOP_PARSE_STACK_SIZE];
  int sp = 0;

  stack[0] = (bebop_parse_frame_t) {
      .state = PARSE_STATE_FILE,
      .def = NULL,
      .keyword = NULL,
      .decorators = NULL,
      .count = 0,
      .capacity = 0,
      .is_mutable = false,
  };

  while (sp >= 0 && !BEBOP_PARSE_IS_FATAL(p)) {
    bebop_parse_frame_t* frame = &stack[sp];

    switch (frame->state) {
      case PARSE_STATE_FILE: {
        if (BEBOP_PARSE_AT_END(p)) {
          sp--;
          continue;
        }

        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_IMPORT)) {
          if (p->preamble_state < BEBOP_PREAMBLE_IMPORTS) {
            p->preamble_state = BEBOP_PREAMBLE_IMPORTS;
          }
          frame->state = PARSE_STATE_IMPORT;
        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_EDITION)) {
          if (p->preamble_state >= BEBOP_PREAMBLE_IMPORTS) {
            BEBOP_PARSE_ERROR_CURRENT(
                p,
                BEBOP_DIAG_INVALID_EDITION,
                "Edition declaration must come before imports and definitions"
            );
          }
          if (p->preamble_state >= BEBOP_PREAMBLE_PACKAGE) {
            BEBOP_PARSE_ERROR_CURRENT(
                p, BEBOP_DIAG_INVALID_EDITION, "Edition declaration must come before package"
            );
          }
          frame->state = PARSE_STATE_EDITION;
        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_PACKAGE)) {
          if (p->preamble_state >= BEBOP_PREAMBLE_IMPORTS) {
            BEBOP_PARSE_ERROR_CURRENT(
                p, BEBOP_DIAG_PACKAGE_AFTER_IMPORT, "Package declaration must come before imports"
            );
          }
          if (p->preamble_state >= BEBOP_PREAMBLE_DONE) {
            BEBOP_PARSE_ERROR_CURRENT(
                p,
                BEBOP_DIAG_PACKAGE_AFTER_DEFINITION,
                "Package declaration must come before definitions"
            );
          }
          frame->state = PARSE_STATE_PACKAGE;
        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_HASH)) {
          if (p->preamble_state < BEBOP_PREAMBLE_DONE) {
            p->preamble_state = BEBOP_PREAMBLE_DONE;
          }
          bebop__parse_macro_decorator(p);

        } else {
          if (p->preamble_state < BEBOP_PREAMBLE_DONE) {
            p->preamble_state = BEBOP_PREAMBLE_DONE;
          }
          frame->state = PARSE_STATE_DEFINITION;
        }
        break;
      }

      case PARSE_STATE_IMPORT: {
        BEBOP_PARSE_ADVANCE(p);
        if (BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_STRING, "Expected import path string")) {
          bebop_token_t* path_tok = BEBOP_PARSE_PREVIOUS(p);
          bebop__schema_add_import(p->schema, path_tok->lexeme, path_tok->span);
        }
        BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_SEMICOLON);
        frame->state = PARSE_STATE_FILE;
        if (p->flags & BEBOP_PARSER_PANIC_MODE) {
          bebop__parse_synchronize(p);
        }
        break;
      }

      case PARSE_STATE_EDITION: {
        BEBOP_PARSE_ADVANCE(p);
        BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_EQUALS, "Expected '=' after 'edition'");
        if (BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_STRING, "Expected edition year string")) {
          bebop_token_t* year_tok = BEBOP_PARSE_PREVIOUS(p);
          const char* year_str = BEBOP_STR(p->ctx, year_tok->lexeme);
          size_t year_len = BEBOP_STR_LEN(p->ctx, year_tok->lexeme);
          if (year_str && BEBOP_STREQ(year_str, year_len, "2026")) {
            p->schema->edition = BEBOP_ED_2026;
          } else {
            bebop__PARSE_ERROR_FMT(
                p, year_tok, BEBOP_DIAG_INVALID_EDITION, "Unsupported edition (supported: \"2026\")"
            );
          }
        }
        p->preamble_state = BEBOP_PREAMBLE_EDITION;
        BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_SEMICOLON);
        frame->state = PARSE_STATE_FILE;
        if (p->flags & BEBOP_PARSER_PANIC_MODE) {
          bebop__parse_synchronize(p);
        }
        break;
      }

      case PARSE_STATE_PACKAGE: {
        bebop_token_t* pkg_tok = BEBOP_PARSE_ADVANCE(p);

        if (!bebop_str_is_null(p->schema->package)) {
          bebop__PARSE_ERROR_FMT(
              p,
              BEBOP_PARSE_PREVIOUS(p),
              BEBOP_DIAG_DUPLICATE_PACKAGE,
              "Duplicate package declaration"
          );
          bebop__schema_diag_add_label(p->schema, p->schema->package_span, "first declared here");
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_IDENTIFIER, "Expected package name")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }
        bebop_token_t* first_tok = BEBOP_PARSE_PREVIOUS(p);

        char pkg_buf[512];
        const char* first_str = BEBOP_STR(p->ctx, first_tok->lexeme);
        size_t first_len = BEBOP_STR_LEN(p->ctx, first_tok->lexeme);
        size_t pkg_len = first_len;

        if (pkg_len >= sizeof(pkg_buf)) {
          BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_QUALIFIED_NAME, "Package name too long");
          frame->state = PARSE_STATE_FILE;
          break;
        }
        memcpy(pkg_buf, first_str, pkg_len);

        bebop_token_t* last_tok = first_tok;
        while (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_DOT)) {
          bebop_token_t* part = BEBOP_PARSE_CURRENT(p);
          bool is_ident_or_keyword = part->kind == BEBOP_TOKEN_IDENTIFIER
              || (part->kind >= BEBOP_TOKEN_ENUM && part->kind <= BEBOP_TOKEN_CONST);
          if (!is_ident_or_keyword) {
            BEBOP_PARSE_ERROR_CURRENT(
                p, BEBOP_DIAG_UNEXPECTED_TOKEN, "Expected identifier after '.'"
            );
            frame->state = PARSE_STATE_FILE;
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize(p);
            }
            break;
          }
          BEBOP_PARSE_ADVANCE(p);

          const char* part_str = BEBOP_STR(p->ctx, part->lexeme);
          size_t part_len = BEBOP_STR_LEN(p->ctx, part->lexeme);

          if (pkg_len + 1 + part_len >= sizeof(pkg_buf)) {
            BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_QUALIFIED_NAME, "Package name too long");
            frame->state = PARSE_STATE_FILE;
            break;
          }

          pkg_buf[pkg_len++] = '.';
          memcpy(pkg_buf + pkg_len, part_str, part_len);
          pkg_len += part_len;
          last_tok = part;
        }

        if (!(p->flags & BEBOP_PARSER_PANIC_MODE)) {
          BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_SEMICOLON);
          p->schema->package = bebop_intern_n(BEBOP_INTERN(p->ctx), pkg_buf, pkg_len);
          p->schema->package_span = bebop__parse_span_from_tokens(pkg_tok, last_tok);
          p->preamble_state = BEBOP_PREAMBLE_PACKAGE;
        }

        frame->state = PARSE_STATE_FILE;
        if (p->flags & BEBOP_PARSER_PANIC_MODE) {
          bebop__parse_synchronize(p);
        }
        break;
      }

      case PARSE_STATE_DEFINITION: {
        bebop_decorator_t* decorators = bebop__parse_decorators(p);
        bool is_nested = frame->nested_parent != NULL;

        bebop_visibility_t visibility = BEBOP_VIS_DEFAULT;
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_EXPORT)) {
          bebop_token_t* vis_tok = BEBOP_PARSE_ADVANCE(p);
          visibility = BEBOP_VIS_EXPORT;

          if (!is_nested) {
            BEBOP_PARSE_WARNING(
                p,
                vis_tok,
                BEBOP_DIAG_REDUNDANT_EXPORT_WARNING,
                "'export' on top-level definition is redundant " "(top-level is exported by "
                                                                 "default)"
            );
          }
        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_LOCAL)) {
          bebop_token_t* vis_tok = BEBOP_PARSE_ADVANCE(p);
          visibility = BEBOP_VIS_LOCAL;

          if (is_nested) {
            BEBOP_PARSE_WARNING(p,
                                vis_tok,
                                BEBOP_DIAG_REDUNDANT_LOCAL_WARNING,
                                "'local' on nested definition is redundant "
                                "(nested definitions are local by default)");
          }
        }

        bool is_mutable = BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_MUT);

        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_READONLY)) {
          BEBOP_PARSE_WARNING(
              p,
              BEBOP_PARSE_CURRENT(p),
              BEBOP_DIAG_DEPRECATED_FEATURE_WARNING,
              "The 'readonly' modifier is deprecated. Structs are immutable by "
              "default; use 'mut' for mutable structs.");
          BEBOP_PARSE_ADVANCE(p);
        }

        bebop_token_kind_t kind = BEBOP_PARSE_CURRENT(p)->kind;

        if (is_mutable && kind != BEBOP_TOKEN_STRUCT) {
          const char* def_kind = bebop_token_kind_name(kind);
          bebop__PARSE_ERROR_FMT(
              p,
              BEBOP_PARSE_CURRENT(p),
              BEBOP_DIAG_INVALID_FIELD,
              "'mut' cannot be applied to %s",
              def_kind ? def_kind : "this definition"
          );
        }

        frame->decorators = decorators;
        frame->is_mutable = is_mutable;
        frame->visibility = visibility;

        switch (kind) {
          case BEBOP_TOKEN_ENUM:
            frame->state = PARSE_STATE_ENUM_START;
            break;
          case BEBOP_TOKEN_STRUCT:
            frame->state = PARSE_STATE_STRUCT_START;
            break;
          case BEBOP_TOKEN_MESSAGE:
            frame->state = PARSE_STATE_MESSAGE_START;
            break;
          case BEBOP_TOKEN_UNION:
            frame->state = PARSE_STATE_UNION_START;
            break;
          case BEBOP_TOKEN_SERVICE:
            frame->state = PARSE_STATE_SERVICE_START;
            break;
          case BEBOP_TOKEN_CONST:
            frame->state = PARSE_STATE_CONST_START;
            break;
          default:
            if (kind == BEBOP_TOKEN_IDENTIFIER) {
              bebop_token_t* tok = BEBOP_PARSE_CURRENT(p);
              const char* lex = BEBOP_STR(p->ctx, tok->lexeme);
              bebop__PARSE_ERROR_FMT(
                  p, tok, BEBOP_DIAG_UNEXPECTED_TOKEN, "Unrecognized keyword '%s'", lex ? lex : "?"
              );
              bebop__suggest_keyword(p, lex, lex ? strlen(lex) : 0, tok->span);
            } else {
              BEBOP_PARSE_ERROR_CURRENT(
                  p,
                  BEBOP_DIAG_UNEXPECTED_TOKEN,
                  "Expected definition (enum, struct, " "message, union, service, or const)"
              );
            }
            frame->state = PARSE_STATE_FILE;
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize(p);
            }
            break;
        }
        break;
      }

      case PARSE_STATE_ENUM_START: {
        bebop_token_t* keyword = BEBOP_PARSE_ADVANCE(p);
        frame->keyword = keyword;

        bebop_str_t doc = bebop__parse_extract_doc(p, &keyword->leading);
        bebop_token_t* name = bebop__parse_def_name(p, "Expected enum name");
        if (!name) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_def_t* def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
        if (!def) {
          bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate definition");
          break;
        }

        def->kind = BEBOP_DEF_ENUM;
        def->name = name->lexeme;
        def->name_span = name->span;
        def->documentation = doc;
        def->decorators = frame->decorators;
        def->schema = p->schema;
        def->parent = frame->nested_parent;
        def->visibility = frame->visibility;
        def->enum_def.base_type = BEBOP_TYPE_UINT32;

        bebop__check_reserved_name(p, name, name->lexeme);

        if (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_COLON)) {
          if (BEBOP_PARSE_CONSUME_AFTER(
                  p, BEBOP_TOKEN_IDENTIFIER, "Expected integer type after ':'")) {
            bebop_token_t* base_tok = BEBOP_PARSE_PREVIOUS(p);
            const char* base_str = BEBOP_STR(p->ctx, base_tok->lexeme);
            size_t base_len = BEBOP_STR_LEN(p->ctx, base_tok->lexeme);
            bebop_type_kind_t base_kind;
            if (base_str && bebop__parse_lookup_scalar(base_str, base_len, &base_kind)
                && bebop__parse_is_integer_type(base_kind))
            {
              if (base_kind == BEBOP_TYPE_INT128 || base_kind == BEBOP_TYPE_UINT128) {
                BEBOP_PARSE_ERROR(
                    p,
                    BEBOP_DIAG_UNRECOGNIZED_TYPE,
                    "int128/uint128 cannot be used as enum base type"
                );
              } else {
                def->enum_def.base_type = base_kind;
              }
            } else {
              BEBOP_PARSE_ERROR(
                  p, BEBOP_DIAG_UNRECOGNIZED_TYPE, "Expected integer type for enum base"
              );
            }
          }
        }

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{'")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        frame->def = def;
        frame->count = 0;
        frame->capacity = 0;
        frame->state = PARSE_STATE_ENUM_BODY;

        bebop_sema_enter_def(p->sema, def);
        break;
      }

      case PARSE_STATE_ENUM_BODY: {
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) || BEBOP_PARSE_AT_END(p)) {
          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          frame->def->enum_def.member_count = frame->count;
          BEBOP_PARSE_SET_DEF_SPAN(frame->def, frame->keyword, rbrace);

          bebop_sema_check_enum_complete(p->sema, frame->def);
          bebop__parse_check_duplicate_def(p, frame->def);

          if (frame->nested_parent) {
            bebop__def_add_nested(frame->nested_parent, frame->def);
            sp--;
          } else {
            bebop__schema_add_def(p->schema, frame->def);
            frame->state = PARSE_STATE_FILE;
            frame->def = NULL;
            frame->decorators = NULL;
          }
        } else {
          bebop__parse_enum_member_inline(p, frame->def, &frame->count, &frame->capacity);
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
        }
        break;
      }

      case PARSE_STATE_STRUCT_START: {
        bebop_token_t* keyword = BEBOP_PARSE_ADVANCE(p);
        frame->keyword = keyword;

        bebop_str_t doc = bebop__parse_extract_doc(p, &keyword->leading);
        bebop_token_t* name = bebop__parse_def_name(p, "Expected struct name");
        if (!name) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_def_t* def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
        if (!def) {
          bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate definition");
          break;
        }

        def->kind = BEBOP_DEF_STRUCT;
        def->name = name->lexeme;
        def->name_span = name->span;
        def->documentation = doc;
        def->decorators = frame->decorators;
        def->schema = p->schema;
        def->parent = frame->nested_parent;
        def->visibility = frame->visibility;
        def->struct_def.is_mutable = frame->is_mutable;

        bebop__check_reserved_name(p, name, name->lexeme);

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{'")) {
          frame->state = frame->nested_parent ? PARSE_STATE_FILE : PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        frame->def = def;
        frame->count = 0;
        frame->capacity = 0;
        frame->state = PARSE_STATE_STRUCT_BODY;

        bebop_sema_enter_def(p->sema, def);
        break;
      }

      case PARSE_STATE_STRUCT_BODY: {
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) || BEBOP_PARSE_AT_END(p)) {
          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          frame->def->struct_def.field_count = frame->count;
          BEBOP_PARSE_SET_DEF_SPAN(frame->def, frame->keyword, rbrace);

          bebop__parse_check_duplicate_def(p, frame->def);

          if (frame->nested_parent) {
            bebop__def_add_nested(frame->nested_parent, frame->def);
            sp--;
          } else {
            bebop__schema_add_def(p->schema, frame->def);
            frame->state = PARSE_STATE_FILE;
            frame->def = NULL;
            frame->decorators = NULL;
          }
        } else if (bebop__parse_is_nested_def_start(p)) {
          if (sp + 1 >= BEBOP_PARSE_STACK_SIZE) {
            bebop__parse_fatal(
                p, BEBOP_ERR_INTERNAL, "Parser stack overflow (too many nested definitions)"
            );
            break;
          }
          sp++;
          stack[sp] = (bebop_parse_frame_t) {
              .state = PARSE_STATE_DEFINITION,
              .def = NULL,
              .keyword = NULL,
              .decorators = NULL,
              .count = 0,
              .capacity = 0,
              .is_mutable = false,
              .visibility = BEBOP_VIS_DEFAULT,
              .nested_parent = frame->def,
          };
        } else {
          bebop__parse_field_inline(
              p, frame->def, &(bebop__field_state_t) {&frame->count, &frame->capacity}, false
          );
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
        }
        break;
      }

      case PARSE_STATE_MESSAGE_START: {
        bebop_token_t* keyword = BEBOP_PARSE_ADVANCE(p);
        frame->keyword = keyword;

        bebop_str_t doc = bebop__parse_extract_doc(p, &keyword->leading);
        bebop_token_t* name = bebop__parse_def_name(p, "Expected message name");
        if (!name) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_def_t* def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
        if (!def) {
          bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate definition");
          break;
        }

        def->kind = BEBOP_DEF_MESSAGE;
        def->name = name->lexeme;
        def->name_span = name->span;
        def->documentation = doc;
        def->decorators = frame->decorators;
        def->schema = p->schema;
        def->parent = frame->nested_parent;
        def->visibility = frame->visibility;

        bebop__check_reserved_name(p, name, name->lexeme);

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{'")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        frame->def = def;
        frame->count = 0;
        frame->capacity = 0;
        frame->state = PARSE_STATE_MESSAGE_BODY;

        bebop_sema_enter_def(p->sema, def);
        break;
      }

      case PARSE_STATE_MESSAGE_BODY: {
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) || BEBOP_PARSE_AT_END(p)) {
          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          frame->def->message_def.field_count = frame->count;
          BEBOP_PARSE_SET_DEF_SPAN(frame->def, frame->keyword, rbrace);

          bebop__parse_check_duplicate_def(p, frame->def);

          if (frame->nested_parent) {
            bebop__def_add_nested(frame->nested_parent, frame->def);
            sp--;
          } else {
            bebop__schema_add_def(p->schema, frame->def);
            frame->state = PARSE_STATE_FILE;
            frame->def = NULL;
            frame->decorators = NULL;
          }
        } else if (bebop__parse_is_nested_def_start(p)) {
          if (sp + 1 >= BEBOP_PARSE_STACK_SIZE) {
            bebop__parse_fatal(
                p, BEBOP_ERR_INTERNAL, "Parser stack overflow (too many nested definitions)"
            );
            break;
          }
          sp++;
          stack[sp] = (bebop_parse_frame_t) {
              .state = PARSE_STATE_DEFINITION,
              .def = NULL,
              .keyword = NULL,
              .decorators = NULL,
              .count = 0,
              .capacity = 0,
              .is_mutable = false,
              .visibility = BEBOP_VIS_DEFAULT,
              .nested_parent = frame->def,
          };
        } else {
          bebop__parse_field_inline(
              p, frame->def, &(bebop__field_state_t) {&frame->count, &frame->capacity}, true
          );
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
        }
        break;
      }

      case PARSE_STATE_UNION_START: {
        bebop_token_t* keyword = BEBOP_PARSE_ADVANCE(p);
        frame->keyword = keyword;

        bebop_str_t doc = bebop__parse_extract_doc(p, &keyword->leading);
        bebop_token_t* name = bebop__parse_def_name(p, "Expected union name");
        if (!name) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_def_t* def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
        if (!def) {
          bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate definition");
          break;
        }

        def->kind = BEBOP_DEF_UNION;
        def->name = name->lexeme;
        def->name_span = name->span;
        def->documentation = doc;
        def->decorators = frame->decorators;
        def->schema = p->schema;
        def->parent = frame->nested_parent;
        def->visibility = frame->visibility;

        bebop__check_reserved_name(p, name, name->lexeme);

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{'")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        frame->def = def;
        frame->count = 0;
        frame->capacity = 0;
        frame->state = PARSE_STATE_UNION_BODY;

        bebop_sema_enter_def(p->sema, def);
        break;
      }

      case PARSE_STATE_UNION_BODY: {
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) || BEBOP_PARSE_AT_END(p)) {
          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          if (frame->count == 0) {
            BEBOP_PARSE_ERROR(p, BEBOP_DIAG_EMPTY_UNION, "Union must have at least one branch");
          }

          frame->def->union_def.branch_count = frame->count;
          BEBOP_PARSE_SET_DEF_SPAN(frame->def, frame->keyword, rbrace);

          bebop__parse_check_duplicate_def(p, frame->def);

          if (frame->nested_parent) {
            bebop__def_add_nested(frame->nested_parent, frame->def);
            sp--;
          } else {
            bebop__schema_add_def(p->schema, frame->def);
            frame->state = PARSE_STATE_FILE;
            frame->def = NULL;
            frame->decorators = NULL;
          }
        } else if (bebop__parse_is_nested_def_start(p)) {
          if (sp + 1 >= BEBOP_PARSE_STACK_SIZE) {
            bebop__parse_fatal(
                p, BEBOP_ERR_INTERNAL, "Parser stack overflow (too many nested definitions)"
            );
            break;
          }
          sp++;
          stack[sp] = (bebop_parse_frame_t) {
              .state = PARSE_STATE_DEFINITION,
              .def = NULL,
              .keyword = NULL,
              .decorators = NULL,
              .count = 0,
              .capacity = 0,
              .is_mutable = false,
              .visibility = BEBOP_VIS_DEFAULT,
              .nested_parent = frame->def,
          };
        } else {
          frame->state = PARSE_STATE_UNION_BRANCH;
        }
        break;
      }

      case PARSE_STATE_UNION_BRANCH: {
        bebop_decorator_t* branch_decorators = bebop__parse_decorators(p);
        const bebop_token_t* first_tok = branch_decorators
            ? &(bebop_token_t) {.span = branch_decorators->span}
            : BEBOP_PARSE_CURRENT(p);

        bebop_visibility_t branch_visibility = BEBOP_VIS_DEFAULT;
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_EXPORT)) {
          BEBOP_PARSE_ADVANCE(p);
          branch_visibility = BEBOP_VIS_EXPORT;
        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_LOCAL)) {
          bebop_token_t* vis_tok = BEBOP_PARSE_ADVANCE(p);
          branch_visibility = BEBOP_VIS_LOCAL;
          BEBOP_PARSE_WARNING(
              p,
              vis_tok,
              BEBOP_DIAG_REDUNDANT_LOCAL_WARNING,
              "'local' on union branch is redundant (nested " "definitions are local by default)"
          );
        }

        if (!BEBOP_PARSE_CONSUME_NAME(p, "Expected branch name")) {
          frame->state = PARSE_STATE_UNION_BODY;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
          break;
        }
        bebop_token_t* branch_name_tok = BEBOP_PARSE_PREVIOUS(p);
        bebop_str_t branch_name = branch_name_tok->lexeme;
        bebop_span_t branch_name_span = branch_name_tok->span;

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LPAREN, "Expected '(' after branch name")) {
          frame->state = PARSE_STATE_UNION_BODY;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
          break;
        }

        if (!BEBOP_PARSE_CONSUME(p, BEBOP_TOKEN_NUMBER, "Expected discriminator")) {
          frame->state = PARSE_STATE_UNION_BODY;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
          break;
        }
        bebop_token_t* disc_tok = BEBOP_PARSE_PREVIOUS(p);

        const char* disc_str = BEBOP_STR(p->ctx, disc_tok->lexeme);
        size_t disc_len = BEBOP_STR_LEN(p->ctx, disc_tok->lexeme);
        int64_t disc_val;
        if (!bebop_util_parse_int(disc_str, disc_len, &disc_val)
            || disc_val < BEBOP_MIN_DISCRIMINATOR || disc_val > BEBOP_MAX_DISCRIMINATOR)
        {
          BEBOP_PARSE_ERROR(
              p, BEBOP_DIAG_INVALID_UNION_BRANCH, "Union discriminator must be 1-255"
          );
          frame->state = PARSE_STATE_UNION_BODY;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
          break;
        }

        for (uint32_t i = 0; i < frame->count; i++) {
          if (frame->def->union_def.branches[i].discriminator == (uint8_t)disc_val) {
            bebop__PARSE_ERROR_FMT(
                p,
                disc_tok,
                BEBOP_DIAG_DUPLICATE_UNION_DISCRIMINATOR,
                "Duplicate union discriminator %d",
                (int)disc_val
            );
            bebop__schema_diag_add_label(
                p->schema, frame->def->union_def.branches[i].span, "first used here"
            );
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize_in_block(p);
            }
            goto next_union_branch;
          }
        }

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RPAREN, "Expected ')' after discriminator")) {
          frame->state = PARSE_STATE_UNION_BODY;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
          break;
        }

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_COLON, "Expected ':' after ')'")) {
          frame->state = PARSE_STATE_UNION_BODY;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
          break;
        }

        bebop_str_t branch_doc = bebop__parse_extract_doc(p, &first_tok->leading);
        bebop_def_t* branch_def = NULL;
        bebop_type_t* type_ref = NULL;

        bool branch_is_mutable = BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_MUT);

        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_LBRACE)) {
          bebop__check_reserved_name(p, branch_name_tok, branch_name);
          bebop_token_t* lbrace = BEBOP_PARSE_ADVANCE(p);

          branch_def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
          if (!branch_def) {
            bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate branch definition");
            break;
          }

          branch_def->kind = BEBOP_DEF_STRUCT;
          branch_def->name = branch_name;
          branch_def->name_span = branch_name_span;
          branch_def->documentation = branch_doc;
          branch_def->schema = p->schema;
          branch_def->parent = frame->def;
          branch_def->visibility = branch_visibility;
          branch_def->struct_def.is_mutable = branch_is_mutable;

          bebop_sema_enter_def(p->sema, branch_def);

          uint32_t field_count = 0;
          uint32_t field_capacity = 0;
          while (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) && !BEBOP_PARSE_AT_END(p)) {
            bebop__parse_field_inline(
                p, branch_def, &(bebop__field_state_t) {&field_count, &field_capacity}, false
            );
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize_in_block(p);
            }
          }

          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          branch_def->struct_def.field_count = field_count;
          BEBOP_PARSE_SET_DEF_SPAN(branch_def, lbrace, rbrace);

        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_MESSAGE)) {
          bebop__check_reserved_name(p, branch_name_tok, branch_name);
          if (branch_is_mutable) {
            BEBOP_PARSE_ERROR_CURRENT(
                p, BEBOP_DIAG_INVALID_UNION_BRANCH, "'mut' cannot be applied to message"
            );
          }
          bebop_token_t* msg_kw = BEBOP_PARSE_ADVANCE(p);

          if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{' after 'message'")) {
            frame->state = PARSE_STATE_UNION_BODY;
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize_in_block(p);
            }
            break;
          }

          branch_def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
          if (!branch_def) {
            bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate branch definition");
            break;
          }

          branch_def->kind = BEBOP_DEF_MESSAGE;
          branch_def->name = branch_name;
          branch_def->name_span = branch_name_span;
          branch_def->documentation = branch_doc;
          branch_def->schema = p->schema;
          branch_def->parent = frame->def;
          branch_def->visibility = branch_visibility;

          bebop_sema_enter_def(p->sema, branch_def);

          uint32_t field_count = 0;
          uint32_t field_capacity = 0;
          while (!BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) && !BEBOP_PARSE_AT_END(p)) {
            bebop__parse_field_inline(
                p, branch_def, &(bebop__field_state_t) {&field_count, &field_capacity}, true
            );
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize_in_block(p);
            }
          }

          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          branch_def->message_def.field_count = field_count;
          BEBOP_PARSE_SET_DEF_SPAN(branch_def, msg_kw, rbrace);

        } else if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_IDENTIFIER)) {
          if (branch_visibility != BEBOP_VIS_DEFAULT) {
            BEBOP_PARSE_ERROR_CURRENT(
                p,
                BEBOP_DIAG_INVALID_UNION_BRANCH,
                "Visibility modifiers cannot be applied " "to type reference branches"
            );
          }
          if (branch_is_mutable) {
            BEBOP_PARSE_ERROR_CURRENT(
                p,
                BEBOP_DIAG_INVALID_UNION_BRANCH,
                "'mut' cannot be applied to type reference branches"
            );
          }

          type_ref = bebop__parse_type(p);
          if (!type_ref) {
            frame->state = PARSE_STATE_UNION_BODY;
            if (p->flags & BEBOP_PARSER_PANIC_MODE) {
              bebop__parse_synchronize_in_block(p);
            }
            break;
          }

          if (type_ref->kind != BEBOP_TYPE_DEFINED) {
            BEBOP_PARSE_ERROR(
                p,
                BEBOP_DIAG_UNION_REF_INVALID_TYPE,
                "Union branch type reference must be a named " "type (struct or message)"
            );
            frame->state = PARSE_STATE_UNION_BODY;
            break;
          }

        } else {
          BEBOP_PARSE_ERROR_CURRENT(
              p,
              BEBOP_DIAG_INVALID_UNION_BRANCH,
              "Expected '{', 'mut', 'message', or type reference after ':'"
          );
          frame->state = PARSE_STATE_UNION_BODY;
          break;
        }

        if (!BEBOP_PARSE_CONSUME_AFTER(
                p, BEBOP_TOKEN_SEMICOLON, "Expected ';' after union branch"
            ))
        {
          frame->state = PARSE_STATE_UNION_BODY;
          break;
        }

        if (branch_def) {
          bebop__def_add_nested(frame->def, branch_def);
        }

        bebop_union_branch_t* branch = BEBOP_ARRAY_PUSH(
            BEBOP_ARENA(p->ctx),
            frame->def->union_def.branches,
            frame->count,
            frame->capacity,
            bebop_union_branch_t
        );
        if (!branch) {
          frame->state = PARSE_STATE_UNION_BODY;
          break;
        }

        branch->discriminator = (uint8_t)disc_val;
        branch->span = bebop__parse_span_from_tokens(first_tok, BEBOP_PARSE_PREVIOUS(p));
        branch->documentation = branch_doc;
        branch->decorators = branch_decorators;
        branch->def = branch_def;
        branch->type_ref = type_ref;
        branch->name = branch_name;
        branch->name_span = branch_name_span;
        branch->parent = frame->def;

      next_union_branch:
        frame->state = PARSE_STATE_UNION_BODY;
        break;
      }

      case PARSE_STATE_SERVICE_START: {
        bebop_token_t* keyword = BEBOP_PARSE_ADVANCE(p);
        frame->keyword = keyword;

        bebop_str_t doc = bebop__parse_extract_doc(p, &keyword->leading);
        bebop_token_t* name = bebop__parse_def_name(p, "Expected service name");
        if (!name) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_def_t* def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
        if (!def) {
          bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate definition");
          break;
        }

        def->kind = BEBOP_DEF_SERVICE;
        def->name = name->lexeme;
        def->name_span = name->span;
        def->documentation = doc;
        def->decorators = frame->decorators;
        def->schema = p->schema;
        def->visibility = frame->visibility;

        bebop__check_reserved_name(p, name, name->lexeme);

        if (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_WITH)) {
          uint32_t mixin_count = 0;
          uint32_t mixin_capacity = 0;
          do {
            bebop_type_t* mixin_type = bebop__parse_type(p);
            if (!mixin_type) {
              break;
            }
            if (mixin_type->kind != BEBOP_TYPE_DEFINED) {
              BEBOP_PARSE_ERROR(
                  p, BEBOP_DIAG_INVALID_SERVICE_TYPE, "Service mixin must be a named service type"
              );
            } else {
              bebop_type_t** slot = BEBOP_ARRAY_PUSH(
                  BEBOP_ARENA(p->ctx),
                  def->service_def.mixins,
                  mixin_count,
                  mixin_capacity,
                  bebop_type_t*
              );
              if (slot) {
                *slot = mixin_type;
              }
            }
          } while (BEBOP_PARSE_MATCH(p, BEBOP_TOKEN_COMMA));
          def->service_def.mixin_count = mixin_count;
        }

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_LBRACE, "Expected '{'")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        frame->def = def;
        frame->count = 0;
        frame->capacity = 0;
        frame->state = PARSE_STATE_SERVICE_BODY;

        bebop_sema_enter_def(p->sema, def);
        break;
      }

      case PARSE_STATE_SERVICE_BODY: {
        if (BEBOP_PARSE_CHECK(p, BEBOP_TOKEN_RBRACE) || BEBOP_PARSE_AT_END(p)) {
          BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_RBRACE, "Expected '}'");
          bebop_token_t* rbrace = BEBOP_PARSE_PREVIOUS(p);
          bebop_sema_exit_def(p->sema);

          frame->def->service_def.method_count = frame->count;
          BEBOP_PARSE_SET_DEF_SPAN(frame->def, frame->keyword, rbrace);

          bebop__parse_check_duplicate_def(p, frame->def);
          bebop__schema_add_def(p->schema, frame->def);

          frame->state = PARSE_STATE_FILE;
          frame->def = NULL;
          frame->decorators = NULL;
        } else {
          bebop__parse_method_inline(p, frame->def, &frame->count, &frame->capacity);
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize_in_block(p);
          }
        }
        break;
      }

      case PARSE_STATE_CONST_START: {
        bebop_token_t* keyword = BEBOP_PARSE_ADVANCE(p);

        bebop_str_t doc = bebop__parse_extract_doc(p, &keyword->leading);

        bebop_type_t* type = bebop__parse_type(p);
        if (!type) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bool is_byte_array = type->kind == BEBOP_TYPE_ARRAY && type->array.element
            && type->array.element->kind == BEBOP_TYPE_BYTE;

        if ((type->kind == BEBOP_TYPE_ARRAY && !is_byte_array)
            || type->kind == BEBOP_TYPE_FIXED_ARRAY || type->kind == BEBOP_TYPE_MAP
            || type->kind == BEBOP_TYPE_DEFINED)
        {
          if (type->kind == BEBOP_TYPE_ARRAY) {
            BEBOP_PARSE_ERROR(
                p, BEBOP_DIAG_INVALID_CONST_TYPE, "Only byte[] arrays are allowed as const types"
            );
          } else {
            BEBOP_PARSE_ERROR(p, BEBOP_DIAG_INVALID_CONST_TYPE, "Const type must be a scalar type");
          }

          if (type->kind == BEBOP_TYPE_DEFINED && !bebop_str_is_null(type->defined.name)) {
            const char* type_name = BEBOP_STR(p->ctx, type->defined.name);
            if (type_name) {
              const char* suggestion = bebop_util_fuzzy_match(
                  type_name,
                  strlen(type_name),
                  bebop__param_type_names,
                  bebop__PARAM_TYPE_NAME_COUNT - 1,
                  3
              );
              if (suggestion) {
                char buf[64];
                snprintf(buf, sizeof(buf), "did you mean '%s'?", suggestion);
                bebop__schema_diag_add_label(p->schema, type->span, buf);
              }
            }
          }
        }

        if (!BEBOP_PARSE_CONSUME_NAME(p, "Expected const name")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }
        bebop_token_t* name = BEBOP_PARSE_PREVIOUS(p);

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_EQUALS, "Expected '=' after const name")) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_literal_t value;
        bebop_type_kind_t literal_type_hint = type->kind;
        if (is_byte_array) {
          literal_type_hint = BEBOP_TYPE_BYTE;
        }
        if (!bebop__parse_literal(p, literal_type_hint, &value)) {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        switch (type->kind) {
          case BEBOP_TYPE_ARRAY:
            if (is_byte_array && value.kind != BEBOP_LITERAL_BYTES) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_BOOL:
            if (value.kind != BEBOP_LITERAL_BOOL) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_BYTE:
          case BEBOP_TYPE_INT8:
          case BEBOP_TYPE_INT16:
          case BEBOP_TYPE_UINT16:
          case BEBOP_TYPE_INT32:
          case BEBOP_TYPE_UINT32:
          case BEBOP_TYPE_INT64:
          case BEBOP_TYPE_UINT64:
          case BEBOP_TYPE_INT128:
          case BEBOP_TYPE_UINT128:
            if (value.kind != BEBOP_LITERAL_INT) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_FLOAT16:
          case BEBOP_TYPE_FLOAT32:
          case BEBOP_TYPE_FLOAT64:
          case BEBOP_TYPE_BFLOAT16:
            if (value.kind != BEBOP_LITERAL_FLOAT && value.kind != BEBOP_LITERAL_INT) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_STRING:
            if (value.kind != BEBOP_LITERAL_STRING) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_UUID:
            if (value.kind != BEBOP_LITERAL_UUID) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_TIMESTAMP:
            if (value.kind != BEBOP_LITERAL_TIMESTAMP) {
              goto type_mismatch;
            }
            break;
          case BEBOP_TYPE_DURATION:
            if (value.kind != BEBOP_LITERAL_DURATION) {
              goto type_mismatch;
            }
            break;
          default:
            break;
        }
        goto type_ok;
      type_mismatch:
        BEBOP_PARSE_ERROR(
            p, BEBOP_DIAG_INVALID_LITERAL, "Literal value does not match declared const type"
        );
      type_ok:

        if (!BEBOP_PARSE_CONSUME_AFTER(p, BEBOP_TOKEN_SEMICOLON, "Expected ';' after const value"))
        {
          frame->state = PARSE_STATE_FILE;
          if (p->flags & BEBOP_PARSER_PANIC_MODE) {
            bebop__parse_synchronize(p);
          }
          break;
        }

        bebop_def_t* def = bebop_arena_new(BEBOP_ARENA(p->ctx), bebop_def_t, 1);
        if (!def) {
          bebop__parse_fatal(p, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate const definition");
          break;
        }

        def->kind = BEBOP_DEF_CONST;
        def->name = name->lexeme;
        def->name_span = name->span;
        def->documentation = doc;
        def->decorators = frame->decorators;
        def->schema = p->schema;
        def->visibility = frame->visibility;
        def->span = bebop__parse_span_from_tokens(keyword, BEBOP_PARSE_PREVIOUS(p));
        def->const_def.type = type;
        def->const_def.value = value;

        bebop__parse_check_duplicate_def(p, def);
        bebop__schema_add_def(p->schema, def);

        frame->state = PARSE_STATE_FILE;
        frame->decorators = NULL;
        break;
      }

      default:

        sp--;
        break;
    }
  }
}

void bebop__parse_tokens_into(
    bebop_context_t* ctx, const bebop_token_stream_t stream, bebop_schema_t* schema
)
{
  BEBOP_ASSERT(ctx != NULL);
  BEBOP_ASSERT(schema != NULL);

  if (stream.count == 0) {
    bebop__context_set_error(ctx, BEBOP_ERR_INTERNAL, "Empty token stream");
    return;
  }

  schema->tokens = stream;

  bebop_sema_t sema;
  if (!bebop_sema_init(&sema, ctx, schema)) {
    return;
  }

  bebop_parser_t parser = {
      .ctx = ctx,
      .schema = schema,
      .stream = stream,
      .current = 0,
      .source = schema->source,
      .source_len = schema->source_len,
      .flags = BEBOP_PARSER_OK,
      .sema = &sema,
  };

  bebop__parse_file(&parser);
}

bebop_schema_t* bebop__parse_tokens(
    bebop_context_t* ctx, const bebop_token_stream_t stream, const bebop_source_t* source
)
{
  BEBOP_ASSERT(ctx != NULL);

  bebop_schema_t* schema = bebop__schema_create(ctx, source->path, source->source, source->len);
  if (!schema) {
    return NULL;
  }

  bebop__parse_tokens_into(ctx, stream, schema);

  return schema;
}
