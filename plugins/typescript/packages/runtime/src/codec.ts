import { BebopReader, BebopWriter } from "./wire";

export interface BebopCodec<T> {
  readFrom(reader: BebopReader): T;
  writeInto(writer: BebopWriter, value: T): void;
  encodedSize(value: T): number;
}

export function encode<T>(codec: BebopCodec<T>, value: T): Uint8Array {
  const writer = new BebopWriter(codec.encodedSize(value));
  codec.writeInto(writer, value);
  return writer.toArray();
}

/**
 * Encodes `value` into a caller-owned writer and returns the number of bytes
 * written.
 *
 * @remarks Keeps the safe `encode()` default intact while letting callers who
 * own a writer's lifecycle reuse its buffer and pack multiple messages back to
 * back (advancing their own cursor by the returned count). The bytes live in
 * the writer's buffer and are not copied; read them with `writer.toArray()`
 * (copy) or `writer.toArrayView()` (view, valid only until the next write,
 * reset, or pool reuse of the same writer).
 */
export function encodeInto<T>(codec: BebopCodec<T>, value: T, writer: BebopWriter): number {
  const start = writer.length;
  codec.writeInto(writer, value);
  return writer.length - start;
}

export function decode<T>(codec: BebopCodec<T>, bytes: Uint8Array): T {
  return codec.readFrom(new BebopReader(bytes));
}