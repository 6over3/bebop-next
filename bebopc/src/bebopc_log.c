#include "bebopc_log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bebopc_utils.h"

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#define ANSI_RESET "\x1b[0m"
#define ANSI_BOLD "\x1b[1m"
#define ANSI_DIM "\x1b[2m"
#define ANSI_ITALIC "\x1b[3m"
#define ANSI_UNDERLINE "\x1b[4m"
#define ANSI_BLINK "\x1b[5m"
#define ANSI_RAPIDBLINK "\x1b[6m"
#define ANSI_REVERSE "\x1b[7m"
#define ANSI_HIDDEN "\x1b[8m"
#define ANSI_STRIKE "\x1b[9m"

#define ANSI_LINK_START "\x1b]8;;"
#define ANSI_LINK_MID "\x07"
#define ANSI_LINK_END "\x1b]8;;\x07"

#define ANSI_FG_BLACK "\x1b[30m"
#define ANSI_FG_RED "\x1b[31m"
#define ANSI_FG_GREEN "\x1b[32m"
#define ANSI_FG_YELLOW "\x1b[33m"
#define ANSI_FG_BLUE "\x1b[34m"
#define ANSI_FG_MAGENTA "\x1b[35m"
#define ANSI_FG_CYAN "\x1b[36m"
#define ANSI_FG_WHITE "\x1b[37m"
#define ANSI_FG_DEFAULT "\x1b[39m"

#define ANSI_FG_BRIGHT_BLACK "\x1b[90m"
#define ANSI_FG_BRIGHT_RED "\x1b[91m"
#define ANSI_FG_BRIGHT_GREEN "\x1b[92m"
#define ANSI_FG_BRIGHT_YELLOW "\x1b[93m"
#define ANSI_FG_BRIGHT_BLUE "\x1b[94m"
#define ANSI_FG_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_FG_BRIGHT_CYAN "\x1b[96m"
#define ANSI_FG_BRIGHT_WHITE "\x1b[97m"

#define ANSI_BG_BLACK "\x1b[40m"
#define ANSI_BG_RED "\x1b[41m"
#define ANSI_BG_GREEN "\x1b[42m"
#define ANSI_BG_YELLOW "\x1b[43m"
#define ANSI_BG_BLUE "\x1b[44m"
#define ANSI_BG_MAGENTA "\x1b[45m"
#define ANSI_BG_CYAN "\x1b[46m"
#define ANSI_BG_WHITE "\x1b[47m"
#define ANSI_BG_DEFAULT "\x1b[49m"

#define ANSI_BG_BRIGHT_BLACK "\x1b[100m"
#define ANSI_BG_BRIGHT_RED "\x1b[101m"
#define ANSI_BG_BRIGHT_GREEN "\x1b[102m"
#define ANSI_BG_BRIGHT_YELLOW "\x1b[103m"
#define ANSI_BG_BRIGHT_BLUE "\x1b[104m"
#define ANSI_BG_BRIGHT_MAGENTA "\x1b[105m"
#define ANSI_BG_BRIGHT_CYAN "\x1b[106m"
#define ANSI_BG_BRIGHT_WHITE "\x1b[107m"

typedef struct {
  const char* name;
  const char* fg_code;
  const char* bg_code;
} _log_color_entry_t;

typedef struct {
  const char* name;
  const char* code;
} _log_style_entry_t;

static const _log_color_entry_t _log_colors[] = {
    {"black", ANSI_FG_BLACK, ANSI_BG_BLACK},
    {"red", ANSI_FG_RED, ANSI_BG_RED},
    {"green", ANSI_FG_GREEN, ANSI_BG_GREEN},
    {"yellow", ANSI_FG_YELLOW, ANSI_BG_YELLOW},
    {"blue", ANSI_FG_BLUE, ANSI_BG_BLUE},
    {"magenta", ANSI_FG_MAGENTA, ANSI_BG_MAGENTA},
    {"cyan", ANSI_FG_CYAN, ANSI_BG_CYAN},
    {"white", ANSI_FG_WHITE, ANSI_BG_WHITE},
    {"grey", ANSI_FG_BRIGHT_BLACK, ANSI_BG_BRIGHT_BLACK},
    {"gray", ANSI_FG_BRIGHT_BLACK, ANSI_BG_BRIGHT_BLACK},
    {"default", ANSI_FG_DEFAULT, ANSI_BG_DEFAULT},
    {"brightblack", ANSI_FG_BRIGHT_BLACK, ANSI_BG_BRIGHT_BLACK},
    {"brightred", ANSI_FG_BRIGHT_RED, ANSI_BG_BRIGHT_RED},
    {"brightgreen", ANSI_FG_BRIGHT_GREEN, ANSI_BG_BRIGHT_GREEN},
    {"brightyellow", ANSI_FG_BRIGHT_YELLOW, ANSI_BG_BRIGHT_YELLOW},
    {"brightblue", ANSI_FG_BRIGHT_BLUE, ANSI_BG_BRIGHT_BLUE},
    {"brightmagenta", ANSI_FG_BRIGHT_MAGENTA, ANSI_BG_BRIGHT_MAGENTA},
    {"brightcyan", ANSI_FG_BRIGHT_CYAN, ANSI_BG_BRIGHT_CYAN},
    {"brightwhite", ANSI_FG_BRIGHT_WHITE, ANSI_BG_BRIGHT_WHITE},
    {NULL, NULL, NULL}
};

