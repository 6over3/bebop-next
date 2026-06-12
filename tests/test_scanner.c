#include "bebop.c"
#include "test_common.h"
#include "unity.h"

#define FIXTURE(name) BEBOP_TEST_FIXTURES_DIR "/valid/" name

static bebop_context_t* ctx;
static bebop_token_stream_t stream;
static uint32_t tok_idx;

void setUp(void)
{
  ctx = test_context_create();
  TEST_ASSERT_NOT_NULL(ctx);
  stream = (bebop_token_stream_t) {0};
  tok_idx = 0;
}

void tearDown(void)
{
  if (ctx) {
    bebop_context_destroy(ctx);
    ctx = NULL;
  }
}

static void scan(const char* source)
{
  stream = bebop_scan(ctx, source, strlen(source));
  tok_idx = 0;
}

static bebop_token_t next_token(void)
{
  TEST_ASSERT_TRUE(tok_idx < stream.count);
  return stream.tokens[tok_idx++];
}

static bebop_token_t expect_token(bebop_token_kind_t kind, const char* lexeme)
{
  bebop_token_t tok = next_token();
  TEST_ASSERT_EQUAL_INT(kind, tok.kind);
  if (lexeme) {
    const char* actual = bebop_str_get(BEBOP_INTERN(ctx), tok.lexeme);
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_STRING(lexeme, actual);
  }
  return tok;
}

static void expect_eof(void)
{
  bebop_token_t tok = next_token();
  TEST_ASSERT_EQUAL_INT(BEBOP_TOKEN_EOF, tok.kind);
}

void test_scanner_keyword_enum(void);
void test_scanner_keyword_struct(void);
void test_scanner_keyword_message(void);
void test_scanner_keyword_mut(void);
void test_scanner_keyword_map(void);
void test_scanner_keyword_array(void);
void test_scanner_keyword_union(void);
void test_scanner_keyword_service(void);
void test_scanner_keyword_stream(void);
void test_scanner_keyword_true(void);
void test_scanner_keyword_false(void);
void test_scanner_keyword_const(void);
void test_scanner_keyword_readonly(void);
void test_scanner_keyword_import(void);
void test_scanner_keyword_edition(void);
void test_scanner_keyword_package(void);
void test_scanner_keyword_export(void);
void test_scanner_keyword_local(void);
void test_scanner_all_keywords(void);

void test_scanner_simple_identifier(void);
void test_scanner_identifier_with_underscore(void);
void test_scanner_identifier_with_numbers(void);
void test_scanner_identifier_starting_underscore(void);

void test_scanner_decimal_number(void);
void test_scanner_hex_number(void);
void test_scanner_float_number(void);
void test_scanner_negative_exponent(void);

void test_scanner_simple_string(void);
void test_scanner_string_with_spaces(void);
void test_scanner_empty_string(void);
void test_scanner_string_with_escaped_quote(void);
void test_scanner_single_quoted_string(void);
void test_scanner_string_escape_backslash(void);
void test_scanner_string_escape_newline(void);
void test_scanner_string_escape_tab(void);
void test_scanner_string_escape_unicode(void);
void test_scanner_string_escape_unicode_emoji(void);
void test_scanner_string_escape_literal_backslash_u(void);
void test_scanner_string_escape_unicode_surrogate(void);
void test_scanner_string_escape_unicode_too_large(void);
void test_scanner_string_escape_unicode_empty(void);
void test_scanner_string_escape_carriage_return(void);
void test_scanner_string_escape_null(void);
void test_scanner_string_escape_single_quote(void);
void test_scanner_string_escape_double_quote(void);
void test_scanner_string_escape_unicode_single_digit(void);
void test_scanner_string_escape_unicode_max_valid(void);
void test_scanner_string_multiple_escapes(void);
void test_scanner_string_escape_invalid(void);
void test_scanner_string_invalid_utf8(void);

void test_scanner_bytes_simple(void);
void test_scanner_bytes_hex_escapes(void);
void test_scanner_bytes_mixed(void);
void test_scanner_bytes_empty(void);
void test_scanner_bytes_non_utf8(void);
void test_scanner_bytes_single_quoted(void);

