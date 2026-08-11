# Bebop for TypeScript

This workspace contains three packages:

- `@bebop/runtime` provides codecs, views, reflection, and RPC types.
- `@bebop/plugin` provides the compiler plugin protocol and contributor API.
- `@bebop/generator-typescript` generates TypeScript from Bebop schemas.

## Requirements

- Node.js 26 or later for package consumers
- Bun for repository builds and tests

## Development

Run these commands from this directory:

```bash
bun install --frozen-lockfile
bun run check
bun run test
bun run test:packages
```

Build the standalone generator with:

```bash
bun run --cwd packages/generator build:binary
```

Create local package archives with:

```bash
bun run package
```

The package command writes archives to `dist/npm`. It does not publish them.
