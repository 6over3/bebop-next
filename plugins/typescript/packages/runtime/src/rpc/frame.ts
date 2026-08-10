import { decode, encode } from "../codec.js";
import { BebopReader, BebopWriter } from "../wire.js";
import { FrameFlags, FrameHeader, RpcError, StatusCode, TrailingMetadata } from "../rpc.bb.js";
import { BebopRpcError, type RpcMetadata } from "./error.js";

const cursorSize = 8;
const emptyBytes: Uint8Array<ArrayBufferLike> = new Uint8Array();
export const defaultMaxFramePayloadSize = 16 * 1024 * 1024;

export class Frame {
  static readonly headerSize = 9;

  readonly header: FrameHeader;
  readonly payload: Uint8Array;
  readonly cursor: bigint | undefined;

  constructor(payload: Uint8Array, flags: number = FrameFlags.NONE, streamId = 0, cursor?: bigint) {
    if (payload.length > 0xffff_ffff) {
      throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "frame payload exceeds uint32 length");
    }
    const resolvedFlags = cursor === undefined ? flags : flags | FrameFlags.CURSOR;
    Frame.validateFlags(resolvedFlags);
    this.header = { length: payload.length, flags: resolvedFlags, streamId };
    this.payload = payload;
    this.cursor = cursor;
  }

  get isEndStream(): boolean { return (this.header.flags & FrameFlags.END_STREAM) !== 0; }
  get isError(): boolean { return (this.header.flags & FrameFlags.ERROR) !== 0; }
  get isTrailer(): boolean { return (this.header.flags & FrameFlags.TRAILER) !== 0; }

  encode(): Uint8Array {
    const writer = new BebopWriter(Frame.headerSize + this.payload.length + (this.cursor === undefined ? 0 : cursorSize));
    FrameHeader.writeInto(writer, this.header);
    writer.writeBytes(this.payload);
    if (this.cursor !== undefined) writer.writeUint64(this.cursor);
    return writer.toArrayView();
  }

  static decode(bytes: Uint8Array): Frame {
    if (bytes.length < Frame.headerSize) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, `frame too short: ${bytes.length} bytes`);
    }
    const header = FrameHeader.readFrom(new BebopReader(bytes, { copyArrays: false }));
    Frame.validateFlags(header.flags);
    const expected = encodedFrameSize(header);
    if (bytes.length !== expected) {
      throw new BebopRpcError(
        StatusCode.INVALID_ARGUMENT,
        `frame length mismatch: expected ${expected} bytes, received ${bytes.length}`,
      );
    }
    const payloadStart = Frame.headerSize;
    const payloadEnd = payloadStart + header.length;
    const payload = bytes.subarray(payloadStart, payloadEnd);
    const cursor = (header.flags & FrameFlags.CURSOR) === 0
      ? undefined
      : new DataView(bytes.buffer, bytes.byteOffset + payloadEnd, cursorSize).getBigUint64(0, true);
    return new Frame(payload, header.flags, header.streamId, cursor);
  }

  static validateFlags(flags: number): void {
    const endStream = (flags & FrameFlags.END_STREAM) !== 0;
    const error = (flags & FrameFlags.ERROR) !== 0;
    const trailer = (flags & FrameFlags.TRAILER) !== 0;
    if (error && !endStream) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "ERROR frame must end the stream");
    }
    if (trailer && !endStream) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "TRAILER frame must end the stream");
    }
    if (error && trailer) {
      throw new BebopRpcError(StatusCode.INVALID_ARGUMENT, "ERROR and TRAILER cannot both be set");
    }
  }
}

export type FrameStreamOptions = {
  readonly maxPayloadSize?: number;
};

/** Writes complete frame payloads to a byte stream without joining header and body. */
export class StreamFrameWriter implements AsyncDisposable {
  private readonly writer: WritableStreamDefaultWriter<Uint8Array>;
  private readonly maxPayloadSize: number;

  constructor(destination: WritableStream<Uint8Array>, options: FrameStreamOptions = {}) {
    this.writer = destination.getWriter();
    this.maxPayloadSize = validMaxPayloadSize(options.maxPayloadSize);
  }