void test_scanner_all_symbols(void);
void test_scanner_arrow(void);

void test_scanner_line_comment_trivia(void);
void test_scanner_block_comment_trivia(void);
void test_scanner_doc_comment_trivia(void);

void test_scanner_whitespace_trivia(void);
void test_scanner_newline_trivia(void);
void test_scanner_mixed_trivia(void);
void test_scanner_crlf_newline(void);

void test_scanner_span_single_token(void);
void test_scanner_span_multiline(void);

void test_scanner_raw_block_simple(void);
void test_scanner_raw_block_multiline(void);
void test_scanner_raw_block_with_brackets(void);
void test_scanner_raw_block_empty(void);
void test_scanner_raw_block_unterminated(void);
void test_scanner_single_bracket_not_raw(void);

void test_scanner_unknown_character(void);
void test_scanner_unterminated_string(void);
void test_scanner_string_crlf_normalized(void);

void test_scanner_fixture_struct(void);
void test_scanner_fixture_enum(void);

void test_scanner_keyword_enum(void)
{
  scan("enum");
  expect_token(BEBOP_TOKEN_ENUM, "enum");
  expect_eof();
}

void test_scanner_keyword_struct(void)
{
  scan("struct");
  expect_token(BEBOP_TOKEN_STRUCT, "struct");
  expect_eof();
}

void test_scanner_keyword_message(void)
{
  scan("message");
  expect_token(BEBOP_TOKEN_MESSAGE, "message");
  expect_eof();
}

void test_scanner_keyword_mut(void)
{
  scan("mut");
  expect_token(BEBOP_TOKEN_MUT, "mut");
  expect_eof();
}

void test_scanner_keyword_map(void)
{
  scan("map");
  expect_token(BEBOP_TOKEN_MAP, "map");
  expect_eof();
}

void test_scanner_keyword_array(void)
{
  scan("array");
  expect_token(BEBOP_TOKEN_ARRAY, "array");
  expect_eof();
}

void test_scanner_keyword_union(void)
{
  scan("union");
  expect_token(BEBOP_TOKEN_UNION, "union");
  expect_eof();
}

void test_scanner_keyword_service(void)
{
  scan("service");
  expect_token(BEBOP_TOKEN_SERVICE, "service");
  expect_eof();
}

void test_scanner_keyword_stream(void)
{
  scan("stream");
  expect_token(BEBOP_TOKEN_STREAM, "stream");
  expect_eof();
}

void test_scanner_keyword_true(void)
{
  scan("true");
  expect_token(BEBOP_TOKEN_TRUE, "true");
  expect_eof();
}

void test_scanner_keyword_false(void)
{
  scan("false");
  expect_token(BEBOP_TOKEN_FALSE, "false");
  expect_eof();
}

void test_scanner_keyword_const(void)
{
  scan("const");
  expect_token(BEBOP_TOKEN_CONST, "const");
  expect_eof();
}

void test_scanner_keyword_readonly(void)
{
  scan("readonly");
  expect_token(BEBOP_TOKEN_READONLY, "readonly");
  expect_eof();
}

void test_scanner_keyword_import(void)
{
  scan("import");
  expect_token(BEBOP_TOKEN_IMPORT, "import");
  expect_eof();
}

void test_scanner_keyword_edition(void)
{
  scan("edition");
  expect_token(BEBOP_TOKEN_EDITION, "edition");
  expect_eof();
}

void test_scanner_keyword_package(void)
{
  scan("package");
  expect_token(BEBOP_TOKEN_PACKAGE, "package");
  expect_eof();
}

void test_scanner_keyword_export(void)
{
  scan("export");
  expect_token(BEBOP_TOKEN_EXPORT, "export");
  expect_eof();
}

void test_scanner_keyword_local(void)
{
  scan("local");
  expect_token(BEBOP_TOKEN_LOCAL, "local");
  expect_eof();
}

