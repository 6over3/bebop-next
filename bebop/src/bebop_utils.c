const int8_t bebop_utf8_width_table[32] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0,
};

int bebop_utf8_decode(const char* s, const size_t len, bebop_codepoint_t* cp)
{
  if (!s || len == 0) {
    *cp = BEBOP_CP_INVALID;
    return 0;
  }

  const uint8_t* p = (const uint8_t*)s;
  const uint8_t a = p[0];

  if (a < 0x80) {
    *cp = a;
    return 1;
  }

  const int width = bebop_utf8_width_table[a >> 3];
  if (width == 0 || (size_t)width > len) {
    *cp = BEBOP_CP_INVALID;
    return 0;
  }

  uint32_t code;

  if (width == 2) {
    const uint8_t b = p[1];
    if ((b & 0xC0) != 0x80) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
    code = (uint32_t)(((a & 0x1F) << 6) | (b & 0x3F));
    if (code < 0x80) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
  } else if (width == 3) {
    const uint8_t b = p[1], c = p[2];
    if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
    code = (uint32_t)(((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F));
    if (code < 0x800) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
    if (code >= 0xD800 && code <= 0xDFFF) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
  } else {
    const uint8_t b = p[1], c = p[2], d = p[3];
    if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 || (d & 0xC0) != 0x80) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
    code = (uint32_t)(((a & 0x07) << 18) | ((b & 0x3F) << 12) | ((c & 0x3F) << 6) | (d & 0x3F));
    if (code < 0x10000 || code > 0x10FFFF) {
      *cp = BEBOP_CP_INVALID;
      return 0;
    }
  }

  *cp = (bebop_codepoint_t)code;
  return width;
}

int bebop_utf8_encode(const bebop_codepoint_t cp, char* dst)
{
  if (!dst) {
    return 0;
  }

  uint8_t* s = (uint8_t*)dst;

  if (cp < 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
    return 0;
  }

  if (cp < 0x80) {
    s[0] = (uint8_t)cp;
    return 1;
  }
  if (cp < 0x800) {
    s[0] = (uint8_t)(cp >> 6 | 0xC0);
    s[1] = (uint8_t)((cp & 0x3F) | 0x80);
    return 2;
  }
  if (cp < 0x10000) {
    s[0] = (uint8_t)((cp >> 12) | 0xE0);
    s[1] = (uint8_t)(((cp >> 6) & 0x3F) | 0x80);
    s[2] = (uint8_t)((cp & 0x3F) | 0x80);
    return 3;
  }
  s[0] = (uint8_t)((cp >> 18) | 0xF0);
  s[1] = (uint8_t)(((cp >> 12) & 0x3F) | 0x80);
  s[2] = (uint8_t)(((cp >> 6) & 0x3F) | 0x80);
  s[3] = (uint8_t)((cp & 0x3F) | 0x80);
  return 4;
}

bool bebop_utf8_valid(const char* s, const size_t len)
{
  size_t pos = 0;
  while (pos < len) {
    bebop_codepoint_t cp;
    const int consumed = bebop_utf8_decode(s + pos, len - pos, &cp);
    if (consumed <= 0 || cp == BEBOP_CP_INVALID) {
      return false;
    }
    pos += (size_t)consumed;
  }
  return true;
}

int bebop_unescape_char(const char* s, size_t len, char* out, int* out_len)
{
  if (!s || len == 0 || !out || !out_len) {
    return 0;
  }

  const char c = s[0];

  switch (c) {
    case '\\':
      out[0] = '\\';
      *out_len = 1;
      return 1;
    case 'n':
      out[0] = '\n';
      *out_len = 1;
      return 1;
    case 'r':
      out[0] = '\r';
      *out_len = 1;
      return 1;
    case 't':
      out[0] = '\t';
      *out_len = 1;
      return 1;
    case '0':
      out[0] = '\0';
      *out_len = 1;
      return 1;
    case '"':
      out[0] = '"';
      *out_len = 1;
      return 1;
    case '\'':
      out[0] = '\'';
      *out_len = 1;
      return 1;
    case 'x': {
      if (len < 3) {
        return 0;
      }
      const int hi = BEBOP_HEX_VALUE(s[1]);
      const int lo = BEBOP_HEX_VALUE(s[2]);
      if (hi < 0 || lo < 0) {
        return 0;
      }
      out[0] = (char)((hi << 4) | lo);
      *out_len = 1;
      return 3;
    }
    case 'u': {
      if (len < 3 || s[1] != '{') {
        return 0;
      }
      size_t pos = 2;
      uint32_t codepoint = 0;
      int digits = 0;
      while (pos < len && s[pos] != '}') {
        const int val = BEBOP_HEX_VALUE(s[pos]);
        if (val < 0) {
          return 0;
        }
        codepoint = (codepoint << 4) | (uint32_t)val;
        digits++;
        pos++;
        if (digits > 6) {
          return 0;
        }
      }
      if (pos >= len || s[pos] != '}' || digits == 0) {
        return 0;
      }
      const int utf8_len = bebop_utf8_encode((bebop_codepoint_t)codepoint, out);
      if (utf8_len == 0) {
        return 0;
      }
      *out_len = utf8_len;
      return (int)(pos + 1);
    }
    default:
      return 0;
  }
}

