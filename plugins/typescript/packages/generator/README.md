# @bebop/generator-typescript

This package generates TypeScript codecs, views, reflection data, RPC clients, and RPC handler contracts from Bebop schemas.

## Use with bebopc

Place `bebopc-gen-typescript` on `PATH`, then run:

```bash
bebopc build schema.bop --typescript_out=./generated
```

Generated files use the `.bb.ts` suffix and import runtime code from `@bebop/runtime`.

## Use as a library

```ts
import { generate } from "@bebop/generator-typescript";

const response = generate(request);
if (response.error !== undefined) {
  throw new Error(response.error);
}
```

`generate` accepts a `CodeGeneratorRequest` from `@bebop/plugin` and returns a `CodeGeneratorResponse`. It does not write files.

## Repository build

From `plugins/typescript`, run:

```bash
bun run --cwd packages/generator build
bun run --cwd packages/generator build:binary
```

The first command builds the npm package. The second also builds the standalone `bebopc-gen-typescript` executable.
