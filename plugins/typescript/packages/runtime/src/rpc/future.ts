import type { BebopCodec } from "../codec.js";
import { decode, encode } from "../codec.js";
import { BebopEmpty } from "../empty.js";
import {
  FutureCancelRequest,
  FutureDispatchRequest,
  FutureHandle,
  FutureResolveRequest,
  FutureResult,
  StatusCode,
  type FutureOutcome,
} from "../rpc.bb.js";
import { BebopTimestamp } from "../temporal.js";
import type { BebopUUID } from "../uuid.js";
import type { BebopChannel, RpcResponse } from "./channel.js";
import { RpcContext } from "./context.js";
import { BebopRpcError, type RpcMetadata } from "./error.js";
import { BebopReservedMethod } from "./router.js";
import type { BebopServiceMethod } from "./service.js";
import { FifoQueue } from "./queue.js";

export type DispatchOptions = {
  readonly idempotencyKey?: BebopUUID;
  readonly discardResult?: boolean;
};

type PendingResult = PromiseWithResolvers<FutureOutcome>;

export class FutureResolver<Metadata = RpcMetadata> {
  private readonly pending = new Map<BebopUUID, Set<PendingResult>>();
  private readonly completed = new Map<BebopUUID, FutureOutcome>();
  private readonly completedOrder = new FifoQueue<BebopUUID>();
  private resolveTask: Promise<void> | undefined;

  constructor(
    private readonly channel: BebopChannel<Uint8Array, Metadata>,
    private readonly maxCompletedResults = 10_000,
  ) {}

  async resolve(id: BebopUUID, signal?: AbortSignal): Promise<FutureOutcome> {
    const cached = this.completed.get(id);
    if (cached !== undefined) return cached;
    const pending = Promise.withResolvers<FutureOutcome>();
    const entries = this.pending.get(id) ?? new Set();
    entries.add(pending);
    this.pending.set(id, entries);
    this.ensureStream();

    let abortHandler: (() => void) | undefined;
    if (signal !== undefined) {
      abortHandler = (): void => {
        entries.delete(pending);
        if (entries.size === 0) this.pending.delete(id);
        pending.reject(signal.reason ?? new BebopRpcError(StatusCode.CANCELLED));
      };
      if (signal.aborted) abortHandler();
      else signal.addEventListener("abort", abortHandler, { once: true });
    }
    try {
      return await pending.promise;
    } finally {
      if (signal !== undefined && abortHandler !== undefined) {
        signal.removeEventListener("abort", abortHandler);
      }
    }
  }

  async cancel(id: BebopUUID, context = new RpcContext()): Promise<void> {
    await this.channel.unary(
      BebopReservedMethod.cancel,
      encode(FutureCancelRequest, { id }),
      context,
    );
  }

  private ensureStream(): void {
    if (this.resolveTask !== undefined) return;
    this.resolveTask = this.consumeResolveStream().finally(() => {
      this.resolveTask = undefined;
    });
  }

  private async consumeResolveStream(): Promise<void> {
    using context = new RpcContext();
    try {
      const response = await this.channel.serverStream(
        BebopReservedMethod.resolve,
        encode(FutureResolveRequest, {}),
        context,
      );
      for await (const payload of response) this.complete(decode(FutureResult, payload));
      throw new BebopRpcError(StatusCode.UNAVAILABLE, "future resolve stream closed");
    } catch (error) {
      const failure = BebopRpcError.from(error, StatusCode.UNAVAILABLE);
      for (const entries of this.pending.values()) {
        for (const entry of entries) entry.reject(failure);
      }
      this.pending.clear();
    }
  }

  private complete(result: FutureResult): void {
    this.completed.set(result.id, result.outcome);
    this.completedOrder.push(result.id);
    while (this.completedOrder.length > this.maxCompletedResults) {
      const evicted = this.completedOrder.shift();
      if (evicted !== undefined) this.completed.delete(evicted);
    }
    const entries = this.pending.get(result.id);
    if (entries === undefined) return;
    this.pending.delete(result.id);
    for (const entry of entries) entry.resolve(result.outcome);
  }
}

export class BebopFuture<Value, Metadata = RpcMetadata> {
  constructor(
    readonly id: BebopUUID,
    private readonly codec: BebopCodec<Value>,
    private readonly resolver: FutureResolver<Metadata>,
  ) {}

  async response(signal?: AbortSignal): Promise<RpcResponse<Value, RpcMetadata>> {
    const outcome = await this.resolver.resolve(this.id, signal);
    switch (outcome.kind) {
      case "success": return {
        value: decode(this.codec, outcome.value.payload),
        metadata: outcome.value.metadata,
      };
      case "error": throw BebopRpcError.fromWire(outcome.value);
      case "unknown": throw new BebopRpcError(
        StatusCode.INTERNAL,
        `unknown future outcome ${outcome.discriminator}`,
      );
    }
  }

  async value(signal?: AbortSignal): Promise<Value> {
    return (await this.response(signal)).value;
  }

  async cancel(context = new RpcContext()): Promise<void> {
    await this.resolver.cancel(this.id, context);
  }
}

export class FutureDispatcher<Metadata = RpcMetadata> {
  readonly resolver: FutureResolver<Metadata>;

  constructor(
    private readonly channel: BebopChannel<Uint8Array, Metadata>,
    resolver = new FutureResolver(channel),
  ) {
    this.resolver = resolver;
  }

  async dispatch<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    request: Request,
    options: DispatchOptions = {},
    context = new RpcContext(),
  ): Promise<BebopFuture<Response, Metadata>> {
    return this.dispatchEncoded(
      method.id,
      encode(method.request, request),
      method.response,
      options,
      context,
    );
  }

  rehydrate<Value>(id: BebopUUID, codec: BebopCodec<Value>): BebopFuture<Value, Metadata> {
    return new BebopFuture(id, codec, this.resolver);
  }

  async dispatchEncoded<Value>(
    methodId: number,
    payload: Uint8Array,
    responseCodec: BebopCodec<Value>,
    options: DispatchOptions = {},
    context = new RpcContext(),
  ): Promise<BebopFuture<Value, Metadata>> {
    const deadline = context.deadline === undefined
      ? undefined
      : BebopTimestamp.fromInstant(context.deadline);
    const request = {
      methodId,
      payload,
      metadata: context.metadata,
      ...(options.idempotencyKey === undefined ? {} : { idempotencyKey: options.idempotencyKey }),
      ...(options.discardResult === undefined ? {} : { discardResult: options.discardResult }),
      ...(deadline === undefined ? {} : { deadline }),
    };
    const result = await this.channel.unary(
      BebopReservedMethod.dispatch,
      encode(FutureDispatchRequest, request),
      new RpcContext({ metadata: context.metadata }),
    );
    const handle = decode(FutureHandle, result.value);
    return new BebopFuture(handle.id, responseCodec, this.resolver);
  }
}

export async function cancelFuture<Metadata>(
  channel: BebopChannel<Uint8Array, Metadata>,
  id: BebopUUID,
  context = new RpcContext(),
): Promise<void> {
  const response = await channel.unary(
    BebopReservedMethod.cancel,
    encode(FutureCancelRequest, { id }),
    context,
  );
  decode(BebopEmpty, response.value);
}
