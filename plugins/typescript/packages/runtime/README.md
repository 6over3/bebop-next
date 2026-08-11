# @bebop/runtime

Runtime types for generated Bebop code.

## Codecs and views

Generated records can encode, decode, or create a view:

```ts
const encoded = Widget.encode({ id: 42, name: "guide" });
const decoded = Widget.decode(encoded);
const view = Widget.view(encoded);

console.log(view.id);
console.log(view.name.string);
```

Views are immutable and use the input bytes as backing storage. Do not change those bytes while a view is in use. Call `view.decoded()` when you need a normal object.

Use `encodeInto` to reuse a writer:

```ts
import { BebopWriter, encodeInto } from "@bebop/runtime";

const writer = new BebopWriter();
const byteCount = encodeInto(Widget, decoded, writer);
```

## Arrays

Generated scalar arrays use native typed arrays:

| Bebop | TypeScript |
| --- | --- |
| `byte[]` | `Uint8Array` |
| `int32[]` | `Int32Array` |
| `uint64[]` | `BigUint64Array` |
| `float16[]` | `Float16Array` |
| `float32[]` | `Float32Array` |
| `bfloat16[]` | `BFloat16Array` |

## RPC

Generated clients accept a `BebopChannel`:

```ts
const client = new WidgetServiceClient(channel);
const { message, metadata } = await client.getWidget({ value: "hello" });
```

Generated handlers receive request views and return normal response values:

```ts
import { BebopRouterBuilder, RpcContext } from "@bebop/runtime/rpc";

@WidgetService.handler
class Widgets implements WidgetServiceHandler {
  getWidget(request: EchoRequestView, context: RpcContext): EchoResponse {
    context.responseMetadata.set("request-id", "42");
    return { value: request.value.string };
  }
}

const router = new BebopRouterBuilder({ maxDepth: 64 })
  .registerService(new Widgets())
  .build();
```

The runtime does not include a transport. Implement `BebopChannel` for clients and adapt requests to `BebopRpcRouter` for servers. Both APIs route calls by method ID.

Duplex methods send from an iterable, async iterable, or `ReadableStream` while responses are received:

```ts
const responses = await client.syncWidgets(outgoingUpdates());

for await (const response of responses) {
  console.log(response);
}

const trailers = await responses.metadata;
```