static const uint8_t bebop__digit_val[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

#if defined(_MSC_VER)
#define BEBOP_HAS_STRTOD_L 1

static _locale_t bebop__c_locale(void)
{
  static _locale_t loc = NULL;
  if (!loc) {
    loc = _create_locale(LC_ALL, "C");
  }
  return loc;
}
#elif defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) \
    || defined(__OpenBSD__)
#define BEBOP_HAS_STRTOD_L 1

static locale_t bebop__c_locale(void)
{
  static locale_t loc = NULL;
  if (!loc) {
    loc = newlocale(LC_ALL_MASK, "C", NULL);
  }
  return loc;
}
#endif

uint32_t bebop_util_hash_method_id(const char* input, const size_t length)
{
  size_t idx = 0;
  uint32_t hash = bebop__HASH_SEED;

  while (idx + 4 <= length) {
    uint32_t block = (uint32_t)(uint8_t)input[idx] | (uint32_t)(uint8_t)input[idx + 1] << 8
        | (uint32_t)(uint8_t)input[idx + 2] << 16 | (uint32_t)(uint8_t)input[idx + 3] << 24;

    block *= bebop__HASH_C1;
    block = block << 15 | block >> 17;
    block *= bebop__HASH_C2;

    hash ^= block;
    hash = hash << 13 | hash >> 19;
    hash = hash * 5 + bebop__HASH_N;

    idx += 4;
  }

  uint32_t tail = 0;
  const size_t remaining = length - idx;

  switch (remaining) {
    case 3:
      tail |= (uint32_t)(uint8_t)input[idx + 2] << 16;
      /* fallthrough */
    case 2:
      tail |= (uint32_t)(uint8_t)input[idx + 1] << 8;
      /* fallthrough */
    case 1:
      tail |= (uint32_t)(uint8_t)input[idx];
      tail *= bebop__HASH_C1;
      tail = tail << 15 | tail >> 17;
      tail *= bebop__HASH_C2;
      hash ^= tail;
      break;
  }

  hash ^= hash >> 16;
  hash *= 0x7feb352d;
  hash ^= hash >> 15;
  hash *= 0x846ca68b;
  hash ^= hash >> 16;

  return hash;
}

static uint64_t bebop__util_parse_decimal(const char** s, const char* end, bool* overflow)
{
  uint64_t acc = 0, prev;
  bool of = false;
  const char* p = *s;
  uint8_t d;

  while (p < end && (d = bebop__digit_val[(unsigned char)*p]) < 10) {
    prev = acc;
    acc = acc * 10 + d;
    if (acc < prev) {
      of = true;
    }
    p++;
  }

  *s = p;
  if (overflow) {
    *overflow = of;
  }
  return of ? UINT64_MAX : acc;
}

static uint64_t bebop__util_parse_hex(const char** s, const char* end, bool* overflow)
{
  uint64_t acc = 0, prev;
  bool of = false;
  const char* p = *s;
  uint8_t d;

  while (p < end && (d = bebop__digit_val[(unsigned char)*p]) < 16) {
    prev = acc;
    acc = acc << 4 | d;
    if (acc < prev) {
      of = true;
    }
    p++;
  }

  *s = p;
  if (overflow) {
    *overflow = of;
  }
  return of ? UINT64_MAX : acc;
}

bool bebop_util_parse_int(const char* str, const size_t len, int64_t* out)
{
  if (!str || len == 0 || !out) {
    return false;
  }

  const char* p = str;
  const char* end = str + len;
  bool neg = false;
  bool overflow = false;

  if (*p == '-') {
    neg = true;
    p++;
  } else if (*p == '+') {
    p++;
  }

  if (p >= end) {
    return false;
  }

  uint64_t uval;

  if (p + 2 <= end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    if (p >= end || bebop__digit_val[(unsigned char)*p] >= 16) {
      return false;
    }
    uval = bebop__util_parse_hex(&p, end, &overflow);
  } else {
    if (bebop__digit_val[(unsigned char)*p] >= 10) {
      return false;
    }
    uval = bebop__util_parse_decimal(&p, end, &overflow);
  }

  if (p != end || overflow) {
    return false;
  }

  if (neg) {
    if (uval > (uint64_t)INT64_MAX + 1) {
      return false;
    }
    *out = -(int64_t)uval;
  } else {
    if (uval > (uint64_t)INT64_MAX) {
      return false;
    }
    *out = (int64_t)uval;
  }
  return true;
}

bool bebop_util_parse_uint(const char* str, const size_t len, uint64_t* out)
{
  if (!str || len == 0 || !out) {
    return false;
  }

  const char* p = str;
  const char* end = str + len;
  bool overflow = false;

  if (*p == '-') {
    return false;
  }
  if (*p == '+') {
    p++;
  }

  if (p >= end) {
    return false;
  }

  uint64_t val;

  if (p + 2 <= end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    if (p >= end || bebop__digit_val[(unsigned char)*p] >= 16) {
      return false;
    }
    val = bebop__util_parse_hex(&p, end, &overflow);
  } else {
    if (bebop__digit_val[(unsigned char)*p] >= 10) {
      return false;
    }
    val = bebop__util_parse_decimal(&p, end, &overflow);
  }

  if (p != end || overflow) {
    return false;
  }

  *out = val;
  return true;
}

bool bebop_util_parse_float(const char* str, size_t len, double* out)
{
  if (!str || len == 0 || !out) {
    return false;
  }

  if (bebop_streqni("inf", str, len)) {
    *out = (double)INFINITY;
    return true;
  }
  if (bebop_streqni("nan", str, len)) {
    *out = (double)NAN;
    return true;
  }
  if (bebop_streqni("-inf", str, len)) {
    *out = (double)-INFINITY;
    return true;
  }
  if (bebop_streqni("+inf", str, len)) {
    *out = (double)INFINITY;
    return true;
  }

  char buf[64];
  if (len >= sizeof(buf)) {
    return false;
  }
  memcpy(buf, str, len);
  buf[len] = '\0';

  char* endptr;
  errno = 0;

#if BEBOP_HAS_STRTOD_L
#if defined(_MSC_VER)
  double val = _strtod_l(buf, &endptr, bebop__c_locale());
#else
  const double val = strtod_l(buf, &endptr, bebop__c_locale());
#endif
#else
  double val = strtod(buf, &endptr);
#endif

  if (errno != 0 || endptr != buf + len) {
    return false;
  }

  *out = val;
  return true;
}

static bool bebop__util_parse_hex_byte(const char* s, uint8_t* out)
{
  const int hi = BEBOP_HEX_VALUE(s[0]);
  const int lo = BEBOP_HEX_VALUE(s[1]);
  if (hi < 0 || lo < 0) {
    return false;
  }
  *out = (uint8_t)(hi << 4 | lo);
  return true;
}

bool bebop_util_parse_uuid(const char* str, const size_t len, uint8_t out[16])
{
  if (!str || !out) {
    return false;
  }

  const char* p = str;

  if (len == 36) {
    if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-') {
      return false;
    }

    // RFC 4122 byte order: big-endian for all fields
    // time_low (4 bytes)
    if (!bebop__util_parse_hex_byte(p + 0, &out[0])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 2, &out[1])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 4, &out[2])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 6, &out[3])) {
      return false;
    }
    p += 9;

    // time_mid (2 bytes)
    if (!bebop__util_parse_hex_byte(p + 0, &out[4])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 2, &out[5])) {
      return false;
    }
    p += 5;

    // time_hi_and_version (2 bytes)
    if (!bebop__util_parse_hex_byte(p + 0, &out[6])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 2, &out[7])) {
      return false;
    }
    p += 5;

    // clock_seq (2 bytes)
    if (!bebop__util_parse_hex_byte(p + 0, &out[8])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 2, &out[9])) {
      return false;
    }
    p += 5;

    // node (6 bytes)
    if (!bebop__util_parse_hex_byte(p + 0, &out[10])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 2, &out[11])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 4, &out[12])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 6, &out[13])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 8, &out[14])) {
      return false;
    }
    if (!bebop__util_parse_hex_byte(p + 10, &out[15])) {
      return false;
    }

    return true;
  }
  if (len == 32) {
    // RFC 4122 byte order: straight through, no byte swapping
    for (int i = 0; i < 16; i++) {
      if (!bebop__util_parse_hex_byte(p + i * 2, &out[i])) {
        return false;
      }
    }
    return true;
  }

  return false;
}

