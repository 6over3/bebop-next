import { StatusCode } from "../rpc.bb.js";
import type { RpcContext } from "./context.js";
import { BebopRpcError, type RpcMetadata } from "./error.js";
import type { BebopPayloadCodec } from "./payload.js";
import type { BebopServiceMethod, StreamSource } from "./service.js";

export type RpcResponse<T, Metadata = RpcMetadata> = {
  readonly message: T;
  readonly metadata: Metadata;
};

export class StreamResponse<T, Metadata = RpcMetadata> implements AsyncIterable<T> {
  constructor(
    readonly values: AsyncIterable<T>,
    readonly metadata: Promise<Metadata>,
  ) {
    void metadata.catch(() => undefined);
  }

  [Symbol.asyncIterator](): AsyncIterator<T> {
    return this.values[Symbol.asyncIterator]();
  }

  map<U>(transform: (value: T) => U | PromiseLike<U>): StreamResponse<U, Metadata> {
    const source = this.values;
    return new StreamResponse(
      (async function* (): AsyncGenerator<U> {
        for await (const value of source) yield transform(value);
      })(),
      this.metadata,
    );
  }
}

export interface BebopChannel<Payload = Uint8Array, Metadata = RpcMetadata> {
  readonly payload: BebopPayloadCodec<Payload>;
  unary(methodId: number, request: Payload, context: RpcContext): Promise<RpcResponse<Payload, Metadata>>;
  serverStream(methodId: number, request: Payload, context: RpcContext): Promise<StreamResponse<Payload, Metadata>>;
  clientStream?(methodId: number, context: RpcContext): Promise<{
    readonly send: (request: Payload) => Promise<void>;
    readonly finish: () => Promise<RpcResponse<Payload, Metadata>>;
  }>;
  duplexStream?(methodId: number, context: RpcContext): Promise<{
    readonly send: (request: Payload) => Promise<void>;
    readonly finish: () => Promise<void>;
    readonly responses: StreamResponse<Payload, Metadata>;
  }>;
}

export class ClientStreamCall<Request, Response, Metadata = RpcMetadata>
implements AsyncDisposable {
  private finishTask: Promise<RpcResponse<Response, Metadata>> | undefined;

  constructor(
    private readonly sendRequest: (request: Request) => Promise<void>,
    private readonly finishRequests: () => Promise<RpcResponse<Response, Metadata>>,
  ) {}

  async send(request: Request): Promise<void> {
    if (this.finishTask !== undefined) throw new Error("request stream has finished");
    await this.sendRequest(request);
  }

  finish(): Promise<RpcResponse<Response, Metadata>> {
    return (this.finishTask ??= Promise.resolve().then(() => this.finishRequests()));
  }

  async [Symbol.asyncDispose](): Promise<void> {
    await this.finish();
  }
}

export class DuplexStreamCall<Request, Response, Metadata = RpcMetadata>
implements AsyncIterable<Response>, AsyncDisposable {
  private finishTask: Promise<void> | undefined;

  constructor(
    private readonly sendRequest: (request: Request) => Promise<void>,
    private readonly finishRequests: () => Promise<void>,
    readonly responses: StreamResponse<Response, Metadata>,
  ) {}

  get metadata(): Promise<Metadata> {
    return this.responses.metadata;
  }

  async send(request: Request): Promise<void> {
    if (this.finishTask !== undefined) throw new Error("request stream has finished");
    await this.sendRequest(request);
  }

  async sendAll(
    requests: Iterable<Request> | AsyncIterable<Request> | ReadableStream<Request>,
  ): Promise<void> {
    for await (const request of requests) await this.send(request);
    await this.finish();
  }

  finish(): Promise<void> {
    return (this.finishTask ??= Promise.resolve().then(() => this.finishRequests()));
  }

  [Symbol.asyncIterator](): AsyncIterator<Response> {
    return this.responses[Symbol.asyncIterator]();
  }

  async [Symbol.asyncDispose](): Promise<void> {
    await this.finish();
  }
}