void test_scanner_all_keywords(void)
{
  scan(
      "enum struct message mut readonly map array union service stream "
      "import edition package export local true false const");

  expect_token(BEBOP_TOKEN_ENUM, "enum");
  expect_token(BEBOP_TOKEN_STRUCT, "struct");
  expect_token(BEBOP_TOKEN_MESSAGE, "message");
  expect_token(BEBOP_TOKEN_MUT, "mut");
  expect_token(BEBOP_TOKEN_READONLY, "readonly");
  expect_token(BEBOP_TOKEN_MAP, "map");
  expect_token(BEBOP_TOKEN_ARRAY, "array");
  expect_token(BEBOP_TOKEN_UNION, "union");
  expect_token(BEBOP_TOKEN_SERVICE, "service");
  expect_token(BEBOP_TOKEN_STREAM, "stream");
  expect_token(BEBOP_TOKEN_IMPORT, "import");
  expect_token(BEBOP_TOKEN_EDITION, "edition");
  expect_token(BEBOP_TOKEN_PACKAGE, "package");
  expect_token(BEBOP_TOKEN_EXPORT, "export");
  expect_token(BEBOP_TOKEN_LOCAL, "local");
  expect_token(BEBOP_TOKEN_TRUE, "true");
  expect_token(BEBOP_TOKEN_FALSE, "false");
  expect_token(BEBOP_TOKEN_CONST, "const");
  expect_eof();
}

void test_scanner_simple_identifier(void)
{
  scan("foo");
  expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");
  expect_eof();
}

void test_scanner_identifier_with_underscore(void)
{
  scan("foo_bar");
  expect_token(BEBOP_TOKEN_IDENTIFIER, "foo_bar");
  expect_eof();
}

void test_scanner_identifier_with_numbers(void)
{
  scan("foo123");
  expect_token(BEBOP_TOKEN_IDENTIFIER, "foo123");
  expect_eof();
}

void test_scanner_identifier_starting_underscore(void)
{
  scan("_private");
  expect_token(BEBOP_TOKEN_IDENTIFIER, "_private");
  expect_eof();
}

void test_scanner_decimal_number(void)
{
  scan("12345");
  expect_token(BEBOP_TOKEN_NUMBER, "12345");
  expect_eof();
}

void test_scanner_hex_number(void)
{
  scan("0xDEADBEEF");
  expect_token(BEBOP_TOKEN_NUMBER, "0xDEADBEEF");
  expect_eof();
}

void test_scanner_float_number(void)
{
  scan("3.14159");
  expect_token(BEBOP_TOKEN_NUMBER, "3.14159");
  expect_eof();
}

void test_scanner_negative_exponent(void)
{
  scan("1e-10");
  expect_token(BEBOP_TOKEN_NUMBER, "1e-10");
  expect_eof();
}

void test_scanner_simple_string(void)
{
  scan("\"hello\"");
  expect_token(BEBOP_TOKEN_STRING, "hello");
  expect_eof();
}

void test_scanner_string_with_spaces(void)
{
  scan("\"hello world\"");
  expect_token(BEBOP_TOKEN_STRING, "hello world");
  expect_eof();
}

void test_scanner_empty_string(void)
{
  scan("\"\"");
  expect_token(BEBOP_TOKEN_STRING, "");
  expect_eof();
}

void test_scanner_string_with_escaped_quote(void)
{
  scan("\"say \"\"hello\"\"\"");
  expect_token(BEBOP_TOKEN_STRING, "say \"hello\"");
  expect_eof();
}

void test_scanner_single_quoted_string(void)
{
  scan("'hello'");
  expect_token(BEBOP_TOKEN_STRING, "hello");
  expect_eof();
}

void test_scanner_string_escape_backslash(void)
{
  scan("\"path\\\\to\\\\file\"");
  expect_token(BEBOP_TOKEN_STRING, "path\\to\\file");
  expect_eof();
}

void test_scanner_string_escape_newline(void)
{
  scan("\"line1\\nline2\"");
  expect_token(BEBOP_TOKEN_STRING, "line1\nline2");
  expect_eof();
}

void test_scanner_string_escape_tab(void)
{
  scan("\"col1\\tcol2\"");
  expect_token(BEBOP_TOKEN_STRING, "col1\tcol2");
  expect_eof();
}

void test_scanner_string_escape_unicode(void)
{
  scan("\"euro: \\u{20AC}\"");
  expect_token(BEBOP_TOKEN_STRING, "euro: \xE2\x82\xAC");
  expect_eof();
}