uint32_t bebop_util_levenshtein(
    const char* a, size_t a_len, const char* b, size_t b_len, uint32_t max_dist
)
{
  if (a_len == 0) {
    return (uint32_t)b_len;
  }
  if (b_len == 0) {
    return (uint32_t)a_len;
  }

  const size_t len_diff = a_len > b_len ? a_len - b_len : b_len - a_len;
  if (len_diff > max_dist) {
    return max_dist + 1;
  }

  if (a_len + 1 > 64 || b_len + 1 > 64) {
    return max_dist + 1;
  }

  uint32_t d[64][64];
  for (size_t i = 0; i <= a_len; i++) {
    d[i][0] = (uint32_t)i;
  }
  for (size_t j = 0; j <= b_len; j++) {
    d[0][j] = (uint32_t)j;
  }

  for (size_t i = 1; i <= a_len; i++) {
    for (size_t j = 1; j <= b_len; j++) {
      const uint32_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      const uint32_t del = d[i - 1][j] + 1;
      const uint32_t ins = d[i][j - 1] + 1;
      const uint32_t sub = d[i - 1][j - 1] + cost;

      d[i][j] = del < ins ? del : ins;
      if (sub < d[i][j]) {
        d[i][j] = sub;
      }

      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
        const uint32_t trans = d[i - 2][j - 2] + 1;
        if (trans < d[i][j]) {
          d[i][j] = trans;
        }
      }
    }
  }

  return d[a_len][b_len] > max_dist ? max_dist + 1 : d[a_len][b_len];
}

