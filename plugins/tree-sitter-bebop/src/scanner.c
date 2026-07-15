// Raw blocks use Lua-style leveled long brackets: [[ ]], [=[ ]=], ...
// The closer must match the opener's level, which regex tokens cannot
// express, so start/content/end are external tokens sharing the level.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tree_sitter/parser.h"

enum TokenType {
  LUA_BLOCK_START,
  LUA_SOURCE,
  LUA_BLOCK_END,
};

typedef struct {
  uint32_t level;
} Scanner;

void* tree_sitter_bebop_external_scanner_create(void);
void tree_sitter_bebop_external_scanner_destroy(void* payload);
unsigned tree_sitter_bebop_external_scanner_serialize(void* payload, char* buffer);
void tree_sitter_bebop_external_scanner_deserialize(
    void* payload, const char* buffer, unsigned length
);
bool tree_sitter_bebop_external_scanner_scan(
    void* payload, TSLexer* lexer, const bool* valid_symbols
);

void* tree_sitter_bebop_external_scanner_create(void)
{
  return calloc(1, sizeof(Scanner));
}

void tree_sitter_bebop_external_scanner_destroy(void* payload)
{
  free(payload);
}

unsigned tree_sitter_bebop_external_scanner_serialize(void* payload, char* buffer)
{
  Scanner* scanner = payload;
  memcpy(buffer, &scanner->level, sizeof scanner->level);
  return sizeof scanner->level;
}

void tree_sitter_bebop_external_scanner_deserialize(
    void* payload, const char* buffer, unsigned length
)
{
  Scanner* scanner = payload;
  scanner->level = 0;
  if (length >= sizeof scanner->level) {
    memcpy(&scanner->level, buffer, sizeof scanner->level);
  }
}

static void advance(TSLexer* lexer)
{
  lexer->advance(lexer, false);
}

static bool scan_block_start(Scanner* scanner, TSLexer* lexer)
{
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r'
         || lexer->lookahead == '\n')
  {
    lexer->advance(lexer, true);
  }

  if (lexer->lookahead != '[') {
    return false;
  }
  advance(lexer);

  uint32_t level = 0;
  while (lexer->lookahead == '=') {
    level++;
    advance(lexer);
  }
  if (lexer->lookahead != '[') {
    return false;
  }
  advance(lexer);

  scanner->level = level;
  lexer->result_symbol = LUA_BLOCK_START;
  return true;
}

// Returns true when the lookahead is `]` followed by level `=`s and `]`,
// leaving the lexer past the final `]`.
static bool scan_closer(Scanner* scanner, TSLexer* lexer)
{
  advance(lexer);
  uint32_t matched = 0;
  while (matched < scanner->level && lexer->lookahead == '=') {
    matched++;
    advance(lexer);
  }
  if (matched == scanner->level && lexer->lookahead == ']') {
    advance(lexer);
    return true;
  }
  return false;
}

bool tree_sitter_bebop_external_scanner_scan(
    void* payload, TSLexer* lexer, const bool* valid_symbols
)
{
  Scanner* scanner = payload;

  if (valid_symbols[LUA_SOURCE] || valid_symbols[LUA_BLOCK_END]) {
    if (valid_symbols[LUA_BLOCK_END] && lexer->lookahead == ']') {
      lexer->mark_end(lexer);
      if (scan_closer(scanner, lexer)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = LUA_BLOCK_END;
        return true;
      }
      if (!valid_symbols[LUA_SOURCE]) {
        return false;
      }
    } else if (!valid_symbols[LUA_SOURCE] || lexer->eof(lexer)) {
      return false;
    }

    // Consume content up to the closer or EOF. mark_end pins the token
    // end before each candidate closer so rejected closers stay content.
    bool has_content = false;
    lexer->mark_end(lexer);
    for (;;) {
      if (lexer->eof(lexer)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = LUA_SOURCE;
        return has_content;
      }
      if (lexer->lookahead == ']') {
        lexer->mark_end(lexer);
        if (scan_closer(scanner, lexer)) {
          lexer->result_symbol = LUA_SOURCE;
          return has_content;
        }
        has_content = true;
      } else {
        advance(lexer);
        has_content = true;
      }
    }
  }

  if (valid_symbols[LUA_BLOCK_START]) {
    return scan_block_start(scanner, lexer);
  }

  return false;
}
