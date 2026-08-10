# tree-sitter-bebop

Bebop grammar for tree-sitter.

`src/parser.c` is committed because consumers (Zed, nvim) compile it
straight from a git checkout.

```sh
npm install
npx tree-sitter generate   # after editing grammar.js
npx tree-sitter test
```
