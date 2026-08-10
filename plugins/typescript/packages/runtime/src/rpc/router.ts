import { decode, encode } from "../codec.js";
import { BebopEmpty } from "../empty.js";
import type { BebopReaderOptions } from "../wire.js";
import {
  BatchRequest,
  BatchResponse,
  DiscoveryResponse,
  FutureCancelRequest,
  FutureDispatchRequest,
  FutureHandle,
  FutureResolveRequest,
  FutureResult,
  MethodType,
  StatusCode,
  type BatchResult,
  type ServiceInfo,
} from "../rpc.bb.js";
import { RpcContext } from "./context.js";
import { peerInfoKey, type PeerInfo } from "./context.js";
import { BebopRpcError } from "./error.js";
import type { FutureStorage } from "./future-storage.js";
import { InMemoryFutureStorage } from "./future-storage.js";
import {
  registeredServices,
  type Awaitable,
  type BebopServiceDefinition,
  type BebopServiceMethod,
  type StreamSource,
} from "./service.js";

export const BebopReservedMethod = {
  discovery: 0,
  batch: 1,
  dispatch: 2,
  resolve: 3,
  cancel: 4,
} as const;

const reservedMethodIds = new Set<number>(Object.values(BebopReservedMethod));
const emptyPayload = new Uint8Array();

export type RpcStreamElement<Payload> = {
  readonly value: Payload;
  readonly cursor?: bigint;
};

export interface BebopRpcRouter<Payload> {
  unary(methodId: number, payload: Payload, context?: RpcContext): Promise<Payload>;
  serverStream(
    methodId: number,
    payload: Payload,
    context?: RpcContext,
  ): Promise<AsyncIterable<RpcStreamElement<Payload>>>;
  clientStream(methodId: number, context?: RpcContext): Promise<{
    readonly send: (payload: Payload) => Promise<void>;
    readonly finish: () => Promise<Payload>;
  }>;
  duplexStream(methodId: number, context?: RpcContext): Promise<{
    readonly send: (payload: Payload) => Promise<void>;
    readonly finish: () => Promise<void>;
    readonly responses: AsyncIterable<RpcStreamElement<Payload>>;
  }>;
  methodType(methodId: number): MethodType | undefined;
}

export interface RpcInterceptor {
  <Result>(
    methodId: number,
    context: RpcContext,
    proceed: () => Promise<Result>,
  ): Promise<Result>;
}

export type BebopRouterConfig = {
  readonly discoveryEnabled?: boolean;
  readonly maxBatchSize?: number;
  readonly maxBatchStreamElements?: number;
  readonly futuresEnabled?: boolean;
  readonly maxPendingFutures?: number;
  readonly maxCompletedFutures?: number;
  readonly allowUnauthenticatedFutureOwners?: boolean;
  /** Number of decoded request messages buffered before applying transport backpressure. */
  readonly requestStreamBufferSize?: number;
  /** Maximum dynamic array or map length accepted from one request. */
  readonly maxCollectionLength?: number;
};

type UnaryRegistration = {
  readonly kind: typeof MethodType.UNARY;
  readonly invoke: (payload: Uint8Array, context: RpcContext) => Promise<Uint8Array>;
};

type ServerStreamRegistration = {
  readonly kind: typeof MethodType.SERVER_STREAM;
  readonly invoke: (payload: Uint8Array, context: RpcContext) => Promise<AsyncIterable<RpcStreamElement<Uint8Array>>>;
};

type ClientStreamRegistration = {
  readonly kind: typeof MethodType.CLIENT_STREAM;
  readonly invoke: (context: RpcContext) => Promise<{
    readonly send: (payload: Uint8Array) => Promise<void>;
    readonly finish: () => Promise<Uint8Array>;
  }>;
};

type DuplexStreamRegistration = {
  readonly kind: typeof MethodType.DUPLEX_STREAM;
  readonly invoke: (context: RpcContext) => Promise<{
    readonly send: (payload: Uint8Array) => Promise<void>;
    readonly finish: () => Promise<void>;
    readonly responses: AsyncIterable<RpcStreamElement<Uint8Array>>;
  }>;
};

