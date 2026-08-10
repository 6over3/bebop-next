import { decode, encode, type BebopCodec } from "../codec.js";
import {
  BatchRequest,
  BatchResponse,
  StatusCode,
  type BatchCall,
  type BatchResult,
} from "../rpc.bb.js";
import type { BebopChannel } from "./channel.js";
import { RpcContext } from "./context.js";
import { BebopRpcError, type RpcMetadata } from "./error.js";
import { BebopReservedMethod } from "./router.js";
import type { BebopServiceMethod } from "./service.js";

const emptyPayload = new Uint8Array();

export type CallRef<Response> = {
  readonly kind: "unary";
  readonly callId: number;
  readonly response: BebopCodec<Response>;
};

export type StreamRef<Response> = {
  readonly kind: "serverStream";
  readonly callId: number;
  readonly response: BebopCodec<Response>;
};

export class BatchResults {
  private readonly results: ReadonlyMap<number, BatchResult>;

  constructor(response: BatchResponse) {
    const results = new Map<number, BatchResult>();
    for (const result of response.results) results.set(result.callId, result);
    this.results = results;
  }

  get<Response>(reference: CallRef<Response>): Response;
  get<Response>(reference: StreamRef<Response>): readonly Response[];
  get<Response>(reference: CallRef<Response> | StreamRef<Response>): Response | readonly Response[] {
    const success = this.success(reference.callId);
    if (reference.kind === "unary") {
      const payload = success.payloads[0];
      if (payload === undefined) {
        throw new BebopRpcError(StatusCode.INTERNAL, `batch call ${reference.callId} returned no payload`);
      }
      return decode(reference.response, payload);
    }
    return success.payloads.map((payload) => decode(reference.response, payload));
  }

  metadata(reference: CallRef<unknown> | StreamRef<unknown>): RpcMetadata {
    return this.success(reference.callId).metadata;
  }

  private success(callId: number): {
    readonly payloads: readonly Uint8Array[];
    readonly metadata: RpcMetadata;
  } {
    const result = this.results.get(callId);
    if (result === undefined) throw new RangeError(`unknown batch call ${callId}`);
    switch (result.outcome.kind) {
      case "success": return result.outcome.value;
      case "error": throw BebopRpcError.fromWire(result.outcome.value);
      case "unknown": throw new BebopRpcError(
        StatusCode.INTERNAL,
        `unknown batch outcome ${result.outcome.discriminator}`,
      );
    }
  }
}

export class Batch<Metadata = RpcMetadata> {
  private readonly calls: BatchCall[] = [];
  private nextId = 0;
  private state: "open" | "executed" = "open";

  constructor(
    private readonly channel: BebopChannel<Uint8Array, Metadata>,
    private readonly metadata: RpcMetadata = new Map(),
  ) {}

  addUnary<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    request: Request | CallRef<Request>,
  ): CallRef<Response> {
    const callId = this.add(method, request);
    return {
      kind: "unary",
      callId,
      response: method.response,
    };
  }

  addServerStream<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    request: Request | CallRef<Request>,
  ): StreamRef<Response> {
    const callId = this.add(method, request);
    return {
      kind: "serverStream",
      callId,
      response: method.response,
    };
  }

  async execute(context = new RpcContext()): Promise<BatchResults> {
    if (this.state === "executed") throw new Error("batch has already executed");
    this.state = "executed";
    const response = await this.channel.unary(
      BebopReservedMethod.batch,
      encode(BatchRequest, { calls: this.calls, metadata: this.metadata }),
      context,
    );
    return new BatchResults(decode(BatchResponse, response.value));
  }

  private add<Request, Response>(
    method: BebopServiceMethod<Request, Response>,
    request: Request | CallRef<Request>,
  ): number {
    if (this.state === "executed") throw new Error("batch has already executed");
    const callId = this.nextId++;
    const forwarding = isCallRef(request);
    this.calls.push({
      callId,
      methodId: method.id,
      payload: forwarding ? emptyPayload : encode(method.request, request),
      inputFrom: forwarding ? request.callId : -1,
    });
    return callId;
  }
}

export function createBatch<Metadata>(
  channel: BebopChannel<Uint8Array, Metadata>,
  metadata: RpcMetadata = new Map(),
): Batch<Metadata> {
  return new Batch(channel, metadata);
}

function isCallRef<Value>(value: Value | CallRef<Value>): value is CallRef<Value> {
  return typeof value === "object" && value !== null && "kind" in value && value.kind === "unary";
}
