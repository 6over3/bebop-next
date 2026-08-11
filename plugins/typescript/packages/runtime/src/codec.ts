import { BebopRuntimeError } from "./error.js";
import { BebopReader, BebopWriter, type BebopReaderOptions } from "./wire.js";

export interface BebopCodec<T> {
  readFrom(reader: BebopReader): T;
  writeInto(writer: BebopWriter, value: T): void;
  encodedSize(value: T): number;
}

export function encode<T>(codec: BebopCodec<T>, value: T): Uint8Array {
  const encodedSize = codec.encodedSize(value);
  const writer = new BebopWriter(encodedSize);
  codec.writeInto(writer, value);
  if (writer.length !== encodedSize) {
    throw new BebopRuntimeError(
      `codec encodedSize returned ${encodedSize}, but writeInto wrote ${writer.length} bytes`,
    );
  }
  return writer.toArrayView();
}

/**
 * Encodes `value` into a caller-owned writer and returns the number of bytes
 * written.
 *
 * @remarks Lets callers who own a writer's lifecycle reuse its buffer and pack
 * multiple messages back to back (advancing their own cursor by the returned
 * count). The bytes live in
 * the writer's buffer and are not copied; read them with `writer.toArray()`
 * (copy) or `writer.toArrayView()` (view, valid only until the next write,
 * reset, or pool reuse of the same writer).
 */
export function encodeInto<T>(codec: BebopCodec<T>, value: T, writer: BebopWriter): number {
  const start = writer.length;
  codec.writeInto(writer, value);
  return writer.length - start;
}

export function decode<T>(
  codec: BebopCodec<T>,
  bytes: Uint8Array,
  options?: BebopReaderOptions,
): T {
  const reader = new BebopReader(bytes, options);
  const value = codec.readFrom(reader);
  if (reader.index !== reader.length) {
    throw new BebopRuntimeError(`Bebop value contains ${reader.length - reader.index} trailing bytes`);
  }
  return value;
}
