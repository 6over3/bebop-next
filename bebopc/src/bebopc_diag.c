#include "bebopc_diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define DIAG_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define DIAG_PRINTF(fmt_idx, arg_idx)
#endif

#define C_RESET "\033[0m"
#define C_BOLD "\033[1m"
#define C_DIM "\033[2m"
#define C_ITALIC "\033[3m"
#define C_UNDERLINE "\033[4m"
#define C_RED "\033[1;31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[1;33m"
#define C_BLUE "\033[1;34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN "\033[36m"
#define C_WHITE "\033[1;37m"
#define C_GRAY "\033[90m"
#define C_BG_DARK "\033[48;5;235m"
#define C_BG_ERR "\033[48;5;52m"

static void _diag_init_box(diag_ctx_t* ctx)
{
  if (ctx->unicode) {
    ctx->box.vert = "│";
    ctx->box.horiz = "─";
    ctx->box.tl = "┌";
    ctx->box.bl = "└";
    ctx->box.dot = "•";
    ctx->box.anchor = "┬";
    ctx->box.corner = "╰";
    ctx->box.tr = "╮";
    ctx->box.br = "╯";
    ctx->box.arrow = "◄";
  } else {
    ctx->box.vert = "|";
    ctx->box.horiz = "-";
    ctx->box.tl = "+";
    ctx->box.bl = "+";
    ctx->box.dot = ":";
    ctx->box.anchor = "^";
    ctx->box.corner = "`";
    ctx->box.tr = "+";
    ctx->box.br = "+";
    ctx->box.arrow = "<";
  }
}

void diag_init(diag_ctx_t* ctx)
{
  ctx->format = DIAG_FMT_TERMINAL;
  ctx->color = true;
  ctx->unicode = true;
  _diag_init_box(ctx);
}

void diag_set_format(diag_ctx_t* ctx, diag_format_t fmt)
{
  ctx->format = fmt;
}

void diag_set_color(diag_ctx_t* ctx, bool enabled)
{
  ctx->color = enabled;
}

void diag_set_unicode(diag_ctx_t* ctx, bool enabled)
{
  ctx->unicode = enabled;
  _diag_init_box(ctx);
}