type MethodRegistration =
  | UnaryRegistration
  | ServerStreamRegistration
  | ClientStreamRegistration
  | DuplexStreamRegistration;

export class BebopRouterBuilder {
  private readonly methods = new Map<number, MethodRegistration>();
  private readonly services: ServiceInfo[] = [];
  private readonly interceptors: RpcInterceptor[] = [];
  private futureStorage: FutureStorage | undefined;
  private readonly config: BebopRouterConfig;
  private readonly requestStreamBufferSize: number;
  private readonly readerOptions: BebopReaderOptions;

  constructor(config: BebopRouterConfig = {}) {
    this.config = config;
    this.requestStreamBufferSize = validRequestStreamBufferSize(config.requestStreamBufferSize);
    const maxCollectionLength = validCollectionLimit(config.maxCollectionLength);
    this.readerOptions = { maxCollectionLength };
  }

  addService(service: BebopServiceDefinition): this {
    if (this.services.some(({ name }) => name === service.serviceName)) {
      throw new Error(`service '${service.serviceName}' is already registered`);
    }
    this.services.push(service.serviceInfo);
    return this;
  }

  registerService(handler: object): this {
    const bindings = registeredServices(handler);
    if (bindings === undefined) {
      throw new TypeError("service handler class is missing a generated @Service.handler decorator");
    }
    for (const binding of bindings) binding.register(this, handler);
    return this;
  }

  addInterceptor(interceptor: RpcInterceptor): this {
    this.interceptors.push(interceptor);
    return this;
  }

  useFutureStorage(storage: FutureStorage): this {
    this.futureStorage = storage;
    return this;
  }

  registerUnary<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    handler: (request: Request, context: RpcContext) => Awaitable<Response>,
  ): this {
    return this.register(method.id, {
      kind: MethodType.UNARY,
      invoke: async (payload, context) => encode(
        method.response,
        await handler(decode(method.request, payload, this.readerOptions), context),
      ),
    });
  }

  registerServerStream<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    handler: (
      request: Request,
      context: RpcContext,
    ) => Awaitable<StreamSource<Response>>,
  ): this {
    const registration: ServerStreamRegistration = {
      kind: MethodType.SERVER_STREAM,
      invoke: async (payload, context) => {
        const source = await handler(decode(method.request, payload, this.readerOptions), context);
        return mapStream(source, (value) => {
          const cursor = context.dequeueCursor();
          return {
            value: encode(method.response, value),
            ...(cursor === undefined ? {} : { cursor }),
          };
        });
      },
    };
    return this.register(method.id, registration);
  }

  registerClientStream<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    handler: (requests: ReadableStream<Request>, context: RpcContext) => Awaitable<Response>,
  ): this {
    return this.register(method.id, {
      kind: MethodType.CLIENT_STREAM,
      invoke: async (context) => {
        const requests = createRequestStream<Request>(context, this.requestStreamBufferSize);
        const response = Promise.resolve(handler(requests.readable, context));
        return {
          send: (payload) => requests.send(decode(method.request, payload, this.readerOptions)),
          finish: async () => {
            await requests.close();
            return encode(method.response, await response);
          },
        };
      },
    });
  }

  registerDuplexStream<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    handler: (
      requests: ReadableStream<Request>,
      context: RpcContext,
    ) => Awaitable<StreamSource<Response>>,
  ): this {
    return this.register(method.id, {
      kind: MethodType.DUPLEX_STREAM,
      invoke: async (context) => {
        const requests = createRequestStream<Request>(context, this.requestStreamBufferSize);
        const response = Promise.resolve(handler(requests.readable, context));
        return {
          send: (payload) => requests.send(decode(method.request, payload, this.readerOptions)),
          finish: () => requests.close(),
          responses: mapStream(response, (value) => {
            const cursor = context.dequeueCursor();
            return {
              value: encode(method.response, value),
              ...(cursor === undefined ? {} : { cursor }),
            };
          }),
        };
      },
    });
  }

  build(): BebopRouter {
    const storage = this.futureStorage ?? (this.config.futuresEnabled === true
      ? new InMemoryFutureStorage({
          ...(this.config.maxPendingFutures === undefined
            ? {}
            : { maxPending: this.config.maxPendingFutures }),
          ...(this.config.maxCompletedFutures === undefined
            ? {}
            : { maxCompleted: this.config.maxCompletedFutures }),
        })
      : undefined);
    return new BebopRouter(
      new Map(this.methods),
      [...this.services],
      [...this.interceptors],
      this.config,
      storage,
    );
  }

  private register(id: number, registration: MethodRegistration): this {
    if (reservedMethodIds.has(id)) throw new Error(`method ID ${id} is reserved`);
    if (this.methods.has(id)) throw new Error(`duplicate method ID 0x${id.toString(16).toUpperCase()}`);
    this.methods.set(id, registration);
    return this;
  }
}