void test_scanner_string_escape_unicode_emoji(void)
{
  scan("\"grin: \\u{1F600}\"");
  expect_token(BEBOP_TOKEN_STRING, "grin: \xF0\x9F\x98\x80");
  expect_eof();
}

void test_scanner_string_escape_literal_backslash_u(void)
{
  scan("\"literal: \\\\u{1F600}\"");
  expect_token(BEBOP_TOKEN_STRING, "literal: \\u{1F600}");
  expect_eof();
}

void test_scanner_string_escape_unicode_surrogate(void)
{
  scan("\"\\u{D800}\"");
  expect_token(BEBOP_TOKEN_ERROR, NULL);
  expect_eof();
}

void test_scanner_string_escape_unicode_too_large(void)
{
  scan("\"\\u{FFFFFF}\"");
  expect_token(BEBOP_TOKEN_ERROR, NULL);
  expect_eof();
}

void test_scanner_string_escape_unicode_empty(void)
{
  scan("\"\\u{}\"");
  expect_token(BEBOP_TOKEN_ERROR, NULL);
  expect_eof();
}

void test_scanner_string_escape_carriage_return(void)
{
  scan("\"line1\\rline2\"");
  expect_token(BEBOP_TOKEN_STRING, "line1\rline2");
  expect_eof();
}

void test_scanner_string_escape_null(void)
{
  scan("\"with\\0null\"");
  expect_token(BEBOP_TOKEN_STRING, "with\0null");
  expect_eof();
}

void test_scanner_string_escape_single_quote(void)
{
  scan("\"it\\'s\"");
  expect_token(BEBOP_TOKEN_STRING, "it's");
  expect_eof();
}

void test_scanner_string_escape_double_quote(void)
{
  scan("\"say \\\"hello\\\"\"");
  expect_token(BEBOP_TOKEN_STRING, "say \"hello\"");
  expect_eof();
}

void test_scanner_string_escape_unicode_single_digit(void)
{
  scan("\"\\u{A}\"");
  expect_token(BEBOP_TOKEN_STRING, "\n");
  expect_eof();
}

void test_scanner_string_escape_unicode_max_valid(void)
{
  scan("\"\\u{10FFFF}\"");
  expect_token(BEBOP_TOKEN_STRING, "\xF4\x8F\xBF\xBF");
  expect_eof();
}

void test_scanner_string_multiple_escapes(void)
{
  scan("\"a\\tb\\nc\\\\d\"");
  expect_token(BEBOP_TOKEN_STRING, "a\tb\nc\\d");
  expect_eof();
}

void test_scanner_string_escape_invalid(void)
{
  scan("\"bad: \\q\"");
  expect_token(BEBOP_TOKEN_ERROR, NULL);
  expect_eof();
}

void test_scanner_string_invalid_utf8(void)
{
  scan("\"\x80\x81\x82\"");
  expect_token(BEBOP_TOKEN_ERROR, NULL);
  expect_eof();
}

void test_scanner_bytes_simple(void)
{
  scan("b\"hello\"");
  expect_token(BEBOP_TOKEN_BYTES, "hello");
  expect_eof();
}

void test_scanner_bytes_hex_escapes(void)
{
  scan("b\"\\x00\\x01\\x02\\xff\"");
  expect_token(BEBOP_TOKEN_BYTES, "\x00\x01\x02\xff");
  expect_eof();
}

void test_scanner_bytes_mixed(void)
{
  scan("b\"abc\\x00def\"");
  expect_token(BEBOP_TOKEN_BYTES, "abc\x00" "def");
  expect_eof();
}

void test_scanner_bytes_empty(void)
{
  scan("b\"\"");
  expect_token(BEBOP_TOKEN_BYTES, "");
  expect_eof();
}

void test_scanner_bytes_non_utf8(void)
{
  scan("b\"\\x89PNG\\r\\n\"");
  expect_token(BEBOP_TOKEN_BYTES, "\x89PNG\r\n");
  expect_eof();
}

void test_scanner_bytes_single_quoted(void)
{
  scan("b'hello'");
  expect_token(BEBOP_TOKEN_BYTES, "hello");
  expect_eof();
}

