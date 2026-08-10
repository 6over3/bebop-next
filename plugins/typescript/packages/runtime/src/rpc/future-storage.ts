import type { FutureOutcome, FutureResult } from "../rpc.bb.js";
import type { BebopUUID } from "../uuid.js";
import type { RpcContext } from "./context.js";
import { BebopRpcError } from "./error.js";
import { StatusCode } from "../rpc.bb.js";
import { FifoQueue } from "./queue.js";

export type FutureRegistration = {
  readonly context: RpcContext;
  readonly idempotencyKey?: BebopUUID;
  readonly owner: string;
  readonly discardResult?: boolean;
  readonly execute: (id: BebopUUID) => Promise<FutureResult>;
};

export interface FutureStorage {
  /** Takes ownership of `registration.context` when registration succeeds. */
  register(registration: FutureRegistration): Promise<BebopUUID>;
  cancel(id: BebopUUID, owner: string): Promise<boolean>;
  subscribe(ids: readonly BebopUUID[] | undefined, owner: string): Promise<{
    readonly immediate: readonly FutureResult[];
    readonly stream: ReadableStream<FutureResult>;
  }>;
  contains(id: BebopUUID): Promise<boolean>;
}

type FutureEntry = {
  readonly owner: string;
  readonly context: RpcContext;
  readonly idempotencyKey?: BebopUUID;
  state: { readonly kind: "pending" } | { readonly kind: "completed"; readonly result: FutureResult };
};

type Subscriber = {
  readonly owner: string;
  readonly ids: ReadonlySet<BebopUUID> | undefined;
  readonly controller: ReadableStreamDefaultController<FutureResult>;
};

export type InMemoryFutureStorageOptions = {
  readonly maxPending?: number;
  readonly maxCompleted?: number;
};

export class InMemoryFutureStorage implements FutureStorage {
  private readonly entries = new Map<BebopUUID, FutureEntry>();
  private readonly idempotency = new Map<BebopUUID, BebopUUID>();
  private readonly subscribers = new Set<Subscriber>();
  private readonly completedOrder = new FifoQueue<BebopUUID>();
  private pendingCount = 0;

  constructor(private readonly options: InMemoryFutureStorageOptions = {}) {}

  async register(registration: FutureRegistration): Promise<BebopUUID> {
    if (registration.idempotencyKey !== undefined) {
      const existingId = this.idempotency.get(registration.idempotencyKey);
      if (existingId !== undefined) {
        const existing = this.entries.get(existingId);
        if (existing?.owner !== registration.owner) {
          throw new BebopRpcError(StatusCode.PERMISSION_DENIED);
        }
        registration.context[Symbol.dispose]();
        return existingId;
      }
    }
    if (this.pendingCount >= (this.options.maxPending ?? Number.POSITIVE_INFINITY)) {
      throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "too many pending futures");
    }

    const id = crypto.randomUUID();
    const entry: FutureEntry = {
      owner: registration.owner,
      context: registration.context,
      state: { kind: "pending" },
      ...(registration.idempotencyKey === undefined
        ? {}
        : { idempotencyKey: registration.idempotencyKey }),
    };
    this.entries.set(id, entry);
    if (registration.idempotencyKey !== undefined) this.idempotency.set(registration.idempotencyKey, id);
    this.pendingCount++;

    void registration.execute(id).then(
      (result) => this.complete(id, result, registration.discardResult === true),
      (error) => this.complete(id, {
        id,
        outcome: errorOutcome(error),
      }, registration.discardResult === true),
    );
    return id;
  }

  async cancel(id: BebopUUID, owner: string): Promise<boolean> {
    const entry = this.entries.get(id);
    if (entry === undefined || entry.owner !== owner) {
      throw new BebopRpcError(StatusCode.PERMISSION_DENIED);
    }
    if (entry.state.kind === "completed") return false;
    entry.context.cancel();
    if (entry.idempotencyKey !== undefined) this.idempotency.delete(entry.idempotencyKey);
    return true;
  }

  async subscribe(ids: readonly BebopUUID[] | undefined, owner: string): Promise<{
    readonly immediate: readonly FutureResult[];
    readonly stream: ReadableStream<FutureResult>;
  }> {
    const idSet = ids === undefined || ids.length === 0 ? undefined : new Set(ids);
    const immediate: FutureResult[] = [];
    for (const [id, entry] of this.entries) {
      if (
        entry.owner === owner
        && entry.state.kind === "completed"
        && (idSet === undefined || idSet.has(id))
      ) {
        immediate.push(entry.state.result);
      }
    }
    let subscriber: Subscriber | undefined;
    const stream = new ReadableStream<FutureResult>({
      start: (controller) => {
        subscriber = { owner, ids: idSet, controller };
        this.subscribers.add(subscriber);
      },
      cancel: () => {
        if (subscriber !== undefined) this.subscribers.delete(subscriber);
      },
    });
    return { immediate, stream };
  }

  async contains(id: BebopUUID): Promise<boolean> {
    return this.entries.has(id);
  }

  private complete(id: BebopUUID, result: FutureResult, discard: boolean): void {
    const entry = this.entries.get(id);
    if (entry === undefined || entry.state.kind !== "pending") return;
    entry.context[Symbol.dispose]();
    this.pendingCount--;
    for (const subscriber of this.subscribers) {
      if (subscriber.owner === entry.owner && (subscriber.ids === undefined || subscriber.ids.has(id))) {
        subscriber.controller.enqueue(result);
      }
    }
    if (discard) {
      this.entries.delete(id);
      if (entry.idempotencyKey !== undefined) this.idempotency.delete(entry.idempotencyKey);
      return;
    }
    entry.state = { kind: "completed", result };
    this.completedOrder.push(id);
    while (this.completedOrder.length > (this.options.maxCompleted ?? 10_000)) {
      const evicted = this.completedOrder.shift();
      if (evicted === undefined) break;
      const removed = this.entries.get(evicted);
      this.entries.delete(evicted);
      if (removed?.idempotencyKey !== undefined) this.idempotency.delete(removed.idempotencyKey);
    }
  }
}

function errorOutcome(error: unknown): FutureOutcome {
  return { kind: "error", value: BebopRpcError.from(error).toWire() };
}
