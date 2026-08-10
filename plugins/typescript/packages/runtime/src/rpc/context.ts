import { StatusCode } from "../rpc.bb.js";
import { BebopRpcError, type RpcMetadata } from "./error.js";
import { FifoQueue } from "./queue.js";

export type RpcContextOptions = {
  readonly metadata?: RpcMetadata;
  readonly deadline?: Temporal.Instant;
  readonly signal?: AbortSignal;
};

export type PeerInfo = {
  readonly remoteAddress?: string;
  readonly localAddress?: string;
  readonly identity?: string;
  readonly transport?: string;
};

export class RpcAttachmentKey<Value> {
  private declare readonly value: Value;

  constructor(readonly description: string) {}
}

export function createAttachmentKey<Value>(description: string): RpcAttachmentKey<Value> {
  return new RpcAttachmentKey(description);
}

export const peerInfoKey = createAttachmentKey<PeerInfo>("bebop.rpc.peerInfo");

export class RpcContext {
  readonly metadata: RpcMetadata;
  readonly deadline: Temporal.Instant | undefined;
  readonly signal: AbortSignal;
  readonly responseMetadata = new Map<string, string>();

  private readonly controller = new AbortController();
  private readonly attachments = new Map<RpcAttachmentKey<unknown>, unknown>();
  private readonly cursors = new FifoQueue<bigint>();
  private deadlineTimer: ReturnType<typeof setTimeout> | undefined;
  private sourceSignal: AbortSignal | undefined;
  private sourceAbortHandler: (() => void) | undefined;

  constructor(options: RpcContextOptions = {}) {
    this.metadata = options.metadata ?? new Map();
    this.deadline = options.deadline;
    this.signal = this.controller.signal;

    if (options.signal !== undefined) {
      if (options.signal.aborted) this.controller.abort(options.signal.reason);
      else {
        this.sourceSignal = options.signal;
        this.sourceAbortHandler = () => this.controller.abort(options.signal?.reason);
        options.signal.addEventListener("abort", this.sourceAbortHandler, { once: true });
      }
    }

    this.signal.addEventListener("abort", () => this.releaseResources(), { once: true });

    if (this.deadline !== undefined && !this.signal.aborted) {
      this.scheduleDeadline();
    }
  }

  cancel(reason: unknown = new BebopRpcError(StatusCode.CANCELLED)): void {
    this.controller.abort(reason);
    this.releaseResources();
  }

  [Symbol.dispose](): void {
    this.cancel();
  }

  throwIfCancelled(): void {
    if (!this.signal.aborted) return;
    throw this.signal.reason instanceof BebopRpcError
      ? this.signal.reason
      : new BebopRpcError(StatusCode.CANCELLED, undefined, new Map(), { cause: this.signal.reason });
  }

  setAttachment<T>(key: RpcAttachmentKey<T>, value: T): void {
    this.attachments.set(key, value);
  }

  getAttachment<T>(key: RpcAttachmentKey<T>): T | undefined {
    return this.attachments.get(key) as T | undefined;
  }

  enqueueCursor(cursor: bigint): void {
    this.cursors.push(cursor);
  }

  dequeueCursor(): bigint | undefined {
    return this.cursors.shift();
  }

  fork(overrides: RpcContextOptions = {}): RpcContext {
    const deadline = overrides.deadline ?? this.deadline;
    return new RpcContext({
      metadata: overrides.metadata ?? this.metadata,
      ...(deadline === undefined ? {} : { deadline }),
      signal: overrides.signal ?? this.signal,
    });
  }

  private scheduleDeadline(): void {
    if (this.deadline === undefined || this.signal.aborted) return;
    const remaining = this.deadline.epochMilliseconds - Date.now();
    if (remaining <= 0) {
      this.controller.abort(new BebopRpcError(StatusCode.DEADLINE_EXCEEDED));
      return;
    }
    this.deadlineTimer = setTimeout(
      () => this.scheduleDeadline(),
      Math.min(remaining, 2_147_483_647),
    );
  }

  private releaseResources(): void {
    if (this.deadlineTimer !== undefined) {
      clearTimeout(this.deadlineTimer);
      this.deadlineTimer = undefined;
    }
    if (this.sourceSignal !== undefined && this.sourceAbortHandler !== undefined) {
      this.sourceSignal.removeEventListener("abort", this.sourceAbortHandler);
      this.sourceSignal = undefined;
      this.sourceAbortHandler = undefined;
    }
  }
}

export async function withDeadline<T>(
  deadline: Temporal.Instant,
  operation: (signal: AbortSignal) => Promise<T>,
): Promise<T> {
  using context = new RpcContext({ deadline });
  return Promise.race([
    operation(context.signal),
    new Promise<never>((_, reject) => {
      context.signal.addEventListener("abort", () => reject(context.signal.reason), { once: true });
    }),
  ]);
}