void test_scanner_all_symbols(void)
{
  scan("( ) { } [ ] < > : ; , . ? / = @ # ! $ \\ ` ~ & | -");

  expect_token(BEBOP_TOKEN_LPAREN, NULL);
  expect_token(BEBOP_TOKEN_RPAREN, NULL);
  expect_token(BEBOP_TOKEN_LBRACE, NULL);
  expect_token(BEBOP_TOKEN_RBRACE, NULL);
  expect_token(BEBOP_TOKEN_LBRACKET, NULL);
  expect_token(BEBOP_TOKEN_RBRACKET, NULL);
  expect_token(BEBOP_TOKEN_LANGLE, NULL);
  expect_token(BEBOP_TOKEN_RANGLE, NULL);
  expect_token(BEBOP_TOKEN_COLON, NULL);
  expect_token(BEBOP_TOKEN_SEMICOLON, NULL);
  expect_token(BEBOP_TOKEN_COMMA, NULL);
  expect_token(BEBOP_TOKEN_DOT, NULL);
  expect_token(BEBOP_TOKEN_QUESTION, NULL);
  expect_token(BEBOP_TOKEN_SLASH, NULL);
  expect_token(BEBOP_TOKEN_EQUALS, NULL);
  expect_token(BEBOP_TOKEN_AT, NULL);
  expect_token(BEBOP_TOKEN_HASH, NULL);
  expect_token(BEBOP_TOKEN_BANG, NULL);
  expect_token(BEBOP_TOKEN_DOLLAR, NULL);
  expect_token(BEBOP_TOKEN_BACKSLASH, NULL);
  expect_token(BEBOP_TOKEN_BACKTICK, NULL);
  expect_token(BEBOP_TOKEN_TILDE, NULL);
  expect_token(BEBOP_TOKEN_AMPERSAND, NULL);
  expect_token(BEBOP_TOKEN_PIPE, NULL);
  expect_token(BEBOP_TOKEN_MINUS, NULL);
  expect_eof();
}

void test_scanner_arrow(void)
{
  scan("->");
  expect_token(BEBOP_TOKEN_ARROW, NULL);
  expect_eof();
}

void test_scanner_line_comment_trivia(void)
{
  scan("foo // comment\nbar");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");

  TEST_ASSERT_EQUAL_UINT32(3, foo.trailing.count);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_WHITESPACE, foo.trailing.items[0].kind);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_LINE_COMMENT, foo.trailing.items[1].kind);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_NEWLINE, foo.trailing.items[2].kind);

  expect_token(BEBOP_TOKEN_IDENTIFIER, "bar");
  expect_eof();
}

void test_scanner_block_comment_trivia(void)
{
  scan("foo /* comment */ bar");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");

  TEST_ASSERT_EQUAL_UINT32(3, foo.trailing.count);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_WHITESPACE, foo.trailing.items[0].kind);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_BLOCK_COMMENT, foo.trailing.items[1].kind);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_WHITESPACE, foo.trailing.items[2].kind);

  bebop_token_t bar = expect_token(BEBOP_TOKEN_IDENTIFIER, "bar");

  TEST_ASSERT_EQUAL_UINT32(0, bar.leading.count);

  expect_eof();
}

void test_scanner_doc_comment_trivia(void)
{
  scan("/** doc */\nfoo");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");

  TEST_ASSERT_TRUE(foo.leading.count >= 1);
  bool found_doc = false;
  for (uint32_t i = 0; i < foo.leading.count; i++) {
    if (foo.leading.items[i].kind == BEBOP_TRIVIA_DOC_COMMENT) {
      found_doc = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(found_doc);

  expect_eof();
}

void test_scanner_whitespace_trivia(void)
{
  scan("   foo");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");
  TEST_ASSERT_EQUAL_UINT32(1, foo.leading.count);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_WHITESPACE, foo.leading.items[0].kind);

  expect_eof();
}

void test_scanner_newline_trivia(void)
{
  scan("\n\nfoo");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");
  TEST_ASSERT_EQUAL_UINT32(2, foo.leading.count);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_NEWLINE, foo.leading.items[0].kind);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_NEWLINE, foo.leading.items[1].kind);

  expect_eof();
}