export async function unaryCall<Request, Response, Payload, Metadata>(
  channel: BebopChannel<Payload, Metadata>,
  method: BebopServiceMethod<Request, Response, unknown>,
  request: Request,
  context: RpcContext,
): Promise<RpcResponse<Response, Metadata>> {
  const response = await channel.unary(
    method.id,
    channel.payload.encode(method.request, request),
    context,
  );
  return {
    message: await channel.payload.decode(method.response, response.message),
    metadata: response.metadata,
  };
}

export async function serverStreamCall<Request, Response, Payload, Metadata>(
  channel: BebopChannel<Payload, Metadata>,
  method: BebopServiceMethod<Request, Response, unknown>,
  request: Request,
  context: RpcContext,
): Promise<StreamResponse<Response, Metadata>> {
  return (await channel.serverStream(
    method.id,
    channel.payload.encode(method.request, request),
    context,
  )).map((value) => channel.payload.decode(method.response, value));
}

export async function openClientStream<Request, Response, Payload, Metadata>(
  channel: BebopChannel<Payload, Metadata>,
  method: BebopServiceMethod<Request, Response, unknown>,
  context: RpcContext,
): Promise<ClientStreamCall<Request, Response, Metadata>> {
  if (channel.clientStream === undefined) {
    throw new BebopRpcError(StatusCode.UNIMPLEMENTED, "channel does not support client streaming");
  }
  const stream = await channel.clientStream(method.id, context);
  return new ClientStreamCall(
    (request) => stream.send(
      channel.payload.encode(method.request, request),
    ),
    async () => {
      const response = await stream.finish();
      return {
        message: await channel.payload.decode(method.response, response.message),
        metadata: response.metadata,
      };
    },
  );
}

export async function clientStreamCall<Request, Response, Payload, Metadata>(
  channel: BebopChannel<Payload, Metadata>,
  method: BebopServiceMethod<Request, Response, unknown>,
  requests: StreamSource<Request>,
  context: RpcContext,
): Promise<RpcResponse<Response, Metadata>> {
  const call = await openClientStream(channel, method, context);
  try {
    for await (const request of requests) {
      context.throwIfCancelled();
      await call.send(request);
    }
    return await call.finish();
  } catch (error) {
    context.cancel(error);
    throw error;
  }
}

export async function openDuplexStream<Request, Response, Payload, Metadata>(
  channel: BebopChannel<Payload, Metadata>,
  method: BebopServiceMethod<Request, Response, unknown>,
  context: RpcContext,
): Promise<DuplexStreamCall<Request, Response, Metadata>> {
  if (channel.duplexStream === undefined) {
    throw new BebopRpcError(StatusCode.UNIMPLEMENTED, "channel does not support duplex streaming");
  }
  const stream = await channel.duplexStream(method.id, context);
  return new DuplexStreamCall(
    (request) => stream.send(
      channel.payload.encode(method.request, request),
    ),
    stream.finish,
    stream.responses.map((value) =>
      channel.payload.decode(method.response, value)),
  );
}

export async function duplexStreamCall<Request, Response, Payload, Metadata>(
  channel: BebopChannel<Payload, Metadata>,
  method: BebopServiceMethod<Request, Response, unknown>,
  requests: StreamSource<Request>,
  context: RpcContext,
): Promise<StreamResponse<Response, Metadata>> {
  const call = await openDuplexStream(channel, method, context);
  const sending = sendDuplexRequests(call, requests, context);
  const metadata = Promise.all([call.metadata, sending]).then(([value]) => value);
  return new StreamResponse(completeWith(call, sending, context), metadata);
}

async function sendDuplexRequests<Request, Response, Metadata>(
  call: DuplexStreamCall<Request, Response, Metadata>,
  requests: StreamSource<Request>,
  context: RpcContext,
): Promise<void> {
  try {
    for await (const request of requests) {
      context.throwIfCancelled();
      await call.send(request);
    }
    await call.finish();
  } catch (error) {
    context.cancel(error);
    throw error;
  }
}

async function* completeWith<Value>(
  source: AsyncIterable<Value>,
  completion: Promise<void>,
  context: RpcContext,
): AsyncGenerator<Value> {
  let completed = false;
  try {
    for await (const value of source) yield value;
    await completion;
    completed = true;
  } finally {
    if (!completed) context.cancel();
  }
}