  async [Symbol.asyncDispose](): Promise<void> {
    await this.close();
  }

  data(payload: Uint8Array, streamId = 0, cursor?: bigint): Promise<void> {
    return this.writeFrame(payload, FrameFlags.NONE, streamId, cursor);
  }

  endStream(payload: Uint8Array = emptyBytes, streamId = 0): Promise<void> {
    return this.writeFrame(payload, FrameFlags.END_STREAM, streamId);
  }

  error(error: BebopRpcError, streamId = 0): Promise<void> {
    return this.writeFrame(
      encode(RpcError, error.toWire()),
      FrameFlags.END_STREAM | FrameFlags.ERROR,
      streamId,
    );
  }

  trailer(metadata: RpcMetadata, streamId = 0): Promise<void> {
    return this.writeFrame(
      encode(TrailingMetadata, { metadata }),
      FrameFlags.END_STREAM | FrameFlags.TRAILER,
      streamId,
    );
  }

  async writeUnary(
    payload: Uint8Array,
    metadata: RpcMetadata = new Map(),
    streamId = 0,
  ): Promise<void> {
    if (metadata.size === 0) await this.endStream(payload, streamId);
    else {
      await this.data(payload, streamId);
      await this.trailer(metadata, streamId);
    }
  }

  async close(): Promise<void> {
    try {
      await this.writer.close();
    } finally {
      this.writer.releaseLock();
    }
  }

  async flush(): Promise<void> {
    await this.writer.ready;
  }

  release(): void {
    this.writer.releaseLock();
  }

  async abort(reason?: unknown): Promise<void> {
    try {
      await this.writer.abort(reason);
    } finally {
      this.writer.releaseLock();
    }
  }

  private async writeFrame(
    payload: Uint8Array,
    flags: number,
    streamId: number,
    cursor?: bigint,
  ): Promise<void> {
    const byteLength = payload.byteLength;
    if (byteLength > this.maxPayloadSize) {
      throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "frame payload too large");
    }
    if (byteLength > 0xffff_ffff) {
      throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "frame payload exceeds uint32 length");
    }
    const resolvedFlags = cursor === undefined ? flags : flags | FrameFlags.CURSOR;
    Frame.validateFlags(resolvedFlags);
    const header = new Uint8Array(Frame.headerSize);
    const view = new DataView(header.buffer);
    view.setUint32(0, byteLength, true);
    view.setUint8(4, resolvedFlags);
    view.setUint32(5, streamId, true);
    await this.writer.write(header);
    if (payload.byteLength !== 0) await this.writer.write(payload);
    if (cursor !== undefined) {
      const bytes = new Uint8Array(cursorSize);
      new DataView(bytes.buffer).setBigUint64(0, cursor, true);
      await this.writer.write(bytes);
    }
  }

}

/**
 * Convert an arbitrarily chunked byte stream into complete Bebop frames.
 *
 * Payloads borrow contiguous input chunks. A payload is copied only when its
 * frame crosses chunk boundaries.
 */
export function createFrameDecoderStream(
  options: FrameStreamOptions = {},
): TransformStream<Uint8Array, Frame> {
  const maxPayloadSize = options.maxPayloadSize ?? defaultMaxFramePayloadSize;
  if (!Number.isSafeInteger(maxPayloadSize) || maxPayloadSize < 0) {
    throw new RangeError(`maxPayloadSize must be a non-negative safe integer: ${maxPayloadSize}`);
  }
  const decoder = new FrameDecoder(maxPayloadSize);

  return new TransformStream<Uint8Array, Frame>({
    transform(chunk, controller) {
      decoder.push(chunk);
      let frame: Frame | undefined;
      while ((frame = decoder.read()) !== undefined) controller.enqueue(frame);
    },
    flush() {
      decoder.finish();
    },
  });
}

export function createFrameEncoderStream(): TransformStream<Frame, Uint8Array> {
  return new TransformStream<Frame, Uint8Array>({
    transform(frame, controller) {
      controller.enqueue(frame.encode());
    },
  });
}