void test_scanner_mixed_trivia(void)
{
  scan("  \n  // comment\n  foo");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");

  TEST_ASSERT_TRUE(foo.leading.count >= 4);

  expect_eof();
}

void test_scanner_crlf_newline(void)
{
  scan("foo\r\nbar");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");

  TEST_ASSERT_EQUAL_UINT32(1, foo.trailing.count);
  TEST_ASSERT_EQUAL_INT(BEBOP_TRIVIA_NEWLINE, foo.trailing.items[0].kind);

  bebop_token_t bar = expect_token(BEBOP_TOKEN_IDENTIFIER, "bar");
  TEST_ASSERT_EQUAL_UINT32(0, bar.leading.count);

  expect_eof();
}

void test_scanner_span_single_token(void)
{
  scan("foo");

  bebop_token_t foo = expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");
  TEST_ASSERT_EQUAL_UINT32(0, foo.span.off);
  TEST_ASSERT_EQUAL_UINT32(3, foo.span.len);
  TEST_ASSERT_EQUAL_UINT32(1, foo.span.start_line);
  TEST_ASSERT_EQUAL_UINT32(1, foo.span.start_col);
  TEST_ASSERT_EQUAL_UINT32(1, foo.span.end_line);
  TEST_ASSERT_EQUAL_UINT32(4, foo.span.end_col);

  expect_eof();
}

void test_scanner_span_multiline(void)
{
  scan("foo\nbar");

  expect_token(BEBOP_TOKEN_IDENTIFIER, "foo");

  bebop_token_t bar = expect_token(BEBOP_TOKEN_IDENTIFIER, "bar");
  TEST_ASSERT_EQUAL_UINT32(4, bar.span.off);
  TEST_ASSERT_EQUAL_UINT32(3, bar.span.len);
  TEST_ASSERT_EQUAL_UINT32(2, bar.span.start_line);
  TEST_ASSERT_EQUAL_UINT32(1, bar.span.start_col);

  expect_eof();
}

void test_scanner_raw_block_simple(void)
{
  scan("[[hello world]]");

  bebop_token_t tok = expect_token(BEBOP_TOKEN_RAW_BLOCK, "hello world");

  TEST_ASSERT_EQUAL_UINT32(0, tok.span.off);
  TEST_ASSERT_EQUAL_UINT32(15, tok.span.len);
  TEST_ASSERT_EQUAL_UINT32(1, tok.span.start_line);
  TEST_ASSERT_EQUAL_UINT32(1, tok.span.start_col);
  expect_eof();
}

void test_scanner_raw_block_multiline(void)
{
  scan("[[\nline1\nline2\n]]");

  bebop_token_t tok = expect_token(BEBOP_TOKEN_RAW_BLOCK, "\nline1\nline2\n");
  TEST_ASSERT_EQUAL_UINT32(0, tok.span.off);
  TEST_ASSERT_EQUAL_UINT32(1, tok.span.start_line);

  TEST_ASSERT_EQUAL_UINT32(4, tok.span.end_line);
  expect_eof();
}

void test_scanner_raw_block_with_brackets(void)
{
  scan("[[a[b]c]]");

  bebop_token_t tok = expect_token(BEBOP_TOKEN_RAW_BLOCK, "a[b]c");
  TEST_ASSERT_EQUAL_UINT32(9, tok.span.len);
  expect_eof();
}

void test_scanner_raw_block_empty(void)
{
  scan("[[]]");

  bebop_token_t tok = expect_token(BEBOP_TOKEN_RAW_BLOCK, "");
  TEST_ASSERT_EQUAL_UINT32(4, tok.span.len);
  expect_eof();
}

void test_scanner_raw_block_unterminated(void)
{
  scan("[[hello world");

  bebop_token_t tok = expect_token(BEBOP_TOKEN_ERROR, NULL);
  TEST_ASSERT_EQUAL_UINT32(0, tok.span.off);
  TEST_ASSERT_EQUAL_UINT32(1, tok.span.start_line);
  expect_eof();
}