export class BebopRouter implements BebopRpcRouter<Uint8Array> {
  private readonly config: Required<BebopRouterConfig>;
  private readonly readerOptions: BebopReaderOptions;

  constructor(
    private readonly methods: ReadonlyMap<number, MethodRegistration>,
    private readonly services: readonly ServiceInfo[],
    private readonly interceptors: readonly RpcInterceptor[],
    config: BebopRouterConfig,
    private readonly futureStorage?: FutureStorage,
  ) {
    this.config = {
      discoveryEnabled: config.discoveryEnabled ?? true,
      maxBatchSize: config.maxBatchSize ?? Number.POSITIVE_INFINITY,
      maxBatchStreamElements: config.maxBatchStreamElements ?? Number.POSITIVE_INFINITY,
      futuresEnabled: config.futuresEnabled ?? futureStorage !== undefined,
      maxPendingFutures: config.maxPendingFutures ?? Number.POSITIVE_INFINITY,
      maxCompletedFutures: config.maxCompletedFutures ?? 10_000,
      allowUnauthenticatedFutureOwners: config.allowUnauthenticatedFutureOwners ?? false,
      requestStreamBufferSize: validRequestStreamBufferSize(config.requestStreamBufferSize),
      maxCollectionLength: validCollectionLimit(config.maxCollectionLength),
    };
    this.readerOptions = { maxCollectionLength: this.config.maxCollectionLength };
  }

  async unary(methodId: number, payload: Uint8Array, context = new RpcContext()): Promise<Uint8Array> {
    return this.runInterceptors(methodId, context, async () => {
      if (methodId === BebopReservedMethod.discovery) {
        if (!this.config.discoveryEnabled) {
          throw new BebopRpcError(StatusCode.UNIMPLEMENTED, "service discovery is disabled");
        }
        return encode(DiscoveryResponse, { services: this.services });
      }
      if (methodId === BebopReservedMethod.batch) return this.handleBatch(payload, context);
      if (methodId === BebopReservedMethod.dispatch) return this.handleDispatch(payload, context);
      if (methodId === BebopReservedMethod.cancel) return this.handleCancel(payload, context);
      return this.requireMethod(methodId, MethodType.UNARY).invoke(payload, context);
    });
  }

  async serverStream(
    methodId: number,
    payload: Uint8Array,
    context = new RpcContext(),
  ): Promise<AsyncIterable<RpcStreamElement<Uint8Array>>> {
    return this.runInterceptors(methodId, context, () => methodId === BebopReservedMethod.resolve
      ? this.handleResolve(payload, context)
      : this.requireMethod(methodId, MethodType.SERVER_STREAM).invoke(payload, context));
  }

  async clientStream(methodId: number, context = new RpcContext()): Promise<{
    readonly send: (payload: Uint8Array) => Promise<void>;
    readonly finish: () => Promise<Uint8Array>;
  }> {
    return this.runInterceptors(
      methodId,
      context,
      () => this.requireMethod(methodId, MethodType.CLIENT_STREAM).invoke(context),
    );
  }

