typedef struct {
  const char* source;
  size_t source_len;
  size_t pos;
  uint32_t line;
  uint32_t col;
  bebop_context_t* ctx;
  bebop_schema_t* schema;

  bebop_trivia_t* trivia_buf;
  uint32_t trivia_count;
  uint32_t trivia_capacity;

  bebop_error_t error;
} bebop__scanner_t;

typedef struct {
  const char* keyword;
  bebop_token_kind_t kind;
} bebop__keyword_t;

static const bebop__keyword_t bebop__keywords[] = {
#define X(name, str) {str, BEBOP_TOKEN_##name},
    BEBOP_KEYWORDS(X)
#undef X
};

#define BEBOP_KEYWORD_COUNT BEBOP_COUNTOF(bebop__keywords)

static bebop_token_kind_t bebop__scan_lookup_keyword(const char* str, const size_t len)
{
  for (size_t i = 0; i < BEBOP_KEYWORD_COUNT; i++) {
    const char* kw = bebop__keywords[i].keyword;
    if (bebop_streqn(kw, str, len)) {
      return bebop__keywords[i].kind;
    }
  }
  return BEBOP_TOKEN_IDENTIFIER;
}

static inline char bebop__scan_peek_char(const bebop__scanner_t* s)
{
  if (s->pos >= s->source_len) {
    return '\0';
  }
  return s->source[s->pos];
}

static inline char bebop__scan_peek_char_at(const bebop__scanner_t* s, const size_t offset)
{
  if (s->pos + offset >= s->source_len) {
    return '\0';
  }
  return s->source[s->pos + offset];
}

static inline void bebop__scan_advance(bebop__scanner_t* s)
{
  if (s->pos >= s->source_len) {
    return;
  }

  const char c = s->source[s->pos];
  if (c == '\n') {
    s->line++;
    s->col = 1;
    s->pos++;
  } else if (c == '\r') {
    s->line++;
    s->col = 1;
    s->pos++;
    if (s->pos < s->source_len && s->source[s->pos] == '\n') {
      s->pos++;
    }
  } else {
    s->col++;
    s->pos++;
  }
}

static inline bebop_span_t bebop__scan_make_span(
    const bebop__scanner_t* s,
    const size_t start_pos,
    const uint32_t start_line,
    const uint32_t start_col
)
{
  return (bebop_span_t) {
      .off = (uint32_t)start_pos,
      .len = (uint32_t)(s->pos - start_pos),
      .start_line = start_line,
      .start_col = start_col,
      .end_line = s->line,
      .end_col = s->col,
  };
}

#define BEBOP_TRIVIA_INITIAL_CAPACITY 8

