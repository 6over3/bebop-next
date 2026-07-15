# tree-sitter-bebop

Tree-sitter grammar for the Bebop schema language (edition 2026).

Mirrors the reference implementation in `bebop/src/bebop_scanner.c` and
`bebop/src/bebop_parser.c`, including decorator definitions with Lua
raw blocks, `map[K, V]`, postfix arrays, streaming service methods, and
env-var substitution in string literals. Intentionally laxer than the
compiler: ordering and semantic rules are diagnosed by the language
server, not the parser.

```sh
npm install
npx tree-sitter generate   # regenerate src/ after editing grammar.js
npx tree-sitter test       # corpus tests in test/corpus/
npx tree-sitter parse --quiet --stat ../../tests/fixtures/valid/*.bop
```

The generated `src/` directory is committed; consumers (including the
Zed extension in `../zed`) build the parser from it.