  async duplexStream(methodId: number, context = new RpcContext()): Promise<{
    readonly send: (payload: Uint8Array) => Promise<void>;
    readonly finish: () => Promise<void>;
    readonly responses: AsyncIterable<RpcStreamElement<Uint8Array>>;
  }> {
    return this.runInterceptors(
      methodId,
      context,
      () => this.requireMethod(methodId, MethodType.DUPLEX_STREAM).invoke(context),
    );
  }

  methodType(methodId: number): MethodType | undefined {
    return this.methods.get(methodId)?.kind;
  }

  private requireMethod<Kind extends MethodType>(
    methodId: number,
    kind: Kind,
  ): Extract<MethodRegistration, { readonly kind: Kind }> {
    const registration = this.methods.get(methodId);
    if (registration === undefined) throw new BebopRpcError(StatusCode.NOT_FOUND, `method ${methodId}`);
    if (registration.kind !== kind) {
      throw new BebopRpcError(StatusCode.UNIMPLEMENTED, `method ${methodId} has a different streaming type`);
    }
    return registration as Extract<MethodRegistration, { readonly kind: Kind }>;
  }

  private async runInterceptors<Result>(
    methodId: number,
    context: RpcContext,
    operation: () => Promise<Result>,
  ): Promise<Result> {
    context.throwIfCancelled();
    let next = operation;
    for (let index = this.interceptors.length - 1; index >= 0; index--) {
      const interceptor = this.interceptors[index]!;
      const proceed = next;
      next = () => interceptor(methodId, context, proceed);
    }
    const result = await next();
    context.throwIfCancelled();
    return result;
  }