const char* bebop_util_fuzzy_match(
    const char* input,
    size_t input_len,
    const char* const* candidates,
    size_t candidate_count,
    uint32_t max_distance
)
{
  if (!input || input_len == 0 || !candidates || candidate_count == 0) {
    return NULL;
  }

  const char* best = NULL;
  uint32_t best_dist = max_distance + 1;

  for (size_t i = 0; i < candidate_count; i++) {
    const char* c = candidates[i];
    if (!c) {
      continue;
    }
    const uint32_t dist = bebop_util_levenshtein(input, input_len, c, strlen(c), best_dist - 1);
    if (dist < best_dist) {
      best_dist = dist;
      best = c;
      if (dist == 0) {
        break;
      }
    }
  }
  return best;
}

bool bebop_util_scan_int(const char* str, const size_t len)
{
  if (!str || len == 0) {
    return false;
  }

  const char* p = str;
  const char* end = str + len;

  if (*p == '+' || *p == '-') {
    p++;
    if (p >= end) {
      return false;
    }
  }

  if (p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    if (p >= end || bebop__digit_val[(unsigned char)*p] >= 16) {
      return false;
    }
    while (p < end && bebop__digit_val[(unsigned char)*p] < 16) {
      p++;
    }
    return p == end;
  }

  if (bebop__digit_val[(unsigned char)*p] >= 10) {
    return false;
  }
  while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
    p++;
  }

  return p == end;
}

bool bebop_util_scan_float(const char* str, const size_t len)
{
  if (!str || len == 0) {
    return false;
  }

  const char* p = str;
  const char* end = str + len;

  if (*p == '+' || *p == '-') {
    p++;
    if (p >= end) {
      return false;
    }
  }

  bool has_digits = false;
  bool has_dot = false;
  bool has_exp = false;

  while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
    has_digits = true;
    p++;
  }

  if (p < end && *p == '.') {
    has_dot = true;
    p++;
    while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
      has_digits = true;
      p++;
    }
  }

  if (!has_digits) {
    return false;
  }

  if (p < end && (*p == 'e' || *p == 'E')) {
    has_exp = true;
    p++;
    if (p < end && (*p == '+' || *p == '-')) {
      p++;
    }
    if (p >= end || bebop__digit_val[(unsigned char)*p] >= 10) {
      return false;
    }
    while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
      p++;
    }
  }

  return p == end && (has_dot || has_exp);
}

