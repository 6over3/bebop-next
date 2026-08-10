import { describe, expect, test } from "vitest";
import { decode, encode, type BebopCodec } from "../src/codec.js";
import { createBatch } from "../src/rpc/batch.js";
import { bufferedPayload } from "../src/rpc/buffered-payload.js";
import {
  StreamResponse,
  clientStreamCall,
  duplexStreamCall,
  serverStreamCall,
  unaryCall,
  type BebopChannel,
  type RpcResponse,
} from "../src/rpc/channel.js";
import { peerInfoKey, RpcContext } from "../src/rpc/context.js";
import { BebopRouterBuilder } from "../src/rpc/router.js";
import type { BebopRouter } from "../src/rpc/router.js";
import { FutureDispatcher } from "../src/rpc/future.js";
import { defineService } from "../src/rpc/service.js";
import { Frame, readFrames } from "../src/rpc/frame.js";
import { MethodType, StatusCode } from "../src/rpc.bb.js";
import type { BebopReader, BebopWriter } from "../src/wire.js";

const Uint32Codec = {
  readFrom(reader: BebopReader): number {
    return reader.readUint32();
  },
  writeInto(writer: BebopWriter, value: number): void {
    writer.writeUint32(value);
  },
  encodedSize(): number {
    return 4;
  },
} satisfies BebopCodec<number>;

interface DoubleHandler {
  double(request: number): number | PromiseLike<number>;
}

interface TripleHandler {
  triple(request: number): number | PromiseLike<number>;
}

const DoubleService = defineService(
  "DoubleService",
  {
    double: {
      id: 102,
      name: "Double",
      methodType: MethodType.UNARY,
      request: Uint32Codec,
      response: Uint32Codec,
      requestTypeUrl: "type.bebop.sh/test.Number",
      responseTypeUrl: "type.bebop.sh/test.Number",
    },
  },
  (builder, handler: DoubleHandler) => builder.registerUnary(
    DoubleService.methods.double,
    (request) => handler.double(request),
  ),
);

const TripleService = defineService(
  "TripleService",
  {
    triple: {
      id: 103,
      name: "Triple",
      methodType: MethodType.UNARY,
      request: Uint32Codec,
      response: Uint32Codec,
      requestTypeUrl: "type.bebop.sh/test.Number",
      responseTypeUrl: "type.bebop.sh/test.Number",
    },
  },
  (builder, handler: TripleHandler) => builder.registerUnary(
    TripleService.methods.triple,
    (request) => handler.triple(request),
  ),
);

class DecoratedDoubleHandler implements DoubleHandler, TripleHandler {
  double(request: number): number {
    return request * 2;
  }

  triple(request: number): number {
    return request * 3;
  }
}

DoubleService.handler(DecoratedDoubleHandler, {
  kind: "class",
  name: "DecoratedDoubleHandler",
  addInitializer() {},
});
TripleService.handler(DecoratedDoubleHandler, {
  kind: "class",
  name: "DecoratedDoubleHandler",
  addInitializer() {},
});

describe("Bebop RPC frames", () => {
  test("decodes frames across every boundary and one-byte transport chunks", async () => {
    const expected = [
      new Frame(new Uint8Array([1, 2, 3]), 0, 7),
      new Frame(new Uint8Array([4, 5]), 0, 8, 123n),
      new Frame(new Uint8Array(), 1, 9),
    ];
    const encoded = concatenate(expected.map((frame) => frame.encode()));

    for (let split = 0; split <= encoded.length; split++) {
      const actual = await collect(readFrames(byteStream([
        encoded.slice(0, split),
        encoded.slice(split),
      ])));
      expect(actual.map(frameValue)).toEqual(expected.map(frameValue));
    }

    const actual = await collect(readFrames(byteStream([...encoded].map((byte) => new Uint8Array([byte])))));
    expect(actual.map(frameValue)).toEqual(expected.map(frameValue));
  });

  test("rejects a truncated final frame", async () => {
    const encoded = new Frame(new Uint8Array([1, 2, 3])).encode();
    await expect(collect(readFrames(byteStream([encoded.slice(0, -1)]))))
      .rejects.toThrow("incomplete frame");
  });
});