static void bebop__scan_trivia_push(
    bebop__scanner_t* s, const bebop_trivia_kind_t kind, const bebop_span_t span
)
{
  if (s->error != BEBOP_ERR_NONE) {
    return;
  }

  if (s->trivia_count >= s->trivia_capacity) {
    const uint32_t old_cap = s->trivia_capacity;
    const uint32_t new_cap = old_cap == 0 ? BEBOP_TRIVIA_INITIAL_CAPACITY : old_cap * 2;
    bebop_trivia_t* new_buf = bebop_arena_realloc(
        BEBOP_ARENA(s->ctx),
        s->trivia_buf,
        (size_t)old_cap * sizeof(bebop_trivia_t),
        (size_t)new_cap * sizeof(bebop_trivia_t)
    );
    if (!new_buf) {
      s->error = BEBOP_ERR_OUT_OF_MEMORY;
      bebop__context_set_error(s->ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate trivia buffer");
      return;
    }
    s->trivia_buf = new_buf;
    s->trivia_capacity = new_cap;
  }
  s->trivia_buf[s->trivia_count++] = (bebop_trivia_t) {.kind = kind, .span = span};
}

// Trivia accumulates into one append-only buffer for the whole file; a list is
// just an (offset, count) slice, resolved to a pointer after the scan.
static bebop_trivia_list_t bebop__scan_trivia_slice(const bebop__scanner_t* s, uint32_t mark)
{
  return (bebop_trivia_list_t) {.items = NULL, .count = s->trivia_count - mark, .off = mark};
}

static void bebop__scan_skip_whitespace(bebop__scanner_t* s)
{
  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  while (BEBOP_IS_WHITESPACE(bebop__scan_peek_char(s))) {
    bebop__scan_advance(s);
  }

  if (s->pos > start) {
    bebop__scan_trivia_push(
        s, BEBOP_TRIVIA_WHITESPACE, bebop__scan_make_span(s, start, start_line, start_col)
    );
  }
}

static bool bebop__scan_skip_newline(bebop__scanner_t* s)
{
  if (!BEBOP_IS_NEWLINE(bebop__scan_peek_char(s))) {
    return false;
  }

  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  bebop__scan_advance(s);

  bebop__scan_trivia_push(
      s, BEBOP_TRIVIA_NEWLINE, bebop__scan_make_span(s, start, start_line, start_col)
  );
  return true;
}

static bool bebop__scan_skip_line_comment(bebop__scanner_t* s)
{
  if (bebop__scan_peek_char(s) != '/' || bebop__scan_peek_char_at(s, 1) != '/') {
    return false;
  }

  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  const bool is_doc = bebop__scan_peek_char_at(s, 2) == '/';

  bebop__scan_advance(s);
  bebop__scan_advance(s);

  while (bebop__scan_peek_char(s) != '\0' && !BEBOP_IS_NEWLINE(bebop__scan_peek_char(s))) {
    bebop__scan_advance(s);
  }

  const bebop_trivia_kind_t kind = is_doc ? BEBOP_TRIVIA_DOC_COMMENT : BEBOP_TRIVIA_LINE_COMMENT;
  bebop__scan_trivia_push(s, kind, bebop__scan_make_span(s, start, start_line, start_col));
  return true;
}

static bool bebop__scan_skip_block_comment(bebop__scanner_t* s)
{
  if (bebop__scan_peek_char(s) != '/' || bebop__scan_peek_char_at(s, 1) != '*') {
    return false;
  }

  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  const bool is_doc =
      bebop__scan_peek_char_at(s, 2) == '*' && bebop__scan_peek_char_at(s, 3) != '/';

  bebop__scan_advance(s);
  bebop__scan_advance(s);

  while (bebop__scan_peek_char(s) != '\0') {
    if (bebop__scan_peek_char(s) == '*' && bebop__scan_peek_char_at(s, 1) == '/') {
      bebop__scan_advance(s);
      bebop__scan_advance(s);
      goto done;
    }
    bebop__scan_advance(s);
  }

  if (s->schema) {
    bebop__schema_add_diagnostic(
        s->schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR,
                             BEBOP_DIAG_UNTERMINATED_COMMENT,
                             bebop__scan_make_span(s, start, start_line, start_col)},
        "Unterminated block comment",
        NULL
    );
  }

done:;
  const bebop_trivia_kind_t kind = is_doc ? BEBOP_TRIVIA_DOC_COMMENT : BEBOP_TRIVIA_BLOCK_COMMENT;
  bebop__scan_trivia_push(s, kind, bebop__scan_make_span(s, start, start_line, start_col));
  return true;
}

static void bebop__scan_skip_leading_trivia(bebop__scanner_t* s)
{
  for (;;) {
    bebop__scan_skip_whitespace(s);

    if (bebop__scan_skip_newline(s)) {
      continue;
    }
    if (bebop__scan_skip_line_comment(s)) {
      continue;
    }
    if (bebop__scan_skip_block_comment(s)) {
      continue;
    }

    break;
  }
}

static bebop_trivia_list_t bebop__scan_collect_trailing_trivia(bebop__scanner_t* s)
{
  const uint32_t mark = s->trivia_count;

  for (;;) {
    bebop__scan_skip_whitespace(s);

    if (bebop__scan_skip_line_comment(s)) {
      continue;
    }
    if (bebop__scan_skip_block_comment(s)) {
      continue;
    }

    if (bebop__scan_skip_newline(s)) {
      break;
    }

    break;
  }

  return bebop__scan_trivia_slice(s, mark);
}