static const _log_style_entry_t _log_styles[] = {
    {"bold", ANSI_BOLD},
    {"dim", ANSI_DIM},
    {"italic", ANSI_ITALIC},
    {"underline", ANSI_UNDERLINE},
    {"blink", ANSI_BLINK},
    {"rapidblink", ANSI_RAPIDBLINK},
    {"reverse", ANSI_REVERSE},
    {"invert", ANSI_REVERSE},
    {"hidden", ANSI_HIDDEN},
    {"conceal", ANSI_HIDDEN},
    {"strikethrough", ANSI_STRIKE},
    {"strike", ANSI_STRIKE},
    {NULL, NULL}
};

typedef struct {
  char* data;
  size_t len;
  size_t cap;
} _log_strbuf_t;

static void _log_strbuf_init(_log_strbuf_t* buf)
{
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static void _log_strbuf_free(_log_strbuf_t* buf)
{
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static bool _log_strbuf_grow(_log_strbuf_t* buf, size_t needed)
{
  if (buf->len + needed + 1 <= buf->cap) {
    return true;
  }

  size_t new_cap = buf->cap == 0 ? 64 : buf->cap;
  while (new_cap < buf->len + needed + 1) {
    new_cap *= 2;
  }

  char* new_data = realloc(buf->data, new_cap);
  if (!new_data) {
    return false;
  }

  buf->data = new_data;
  buf->cap = new_cap;
  return true;
}

static bool _log_strbuf_append(_log_strbuf_t* buf, const char* str, size_t len)
{
  if (!_log_strbuf_grow(buf, len)) {
    return false;
  }
  memcpy(buf->data + buf->len, str, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return true;
}

static bool _log_strbuf_append_str(_log_strbuf_t* buf, const char* str)
{
  return _log_strbuf_append(buf, str, strlen(str));
}

static bool _log_strbuf_append_char(_log_strbuf_t* buf, char c)
{
  return _log_strbuf_append(buf, &c, 1);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static bool _log_strbuf_vappendf_raw(_log_strbuf_t* buf, const char* fmt, va_list args)
{
  va_list args_copy;
  va_copy(args_copy, args);
  int needed = vsnprintf(NULL, 0, fmt, args_copy);
  va_end(args_copy);

  if (needed <= 0) {
    return needed == 0;
  }

  char* msg = malloc((size_t)needed + 1);
  if (!msg) {
    return false;
  }

  vsnprintf(msg, (size_t)needed + 1, fmt, args);
  bool ok = _log_strbuf_append_str(buf, msg);
  free(msg);
  return ok;
}

// Escape markup metacharacters so substituted values render literally.
static bool _log_strbuf_append_markup_escaped(_log_strbuf_t* buf, const char* s)
{
  for (; *s; s++) {
    const char esc[2] = {*s, *s};
    if (*s == '[' || *s == ']') {
      if (!_log_strbuf_append(buf, esc, 2)) {
        return false;
      }
    } else if (!_log_strbuf_append(buf, s, 1)) {
      return false;
    }
  }
  return true;
}

// Star widths/precisions and %n are not worth per-spec handling.
static bool _log_fmt_is_simple(const char* fmt)
{
  for (const char* p = fmt; *p; p++) {
    if (*p != '%') {
      continue;
    }
    p++;
    if (*p == '%') {
      continue;
    }
    while (*p && !strchr("diouxXeEfFgGaAcspn", *p)) {
      if (*p == '*') {
        return false;
      }
      p++;
    }
    if (!*p || *p == 'n') {
      return false;
    }
  }
  return true;
}

// Format each conversion separately so '[' and ']' coming from string
// arguments (file names, plugin diagnostics) cannot inject styling or OSC-8
// hyperlinks into the markup renderer.
static bool _log_strbuf_vappendf(_log_strbuf_t* buf, const char* fmt, va_list args)
{
  if (!_log_fmt_is_simple(fmt)) {
    return _log_strbuf_vappendf_raw(buf, fmt, args);
  }

  const char* p = fmt;
  while (*p) {
    if (*p != '%') {
      const char* start = p;
      while (*p && *p != '%') {
        p++;
      }
      if (!_log_strbuf_append(buf, start, (size_t)(p - start))) {
        return false;
      }
      continue;
    }

    const char* spec_start = p;
    p++;
    if (*p == '%') {
      if (!_log_strbuf_append_char(buf, '%')) {
        return false;
      }
      p++;
      continue;
    }

    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      p++;
    }
    if (*p == '.') {
      p++;
      while (*p >= '0' && *p <= '9') {
        p++;
      }
    }

    enum { L_NONE, L_HH, L_H, L_L, L_LL, L_Z, L_T, L_J, L_BIGL } mod = L_NONE;
    if (p[0] == 'h' && p[1] == 'h') {
      mod = L_HH;
      p += 2;
    } else if (*p == 'h') {
      mod = L_H;
      p++;
    } else if (p[0] == 'l' && p[1] == 'l') {
      mod = L_LL;
      p += 2;
    } else if (*p == 'l') {
      mod = L_L;
      p++;
    } else if (*p == 'z') {
      mod = L_Z;
      p++;
    } else if (*p == 't') {
      mod = L_T;
      p++;
    } else if (*p == 'j') {
      mod = L_J;
      p++;
    } else if (*p == 'L') {
      mod = L_BIGL;
      p++;
    }

    const char conv = *p;
    if (!conv) {
      return _log_strbuf_append_str(buf, spec_start);
    }
    p++;

    char spec[32];
    const size_t spec_len = (size_t)(p - spec_start);
    if (spec_len >= sizeof(spec)) {
      return false;
    }
    memcpy(spec, spec_start, spec_len);
    spec[spec_len] = '\0';

    char small[256];
    char* out = small;
    int n = -1;

#define LOG_FMT_ONE(value) \
  do { \
    n = snprintf(small, sizeof(small), spec, (value)); \
    if (n < 0) { \
      return false; \
    } \
    if (n >= (int)sizeof(small)) { \
      out = malloc((size_t)n + 1); \
      if (!out) { \
        return false; \
      } \
      snprintf(out, (size_t)n + 1, spec, (value)); \
    } \
  } while (0)

    bool escape = false;
    switch (conv) {
      case 's': {
        const char* sv = va_arg(args, const char*);
        LOG_FMT_ONE(sv ? sv : "(null)");
        escape = true;
        break;
      }
      case 'c':
        LOG_FMT_ONE(va_arg(args, int));
        escape = true;
        break;
      case 'd':
      case 'i':
        switch (mod) {
          case L_LL:
            LOG_FMT_ONE(va_arg(args, long long));
            break;
          case L_L:
            LOG_FMT_ONE(va_arg(args, long));
            break;
          case L_J:
            LOG_FMT_ONE(va_arg(args, intmax_t));
            break;
          case L_Z:
          case L_T:
            LOG_FMT_ONE(va_arg(args, ptrdiff_t));
            break;
          default:
            LOG_FMT_ONE(va_arg(args, int));
            break;
        }
        break;
      case 'u':
      case 'o':
      case 'x':
      case 'X':
        switch (mod) {
          case L_LL:
            LOG_FMT_ONE(va_arg(args, unsigned long long));
            break;
          case L_L:
            LOG_FMT_ONE(va_arg(args, unsigned long));
            break;
          case L_J:
            LOG_FMT_ONE(va_arg(args, uintmax_t));
            break;
          case L_Z:
          case L_T:
            LOG_FMT_ONE(va_arg(args, size_t));
            break;
          default:
            LOG_FMT_ONE(va_arg(args, unsigned int));
            break;
        }
        break;
      case 'e':
      case 'E':
      case 'f':
      case 'F':
      case 'g':
      case 'G':
      case 'a':
      case 'A':
        if (mod == L_BIGL) {
          LOG_FMT_ONE(va_arg(args, long double));
        } else {
          LOG_FMT_ONE(va_arg(args, double));
        }
        break;
      case 'p':
        LOG_FMT_ONE(va_arg(args, void*));
        break;
      default:
        return false;
    }
#undef LOG_FMT_ONE

    const bool ok = escape ? _log_strbuf_append_markup_escaped(buf, out)
                           : _log_strbuf_append_str(buf, out);
    if (out != small) {
      free(out);
    }
    if (!ok) {
      return false;
    }
  }
  return true;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static const _log_color_entry_t* _log_lookup_color(const char* name, size_t len)
{
  for (const _log_color_entry_t* c = _log_colors; c->name; c++) {
    if (strlen(c->name) == len && bebopc_memicmp(c->name, name, len) == 0) {
      return c;
    }
  }
  return NULL;
}

static const _log_style_entry_t* _log_lookup_style(const char* name, size_t len)
{
  for (const _log_style_entry_t* s = _log_styles; s->name; s++) {
    if (strlen(s->name) == len && bebopc_memicmp(s->name, name, len) == 0) {
      return s;
    }
  }
  return NULL;
}

static bool _log_parse_hex_color(const char* str, size_t len, int* r, int* g, int* b)
{
  if (len != 7 || str[0] != '#') {
    return false;
  }

  int h1 = BEBOPC_HEX_VALUE(str[1]);
  int h2 = BEBOPC_HEX_VALUE(str[2]);
  int h3 = BEBOPC_HEX_VALUE(str[3]);
  int h4 = BEBOPC_HEX_VALUE(str[4]);
  int h5 = BEBOPC_HEX_VALUE(str[5]);
  int h6 = BEBOPC_HEX_VALUE(str[6]);

  if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 || h5 < 0 || h6 < 0) {
    return false;
  }

  *r = (h1 << 4) | h2;
  *g = (h3 << 4) | h4;
  *b = (h5 << 4) | h6;
  return true;
}

static bool _log_parse_rgb_color(const char* str, size_t len, int* r, int* g, int* b)
{
  if (len < 10 || bebopc_memicmp(str, "rgb(", 4) != 0) {
    return false;
  }
  if (str[len - 1] != ')') {
    return false;
  }

  const char* p = str + 4;
  const char* end = str + len - 1;

  while (p < end && BEBOPC_IS_WHITE(*p)) {
    p++;
  }
  int rv = 0;
  while (p < end && BEBOPC_IS_DIGIT(*p)) {
    rv = rv * 10 + (*p - '0');
    p++;
  }

  while (p < end && BEBOPC_IS_WHITE(*p)) {
    p++;
  }
  if (p >= end || *p != ',') {
    return false;
  }
  p++;

  while (p < end && BEBOPC_IS_WHITE(*p)) {
    p++;
  }
  int gv = 0;
  while (p < end && BEBOPC_IS_DIGIT(*p)) {
    gv = gv * 10 + (*p - '0');
    p++;
  }

  while (p < end && BEBOPC_IS_WHITE(*p)) {
    p++;
  }
  if (p >= end || *p != ',') {
    return false;
  }
  p++;

  while (p < end && BEBOPC_IS_WHITE(*p)) {
    p++;
  }
  int bv = 0;
  while (p < end && BEBOPC_IS_DIGIT(*p)) {
    bv = bv * 10 + (*p - '0');
    p++;
  }

  while (p < end && BEBOPC_IS_WHITE(*p)) {
    p++;
  }
  if (p != end) {
    return false;
  }

  if (rv > 255 || gv > 255 || bv > 255) {
    return false;
  }

  *r = rv;
  *g = gv;
  *b = bv;
  return true;
}

static void _log_emit_rgb(_log_strbuf_t* buf, int r, int g, int b, bool is_bg)
{
  char code[64];
  snprintf(
      code, sizeof(code), "\x1b[%d;2;%d;%d;%dm", is_bg ? 48 : 38, r & 0xFF, g & 0xFF, b & 0xFF
  );
  _log_strbuf_append_str(buf, code);
}

static const char* _log_parse_link_tag(const char* tag, size_t tag_len, size_t* url_len)
{
  if (tag_len < 6 || bebopc_memicmp(tag, "link=", 5) != 0) {
    return NULL;
  }
  *url_len = tag_len - 5;
  return tag + 5;
}

static void _log_emit_link_start(_log_strbuf_t* buf, const char* url, size_t url_len)
{
  _log_strbuf_append_str(buf, ANSI_LINK_START);
  _log_strbuf_append(buf, url, url_len);
  _log_strbuf_append_str(buf, ANSI_LINK_MID);
  _log_strbuf_append_str(buf, ANSI_UNDERLINE);
}

static void _log_emit_link_end(_log_strbuf_t* buf)
{
  _log_strbuf_append_str(buf, ANSI_RESET);
  _log_strbuf_append_str(buf, ANSI_LINK_END);
}

typedef struct {
  bool is_link;
} _log_tag_info_t;

static bool _log_parse_and_emit_tag(
    _log_strbuf_t* buf, const char* tag, size_t tag_len, bool use_colors, _log_tag_info_t* info
)
{
  if (info) {
    info->is_link = false;
  }

  if (!use_colors) {
    return true;
  }

  size_t url_len;
  const char* url = _log_parse_link_tag(tag, tag_len, &url_len);
  if (url) {
    _log_emit_link_start(buf, url, url_len);
    if (info) {
      info->is_link = true;
    }
    return true;
  }

  const char* p = tag;
  const char* end = tag + tag_len;
  bool found_on = false;

  while (p < end) {
    while (p < end && BEBOPC_IS_WHITE(*p)) {
      p++;
    }
    if (p >= end) {
      break;
    }

    const char* token_start = p;
    while (p < end && !BEBOPC_IS_WHITE(*p)) {
      p++;
    }
    size_t token_len = (size_t)(p - token_start);

    if (token_len == 2 && bebopc_memicmp(token_start, "on", 2) == 0) {
      found_on = true;
      continue;
    }

    int r, g, b;
    if (_log_parse_hex_color(token_start, token_len, &r, &g, &b)) {
      _log_emit_rgb(buf, r, g, b, found_on);
      continue;
    }

    if (_log_parse_rgb_color(token_start, token_len, &r, &g, &b)) {
      _log_emit_rgb(buf, r, g, b, found_on);
      continue;
    }

    const _log_color_entry_t* color = _log_lookup_color(token_start, token_len);
    if (color) {
      if (found_on) {
        _log_strbuf_append_str(buf, color->bg_code);
      } else {
        _log_strbuf_append_str(buf, color->fg_code);
      }
      continue;
    }

    const _log_style_entry_t* style = _log_lookup_style(token_start, token_len);
    if (style) {
      _log_strbuf_append_str(buf, style->code);
    }
  }

  return true;
}

#define LOG_MAX_STYLE_DEPTH 32

static bool _log_render_markup(
    _log_strbuf_t* buf, const char* markup, bool use_colors, bool strip_only
)
{
  if (!markup) {
    return true;
  }
  const char* p = markup;
  int style_depth = 0;
  bool link_stack[LOG_MAX_STYLE_DEPTH] = {false};

  while (*p) {
    if (*p == '[') {
      if (p[1] == '[') {
        _log_strbuf_append_char(buf, '[');
        p += 2;
        continue;
      }

      const char* tag_start = p + 1;
      const char* tag_end = tag_start;
      while (*tag_end && *tag_end != ']') {
        tag_end++;
      }

      if (*tag_end != ']') {
        _log_strbuf_append_char(buf, '[');
        p++;
        continue;
      }

      size_t tag_len = (size_t)(tag_end - tag_start);

      if (tag_len >= 1 && tag_start[0] == '/') {
        if (!strip_only && use_colors && style_depth > 0) {
          style_depth--;
          if (link_stack[style_depth]) {
            _log_emit_link_end(buf);
            link_stack[style_depth] = false;
          } else {
            _log_strbuf_append_str(buf, ANSI_RESET);
          }
        }
        p = tag_end + 1;
        continue;
      }

      if (!strip_only && style_depth < LOG_MAX_STYLE_DEPTH) {
        _log_tag_info_t info = {false};
        _log_parse_and_emit_tag(buf, tag_start, tag_len, use_colors, &info);
        link_stack[style_depth] = info.is_link;
      }
      if (style_depth < LOG_MAX_STYLE_DEPTH) {
        style_depth++;
      }
      p = tag_end + 1;
      continue;
    }

    if (*p == ']' && p[1] == ']') {
      _log_strbuf_append_char(buf, ']');
      p += 2;
      continue;
    }

    _log_strbuf_append_char(buf, *p);
    p++;
  }

  while (style_depth > 0 && !strip_only && use_colors) {
    style_depth--;
    if (link_stack[style_depth]) {
      _log_emit_link_end(buf);
    } else {
      _log_strbuf_append_str(buf, ANSI_RESET);
    }
  }

  return true;
}

static FILE* _log_get_output(bebopc_log_ctx_t* ctx)
{
  return (ctx && ctx->output) ? ctx->output : stderr;
}

void bebopc_log_ctx_init(bebopc_log_ctx_t* ctx, bool colors_enabled)
{
  if (!ctx) {
    return;
  }
  ctx->level = BEBOPC_LOG_INFO;
  ctx->colors_enabled = colors_enabled;
  ctx->show_timestamp = false;
  ctx->show_level = true;
  ctx->show_source = false;
  ctx->output = stderr;
}

const char* bebopc_log_level_name(bebopc_log_level_t level)
{
  switch (level) {
    case BEBOPC_LOG_TRACE:
      return "TRACE";
    case BEBOPC_LOG_DEBUG:
      return "DEBUG";
    case BEBOPC_LOG_INFO:
      return "INFO";
    case BEBOPC_LOG_WARN:
      return "WARN";
    case BEBOPC_LOG_ERROR:
      return "ERROR";
    case BEBOPC_LOG_FATAL:
      return "FATAL";
    default:
      return "?????";
  }
}

const char* bebopc_log_level_color(bebopc_log_level_t level)
{
  switch (level) {
    case BEBOPC_LOG_TRACE:
      return "[dim]";
    case BEBOPC_LOG_DEBUG:
      return "[cyan]";
    case BEBOPC_LOG_INFO:
      return "[green]";
    case BEBOPC_LOG_WARN:
      return "[yellow]";
    case BEBOPC_LOG_ERROR:
      return "[red]";
    case BEBOPC_LOG_FATAL:
      return "[bold red]";
    default:
      return "";
  }
}

bebopc_log_level_t bebopc_log_level_parse(const char* name)
{
  if (!name) {
    return BEBOPC_LOG_INFO;
  }

  if (bebopc_strcaseeq(name, "trace")) {
    return BEBOPC_LOG_TRACE;
  }
  if (bebopc_strcaseeq(name, "debug")) {
    return BEBOPC_LOG_DEBUG;
  }
  if (bebopc_strcaseeq(name, "info")) {
    return BEBOPC_LOG_INFO;
  }
  if (bebopc_strcaseeq(name, "warn")) {
    return BEBOPC_LOG_WARN;
  }
  if (bebopc_strcaseeq(name, "warning")) {
    return BEBOPC_LOG_WARN;
  }
  if (bebopc_strcaseeq(name, "error")) {
    return BEBOPC_LOG_ERROR;
  }
  if (bebopc_strcaseeq(name, "fatal")) {
    return BEBOPC_LOG_FATAL;
  }
  if (bebopc_strcaseeq(name, "off")) {
    return BEBOPC_LOG_OFF;
  }

  return BEBOPC_LOG_INFO;
}

void bebopc_logv(bebopc_log_ctx_t* ctx, bebopc_log_level_t level, const char* fmt, va_list args)
{
  if (!ctx) {
    return;
  }
  if (level < ctx->level) {
    return;
  }

  FILE* out = _log_get_output(ctx);
  bool use_colors = ctx->colors_enabled;

  _log_strbuf_t buf;
  _log_strbuf_init(&buf);

  if (ctx->show_timestamp) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    _log_strbuf_append_str(&buf, "[dim]");
    _log_strbuf_append_str(&buf, time_buf);
    _log_strbuf_append_str(&buf, "[/] ");
  }

  if (ctx->show_level) {
    _log_strbuf_append_str(&buf, bebopc_log_level_color(level));
    _log_strbuf_append_str(&buf, bebopc_log_level_name(level));
    _log_strbuf_append_str(&buf, "[/] ");
  }

  _log_strbuf_vappendf(&buf, fmt, args);

  _log_strbuf_t rendered;
  _log_strbuf_init(&rendered);
  _log_render_markup(&rendered, buf.data, use_colors, false);

  if (rendered.data) {
    fputs(rendered.data, out);
    fputc('\n', out);
    fflush(out);
  }

  _log_strbuf_free(&rendered);
  _log_strbuf_free(&buf);
}

void bebopc_log(bebopc_log_ctx_t* ctx, bebopc_log_level_t level, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  bebopc_logv(ctx, level, fmt, args);
  va_end(args);
}

void bebopc_log_src(
    bebopc_log_ctx_t* ctx, bebopc_log_level_t level, const char* file, int line, const char* fmt, ...
)
{
  if (!ctx || level < ctx->level) {
    return;
  }

  FILE* out = _log_get_output(ctx);
  bool use_colors = ctx->colors_enabled;

  _log_strbuf_t buf;
  _log_strbuf_init(&buf);

  if (ctx->show_timestamp) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    _log_strbuf_append_str(&buf, "[dim]");
    _log_strbuf_append_str(&buf, time_buf);
    _log_strbuf_append_str(&buf, "[/] ");
  }

  if (ctx->show_level) {
    _log_strbuf_append_str(&buf, bebopc_log_level_color(level));
    _log_strbuf_append_str(&buf, bebopc_log_level_name(level));
    _log_strbuf_append_str(&buf, "[/] ");
  }

  if (ctx->show_source && file) {
    const char* basename = file;
    for (const char* p = file; *p; p++) {
      if (*p == '/' || *p == '\\') {
        basename = p + 1;
      }
    }

    char loc_buf[128];
    snprintf(loc_buf, sizeof(loc_buf), "[dim]%s:%d[/] ", basename, line);
    _log_strbuf_append_str(&buf, loc_buf);
  }

  va_list args;
  va_start(args, fmt);
  _log_strbuf_vappendf(&buf, fmt, args);
  va_end(args);

  _log_strbuf_t rendered;
  _log_strbuf_init(&rendered);
  _log_render_markup(&rendered, buf.data, use_colors, false);

  if (rendered.data) {
    fputs(rendered.data, out);
    fputc('\n', out);
    fflush(out);
  }

  _log_strbuf_free(&rendered);
  _log_strbuf_free(&buf);
}

void bebopc_markupv(bebopc_log_ctx_t* ctx, const char* fmt, va_list args)
{
  FILE* out = _log_get_output(ctx);
  bool use_colors = ctx->colors_enabled;

  _log_strbuf_t buf;
  _log_strbuf_init(&buf);
  _log_strbuf_vappendf(&buf, fmt, args);

  if (!buf.data) {
    return;
  }

  _log_strbuf_t rendered;
  _log_strbuf_init(&rendered);
  _log_render_markup(&rendered, buf.data, use_colors, false);
  _log_strbuf_free(&buf);

  if (rendered.data) {
    fputs(rendered.data, out);
    fflush(out);
  }

  _log_strbuf_free(&rendered);
}

void bebopc_markup(bebopc_log_ctx_t* ctx, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  bebopc_markupv(ctx, fmt, args);
  va_end(args);
}

void bebopc_markup_line(bebopc_log_ctx_t* ctx, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  bebopc_markupv(ctx, fmt, args);
  va_end(args);
  fputc('\n', _log_get_output(ctx));
  fflush(_log_get_output(ctx));
}

void bebopc_markup_to(FILE* stream, bool use_colors, const char* fmt, ...)
{
  if (!stream) {
    return;
  }

  if (use_colors) {
    use_colors = isatty(fileno(stream)) != 0;
  }

  va_list args;
  va_start(args, fmt);

  _log_strbuf_t buf;
  _log_strbuf_init(&buf);
  _log_strbuf_vappendf(&buf, fmt, args);
  va_end(args);

  if (!buf.data) {
    return;
  }

  _log_strbuf_t rendered;
  _log_strbuf_init(&rendered);
  _log_render_markup(&rendered, buf.data, use_colors, false);
  _log_strbuf_free(&buf);

  if (rendered.data) {
    fputs(rendered.data, stream);
    fflush(stream);
  }

  _log_strbuf_free(&rendered);
}

char* bebopc_markup_strip(const char* markup)
{
  if (!markup) {
    return NULL;
  }

  _log_strbuf_t buf;
  _log_strbuf_init(&buf);
  _log_render_markup(&buf, markup, false, true);

  if (!buf.data) {
    buf.data = malloc(1);
    if (buf.data) {
      buf.data[0] = '\0';
    }
  }
  return buf.data;
}

char* bebopc_markup_render(const char* markup, bool use_colors)
{
  if (!markup) {
    return NULL;
  }

  _log_strbuf_t buf;
  _log_strbuf_init(&buf);
  _log_render_markup(&buf, markup, use_colors, false);

  if (!buf.data) {
    buf.data = malloc(1);
    if (buf.data) {
      buf.data[0] = '\0';
    }
  }
  return buf.data;
}

typedef struct {
  const char *top_left, *top, *top_sep, *top_right;
  const char *left, *sep, *right;
  const char *mid_left, *mid, *mid_sep, *mid_right;
  const char *bot_left, *bot, *bot_sep, *bot_right;
} _table_border_chars_t;

static const _table_border_chars_t _border_none = {
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};

static const _table_border_chars_t _border_rounded = {
    "╭", "─", "┬", "╮", "│", "│", "│", "├", "─", "┼", "┤", "╰", "─", "┴", "╯"
};

static const _table_border_chars_t _border_square = {
    "┌", "─", "┬", "┐", "│", "│", "│", "├", "─", "┼", "┤", "└", "─", "┴", "┘"
};

static const _table_border_chars_t _border_heavy = {
    "┏", "━", "┳", "┓", "┃", "┃", "┃", "┣", "━", "╋", "┫", "┗", "━", "┻", "┛"
};

static const _table_border_chars_t _border_double = {
    "╔", "═", "╦", "╗", "║", "║", "║", "╠", "═", "╬", "╣", "╚", "═", "╩", "╝"
};

static const _table_border_chars_t _border_ascii = {
    "+", "-", "+", "+", "|", "|", "|", "+", "-", "+", "+", "+", "-", "+", "+"
};

#define TABLE_MAX_COLS 16
#define TABLE_MAX_ROWS 64

struct bebopc_table {
  bebopc_table_border_t border;
  char* border_color;
  char* title;
  char* columns[TABLE_MAX_COLS];
  uint32_t col_count;
  char* rows[TABLE_MAX_ROWS][TABLE_MAX_COLS];
  uint32_t row_count;
};

static size_t _visible_len(const char* markup)
{
  char* stripped = bebopc_markup_strip(markup);
  size_t len = stripped ? strlen(stripped) : 0;
  free(stripped);
  return len;
}

bebopc_table_t* bebopc_table_new(void)
{
  bebopc_table_t* t = calloc(1, sizeof(bebopc_table_t));
  if (t) {
    t->border = BEBOPC_TABLE_BORDER_ROUNDED;
  }
  return t;
}

void bebopc_table_free(bebopc_table_t* t)
{
  if (!t) {
    return;
  }
  free(t->border_color);
  free(t->title);
  for (uint32_t c = 0; c < t->col_count; c++) {
    free(t->columns[c]);
  }
  for (uint32_t r = 0; r < t->row_count; r++) {
    for (uint32_t c = 0; c < t->col_count; c++) {
      free(t->rows[r][c]);
    }
  }
  free(t);
}

void bebopc_table_set_border(bebopc_table_t* t, bebopc_table_border_t border)
{
  if (t) {
    t->border = border;
  }
}

void bebopc_table_set_border_color(bebopc_table_t* t, const char* color)
{
  if (!t) {
    return;
  }
  free(t->border_color);
  t->border_color = color ? bebopc_strdup(color) : NULL;
}

void bebopc_table_set_title(bebopc_table_t* t, const char* title)
{
  if (!t) {
    return;
  }
  free(t->title);
  t->title = title ? bebopc_strdup(title) : NULL;
}

void bebopc_table_add_column(bebopc_table_t* t, const char* header)
{
  if (!t || t->col_count >= TABLE_MAX_COLS) {
    return;
  }
  t->columns[t->col_count++] = header ? bebopc_strdup(header) : bebopc_strdup("");
}

void bebopc_table_add_row(bebopc_table_t* t, ...)
{
  if (!t || t->row_count >= TABLE_MAX_ROWS) {
    return;
  }
  va_list args;
  va_start(args, t);
  for (uint32_t c = 0; c < t->col_count; c++) {
    const char* cell = va_arg(args, const char*);
    if (!cell) {
      break;
    }
    t->rows[t->row_count][c] = bebopc_strdup(cell);
  }
  va_end(args);
  t->row_count++;
}

void bebopc_table_render(bebopc_table_t* t, bebopc_log_ctx_t* ctx)
{
  if (!t || !ctx || t->col_count == 0) {
    return;
  }

  const _table_border_chars_t* b;
  switch (t->border) {
    case BEBOPC_TABLE_BORDER_NONE:
      b = &_border_none;
      break;
    case BEBOPC_TABLE_BORDER_SQUARE:
      b = &_border_square;
      break;
    case BEBOPC_TABLE_BORDER_HEAVY:
      b = &_border_heavy;
      break;
    case BEBOPC_TABLE_BORDER_DOUBLE:
      b = &_border_double;
      break;
    case BEBOPC_TABLE_BORDER_ASCII:
      b = &_border_ascii;
      break;
    default:
      b = &_border_rounded;
      break;
  }

  size_t widths[TABLE_MAX_COLS] = {0};
  for (uint32_t c = 0; c < t->col_count; c++) {
    widths[c] = _visible_len(t->columns[c]);
    for (uint32_t r = 0; r < t->row_count; r++) {
      size_t w = _visible_len(t->rows[r][c] ? t->rows[r][c] : "");
      if (w > widths[c]) {
        widths[c] = w;
      }
    }
    if (widths[c] < 1) {
      widths[c] = 1;
    }
  }

  FILE* out = ctx->output ? ctx->output : stderr;
  bool colors = ctx->colors_enabled;
  const char* bc = t->border_color ? t->border_color : "grey";

  _log_strbuf_t line;
  _log_strbuf_init(&line);

#define BORDER(s) \
  do { \
    if (t->border != BEBOPC_TABLE_BORDER_NONE) { \
      _log_strbuf_append_str(&line, "["); \
      _log_strbuf_append_str(&line, bc); \
      _log_strbuf_append_str(&line, "]"); \
      _log_strbuf_append_str(&line, s); \
      _log_strbuf_append_str(&line, "[/]"); \
    } \
  } while (0)

#define REPEAT(s, n) \
  do { \
    for (size_t _i = 0; _i < (n); _i++) \
      _log_strbuf_append_str(&line, s); \
  } while (0)

#define HLINE(s, n) \
  do { \
    if (t->border != BEBOPC_TABLE_BORDER_NONE) { \
      _log_strbuf_append_str(&line, "["); \
      _log_strbuf_append_str(&line, bc); \
      _log_strbuf_append_str(&line, "]"); \
      for (size_t _i = 0; _i < (n); _i++) \
        _log_strbuf_append_str(&line, s); \
      _log_strbuf_append_str(&line, "[/]"); \
    } \
  } while (0)

#define FLUSH() \
  do { \
    if (line.data) { \
      char* rendered = bebopc_markup_render(line.data, colors); \
      if (rendered) { \
        fputs(rendered, out); \
        free(rendered); \
      } \
      fputc('\n', out); \
    } \
    line.len = 0; \
    if (line.data) \
      line.data[0] = '\0'; \
  } while (0)

  BORDER(b->top_left);
  for (uint32_t c = 0; c < t->col_count; c++) {
    if (c > 0) {
      BORDER(b->top_sep);
    }
    HLINE(b->top, widths[c] + 2);
  }
  BORDER(b->top_right);
  FLUSH();

  BORDER(b->left);
  for (uint32_t c = 0; c < t->col_count; c++) {
    if (c > 0) {
      BORDER(b->sep);
    }
    _log_strbuf_append_str(&line, " [bold]");
    _log_strbuf_append_str(&line, t->columns[c] ? t->columns[c] : "");
    _log_strbuf_append_str(&line, "[/]");
    size_t vlen = _visible_len(t->columns[c] ? t->columns[c] : "");
    for (size_t i = vlen; i < widths[c]; i++) {
      _log_strbuf_append_char(&line, ' ');
    }
    _log_strbuf_append_char(&line, ' ');
  }
  BORDER(b->right);
  FLUSH();

  BORDER(b->mid_left);
  for (uint32_t c = 0; c < t->col_count; c++) {
    if (c > 0) {
      BORDER(b->mid_sep);
    }
    HLINE(b->mid, widths[c] + 2);
  }
  BORDER(b->mid_right);
  FLUSH();

  for (uint32_t r = 0; r < t->row_count; r++) {
    BORDER(b->left);
    for (uint32_t c = 0; c < t->col_count; c++) {
      if (c > 0) {
        BORDER(b->sep);
      }
      const char* cell = t->rows[r][c] ? t->rows[r][c] : "";
      _log_strbuf_append_str(&line, " ");
      _log_strbuf_append_str(&line, cell);
      size_t vlen = _visible_len(cell);
      for (size_t i = vlen; i < widths[c]; i++) {
        _log_strbuf_append_char(&line, ' ');
      }
      _log_strbuf_append_char(&line, ' ');
    }
    BORDER(b->right);
    FLUSH();
  }

  BORDER(b->bot_left);
  for (uint32_t c = 0; c < t->col_count; c++) {
    if (c > 0) {
      BORDER(b->bot_sep);
    }
    HLINE(b->bot, widths[c] + 2);
  }
  BORDER(b->bot_right);
  FLUSH();

#undef BORDER
#undef REPEAT
#undef HLINE
#undef FLUSH

  _log_strbuf_free(&line);
  fflush(out);
}