void test_scanner_single_bracket_not_raw(void)
{
  scan("[x]");

  expect_token(BEBOP_TOKEN_LBRACKET, NULL);
  expect_token(BEBOP_TOKEN_IDENTIFIER, "x");
  expect_token(BEBOP_TOKEN_RBRACKET, NULL);
  expect_eof();
}

void test_scanner_unknown_character(void)
{
  scan("^");

  bebop_token_t err = expect_token(BEBOP_TOKEN_ERROR, "^");
  TEST_ASSERT_EQUAL_UINT32(0, err.span.off);
  TEST_ASSERT_EQUAL_UINT32(1, err.span.len);

  expect_eof();
}

void test_scanner_unterminated_string(void)
{
  scan("\"hello\nworld");

  bebop_token_t str = expect_token(BEBOP_TOKEN_STRING, "hello\nworld");
  TEST_ASSERT_EQUAL_UINT32(1, str.span.start_line);
  expect_eof();
}

void test_scanner_string_crlf_normalized(void)
{
  // CRLF inside a literal normalizes to \n and must not drop the following
  // byte or double-count lines.
  scan("\"a\r\nb\" x");

  bebop_token_t str = expect_token(BEBOP_TOKEN_STRING, "a\nb");
  TEST_ASSERT_EQUAL_UINT32(1, str.span.start_line);

  bebop_token_t ident = expect_token(BEBOP_TOKEN_IDENTIFIER, "x");
  TEST_ASSERT_EQUAL_UINT32(2, ident.span.start_line);
  expect_eof();
}