  private async handleBatch(payload: Uint8Array, parentContext: RpcContext): Promise<Uint8Array> {
    const request = decode(BatchRequest, payload, this.readerOptions);
    if (request.calls.length > this.config.maxBatchSize) {
      throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "batch is too large");
    }
    const calls = new Map<number, (typeof request.calls)[number]>();
    for (const call of request.calls) calls.set(call.callId, call);
    if (calls.size !== request.calls.length) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "batch call IDs must be unique");
    }
    const executionOrder = orderBatchDependencies(calls);
    const results = new Map<number, Promise<BatchResult>>();
    for (const callId of executionOrder) {
      const call = calls.get(callId)!;
      const pending = (async (): Promise<BatchResult> => {
        try {
          let callPayload = call.payload;
          if (call.inputFrom >= 0) {
            const dependency = await results.get(call.inputFrom)!;
            if (dependency.outcome.kind !== "success") {
              throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, `dependency ${call.inputFrom} failed`);
            }
            callPayload = dependency.outcome.value.payloads[0] ?? emptyPayload;
          }
          using context = parentContext.fork({ metadata: request.metadata });
          const type = this.methodType(call.methodId);
          let payloads: readonly Uint8Array[];
          if (type === MethodType.UNARY) payloads = [await this.unary(call.methodId, callPayload, context)];
          else if (type === MethodType.SERVER_STREAM) {
            const values: Uint8Array[] = [];
            for await (const element of await this.serverStream(call.methodId, callPayload, context)) {
              if (values.length >= this.config.maxBatchStreamElements) {
                throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "batch stream has too many elements");
              }
              values.push(element.value);
            }
            payloads = values;
          } else {
            throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, `method ${call.methodId} cannot run in a batch`);
          }
          return {
            callId,
            outcome: { kind: "success", value: { payloads, metadata: context.responseMetadata } },
          };
        } catch (error) {
          const rpcError = BebopRpcError.from(error);
          return { callId, outcome: { kind: "error", value: rpcError.toWire() } };
        }
      })();
      results.set(callId, pending);
    }
    const ordered = await Promise.all(request.calls.map((call) => results.get(call.callId)!));
    return encode(BatchResponse, { results: ordered });
  }

  private async handleDispatch(payload: Uint8Array, context: RpcContext): Promise<Uint8Array> {
    const storage = this.requireFutureStorage();
    const owner = this.requireOwner(context);
    const request = decode(FutureDispatchRequest, payload, this.readerOptions);
    if (request.methodId === undefined) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "missing method_id");
    }
    if (request.payload === undefined) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "missing payload");
    }
    if (
      request.methodId === BebopReservedMethod.dispatch
      || request.methodId === BebopReservedMethod.resolve
      || request.methodId === BebopReservedMethod.cancel
    ) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "cannot dispatch this reserved method");
    }
    const methodType = this.methodType(request.methodId);
    if (
      request.methodId !== BebopReservedMethod.discovery
      && request.methodId !== BebopReservedMethod.batch
      && methodType === undefined
    ) {
      throw new BebopRpcError(StatusCode.NOT_FOUND, `method ${request.methodId}`);
    }
    if (methodType !== undefined && methodType !== MethodType.UNARY) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "only unary methods can be dispatched");
    }

    const innerContext = new RpcContext({
      metadata: mergeMetadata(context.metadata, request.metadata),
      ...(request.deadline === undefined ? {} : { deadline: request.deadline.toInstant() }),
    });
    const peer = context.getAttachment<PeerInfo>(peerInfoKey);
    if (peer !== undefined) innerContext.setAttachment(peerInfoKey, peer);
    const methodId = request.methodId;
    const innerPayload = request.payload;
    let id: FutureHandle["id"];
    try {
      id = await storage.register({
        context: innerContext,
        owner,
        ...(request.idempotencyKey === undefined ? {} : { idempotencyKey: request.idempotencyKey }),
        ...(request.discardResult === undefined ? {} : { discardResult: request.discardResult }),
        execute: async (futureId) => {
          try {
            const result = await this.unary(methodId, innerPayload, innerContext);
            return {
              id: futureId,
              outcome: {
                kind: "success",
                value: { payload: result, metadata: innerContext.responseMetadata },
              },
            };
          } catch (error) {
            return {
              id: futureId,
              outcome: { kind: "error", value: BebopRpcError.from(error).toWire() },
            };
          }
        },
      });
    } catch (error) {
      innerContext[Symbol.dispose]();
      throw error;
    }
    return encode(FutureHandle, { id });
  }

  private async handleResolve(payload: Uint8Array, context: RpcContext): Promise<AsyncIterable<RpcStreamElement<Uint8Array>>> {
    const storage = this.requireFutureStorage();
    const owner = this.requireOwner(context);
    const request = decode(FutureResolveRequest, payload, this.readerOptions);
    const target = request.ids === undefined || request.ids.length === 0
      ? undefined
      : new Set(request.ids);
    return (async function* (): AsyncGenerator<RpcStreamElement<Uint8Array>> {
      const subscription = await storage.subscribe(request.ids, owner);
      const resolved = new Set<string>();
      for (const result of subscription.immediate) {
        resolved.add(result.id);
        yield { value: encode(FutureResult, result) };
      }
      if (target !== undefined && isSubset(target, resolved)) {
        await subscription.stream.cancel();
        return;
      }
      for await (const result of subscription.stream) {
        context.throwIfCancelled();
        resolved.add(result.id);
        yield { value: encode(FutureResult, result) };
        if (target !== undefined && isSubset(target, resolved)) return;
      }
    })();
  }

  private async handleCancel(payload: Uint8Array, context: RpcContext): Promise<Uint8Array> {
    const storage = this.requireFutureStorage();
    const owner = this.requireOwner(context);
    const request = decode(FutureCancelRequest, payload, this.readerOptions);
    await storage.cancel(request.id, owner);
    return encode(BebopEmpty, {});
  }

  private requireFutureStorage(): FutureStorage {
    if (this.futureStorage === undefined) {
      throw new BebopRpcError(StatusCode.UNIMPLEMENTED, "futures are disabled");
    }
    return this.futureStorage;
  }

  private requireOwner(context: RpcContext): string {
    const peer = context.getAttachment<PeerInfo>(peerInfoKey);
    if (peer?.identity !== undefined) return peer.identity;
    if (this.config.allowUnauthenticatedFutureOwners && peer?.remoteAddress !== undefined) {
      return peer.remoteAddress;
    }
    throw new BebopRpcError(StatusCode.UNAUTHENTICATED);
  }
}