static bool bebop__util_parse_2digit(const char** p, const char* end, int* out)
{
  if (*p + 2 > end) {
    return false;
  }
  const uint8_t d0 = bebop__digit_val[(unsigned char)(*p)[0]];
  const uint8_t d1 = bebop__digit_val[(unsigned char)(*p)[1]];
  if (d0 >= 10 || d1 >= 10) {
    return false;
  }
  *out = d0 * 10 + d1;
  *p += 2;
  return true;
}

static bool bebop__util_parse_4digit(const char** p, const char* end, int* out)
{
  if (*p + 4 > end) {
    return false;
  }
  const uint8_t d0 = bebop__digit_val[(unsigned char)(*p)[0]];
  const uint8_t d1 = bebop__digit_val[(unsigned char)(*p)[1]];
  const uint8_t d2 = bebop__digit_val[(unsigned char)(*p)[2]];
  const uint8_t d3 = bebop__digit_val[(unsigned char)(*p)[3]];
  if (d0 >= 10 || d1 >= 10 || d2 >= 10 || d3 >= 10) {
    return false;
  }
  *out = d0 * 1000 + d1 * 100 + d2 * 10 + d3;
  *p += 4;
  return true;
}

static int64_t bebop__util_days_from_civil(int y, int m, int d)
{
  y -= (m <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - (int)(era * 400);
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool bebop_util_parse_timestamp(
    const char* str,
    const size_t len,
    int64_t* out_seconds,
    int32_t* out_nanos,
    int32_t* out_offset_ms
)
{
  if (!str || len < 20 || !out_seconds || !out_nanos || !out_offset_ms) {
    return false;
  }

  const char* p = str;
  const char* end = str + len;

  int year, month, day, hour, minute, second;

  if (!bebop__util_parse_4digit(&p, end, &year)) {
    return false;
  }
  if (p >= end || *p != '-') {
    return false;
  }
  p++;

  if (!bebop__util_parse_2digit(&p, end, &month)) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }
  if (p >= end || *p != '-') {
    return false;
  }
  p++;

  if (!bebop__util_parse_2digit(&p, end, &day)) {
    return false;
  }
  if (day < 1 || day > 31) {
    return false;
  }

  if (p >= end || (*p != 'T' && *p != 't' && *p != ' ')) {
    return false;
  }
  p++;

  if (!bebop__util_parse_2digit(&p, end, &hour)) {
    return false;
  }
  if (hour > 23) {
    return false;
  }
  if (p >= end || *p != ':') {
    return false;
  }
  p++;

  if (!bebop__util_parse_2digit(&p, end, &minute)) {
    return false;
  }
  if (minute > 59) {
    return false;
  }
  if (p >= end || *p != ':') {
    return false;
  }
  p++;

  if (!bebop__util_parse_2digit(&p, end, &second)) {
    return false;
  }
  if (second > 60) {
    return false;
  }

  int32_t nanos = 0;
  if (p < end && *p == '.') {
    p++;
    int32_t frac = 0;
    int digits = 0;
    while (p < end && digits < 9) {
      const uint8_t d = bebop__digit_val[(unsigned char)*p];
      if (d >= 10) {
        break;
      }
      frac = frac * 10 + d;
      digits++;
      p++;
    }
    while (digits < 9) {
      frac *= 10;
      digits++;
    }
    nanos = frac;
    while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
      p++;
    }
  }

  int64_t tz_offset = 0;
  int32_t offset_ms = 0;
  if (p < end) {
    if (*p == 'Z' || *p == 'z') {
      p++;
    } else if (*p == '+' || *p == '-') {
      const bool neg = (*p == '-');
      p++;
      int tz_hour = 0, tz_min = 0, tz_sec = 0, tz_ms = 0;
      if (!bebop__util_parse_2digit(&p, end, &tz_hour)) {
        return false;
      }
      if (tz_hour > 23) {
        return false;
      }
      if (p < end && *p == ':') {
        p++;
      }
      if (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
        if (!bebop__util_parse_2digit(&p, end, &tz_min)) {
          return false;
        }
        if (tz_min > 59) {
          return false;
        }
      }
      if (p < end && *p == ':') {
        p++;
        if (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
          if (!bebop__util_parse_2digit(&p, end, &tz_sec)) {
            return false;
          }
          if (tz_sec > 59) {
            return false;
          }
        }
      }
      if (p < end && *p == '.') {
        p++;
        int frac = 0;
        int digits = 0;
        while (p < end && digits < 3) {
          const uint8_t d = bebop__digit_val[(unsigned char)*p];
          if (d >= 10) {
            break;
          }
          frac = frac * 10 + d;
          digits++;
          p++;
        }
        while (digits < 3) {
          frac *= 10;
          digits++;
        }
        tz_ms = frac;
        while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
          p++;
        }
      }
      tz_offset = (int64_t)tz_hour * 3600 + (int64_t)tz_min * 60 + (int64_t)tz_sec;
      offset_ms = tz_hour * 3600000 + tz_min * 60000 + tz_sec * 1000 + tz_ms;
      if (neg) {
        tz_offset = -tz_offset;
        offset_ms = -offset_ms;
      }
      if (offset_ms < -86400000 || offset_ms > 86400000) {
        return false;
      }
    }
  }

  if (p != end) {
    return false;
  }

  const int64_t days = bebop__util_days_from_civil(year, month, day);
  const int64_t secs =
      days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + (int64_t)second - tz_offset;

  *out_seconds = secs;
  *out_nanos = nanos;
  *out_offset_ms = offset_ms;
  return true;
}