void test_scanner_fixture_struct(void)
{
  const char* path = FIXTURE("struct.bop");
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "Could not open struct.bop fixture");

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* source = malloc((size_t)len + 1);
  size_t read_len = fread(source, 1, (size_t)len, f);
  source[read_len] = '\0';
  fclose(f);

  stream = bebop_scan(ctx, source, read_len);
  tok_idx = 0;

  TEST_ASSERT_TRUE(stream.count > 0);

  TEST_ASSERT_EQUAL_INT(BEBOP_TOKEN_EOF, stream.tokens[stream.count - 1].kind);

  bool found_struct = false;
  for (uint32_t i = 0; i < stream.count; i++) {
    if (stream.tokens[i].kind == BEBOP_TOKEN_STRUCT) {
      found_struct = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(found_struct);

  free(source);
}

void test_scanner_fixture_enum(void)
{
  const char* path = FIXTURE("enum.bop");
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "Could not open enum.bop fixture");

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* source = malloc((size_t)len + 1);
  size_t read_len = fread(source, 1, (size_t)len, f);
  source[read_len] = '\0';
  fclose(f);

  stream = bebop_scan(ctx, source, read_len);
  tok_idx = 0;

  TEST_ASSERT_TRUE(stream.count > 0);
  TEST_ASSERT_EQUAL_INT(BEBOP_TOKEN_EOF, stream.tokens[stream.count - 1].kind);

  bool found_enum = false;
  for (uint32_t i = 0; i < stream.count; i++) {
    if (stream.tokens[i].kind == BEBOP_TOKEN_ENUM) {
      found_enum = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(found_enum);

  bool found_deprecated = false;
  for (uint32_t i = 0; i < stream.count; i++) {
    if (stream.tokens[i].kind == BEBOP_TOKEN_IDENTIFIER) {
      const char* lex = bebop_str_get(BEBOP_INTERN(ctx), stream.tokens[i].lexeme);
      if (lex && strcmp(lex, "deprecated") == 0) {
        found_deprecated = true;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(found_deprecated);

  free(source);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_scanner_keyword_enum);
  RUN_TEST(test_scanner_keyword_struct);
  RUN_TEST(test_scanner_keyword_message);
  RUN_TEST(test_scanner_keyword_mut);
  RUN_TEST(test_scanner_keyword_map);
  RUN_TEST(test_scanner_keyword_array);
  RUN_TEST(test_scanner_keyword_union);
  RUN_TEST(test_scanner_keyword_service);
  RUN_TEST(test_scanner_keyword_stream);
  RUN_TEST(test_scanner_keyword_true);
  RUN_TEST(test_scanner_keyword_false);
  RUN_TEST(test_scanner_keyword_const);
  RUN_TEST(test_scanner_keyword_readonly);
  RUN_TEST(test_scanner_keyword_import);
  RUN_TEST(test_scanner_keyword_edition);
  RUN_TEST(test_scanner_keyword_package);
  RUN_TEST(test_scanner_keyword_export);
  RUN_TEST(test_scanner_keyword_local);
  RUN_TEST(test_scanner_all_keywords);

  RUN_TEST(test_scanner_simple_identifier);
  RUN_TEST(test_scanner_identifier_with_underscore);
  RUN_TEST(test_scanner_identifier_with_numbers);
  RUN_TEST(test_scanner_identifier_starting_underscore);

  RUN_TEST(test_scanner_decimal_number);
  RUN_TEST(test_scanner_hex_number);
  RUN_TEST(test_scanner_float_number);
  RUN_TEST(test_scanner_negative_exponent);

  RUN_TEST(test_scanner_simple_string);
  RUN_TEST(test_scanner_string_with_spaces);
  RUN_TEST(test_scanner_empty_string);
  RUN_TEST(test_scanner_string_with_escaped_quote);
  RUN_TEST(test_scanner_single_quoted_string);
  RUN_TEST(test_scanner_string_escape_backslash);
  RUN_TEST(test_scanner_string_escape_newline);
  RUN_TEST(test_scanner_string_escape_tab);
  RUN_TEST(test_scanner_string_escape_unicode);
  RUN_TEST(test_scanner_string_escape_unicode_emoji);
  RUN_TEST(test_scanner_string_escape_literal_backslash_u);
  RUN_TEST(test_scanner_string_escape_unicode_surrogate);
  RUN_TEST(test_scanner_string_escape_unicode_too_large);
  RUN_TEST(test_scanner_string_escape_unicode_empty);
  RUN_TEST(test_scanner_string_escape_carriage_return);
  RUN_TEST(test_scanner_string_escape_null);
  RUN_TEST(test_scanner_string_escape_single_quote);
  RUN_TEST(test_scanner_string_escape_double_quote);
  RUN_TEST(test_scanner_string_escape_unicode_single_digit);
  RUN_TEST(test_scanner_string_escape_unicode_max_valid);
  RUN_TEST(test_scanner_string_multiple_escapes);
  RUN_TEST(test_scanner_string_escape_invalid);
  RUN_TEST(test_scanner_string_invalid_utf8);

  RUN_TEST(test_scanner_bytes_simple);
  RUN_TEST(test_scanner_bytes_hex_escapes);
  RUN_TEST(test_scanner_bytes_mixed);
  RUN_TEST(test_scanner_bytes_empty);
  RUN_TEST(test_scanner_bytes_non_utf8);
  RUN_TEST(test_scanner_bytes_single_quoted);

  RUN_TEST(test_scanner_all_symbols);
  RUN_TEST(test_scanner_arrow);

  RUN_TEST(test_scanner_line_comment_trivia);
  RUN_TEST(test_scanner_block_comment_trivia);
  RUN_TEST(test_scanner_doc_comment_trivia);

  RUN_TEST(test_scanner_whitespace_trivia);
  RUN_TEST(test_scanner_newline_trivia);
  RUN_TEST(test_scanner_mixed_trivia);
  RUN_TEST(test_scanner_crlf_newline);

  RUN_TEST(test_scanner_span_single_token);
  RUN_TEST(test_scanner_span_multiline);

  RUN_TEST(test_scanner_raw_block_simple);
  RUN_TEST(test_scanner_raw_block_multiline);
  RUN_TEST(test_scanner_raw_block_with_brackets);
  RUN_TEST(test_scanner_raw_block_empty);
  RUN_TEST(test_scanner_raw_block_unterminated);
  RUN_TEST(test_scanner_single_bracket_not_raw);

  RUN_TEST(test_scanner_unknown_character);
  RUN_TEST(test_scanner_unterminated_string);
  RUN_TEST(test_scanner_string_crlf_normalized);

  RUN_TEST(test_scanner_fixture_struct);
  RUN_TEST(test_scanner_fixture_enum);

  return UNITY_END();
}
