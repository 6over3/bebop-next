import type { RpcError as WireRpcError, StatusCode } from "../rpc.bb.js";
import { StatusCode as Codes } from "../rpc.bb.js";

export type RpcMetadata = ReadonlyMap<string, string>;

const statusNames = new Map<number, string>(
  Object.entries(Codes).map(([name, code]) => [code, name]),
);

export class BebopRpcError extends Error {
  readonly code: StatusCode;
  readonly detail: string;
  readonly metadata: RpcMetadata;

  constructor(
    code: StatusCode,
    detail?: string,
    metadata: RpcMetadata = new Map(),
    options?: ErrorOptions,
  ) {
    const name = statusNames.get(code) ?? `STATUS_${code}`;
    const resolvedDetail = detail ?? "";
    super(resolvedDetail.length === 0 ? name : `${name}: ${resolvedDetail}`, options);
    this.name = "BebopRpcError";
    this.code = code;
    this.detail = resolvedDetail;
    this.metadata = metadata;
  }

  toWire(): WireRpcError {
    return {
      code: this.code,
      ...(this.detail.length === 0 ? {} : { detail: this.detail }),
      ...(this.metadata.size === 0 ? {} : { metadata: this.metadata }),
    };
  }

  static fromWire(value: WireRpcError): BebopRpcError {
    return new BebopRpcError(
      value.code ?? Codes.UNKNOWN,
      value.detail,
      value.metadata ?? new Map(),
    );
  }

  static from(error: unknown, fallback: StatusCode = Codes.INTERNAL): BebopRpcError {
    if (error instanceof BebopRpcError) return error;
    if (error instanceof DOMException && error.name === "AbortError") {
      return new BebopRpcError(Codes.CANCELLED, error.message, new Map(), { cause: error });
    }
    return new BebopRpcError(
      fallback,
      error instanceof Error ? error.message : String(error),
      new Map(),
      { cause: error },
    );
  }
}

export function statusCodeName(code: StatusCode): string {
  return statusNames.get(code) ?? `STATUS_${code}`;
}