function orderBatchDependencies(
  calls: ReadonlyMap<number, { readonly inputFrom: number }>,
): readonly number[] {
  const visited = new Set<number>();
  const order: number[] = [];
  for (const start of calls.keys()) {
    if (visited.has(start)) continue;
    const path: number[] = [];
    const pathIndexes = new Map<number, number>();
    let id = start;
    while (!visited.has(id)) {
      if (pathIndexes.has(id)) {
        throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "batch dependency cycle");
      }
      pathIndexes.set(id, path.length);
      path.push(id);
      const dependency = calls.get(id)?.inputFrom ?? -1;
      if (dependency < 0) break;
      if (!calls.has(dependency)) {
        throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, `unknown batch dependency ${dependency}`);
      }
      id = dependency;
    }
    for (let index = path.length - 1; index >= 0; index--) {
      const value = path[index]!;
      if (visited.has(value)) continue;
      visited.add(value);
      order.push(value);
    }
  }
  return order;
}

async function* mapStream<Input, Output>(
  source:
    | Iterable<Input>
    | AsyncIterable<Input>
    | ReadableStream<Input>
    | PromiseLike<Iterable<Input> | AsyncIterable<Input> | ReadableStream<Input>>,
  transform: (value: Input) => Output,
): AsyncGenerator<Output> {
  for await (const value of await source) yield transform(value);
}

function createRequestStream<Request>(context: RpcContext, bufferSize: number): {
  readonly readable: ReadableStream<Request>;
  readonly send: (request: Request) => Promise<void>;
  readonly close: () => Promise<void>;
} {
  const transform = new TransformStream<Request, Request>(
    undefined,
    new CountQueuingStrategy({ highWaterMark: 1 }),
    new CountQueuingStrategy({ highWaterMark: bufferSize }),
  );
  const writer = transform.writable.getWriter();
  let state: "open" | "closed" = "open";
  const cleanup = (): void => context.signal.removeEventListener("abort", abort);
  const abort = (): void => {
    if (state === "closed") return;
    state = "closed";
    cleanup();
    void writer.abort(context.signal.reason)
      .catch(() => undefined)
      .finally(() => writer.releaseLock());
  };
  context.signal.addEventListener("abort", abort, { once: true });
  return {
    readable: transform.readable,
    send(request) {
      context.throwIfCancelled();
      if (state === "closed") {
        throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "request stream is closed");
      }
      return writer.write(request);
    },
    async close() {
      if (state === "closed") return;
      state = "closed";
      cleanup();
      try {
        await writer.close();
      } finally {
        writer.releaseLock();
      }
    },
  };
}

function validRequestStreamBufferSize(value: number | undefined): number {
  const bufferSize = value ?? 16;
  if (!Number.isSafeInteger(bufferSize) || bufferSize < 1) {
    throw new RangeError(`requestStreamBufferSize must be a positive safe integer: ${bufferSize}`);
  }
  return bufferSize;
}

function validCollectionLimit(value: number | undefined): number {
  if (value === undefined || value === Number.POSITIVE_INFINITY) {
    return Number.POSITIVE_INFINITY;
  }
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new RangeError(`maxCollectionLength must be a non-negative safe integer: ${value}`);
  }
  return value;
}

function mergeMetadata(left: ReadonlyMap<string, string>, right?: ReadonlyMap<string, string>): Map<string, string> {
  const result = new Map(left);
  if (right !== undefined) for (const [key, value] of right) result.set(key, value);
  return result;
}

function isSubset<T>(subset: ReadonlySet<T>, superset: ReadonlySet<T>): boolean {
  for (const value of subset) if (!superset.has(value)) return false;
  return true;
}