export class FrameWriter {
  private readonly writer: WritableStreamDefaultWriter<Uint8Array>;
  private readonly maxPayloadSize: number;

  constructor(stream: WritableStream<Uint8Array>, options: FrameStreamOptions = {}) {
    this.writer = stream.getWriter();
    this.maxPayloadSize = options.maxPayloadSize ?? defaultMaxFramePayloadSize;
  }

  async data(payload: Uint8Array, streamId = 0, cursor?: bigint): Promise<void> {
    await this.writeFrame(new Frame(payload, FrameFlags.NONE, streamId, cursor));
  }

  async endStream(payload: Uint8Array = emptyBytes, streamId = 0): Promise<void> {
    await this.writeFrame(new Frame(payload, FrameFlags.END_STREAM, streamId));
  }

  async error(error: BebopRpcError, streamId = 0): Promise<void> {
    await this.writeFrame(new Frame(
      encode(RpcError, error.toWire()),
      FrameFlags.END_STREAM | FrameFlags.ERROR,
      streamId,
    ));
  }

  async trailer(metadata: RpcMetadata, streamId = 0): Promise<void> {
    await this.writeFrame(new Frame(
      encode(TrailingMetadata, { metadata }),
      FrameFlags.END_STREAM | FrameFlags.TRAILER,
      streamId,
    ));
  }

  async writeUnary(payload: Uint8Array, metadata: RpcMetadata = new Map(), streamId = 0): Promise<void> {
    if (metadata.size === 0) await this.endStream(payload, streamId);
    else {
      await this.data(payload, streamId);
      await this.trailer(metadata, streamId);
    }
  }

  async drain(
    source: AsyncIterable<{ readonly value: Uint8Array; readonly cursor?: bigint }>,
    metadata: () => RpcMetadata = () => new Map(),
    streamId = 0,
  ): Promise<void> {
    try {
      for await (const element of source) await this.data(element.value, streamId, element.cursor);
      const trailing = metadata();
      if (trailing.size === 0) await this.endStream(emptyBytes, streamId);
      else await this.trailer(trailing, streamId);
    } catch (error) {
      await this.error(BebopRpcError.from(error), streamId);
    }
  }

  async close(): Promise<void> {
    await this.writer.close();
  }

  async abort(reason?: unknown): Promise<void> {
    await this.writer.abort(reason);
  }

  releaseLock(): void {
    this.writer.releaseLock();
  }

  [Symbol.dispose](): void {
    this.releaseLock();
  }

  private async writeFrame(frame: Frame): Promise<void> {
    if (frame.payload.length > this.maxPayloadSize) {
      throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "frame payload too large");
    }
    await this.writer.write(frame.encode());
  }
}

export async function* readFrames(
  stream: ReadableStream<Uint8Array> | AsyncIterable<Uint8Array>,
  options: FrameStreamOptions = {},
): AsyncGenerator<Frame> {
  const maxPayloadSize = options.maxPayloadSize ?? defaultMaxFramePayloadSize;
  if (!Number.isSafeInteger(maxPayloadSize) || maxPayloadSize < 0) {
    throw new RangeError(`maxPayloadSize must be a non-negative safe integer: ${maxPayloadSize}`);
  }
  const decoder = new FrameDecoder(maxPayloadSize);
  for await (const chunk of stream) {
    decoder.push(chunk);
    let frame: Frame | undefined;
    while ((frame = decoder.read()) !== undefined) yield frame;
  }
  decoder.finish();
}

export async function writeFrames(
  stream: WritableStream<Uint8Array>,
  frames: AsyncIterable<Frame> | Iterable<Frame>,
): Promise<void> {
  const writer = stream.getWriter();
  try {
    for await (const frame of frames) await writer.write(frame.encode());
    await writer.close();
  } catch (error) {
    await writer.abort(error);
    throw error;
  } finally {
    writer.releaseLock();
  }
}

export function decodeFrameError(frame: Frame): BebopRpcError | undefined {
  return frame.isError ? BebopRpcError.fromWire(decode(RpcError, frame.payload)) : undefined;
}

