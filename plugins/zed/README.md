# Bebop for Zed

Bebop support for Zed. Highlighting, outline, and indentation come from
[tree-sitter-bebop](../tree-sitter-bebop); everything else comes from
`bebopc lsp` (>= 2026.0.0).

`bebopc` is resolved from `lsp.bebopc.binary` in settings, then PATH,
then the latest [GitHub release](https://github.com/6over3/bebop-next/releases)
(downloaded and cached).

```json
{
  "lsp": {
    "bebopc": {
      "binary": {
        "path": "/usr/local/bin/bebopc",
        "arguments": ["lsp", "-I", "schemas/"]
      }
    }
  }
}
```

`arguments` replaces the full argument list, so include `lsp`.

## Development

Zed fetches the grammar by git `repository` + `rev`, so grammar changes
must be committed before they are installable:

1. `npx tree-sitter generate && npx tree-sitter test` in
   `../tree-sitter-bebop`
2. Commit, then update `rev` in `extension.toml`
3. `zed: install dev extension` (or `zed: rebuild dev extension`) on
   this directory

The pinned `rev` must be reachable on GitHub; to iterate on unpushed
grammar commits, temporarily point `repository` at this clone
(`file:///path/to/bebop-next`). Zed builds in place (`target/`,
`grammars/`, `extension.wasm` — all gitignored) and `zed --foreground`
shows server logs.

To publish, follow
[zed-industries/extensions](https://github.com/zed-industries/extensions).
