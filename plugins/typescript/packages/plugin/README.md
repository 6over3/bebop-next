# @bebop/plugin

This package provides TypeScript codecs and helpers for Bebop compiler plugins.

## Create a plugin

A contributor reads the compiler request and writes files or diagnostics to the shared response.

```ts
import {
  composePlugin,
  defineContributor,
} from "@bebop/plugin/sdk";
import { runPlugin } from "@bebop/plugin/node";

const models = defineContributor({
  name: "models",
  contribute({ request, response }) {
    const source = generateModels(request.schemas ?? []);
    response.addFile("models.ts", source);
  },
});

const plugin = composePlugin(models);
await runPlugin(plugin);
```

`runPlugin` reads one encoded request from standard input and writes one encoded response to standard output.

## Compose contributors

Use these fields to define contributor order:

- `requires` names contributors that must be present and must run first.
- `after` runs after a named contributor when that contributor is present.
- `before` runs before a named contributor when that contributor is present.

`composePlugin` rejects duplicate names, missing required contributors, and dependency cycles before it handles a request.

Use `createContributorKey<T>()` with `context.state` to share typed data between contributors. Use `ResponseBuilder.addFile`, `insert`, `addDiagnostic`, and `fail` to build the compiler response.

Protocol types are available from `@bebop/plugin/protocol`. Schema descriptor types are available from `@bebop/plugin/descriptor`.