static bebop_token_t bebop__scan_identifier(bebop__scanner_t* s)
{
  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  while (BEBOP_IS_IDENT_CHAR(bebop__scan_peek_char(s))) {
    bebop__scan_advance(s);
  }

  const size_t len = s->pos - start;
  const bebop_token_kind_t kind = bebop__scan_lookup_keyword(s->source + start, len);

  const bebop_token_t tok = {
      .kind = kind,
      .span = bebop__scan_make_span(s, start, start_line, start_col),
      .lexeme = bebop_intern_n(BEBOP_INTERN(s->ctx), s->source + start, len),
  };
  return tok;
}

static bebop_token_t bebop__scan_number(bebop__scanner_t* s)
{
  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  while (BEBOP_IS_IDENT_CHAR(bebop__scan_peek_char(s)) || bebop__scan_peek_char(s) == '.'
         || bebop__scan_peek_char(s) == '-')
  {
    if (bebop__scan_peek_char(s) == '-') {
      const char prev = s->pos > 0 ? s->source[s->pos - 1] : '\0';
      if (prev != 'e' && prev != 'E') {
        break;
      }
    }
    bebop__scan_advance(s);
  }

  const size_t len = s->pos - start;

  const bebop_token_t tok = {
      .kind = BEBOP_TOKEN_NUMBER,
      .span = bebop__scan_make_span(s, start, start_line, start_col),
      .lexeme = bebop_intern_n(BEBOP_INTERN(s->ctx), s->source + start, len),
  };
  return tok;
}