bool bebop_util_parse_duration(
    const char* str, const size_t len, int64_t* out_seconds, int32_t* out_nanos
)
{
  if (!str || len == 0 || !out_seconds || !out_nanos) {
    return false;
  }

  const char* p = str;
  const char* end = str + len;
  bool neg = false;

  if (*p == '-') {
    neg = true;
    p++;
  } else if (*p == '+') {
    p++;
  }

  if (p >= end) {
    return false;
  }

  int64_t total_nanos = 0;
  bool had_value = false;

  while (p < end) {
    uint64_t val = 0;
    int digits = 0;
    while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
      val = val * 10 + bebop__digit_val[(unsigned char)*p];
      digits++;
      p++;
    }

    uint64_t frac_nanos = 0;
    int frac_digits = 0;
    if (p < end && *p == '.') {
      p++;
      uint64_t frac = 0;
      while (p < end && frac_digits < 9) {
        const uint8_t d = bebop__digit_val[(unsigned char)*p];
        if (d >= 10) {
          break;
        }
        frac = frac * 10 + d;
        frac_digits++;
        p++;
      }
      while (frac_digits < 9) {
        frac *= 10;
        frac_digits++;
      }
      frac_nanos = frac;
      while (p < end && bebop__digit_val[(unsigned char)*p] < 10) {
        p++;
      }
    }

    if (digits == 0 && frac_digits == 0) {
      return false;
    }

    if (p >= end) {
      if (!had_value) {
        total_nanos = (int64_t)val * 1000000000LL + (int64_t)frac_nanos;
        had_value = true;
      }
      break;
    }

    const char unit = *p;
    p++;

    uint64_t multiplier_ns = 0;
    bool is_ms = false;
    bool is_us = false;
    bool is_ns = false;

    switch (unit) {
      case 'h':
      case 'H':
        multiplier_ns = 3600000000000ULL;
        break;
      case 'm':
      case 'M':
        if (p < end && (*p == 's' || *p == 'S')) {
          p++;
          is_ms = true;
          multiplier_ns = 1000000ULL;
        } else {
          multiplier_ns = 60000000000ULL;
        }
        break;
      case 's':
      case 'S':
        multiplier_ns = 1000000000ULL;
        break;
      case 'u':
        if (p < end && (*p == 's' || *p == 'S')) {
          p++;
        }
        is_us = true;
        multiplier_ns = 1000ULL;
        break;
      case 'n':
        if (p < end && (*p == 's' || *p == 'S')) {
          p++;
        }
        is_ns = true;
        multiplier_ns = 1ULL;
        break;
      default:
        return false;
    }

    (void)is_ms;
    (void)is_us;
    (void)is_ns;

    total_nanos += (int64_t)(val * multiplier_ns);
    if (frac_digits > 0 && multiplier_ns >= 1000000000ULL) {
      total_nanos += (int64_t)frac_nanos * (int64_t)(multiplier_ns / 1000000000ULL);
    }
    had_value = true;
  }

  if (!had_value) {
    return false;
  }

  if (neg) {
    total_nanos = -total_nanos;
  }

  *out_seconds = total_nanos / 1000000000LL;
  *out_nanos = (int32_t)(total_nanos % 1000000000LL);

  if (*out_nanos < 0 && *out_seconds > 0) {
    *out_seconds -= 1;
    *out_nanos += 1000000000;
  } else if (*out_nanos > 0 && *out_seconds < 0) {
    *out_seconds += 1;
    *out_nanos -= 1000000000;
  }

  return true;
}