void diag_buf_init(diag_buf_t* buf)
{
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

void diag_buf_cleanup(diag_buf_t* buf)
{
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static bool _diag_buf_grow(diag_buf_t* buf, size_t need)
{
  if (buf->len + need <= buf->cap) {
    return true;
  }
  size_t new_cap = buf->cap ? buf->cap * 2 : 256;
  while (new_cap < buf->len + need) {
    new_cap *= 2;
  }
  char* p = (char*)realloc(buf->data, new_cap);
  if (!p) {
    return false;
  }
  buf->data = p;
  buf->cap = new_cap;
  return true;
}

static void _diag_buf_str(diag_buf_t* buf, const char* s)
{
  if (!s) {
    return;
  }
  size_t n = strlen(s);
  if (_diag_buf_grow(buf, n + 1)) {
    memcpy(buf->data + buf->len, s, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
  }
}

static void _diag_buf_char(diag_buf_t* buf, char c)
{
  if (_diag_buf_grow(buf, 2)) {
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
  }
}

static void _diag_buf_repeat(diag_buf_t* buf, char c, size_t n)
{
  if (_diag_buf_grow(buf, n + 1)) {
    memset(buf->data + buf->len, c, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
  }
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
DIAG_PRINTF(2, 3)

static void _diag_buf_fmt(diag_buf_t* buf, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap2);
  va_end(ap2);
  if (n > 0 && _diag_buf_grow(buf, (size_t)n + 1)) {
    vsnprintf(buf->data + buf->len, (size_t)n + 1, fmt, ap);
    buf->len += (size_t)n;
  }
  va_end(ap);
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

void diag_source_init(diag_source_t* src, const char* path, const char* text, uint32_t text_len)
{
  src->path = path;
  src->text = text;
  src->text_len = text_len;

  uint32_t count = 1;
  for (uint32_t i = 0; i < text_len; i++) {
    if (text[i] == '\n') {
      count++;
    }
  }

  src->line_offsets = (uint32_t*)malloc(count * sizeof(uint32_t));
  if (!src->line_offsets) {
    src->line_count = 0;
    return;
  }
  src->line_count = count;

  uint32_t idx = 0;
  src->line_offsets[idx++] = 0;
  for (uint32_t i = 0; i < text_len && idx < count; i++) {
    if (text[i] == '\n') {
      src->line_offsets[idx++] = i + 1;
    }
  }
}

void diag_source_cleanup(diag_source_t* src)
{
  free(src->line_offsets);
  src->line_offsets = NULL;
  src->line_count = 0;
}

void diag_source_loc(const diag_source_t* src, uint32_t offset, uint32_t* line, uint32_t* col)
{
  uint32_t lo = 0, hi = src->line_count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (src->line_offsets[mid] <= offset) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  uint32_t idx = lo > 0 ? lo - 1 : 0;
  *line = idx + 1;
  *col = offset - src->line_offsets[idx] + 1;
}

static void _diag_get_line_bounds(
    const diag_source_t* src, uint32_t line_idx, uint32_t* start, uint32_t* end
)
{
  *start = src->line_offsets[line_idx];
  *end = (line_idx + 1 < src->line_count) ? src->line_offsets[line_idx + 1] : src->text_len;
  while (*end > *start && (src->text[*end - 1] == '\n' || src->text[*end - 1] == '\r')) {
    (*end)--;
  }
}

static int _diag_digit_count(uint32_t n)
{
  int c = 1;
  while (n >= 10) {
    n /= 10;
    c++;
  }
  return c;
}

typedef struct {
  const diag_label_t* label;
  uint32_t line;
  uint32_t col_start;
  uint32_t col_end;
  uint32_t anchor;
} _diag_label_info_t;

static bool _diag_is_ident_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool _diag_match_word(const char* s, uint32_t len, const char* word)
{
  size_t wlen = strlen(word);
  return len == wlen && memcmp(s, word, wlen) == 0;
}

typedef enum {
  TOK_DEFAULT,
  TOK_KEYWORD,
  TOK_TYPE,
  TOK_STRING,
  TOK_NUMBER,
  TOK_COMMENT,
  TOK_PUNCT
} _diag_tok_kind_t;

static const char* _diag_tok_color(_diag_tok_kind_t kind)
{
  switch (kind) {
    case TOK_KEYWORD:
      return C_BLUE;
    case TOK_TYPE:
      return C_CYAN;
    case TOK_STRING:
      return C_GREEN;
    case TOK_NUMBER:
      return C_CYAN;
    case TOK_COMMENT:
      return C_GRAY;
    case TOK_PUNCT:
      return C_WHITE;
    default:
      return NULL;
  }
}

static _diag_tok_kind_t _diag_classify_word(const char* s, uint32_t len)
{
  if (_diag_match_word(s, len, "struct") || _diag_match_word(s, len, "message")
      || _diag_match_word(s, len, "enum") || _diag_match_word(s, len, "union")
      || _diag_match_word(s, len, "service") || _diag_match_word(s, len, "const")
      || _diag_match_word(s, len, "import") || _diag_match_word(s, len, "map")
      || _diag_match_word(s, len, "array") || _diag_match_word(s, len, "readonly")
      || _diag_match_word(s, len, "mut") || _diag_match_word(s, len, "stream")
      || _diag_match_word(s, len, "true") || _diag_match_word(s, len, "false"))
  {
    return TOK_KEYWORD;
  }

  if (_diag_match_word(s, len, "int8") || _diag_match_word(s, len, "int16")
      || _diag_match_word(s, len, "int32") || _diag_match_word(s, len, "int64")
      || _diag_match_word(s, len, "uint8") || _diag_match_word(s, len, "uint16")
      || _diag_match_word(s, len, "uint32") || _diag_match_word(s, len, "uint64")
      || _diag_match_word(s, len, "float32") || _diag_match_word(s, len, "float64")
      || _diag_match_word(s, len, "bool") || _diag_match_word(s, len, "string")
      || _diag_match_word(s, len, "uuid") || _diag_match_word(s, len, "guid")
      || _diag_match_word(s, len, "date"))
  {
    return TOK_TYPE;
  }

  return TOK_DEFAULT;
}

static void _diag_render_source_line(
    diag_buf_t* buf,
    const char* text,
    uint32_t len,
    const _diag_label_info_t* infos,
    uint32_t info_count,
    uint32_t cur_line,
    const char* sev_color,
    bool color
)
{
  uint32_t i = 0;
  while (i < len) {
    bool in_error = false;
    for (uint32_t li = 0; li < info_count; li++) {
      if (infos[li].line == cur_line && i >= infos[li].col_start && i < infos[li].col_end) {
        in_error = true;
        break;
      }
    }

    if (i + 1 < len && text[i] == '/' && text[i + 1] == '/') {
      if (color) {
        _diag_buf_str(buf, in_error ? sev_color : _diag_tok_color(TOK_COMMENT));
      }
      while (i < len) {
        _diag_buf_char(buf, text[i++]);
      }
      if (color) {
        _diag_buf_str(buf, C_RESET);
      }
      break;
    }

    if (text[i] == '"') {
      if (color) {
        _diag_buf_str(buf, in_error ? sev_color : _diag_tok_color(TOK_STRING));
      }
      _diag_buf_char(buf, text[i++]);
      while (i < len && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < len) {
          _diag_buf_char(buf, text[i++]);
        }
        _diag_buf_char(buf, text[i++]);
      }
      if (i < len) {
        _diag_buf_char(buf, text[i++]);
      }
      if (color) {
        _diag_buf_str(buf, C_RESET);
      }
      continue;
    }

    if (text[i] >= '0' && text[i] <= '9') {
      if (color) {
        _diag_buf_str(buf, in_error ? sev_color : _diag_tok_color(TOK_NUMBER));
      }
      while (i < len
             && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.' || text[i] == 'x'
                 || text[i] == 'X' || (text[i] >= 'a' && text[i] <= 'f')
                 || (text[i] >= 'A' && text[i] <= 'F')))
      {
        _diag_buf_char(buf, text[i++]);
      }
      if (color) {
        _diag_buf_str(buf, C_RESET);
      }
      continue;
    }

    if (_diag_is_ident_char(text[i]) && !(text[i] >= '0' && text[i] <= '9')) {
      uint32_t start = i;
      while (i < len && _diag_is_ident_char(text[i])) {
        i++;
      }
      _diag_tok_kind_t kind = _diag_classify_word(text + start, i - start);
      const char* c = in_error ? sev_color : _diag_tok_color(kind);
      if (color && c) {
        _diag_buf_str(buf, c);
      }
      for (uint32_t j = start; j < i; j++) {
        _diag_buf_char(buf, text[j]);
      }
      if (color && c) {
        _diag_buf_str(buf, C_RESET);
      }
      continue;
    }

    if (in_error && color) {
      _diag_buf_str(buf, sev_color);
    }
    _diag_buf_char(buf, text[i++]);
    if (in_error && color) {
      _diag_buf_str(buf, C_RESET);
    }
  }
}

static int _diag_label_cmp(const void* a, const void* b)
{
  const _diag_label_info_t* la = a;
  const _diag_label_info_t* lb = b;
  if (la->label->priority != lb->label->priority) {
    return la->label->priority - lb->label->priority;
  }
  return (int)la->col_start - (int)lb->col_start;
}

#define CONTEXT_LINES 2

static void _diag_render_xcode(diag_buf_t* buf, const diag_t* diag)
{
  const char* sev_name = diag->severity == DIAG_ERROR ? "error"
      : diag->severity == DIAG_WARNING                ? "warning"
                                                      : "note";
  const char* path = (diag->source && diag->source->path) ? diag->source->path : "<input>";

  if (diag->label_count > 0 && diag->source) {
    uint32_t line, col;
    diag_source_loc(diag->source, diag->labels[0].start, &line, &col);
    _diag_buf_fmt(buf, "%s:%u:%u: %s: ", path, line, col, sev_name);
  } else {
    _diag_buf_fmt(buf, "%s: %s: ", path, sev_name);
  }

  if (diag->code) {
    _diag_buf_fmt(buf, "[%s] ", diag->code);
  }
  _diag_buf_str(buf, diag->message ? diag->message : "");
  _diag_buf_char(buf, '\n');

  if (diag->note) {
    if (diag->label_count > 0 && diag->source) {
      uint32_t line, col;
      diag_source_loc(diag->source, diag->labels[0].start, &line, &col);
      _diag_buf_fmt(buf, "%s:%u:%u: note: %s\n", path, line, col, diag->note);
    } else {
      _diag_buf_fmt(buf, "%s: note: %s\n", path, diag->note);
    }
  }

  for (uint32_t i = 0; i < diag->label_count; i++) {
    if (!diag->labels[i].message) {
      continue;
    }
    uint32_t line, col;
    diag_source_loc(diag->source, diag->labels[i].start, &line, &col);
    _diag_buf_fmt(buf, "%s:%u:%u: note: %s\n", path, line, col, diag->labels[i].message);
  }
}

void diag_render(diag_ctx_t* ctx, diag_buf_t* buf, const diag_t* diag)
{
  _diag_init_box(ctx);
  if (!diag || !diag->source) {
    return;
  }

  if (ctx->format == DIAG_FMT_XCODE) {
    _diag_render_xcode(buf, diag);
    return;
  }

  const diag_source_t* src = diag->source;
  const char* sev_name = diag->severity == DIAG_ERROR ? "Error"
      : diag->severity == DIAG_WARNING                ? "Warning"
                                                      : "Info";
  const char* sev_color = diag->severity == DIAG_ERROR ? C_RED
      : diag->severity == DIAG_WARNING                 ? C_YELLOW
                                                       : C_CYAN;

  _diag_label_info_t* infos = NULL;
  uint32_t min_line = UINT32_MAX, max_line = 0;
  int w = 2;

  if (diag->label_count > 0) {
    infos = (_diag_label_info_t*)malloc(diag->label_count * sizeof(_diag_label_info_t));
    if (!infos) {
      return;
    }

    for (uint32_t i = 0; i < diag->label_count; i++) {
      const diag_label_t* lbl = &diag->labels[i];
      uint32_t line, col;
      diag_source_loc(src, lbl->start, &line, &col);

      uint32_t ls, le;
      _diag_get_line_bounds(src, line - 1, &ls, &le);

      infos[i].label = lbl;
      infos[i].line = line;
      infos[i].col_start = lbl->start - ls;
      infos[i].col_end = lbl->end - ls;
      if (infos[i].col_end > le - ls) {
        infos[i].col_end = le - ls;
      }
      infos[i].anchor = (infos[i].col_start + infos[i].col_end) / 2;

      if (line < min_line) {
        min_line = line;
      }
      if (line > max_line) {
        max_line = line;
      }
    }

    qsort(infos, diag->label_count, sizeof(_diag_label_info_t), _diag_label_cmp);

    uint32_t display_end = max_line + CONTEXT_LINES;
    if (display_end > src->line_count) {
      display_end = src->line_count;
    }
    w = _diag_digit_count(display_end);
    if (w < 2) {
      w = 2;
    }
  }

  uint32_t display_start = 1, display_end = 1;
  if (diag->label_count > 0) {
    display_start = (min_line > CONTEXT_LINES) ? min_line - CONTEXT_LINES : 1;
    display_end = max_line + CONTEXT_LINES;
    if (display_end > src->line_count) {
      display_end = src->line_count;
    }
  }

  if (ctx->color) {
    _diag_buf_str(buf, C_BOLD);
  }
  if (ctx->color) {
    _diag_buf_str(buf, sev_color);
  }
  _diag_buf_str(buf, sev_name);
  if (diag->code) {
    _diag_buf_char(buf, '[');
    _diag_buf_str(buf, diag->code);
    _diag_buf_char(buf, ']');
  }
  if (ctx->color) {
    _diag_buf_str(buf, C_RESET);
  }
  if (ctx->color) {
    _diag_buf_str(buf, C_BOLD);
  }
  _diag_buf_str(buf, ": ");
  if (ctx->color) {
    _diag_buf_str(buf, C_RESET);
  }
  _diag_buf_str(buf, diag->message ? diag->message : "");

  _diag_buf_char(buf, '\n');

  if (diag->note) {
    if (ctx->color) {
      _diag_buf_str(buf, C_CYAN);
    }
    _diag_buf_str(buf, "NOTE");
    if (ctx->color) {
      _diag_buf_str(buf, C_RESET);
    }
    _diag_buf_str(buf, ": ");
    _diag_buf_str(buf, diag->note);
    _diag_buf_char(buf, '\n');
  }

  if (diag->label_count == 0) {
    return;
  }

  _diag_buf_repeat(buf, ' ', (size_t)w + 1);
  if (ctx->color) {
    _diag_buf_str(buf, C_GRAY);
  }
  _diag_buf_str(buf, ctx->box.tl);
  _diag_buf_char(buf, '[');
  if (ctx->color) {
    _diag_buf_str(buf, C_WHITE);
  }
  _diag_buf_str(buf, src->path ? src->path : "<input>");
  if (ctx->color) {
    _diag_buf_str(buf, C_GRAY);
  }
  _diag_buf_char(buf, ']');
  if (ctx->color) {
    _diag_buf_str(buf, C_RESET);
  }
  _diag_buf_char(buf, '\n');

  _diag_buf_repeat(buf, ' ', (size_t)w + 1);
  if (ctx->color) {
    _diag_buf_str(buf, C_GRAY);
  }
  _diag_buf_str(buf, ctx->box.vert);
  if (ctx->color) {
    _diag_buf_str(buf, C_RESET);
  }
  _diag_buf_char(buf, '\n');

  for (uint32_t line = display_start; line <= display_end; line++) {
    uint32_t ls, le;
    _diag_get_line_bounds(src, line - 1, &ls, &le);
    uint32_t line_len = le - ls;

    bool has_label = false;
    for (uint32_t li = 0; li < diag->label_count; li++) {
      if (infos[li].line == line) {
        has_label = true;
        break;
      }
    }

    if (ctx->color) {
      _diag_buf_str(buf, C_GRAY);
    }
    _diag_buf_fmt(buf, "%*u ", w, line);
    _diag_buf_str(buf, ctx->box.vert);
    if (ctx->color) {
      _diag_buf_str(buf, C_RESET);
    }
    _diag_buf_char(buf, ' ');

    _diag_render_source_line(
        buf, src->text + ls, line_len, infos, diag->label_count, line, sev_color, ctx->color
    );
    _diag_buf_char(buf, '\n');

    if (has_label) {
      uint32_t max_col = 0;
      for (uint32_t li = 0; li < diag->label_count; li++) {
        if (infos[li].line == line && infos[li].col_end > max_col) {
          max_col = infos[li].col_end;
        }
      }

      _diag_buf_repeat(buf, ' ', (size_t)w + 1);
      if (ctx->color) {
        _diag_buf_str(buf, C_GRAY);
      }
      _diag_buf_str(buf, ctx->box.dot);
      if (ctx->color) {
        _diag_buf_str(buf, C_RESET);
      }
      _diag_buf_char(buf, ' ');
      if (ctx->color) {
        _diag_buf_str(buf, sev_color);
      }

      bool any_has_message = false;
      for (uint32_t li = 0; li < diag->label_count; li++) {
        if (infos[li].line == line && infos[li].label->message) {
          any_has_message = true;
          break;
        }
      }

      for (uint32_t i = 0; i < max_col; i++) {
        bool is_anchor = false, is_ul = false;
        for (uint32_t li = 0; li < diag->label_count; li++) {
          if (infos[li].line != line) {
            continue;
          }
          if (i >= infos[li].col_start && i < infos[li].col_end) {
            is_ul = true;
          }
          if (i == infos[li].anchor && infos[li].label->message) {
            is_anchor = true;
          }
        }
        if (any_has_message && is_anchor) {
          _diag_buf_str(buf, ctx->box.anchor);
        } else if (is_ul) {
          _diag_buf_str(buf, ctx->box.horiz);
        } else {
          _diag_buf_char(buf, ' ');
        }
      }

      if (ctx->color) {
        _diag_buf_str(buf, C_RESET);
      }
      _diag_buf_char(buf, '\n');

      for (uint32_t li = 0; li < diag->label_count; li++) {
        if (infos[li].line != line || !infos[li].label->message) {
          continue;
        }

        _diag_buf_repeat(buf, ' ', (size_t)w + 1);
        if (ctx->color) {
          _diag_buf_str(buf, C_GRAY);
        }
        _diag_buf_str(buf, ctx->box.dot);
        if (ctx->color) {
          _diag_buf_str(buf, C_RESET);
        }
        _diag_buf_char(buf, ' ');
        if (ctx->color) {
          _diag_buf_str(buf, sev_color);
        }

        for (uint32_t i = 0; i <= infos[li].anchor; i++) {
          if (i == infos[li].anchor) {
            _diag_buf_str(buf, ctx->box.corner);
          } else {
            bool vert = false;
            for (uint32_t lj = li + 1; lj < diag->label_count; lj++) {
              if (infos[lj].line == line && infos[lj].anchor == i) {
                vert = true;
                break;
              }
            }
            _diag_buf_str(buf, vert ? ctx->box.vert : " ");
          }
        }

        _diag_buf_str(buf, ctx->box.horiz);
        _diag_buf_str(buf, ctx->box.horiz);
        _diag_buf_str(buf, " ");
        _diag_buf_str(buf, infos[li].label->message);
        if (ctx->color) {
          _diag_buf_str(buf, C_RESET);
        }
        _diag_buf_char(buf, '\n');
      }
    }
  }

  _diag_buf_repeat(buf, ' ', (size_t)w + 1);
  if (ctx->color) {
    _diag_buf_str(buf, C_GRAY);
  }
  _diag_buf_str(buf, ctx->box.bl);
  if (ctx->color) {
    _diag_buf_str(buf, C_RESET);
  }
  _diag_buf_char(buf, '\n');

  free(infos);
}