static bool bebop__scan_grow_buf(bebop__scanner_t* s, char** buf, size_t* buf_cap, size_t buf_len)
{
  const size_t new_cap = *buf_cap == 0 ? 64 : *buf_cap * 2;
  char* new_buf = bebop_arena_new(BEBOP_ARENA(s->ctx), char, new_cap);
  if (!new_buf) {
    s->error = BEBOP_ERR_OUT_OF_MEMORY;
    bebop__context_set_error(s->ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate string buffer");
    return false;
  }
  if (*buf && buf_len > 0) {
    memcpy(new_buf, *buf, buf_len);
  }
  *buf = new_buf;
  *buf_cap = new_cap;
  return true;
}

// Shared scanner for string and bytes literals; they differ only in the b
// prefix, the UTF-8 validation, and the token kind.
static bebop_token_t bebop__scan_quoted(bebop__scanner_t* s, const bool is_bytes)
{
  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  if (is_bytes) {
    bebop__scan_advance(s);

    const char prefix_quote = bebop__scan_peek_char(s);
    if (prefix_quote != '"' && prefix_quote != '\'') {
      return (bebop_token_t) {
          .kind = BEBOP_TOKEN_ERROR,
          .span = bebop__scan_make_span(s, start, start_line, start_col),
      };
    }
  }

  const char quote = bebop__scan_peek_char(s);
  bebop__scan_advance(s);

  char* buf = NULL;
  size_t buf_len = 0;
  size_t buf_cap = 0;

  while (bebop__scan_peek_char(s) != '\0' && s->error == BEBOP_ERR_NONE) {
    const char c = bebop__scan_peek_char(s);

    if (c == quote) {
      if (bebop__scan_peek_char_at(s, 1) == quote) {
        bebop__scan_advance(s);
        bebop__scan_advance(s);
        if (buf_len >= buf_cap && !bebop__scan_grow_buf(s, &buf, &buf_cap, buf_len)) {
          break;
        }
        buf[buf_len++] = quote;
      } else {
        bebop__scan_advance(s);
        goto done_lit;
      }
    } else if (c == '\\') {
      const size_t esc_start = s->pos;
      const uint32_t esc_line = s->line;
      const uint32_t esc_col = s->col;
      bebop__scan_advance(s);

      const size_t remaining = s->source_len - s->pos;
      char esc_out[4];
      int esc_out_len = 0;
      const int consumed =
          bebop_unescape_char(s->source + s->pos, remaining, esc_out, &esc_out_len);

      if (consumed == 0) {
        if (s->schema) {
          bebop__schema_add_diagnostic(
              s->schema,
              (bebop__diag_loc_t) {BEBOP_DIAG_ERROR,
                                   BEBOP_DIAG_INVALID_ESCAPE,
                                   bebop__scan_make_span(s, esc_start, esc_line, esc_col)},
              "Invalid escape sequence",
              NULL
          );
        }
        goto skip_lit_to_end;
      }

      while (buf_len + (size_t)esc_out_len > buf_cap) {
        if (!bebop__scan_grow_buf(s, &buf, &buf_cap, buf_len)) {
          break;
        }
      }
      if (s->error != BEBOP_ERR_NONE) {
        break;
      }
      for (int i = 0; i < esc_out_len; i++) {
        buf[buf_len++] = esc_out[i];
      }
      for (int i = 0; i < consumed; i++) {
        bebop__scan_advance(s);
      }
    } else if (BEBOP_IS_NEWLINE(c)) {
      if (buf_len >= buf_cap && !bebop__scan_grow_buf(s, &buf, &buf_cap, buf_len)) {
        break;
      }
      buf[buf_len++] = '\n';
      // bebop__scan_advance consumes a full \r\n pair and tracks line/col;
      // advancing again here would eat a content byte and double-count lines.
      bebop__scan_advance(s);
    } else {
      if (buf_len >= buf_cap && !bebop__scan_grow_buf(s, &buf, &buf_cap, buf_len)) {
        break;
      }
      buf[buf_len++] = c;
      bebop__scan_advance(s);
    }
  }


done_lit:
  if (s->error != BEBOP_ERR_NONE) {
    return (bebop_token_t) {
        .kind = BEBOP_TOKEN_ERROR,
        .span = bebop__scan_make_span(s, start, start_line, start_col),
    };
  }

  if (buf_len >= buf_cap) {
    const size_t final_cap = buf_len + 1;
    char* new_buf = bebop_arena_new(BEBOP_ARENA(s->ctx), char, final_cap);
    if (!new_buf) {
      s->error = BEBOP_ERR_OUT_OF_MEMORY;
      bebop__context_set_error(s->ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate literal buffer");
      return (bebop_token_t) {
          .kind = BEBOP_TOKEN_ERROR,
          .span = bebop__scan_make_span(s, start, start_line, start_col),
      };
    }
    if (buf && buf_len > 0) {
      memcpy(new_buf, buf, buf_len);
    }
    buf = new_buf;
  }
  if (buf) {
    buf[buf_len] = '\0';
  }

  if (!is_bytes && buf_len > 0 && !bebop_utf8_valid(buf, buf_len)) {
    if (s->schema) {
      bebop__schema_add_diagnostic(
          s->schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR,
                               BEBOP_DIAG_INVALID_UTF8,
                               bebop__scan_make_span(s, start, start_line, start_col)},
          "Invalid UTF-8 encoding in string literal",
          NULL
      );
    }
    return (bebop_token_t) {
        .kind = BEBOP_TOKEN_ERROR,
        .span = bebop__scan_make_span(s, start, start_line, start_col),
    };
  }

  return (bebop_token_t) {
      .kind = is_bytes ? BEBOP_TOKEN_BYTES : BEBOP_TOKEN_STRING,
      .span = bebop__scan_make_span(s, start, start_line, start_col),
      .lexeme = bebop_intern_n(BEBOP_INTERN(s->ctx), buf ? buf : "", buf_len),
  };

skip_lit_to_end:
  while (bebop__scan_peek_char(s) != '\0') {
    const char c = bebop__scan_peek_char(s);
    if (c == quote && bebop__scan_peek_char_at(s, 1) != quote) {
      bebop__scan_advance(s);
      break;
    }
    bebop__scan_advance(s);
  }
  return (bebop_token_t) {
      .kind = BEBOP_TOKEN_ERROR,
      .span = bebop__scan_make_span(s, start, start_line, start_col),
  };
}

static bebop_token_t bebop__scan_token(bebop__scanner_t* s)
{
  const size_t start = s->pos;
  const uint32_t start_line = s->line;
  const uint32_t start_col = s->col;

  const char c = bebop__scan_peek_char(s);

  if (c == '\0') {
    return (bebop_token_t) {
        .kind = BEBOP_TOKEN_EOF,
        .span = bebop__scan_make_span(s, start, start_line, start_col),
    };
  }

  if (BEBOP_IS_IDENT_START(c)) {
    if (c == 'b') {
      const char next = bebop__scan_peek_char_at(s, 1);
      if (next == '"' || next == '\'') {
        return bebop__scan_quoted(s, true);
      }
    }
    return bebop__scan_identifier(s);
  }

  if (BEBOP_IS_DIGIT(c)) {
    return bebop__scan_number(s);
  }

  if (c == '"' || c == '\'') {
    return bebop__scan_quoted(s, false);
  }

  bebop__scan_advance(s);

  switch (c) {
    case '(':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_LPAREN,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case ')':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_RPAREN,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '{':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_LBRACE,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '}':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_RBRACE,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '[': {
      size_t level = 0;
      while (bebop__scan_peek_char_at(s, level) == '=') {
        level++;
      }
      if (bebop__scan_peek_char_at(s, level) == '[') {
        for (size_t i = 0; i <= level; i++) {
          bebop__scan_advance(s);
        }
        const size_t content_start = s->pos;
        while (bebop__scan_peek_char(s) != '\0') {
          if (bebop__scan_peek_char(s) == ']') {
            size_t match = 1;
            while (match <= level && bebop__scan_peek_char_at(s, match) == '=') {
              match++;
            }
            if (match == level + 1 && bebop__scan_peek_char_at(s, match) == ']') {
              const size_t content_len = s->pos - content_start;
              for (size_t i = 0; i < level + 2; i++) {
                bebop__scan_advance(s);
              }
              return (bebop_token_t) {
                  .kind = BEBOP_TOKEN_RAW_BLOCK,
                  .span = bebop__scan_make_span(s, start, start_line, start_col),
                  .lexeme =
                      bebop_intern_n(BEBOP_INTERN(s->ctx), s->source + content_start, content_len),
              };
            }
          }
          bebop__scan_advance(s);
        }

        if (s->schema) {
          char closer[40];
          const size_t shown = level < sizeof(closer) - 3 ? level : sizeof(closer) - 3;
          closer[0] = ']';
          memset(closer + 1, '=', shown);
          closer[shown + 1] = ']';
          closer[shown + 2] = '\0';
          char msg[80];
          snprintf(msg, sizeof(msg), "Unterminated raw block: expected '%s'", closer);
          bebop__schema_add_diagnostic(
              s->schema,
              (bebop__diag_loc_t) {BEBOP_DIAG_ERROR,
                                   BEBOP_DIAG_INVALID_MACRO,
                                   bebop__scan_make_span(s, start, start_line, start_col)},
              msg,
              NULL
          );
        }
        return (bebop_token_t) {
            .kind = BEBOP_TOKEN_ERROR,
            .span = bebop__scan_make_span(s, start, start_line, start_col),
        };
      }
      return (bebop_token_t) {.kind = BEBOP_TOKEN_LBRACKET,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    }
    case ']':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_RBRACKET,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '<':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_LANGLE,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '>':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_RANGLE,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case ':':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_COLON,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case ';':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_SEMICOLON,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case ',':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_COMMA,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '.':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_DOT,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '?':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_QUESTION,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '/':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_SLASH,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '=':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_EQUALS,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '@':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_AT,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '#':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_HASH,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '!':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_BANG,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '$':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_DOLLAR,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '\\':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_BACKSLASH,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '`':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_BACKTICK,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '~':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_TILDE,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '&':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_AMPERSAND,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};
    case '|':
      return (bebop_token_t) {.kind = BEBOP_TOKEN_PIPE,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};

    case '-':
      if (bebop__scan_peek_char(s) == '>') {
        bebop__scan_advance(s);
        return (bebop_token_t) {.kind = BEBOP_TOKEN_ARROW,
                                .span = bebop__scan_make_span(s, start, start_line, start_col)};
      }
      return (bebop_token_t) {.kind = BEBOP_TOKEN_MINUS,
                              .span = bebop__scan_make_span(s, start, start_line, start_col)};

    default:
      return (bebop_token_t) {
          .kind = BEBOP_TOKEN_ERROR,
          .span = bebop__scan_make_span(s, start, start_line, start_col),
          .lexeme = bebop_intern_n(BEBOP_INTERN(s->ctx), s->source + start, 1),
      };
  }
}

static bebop_token_t bebop__scan_next(bebop__scanner_t* s)
{
  if (s->error != BEBOP_ERR_NONE) {
    return (bebop_token_t) {.kind = BEBOP_TOKEN_EOF};
  }

  const uint32_t leading_mark = s->trivia_count;
  bebop__scan_skip_leading_trivia(s);
  if (s->error != BEBOP_ERR_NONE) {
    return (bebop_token_t) {.kind = BEBOP_TOKEN_EOF};
  }
  const bebop_trivia_list_t leading = bebop__scan_trivia_slice(s, leading_mark);

  bebop_token_t tok = bebop__scan_token(s);
  tok.leading = leading;

  if (tok.kind != BEBOP_TOKEN_EOF && s->error == BEBOP_ERR_NONE) {
    tok.trailing = bebop__scan_collect_trailing_trivia(s);
  }

  return tok;
}

bebop_token_stream_t bebop__scan_with_schema(
    bebop_context_t* ctx, const char* source, const size_t len, bebop_schema_t* schema
)
{
  BEBOP_ASSERT(ctx != NULL);

  bebop__scanner_t s = {
      .source = source,
      .source_len = len,
      .pos = 0,
      .line = 1,
      .col = 1,
      .ctx = ctx,
      .schema = schema,
      .error = BEBOP_ERR_NONE,
  };

  // Estimate token count from source length to avoid repeated doublings; the
  // average token (keyword/identifier/punct plus surrounding whitespace) is a
  // handful of bytes, so len/4 + 16 is a reasonable starting point.
  uint32_t capacity = (uint32_t)(len / 4) + 16;
  bebop_token_t* tokens = bebop_arena_new(BEBOP_ARENA(ctx), bebop_token_t, capacity);
  uint32_t count = 0;
  if (!tokens) {
    s.error = BEBOP_ERR_OUT_OF_MEMORY;
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate token buffer");
  }

  while (s.error == BEBOP_ERR_NONE) {
    const bebop_token_t tok = bebop__scan_next(&s);

    if (count >= capacity) {
      const uint32_t new_cap = capacity * 2;
      bebop_token_t* new_buf = bebop_arena_realloc(
          BEBOP_ARENA(ctx),
          tokens,
          (size_t)capacity * sizeof(bebop_token_t),
          (size_t)new_cap * sizeof(bebop_token_t)
      );
      if (!new_buf) {
        s.error = BEBOP_ERR_OUT_OF_MEMORY;
        bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate token buffer");
        break;
      }
      tokens = new_buf;
      capacity = new_cap;
    }

    tokens[count++] = tok;

    if (tok.kind == BEBOP_TOKEN_EOF) {
      break;
    }
  }

  // Resolve each token's trivia (offset, count) slice against the now-final
  // shared buffer base.
  if (s.error == BEBOP_ERR_NONE) {
    for (uint32_t i = 0; i < count; i++) {
      if (tokens[i].leading.count > 0) {
        tokens[i].leading.items = s.trivia_buf + tokens[i].leading.off;
      }
      if (tokens[i].trailing.count > 0) {
        tokens[i].trailing.items = s.trivia_buf + tokens[i].trailing.off;
      }
    }
  }

  return (bebop_token_stream_t) {
      .tokens = tokens,
      .count = count,
  };
}

bebop_token_stream_t bebop_scan(bebop_context_t* ctx, const char* source, const size_t len)
{
  return bebop__scan_with_schema(ctx, source, len, NULL);
}

const char* bebop_token_kind_name(const bebop_token_kind_t kind)
{
  switch (kind) {
    case BEBOP_TOKEN_ENUM:
      return "enum";
    case BEBOP_TOKEN_STRUCT:
      return "struct";
    case BEBOP_TOKEN_MESSAGE:
      return "message";
    case BEBOP_TOKEN_MUT:
      return "mut";
    case BEBOP_TOKEN_READONLY:
      return "readonly";
    case BEBOP_TOKEN_MAP:
      return "map";
    case BEBOP_TOKEN_ARRAY:
      return "array";
    case BEBOP_TOKEN_UNION:
      return "union";
    case BEBOP_TOKEN_SERVICE:
      return "service";
    case BEBOP_TOKEN_STREAM:
      return "stream";
    case BEBOP_TOKEN_IMPORT:
      return "import";
    case BEBOP_TOKEN_EDITION:
      return "edition";
    case BEBOP_TOKEN_PACKAGE:
      return "package";
    case BEBOP_TOKEN_EXPORT:
      return "export";
    case BEBOP_TOKEN_LOCAL:
      return "local";
    case BEBOP_TOKEN_TRUE:
      return "true";
    case BEBOP_TOKEN_FALSE:
      return "false";
    case BEBOP_TOKEN_CONST:
      return "const";
    case BEBOP_TOKEN_WITH:
      return "with";
    case BEBOP_TOKEN_IDENTIFIER:
      return "identifier";
    case BEBOP_TOKEN_STRING:
      return "string";
    case BEBOP_TOKEN_BYTES:
      return "bytes";
    case BEBOP_TOKEN_NUMBER:
      return "number";
    case BEBOP_TOKEN_BLOCK_COMMENT:
      return "block_comment";
    case BEBOP_TOKEN_LPAREN:
      return "(";
    case BEBOP_TOKEN_RPAREN:
      return ")";
    case BEBOP_TOKEN_LBRACE:
      return "{";
    case BEBOP_TOKEN_RBRACE:
      return "}";
    case BEBOP_TOKEN_LBRACKET:
      return "[";
    case BEBOP_TOKEN_RBRACKET:
      return "]";
    case BEBOP_TOKEN_LANGLE:
      return "<";
    case BEBOP_TOKEN_RANGLE:
      return ">";
    case BEBOP_TOKEN_COLON:
      return ":";
    case BEBOP_TOKEN_SEMICOLON:
      return ";";
    case BEBOP_TOKEN_COMMA:
      return ",";
    case BEBOP_TOKEN_DOT:
      return ".";
    case BEBOP_TOKEN_QUESTION:
      return "?";
    case BEBOP_TOKEN_SLASH:
      return "/";
    case BEBOP_TOKEN_EQUALS:
      return "=";
    case BEBOP_TOKEN_AT:
      return "@";
    case BEBOP_TOKEN_DOLLAR:
      return "$";
    case BEBOP_TOKEN_BACKSLASH:
      return "\\";
    case BEBOP_TOKEN_BACKTICK:
      return "`";
    case BEBOP_TOKEN_TILDE:
      return "~";
    case BEBOP_TOKEN_AMPERSAND:
      return "&";
    case BEBOP_TOKEN_PIPE:
      return "|";
    case BEBOP_TOKEN_MINUS:
      return "-";
    case BEBOP_TOKEN_ARROW:
      return "->";
    case BEBOP_TOKEN_HASH:
      return "#";
    case BEBOP_TOKEN_BANG:
      return "!";
    case BEBOP_TOKEN_RAW_BLOCK:
      return "raw_block";
    case BEBOP_TOKEN_EOF:
      return "EOF";
    case BEBOP_TOKEN_ERROR:
      return "error";
  }
  return "unknown";
}

const char* bebop_trivia_kind_name(const bebop_trivia_kind_t kind)
{
  switch (kind) {
    case BEBOP_TRIVIA_WHITESPACE:
      return "whitespace";
    case BEBOP_TRIVIA_NEWLINE:
      return "newline";
    case BEBOP_TRIVIA_LINE_COMMENT:
      return "line_comment";
    case BEBOP_TRIVIA_BLOCK_COMMENT:
      return "block_comment";
    case BEBOP_TRIVIA_DOC_COMMENT:
      return "doc_comment";
  }
  return "unknown";
}