describe("Bebop RPC interceptors", () => {
  test("compose around the handler in registration order", async () => {
    const events: string[] = [];
    const method = {
      id: 100,
      name: "double",
      methodType: MethodType.UNARY,
      request: Uint32Codec,
      response: Uint32Codec,
      requestTypeUrl: "type.bebop.sh/test.Request",
      responseTypeUrl: "type.bebop.sh/test.Response",
    } as const;
    const router = new BebopRouterBuilder()
      .addInterceptor(async (_methodId, _context, proceed) => {
        events.push("outer:before");
        const result = await proceed();
        events.push("outer:after");
        return result;
      })
      .addInterceptor(async (_methodId, _context, proceed) => {
        events.push("inner:before");
        const result = await proceed();
        events.push("inner:after");
        return result;
      })
      .registerUnary(method, (value) => {
        events.push("handler");
        return value * 2;
      })
      .build();

    const response = await router.unary(method.id, encode(Uint32Codec, 21));

    expect(decode(Uint32Codec, response)).toBe(42);
    expect(events).toEqual([
      "outer:before",
      "inner:before",
      "handler",
      "inner:after",
      "outer:after",
    ]);
  });

  test("duplex handlers may await their first request without deadlocking setup", async () => {
    const method = {
      id: 101,
      name: "duplex",
      methodType: MethodType.DUPLEX_STREAM,
      request: Uint32Codec,
      response: Uint32Codec,
      requestTypeUrl: "type.bebop.sh/test.Request",
      responseTypeUrl: "type.bebop.sh/test.Response",
    } as const;
    const router = new BebopRouterBuilder()
      .registerDuplexStream(method, async (requests) => {
        const reader = requests.getReader();
        const first = await reader.read();
        reader.releaseLock();
        return (async function* () {
          if (!first.done) yield first.value * 2;
        })();
      })
      .build();

    const stream = await router.duplexStream(method.id);
    await stream.send(encode(Uint32Codec, 21));
    await stream.finish();
    const responses = await collect(stream.responses);

    expect(responses.map((response) => decode(Uint32Codec, response.value))).toEqual([42]);
  });

  test("applies backpressure until handlers consume client-streamed requests", async () => {
    const method = rpcMethod(104, MethodType.CLIENT_STREAM);
    const consume = Promise.withResolvers<void>();
    const router = new BebopRouterBuilder()
      .registerClientStream(method, async (requests) => {
        await consume.promise;
        let total = 0;
        for await (const value of requests) total += value;
        return total;
      })
      .build();
    const stream = await router.clientStream(method.id);
    for (let index = 0; index < 16; index++) {
      await stream.send(encode(Uint32Codec, index === 0 ? 40 : 0));
    }
    let sent = false;
    const sending = stream.send(encode(Uint32Codec, 2)).then(() => { sent = true; });

    await Promise.resolve();
    expect(sent).toBe(false);
    consume.resolve();
    await sending;
    expect(decode(Uint32Codec, await stream.finish())).toBe(42);
  });
});

describe("Bebop RPC generated service registration", () => {
  test("composes decorated services on one handler without global side effects", async () => {
    const router = new BebopRouterBuilder()
      .registerService(new DecoratedDoubleHandler())
      .build();

    const response = await router.unary(DoubleService.methods.double.id, encode(Uint32Codec, 21));
    const second = await router.unary(TripleService.methods.triple.id, encode(Uint32Codec, 14));

    expect(decode(Uint32Codec, response)).toBe(42);
    expect(decode(Uint32Codec, second)).toBe(42);
  });

  test("supports explicit registration without decorators", async () => {
    const router = DoubleService.register(new BebopRouterBuilder(), {
      double: (request) => request * 3,
    }).build();

    const response = await router.unary(DoubleService.methods.double.id, encode(Uint32Codec, 14));

    expect(decode(Uint32Codec, response)).toBe(42);
  });
});

describe("Bebop RPC typed client foundations", () => {
  test("runs unary and every streaming shape through typed method descriptors", async () => {
    const unary = rpcMethod(110, MethodType.UNARY);
    const serverStream = rpcMethod(111, MethodType.SERVER_STREAM);
    const clientStream = rpcMethod(112, MethodType.CLIENT_STREAM);
    const duplex = rpcMethod(113, MethodType.DUPLEX_STREAM);
    const builder = new BebopRouterBuilder()
      .registerUnary(unary, (value) => value * 2)
      .registerServerStream(serverStream, (value) => new ReadableStream({
        start(controller) {
          controller.enqueue(value);
          controller.enqueue(value * 2);
          controller.close();
        },
      }))
      .registerClientStream(clientStream, async (requests) => {
        let sum = 0;
        for await (const value of requests) sum += value;
        return sum;
      })
      .registerDuplexStream(duplex, async function* (requests) {
        for await (const value of requests) yield value * 2;
      });
    const channel = new InMemoryChannel(builder.build());

    expect((await unaryCall(channel, unary, 21, new RpcContext())).value).toBe(42);
    expect(await collect(await serverStreamCall(channel, serverStream, 21, new RpcContext())))
      .toEqual([21, 42]);

    const upload = await clientStreamCall(channel, clientStream, [20, 22], new RpcContext());
    expect(upload.value).toBe(42);

    const sync = await duplexStreamCall(channel, duplex, values(20, 21), new RpcContext());
    expect(await collect(sync)).toEqual([40, 42]);

  });

  test("executes typed dependent batches and futures", async () => {
    const method = rpcMethod(114, MethodType.UNARY);
    const router = new BebopRouterBuilder({ futuresEnabled: true })
      .registerUnary(method, (value) => value * 2)
      .build();
    const channel = new InMemoryChannel(router);

    const batch = createBatch(channel);
    const first = batch.addUnary(method, 21);
    const second = batch.addUnary(method, first);
    const results = await batch.execute();
    expect(results.get(first)).toBe(42);
    expect(results.get(second)).toBe(84);

    const future = await new FutureDispatcher(channel).dispatch(method, 21);
    expect(await future.value()).toBe(42);
  });

  test("reports unsupported client and duplex streaming before starting a call", async () => {
    const channel: BebopChannel = {
      payload: bufferedPayload,
      unary: async (_methodId, request) => ({ value: request, metadata: new Map() }),
      serverStream: async () => new StreamResponse(emptyAsync(), Promise.resolve(new Map())),
    };

    await expect(clientStreamCall(
      channel,
      rpcMethod(115, MethodType.CLIENT_STREAM),
      [],
      new RpcContext(),
    )).rejects.toMatchObject({ code: StatusCode.UNIMPLEMENTED });
    await expect(duplexStreamCall(
      channel,
      rpcMethod(116, MethodType.DUPLEX_STREAM),
      [],
      new RpcContext(),
    )).rejects.toMatchObject({ code: StatusCode.UNIMPLEMENTED });
  });
});