function encodedFrameSize(header: FrameHeader): number {
  return Frame.headerSize + header.length + ((header.flags & FrameFlags.CURSOR) === 0 ? 0 : cursorSize);
}

function validMaxPayloadSize(value: number | undefined): number {
  const maxPayloadSize = value ?? defaultMaxFramePayloadSize;
  if (!Number.isSafeInteger(maxPayloadSize) || maxPayloadSize < 0) {
    throw new RangeError(`maxPayloadSize must be a non-negative safe integer: ${maxPayloadSize}`);
  }
  return maxPayloadSize;
}

class ByteQueue {
  private readonly chunks: Uint8Array[] = [];
  private head = 0;
  private offset = 0;
  private byteLength = 0;

  get length(): number {
    return this.byteLength;
  }

  push(chunk: Uint8Array): void {
    if (chunk.length === 0) return;
    this.chunks.push(chunk);
    this.byteLength += chunk.length;
  }

  read(length: number): Uint8Array {
    if (length > this.byteLength) throw new RangeError("ByteQueue read exceeds buffered data");
    if (length === 0) return emptyBytes;

    const first = this.chunks[this.head]!;
    const firstRemaining = first.length - this.offset;
    if (length <= firstRemaining) {
      const value = first.subarray(this.offset, this.offset + length);
      this.offset += length;
      this.byteLength -= length;
      if (this.offset === first.length) {
        this.dropFirst();
        this.offset = 0;
      }
      return value;
    }

    const value = new Uint8Array(length);
    let writeIndex = 0;
    while (writeIndex < length) {
      const chunk = this.chunks[this.head]!;
      const count = Math.min(chunk.length - this.offset, length - writeIndex);
      value.set(chunk.subarray(this.offset, this.offset + count), writeIndex);
      writeIndex += count;
      this.offset += count;
      this.byteLength -= count;
      if (this.offset === chunk.length) {
        this.dropFirst();
        this.offset = 0;
      }
    }
    return value;
  }

  private dropFirst(): void {
    this.chunks[this.head] = emptyBytes;
    this.head++;
    if (this.head >= 1_024 && this.head * 2 >= this.chunks.length) {
      this.chunks.splice(0, this.head);
      this.head = 0;
    }
  }
}

class FrameDecoder {
  private readonly queue = new ByteQueue();
  private pendingHeader: FrameHeader | undefined;

  constructor(private readonly maxPayloadSize: number) {}

  push(chunk: Uint8Array): void {
    this.queue.push(chunk);
  }

  read(): Frame | undefined {
    if (this.pendingHeader === undefined) {
      if (this.queue.length < Frame.headerSize) return undefined;
      this.pendingHeader = FrameHeader.readFrom(
        new BebopReader(this.queue.read(Frame.headerSize), { copyArrays: false }),
      );
      Frame.validateFlags(this.pendingHeader.flags);
      if (this.pendingHeader.length > this.maxPayloadSize) {
        throw new BebopRpcError(StatusCode.RESOURCE_EXHAUSTED, "frame payload too large");
      }
    }
    const cursorLength = (this.pendingHeader.flags & FrameFlags.CURSOR) === 0 ? 0 : cursorSize;
    if (this.queue.length < this.pendingHeader.length + cursorLength) return undefined;
    const payload = this.queue.read(this.pendingHeader.length);
    let cursor: bigint | undefined;
    if (cursorLength !== 0) {
      const bytes = this.queue.read(cursorSize);
      cursor = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getBigUint64(0, true);
    }
    const frame = new Frame(
      payload,
      this.pendingHeader.flags,
      this.pendingHeader.streamId,
      cursor,
    );
    this.pendingHeader = undefined;
    return frame;
  }

  finish(): void {
    if (this.pendingHeader !== undefined || this.queue.length !== 0) {
      throw new BebopRpcError(
        StatusCode.INVALID_ARGUMENT,
        `stream ended with an incomplete frame (${this.queue.length} buffered body byte(s))`,
      );
    }
  }
}