function byteStream(chunks: readonly Uint8Array[]): ReadableStream<Uint8Array> {
  return new ReadableStream({
    start(controller) {
      for (const chunk of chunks) controller.enqueue(chunk);
      controller.close();
    },
  });
}

function concatenate(chunks: readonly Uint8Array[]): Uint8Array {
  const result = new Uint8Array(chunks.reduce((length, chunk) => length + chunk.length, 0));
  let offset = 0;
  for (const chunk of chunks) {
    result.set(chunk, offset);
    offset += chunk.length;
  }
  return result;
}

async function collect<T>(source: AsyncIterable<T>): Promise<T[]> {
  const values: T[] = [];
  for await (const value of source) values.push(value);
  return values;
}

async function* values(...items: readonly number[]): AsyncGenerator<number> {
  for (const item of items) yield item;
}

async function* emptyAsync<T>(): AsyncGenerator<T> {}

function frameValue(frame: Frame): unknown {
  return {
    header: frame.header,
    payload: [...frame.payload],
    cursor: frame.cursor,
  };
}

function rpcMethod(id: number, methodType: MethodType) {
  return {
    id,
    name: `Method${id}`,
    methodType,
    request: Uint32Codec,
    response: Uint32Codec,
    requestTypeUrl: "type.bebop.sh/test.Number",
    responseTypeUrl: "type.bebop.sh/test.Number",
  } as const;
}

class InMemoryChannel implements BebopChannel {
  readonly payload = bufferedPayload;

  constructor(private readonly router: BebopRouter) {}

  async unary(methodId: number, request: Uint8Array, context: RpcContext): Promise<RpcResponse<Uint8Array>> {
    this.attachPeer(context);
    return {
      value: await this.router.unary(methodId, request, context),
      metadata: context.responseMetadata,
    };
  }

  async serverStream(
    methodId: number,
    request: Uint8Array,
    context: RpcContext,
  ): Promise<StreamResponse<Uint8Array>> {
    this.attachPeer(context);
    const source = await this.router.serverStream(methodId, request, context);
    return new StreamResponse(mapElements(source, context), Promise.resolve(context.responseMetadata));
  }

  async clientStream(methodId: number, context: RpcContext): Promise<{
    readonly send: (request: Uint8Array) => Promise<void>;
    readonly finish: () => Promise<RpcResponse<Uint8Array>>;
  }> {
    this.attachPeer(context);
    const stream = await this.router.clientStream(methodId, context);
    return {
      send: stream.send,
      finish: async () => ({
        value: await stream.finish(),
        metadata: context.responseMetadata,
      }),
    };
  }

  async duplexStream(methodId: number, context: RpcContext): Promise<{
    readonly send: (request: Uint8Array) => Promise<void>;
    readonly finish: () => Promise<void>;
    readonly responses: StreamResponse<Uint8Array>;
  }> {
    this.attachPeer(context);
    const stream = await this.router.duplexStream(methodId, context);
    return {
      send: stream.send,
      finish: stream.finish,
      responses: new StreamResponse(
        mapElements(stream.responses, context),
        Promise.resolve(context.responseMetadata),
      ),
    };
  }

  private attachPeer(context: RpcContext): void {
    context.setAttachment(peerInfoKey, { identity: "test-user" });
  }
}

async function* mapElements(
  source: AsyncIterable<{ readonly value: Uint8Array; readonly cursor?: bigint }>,
  context: RpcContext,
): AsyncGenerator<Uint8Array> {
  for await (const element of source) {
    if (element.cursor !== undefined) context.enqueueCursor(element.cursor);
    yield element.value;
  }
}
