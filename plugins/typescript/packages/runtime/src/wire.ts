import { BFloat16, BFloat16Array } from "./bfloat16.js";
import { BebopRuntimeError } from "./error.js";
import { BebopDuration, BebopTimestamp } from "./temporal.js";

const emptyBytes = new Uint8Array(0);
const emptyArrayBuffer = new ArrayBuffer(0);
const emptyString = "";
const textDecoderThreshold = 300;
let sharedFatalTextDecoder: TextDecoder | undefined;

export type BebopTypedArray =
  | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array
  | BigInt64Array | BigUint64Array | Float16Array | Float32Array | Float64Array;

type BebopTypedArrayConstructor<T extends BebopTypedArray> = {
  readonly BYTES_PER_ELEMENT: number;
  new (buffer: ArrayBufferLike, byteOffset: number, length: number): T;
};

export type BebopReaderOptions = {
  /**
   * When `false`, byte/typed-array reads return zero-copy views over the input
   * buffer instead of copies. Only set this when the input buffer outlives every
   * decoded value and is never mutated or reused. Default `true`.
   */
  readonly copyArrays?: boolean;
  /** Reject dynamic arrays and maps larger than this many elements. */
  readonly maxCollectionLength?: number;
};

function invalidUtf8(cause?: TypeError): never {
  throw new BebopRuntimeError("malformed UTF-8 string", cause === undefined ? undefined : { cause });
}

function requireLength(length: number): void {
  if (!Number.isInteger(length) || length < 0) {
    throw new BebopRuntimeError(`array length must be a non-negative integer: ${length}`);
  }
}

function fatalTextDecoder(): TextDecoder {
  return (sharedFatalTextDecoder ??= new TextDecoder("utf-8", { fatal: true }));
}

/** Returns the exact UTF-8 byte length without allocating an encoded buffer. */
export function utf8ByteLength(value: string): number {
  let byteLength = 0;
  for (let index = 0; index < value.length; index++) {
    const first = value.charCodeAt(index);
    if (first < 0x80) {
      byteLength += 1;
    } else if (first < 0x800) {
      byteLength += 2;
    } else if (first >= 0xd800 && first <= 0xdbff) {
      const second = index + 1 < value.length ? value.charCodeAt(index + 1) : 0;
      if (second >= 0xdc00 && second <= 0xdfff) {
        index++;
        byteLength += 4;
      } else {
        byteLength += 3;
      }
    } else {
      byteLength += 3;
    }
  }
  return byteLength;
}

export class BebopReader {
  readonly buffer: Uint8Array;
  readonly view: DataView;
  readonly copyArrays: boolean;
  readonly maxCollectionLength: number;
  index = 0;

  constructor(buffer: Uint8Array, options?: BebopReaderOptions) {
    this.buffer = buffer;
    this.view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
    this.copyArrays = options?.copyArrays !== false;
    this.maxCollectionLength = validCollectionLength(options?.maxCollectionLength);
  }

  get length(): number {
    return this.buffer.length;
  }

  private require(byteCount: number): void {
    if (byteCount < 0 || this.index + byteCount > this.buffer.length) {
      throw new BebopRuntimeError("unexpected end of Bebop data");
    }
  }

  seek(index: number): void {
    if (index < 0 || index > this.buffer.length) {
      throw new BebopRuntimeError(`reader seek out of bounds: ${index}`);
    }
    this.index = index;
  }

  skip(byteCount: number): void {
    this.require(byteCount);
    this.index += byteCount;
  }

  readByte(): number {
    this.require(1);
    return this.buffer[this.index++]!;
  }

  readTag(): number {
    return this.readByte();
  }

  readBool(): boolean {
    return this.readByte() !== 0;
  }

  readInt8(): number {
    this.require(1);
    return this.view.getInt8(this.index++);
  }

  readUint16(): number {
    this.require(2);
    const v = this.view.getUint16(this.index, true);
    this.index += 2;
    return v;
  }

  readInt16(): number {
    this.require(2);
    const v = this.view.getInt16(this.index, true);
    this.index += 2;
    return v;
  }

  readUint32(): number {
    this.require(4);
    const v = this.view.getUint32(this.index, true);
    this.index += 4;
    return v;
  }

  readInt32(): number {
    this.require(4);
    const v = this.view.getInt32(this.index, true);
    this.index += 4;
    return v;
  }

  readUint64(): bigint {
    this.require(8);
    const v = this.view.getBigUint64(this.index, true);
    this.index += 8;
    return v;
  }

  readInt64(): bigint {
    this.require(8);
    const v = this.view.getBigInt64(this.index, true);
    this.index += 8;
    return v;
  }

  readUint128(): bigint {
    const low = this.readUint64();
    const high = this.readUint64();
    return (high << 64n) | low;
  }

  readInt128(): bigint {
    return BigInt.asIntN(128, this.readUint128());
  }

  readFloat16(): number {
    this.require(2);
    if (typeof this.view.getFloat16 !== "function") {
      throw new BebopRuntimeError("DataView.getFloat16 is not available in this JavaScript runtime");
    }
    const v = this.view.getFloat16(this.index, true);
    this.index += 2;
    return v;
  }

  readFloat32(): number {
    this.require(4);
    const v = this.view.getFloat32(this.index, true);
    this.index += 4;
    return v;
  }

  readFloat64(): number {
    this.require(8);
    const v = this.view.getFloat64(this.index, true);
    this.index += 8;
    return v;
  }

  readBFloat16(): BFloat16 {
    return BFloat16.readFrom(this);
  }

  readTimestamp(): BebopTimestamp {
    const seconds = this.readInt64();
    const nanoseconds = this.readInt32();
    const offsetMs = this.readInt32();
    return BebopTimestamp.fromWire(seconds, nanoseconds, offsetMs);
  }

  readDuration(): BebopDuration {
    const seconds = this.readInt64();
    const nanoseconds = this.readInt32();
    return BebopDuration.fromWire(seconds, nanoseconds);
  }

  /**
   * Reads `byteCount` bytes. Copies by default; returns a view when the reader
   * was constructed with `copyArrays: false`.
   */
  readBytes(byteCount: number): Uint8Array {
    if (byteCount === 0) {
      return emptyBytes;
    }
    this.require(byteCount);
    const start = this.index;
    const end = start + byteCount;
    this.index = end;
    return this.copyArrays
      ? new Uint8Array(this.buffer.subarray(start, end))
      : this.buffer.subarray(start, end);
  }

  /** Reads a length-prefixed byte array, or exactly `length` bytes for fixed arrays. */
  readUint8Array(length?: number): Uint8Array {
    if (length === undefined) {
      return this.readBytes(this.readUint32());
    }
    requireLength(length);
    return this.readBytes(length);
  }

  readInt8Array(length?: number): Int8Array {
    return this.readTypedArray(length ?? this.readUint32(), Int8Array);
  }

  readUint16Array(length?: number): Uint16Array {
    return this.readTypedArray(length ?? this.readUint32(), Uint16Array);
  }

  readInt16Array(length?: number): Int16Array {
    return this.readTypedArray(length ?? this.readUint32(), Int16Array);
  }

  readUint32Array(length?: number): Uint32Array {
    return this.readTypedArray(length ?? this.readUint32(), Uint32Array);
  }

  readInt32Array(length?: number): Int32Array {
    return this.readTypedArray(length ?? this.readUint32(), Int32Array);
  }

  readBigUint64Array(length?: number): BigUint64Array {
    return this.readTypedArray(length ?? this.readUint32(), BigUint64Array);
  }

  readBigInt64Array(length?: number): BigInt64Array {
    return this.readTypedArray(length ?? this.readUint32(), BigInt64Array);
  }

  readFloat16Array(length?: number): Float16Array {
    if (typeof Float16Array === "undefined") {
      throw new BebopRuntimeError("Float16Array is not available in this JavaScript runtime");
    }
    return this.readTypedArray(length ?? this.readUint32(), Float16Array);
  }

  readFloat32Array(length?: number): Float32Array {
    return this.readTypedArray(length ?? this.readUint32(), Float32Array);
  }

  readFloat64Array(length?: number): Float64Array {
    return this.readTypedArray(length ?? this.readUint32(), Float64Array);
  }

  readBFloat16Array(length?: number): BFloat16Array {
    return BFloat16Array.from(this.readUint16Array(length), { as: "bit-patterns", copy: false });
  }

  readString(): string {
    const lengthBytes = this.readUint32();
    this.require(lengthBytes + 1);
    const start = this.index;
    const end = start + lengthBytes;
    if (this.buffer[end] !== 0) {
      throw new BebopRuntimeError("string missing NUL terminator");
    }
    if (lengthBytes === 0) {
      this.index = end + 1;
      return emptyString;
    }

    if (lengthBytes >= textDecoderThreshold) {
      const decoder = fatalTextDecoder();
      let value: string;
      try {
        value = decoder.decode(this.buffer.subarray(start, end));
      } catch (error) {
        if (error instanceof TypeError) invalidUtf8(error);
        throw error;
      }
      this.index = end + 1;
      return value;
    }

    let result = "";
    let codePoint = 0;
    while (this.index < end) {
      const a = this.buffer[this.index++]!;
      if (a < 0x80) {
        codePoint = a;
      } else if (a >= 0xc2 && a < 0xe0) {
        const b = this.readContinuationByte(end);
        codePoint = ((a & 0x1f) << 6) | (b & 0x3f);
      } else if (a >= 0xe0 && a < 0xf0) {
        const b = this.readContinuationByte(end);
        const c = this.readContinuationByte(end);
        if ((a === 0xe0 && b < 0xa0) || (a === 0xed && b >= 0xa0)) invalidUtf8();
        codePoint = ((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f);
      } else if (a >= 0xf0 && a < 0xf5) {
        const b = this.readContinuationByte(end);
        const c = this.readContinuationByte(end);
        const d = this.readContinuationByte(end);
        if ((a === 0xf0 && b < 0x90) || (a === 0xf4 && b >= 0x90)) invalidUtf8();
        codePoint = ((a & 0x07) << 18) | ((b & 0x3f) << 12) | ((c & 0x3f) << 6) | (d & 0x3f);
      } else {
        invalidUtf8();
      }

      if (codePoint < 0x10000) {
        result += String.fromCharCode(codePoint);
      } else {
        codePoint -= 0x10000;
        result += String.fromCharCode((codePoint >> 10) + 0xd800, (codePoint & 0x03ff) + 0xdc00);
      }
    }
    this.index = end + 1;
    return result;
  }

  readMessageEnd(): number {
    const lengthBytes = this.readUint32();
    const end = this.index + lengthBytes;
    if (end > this.buffer.length) {
      throw new BebopRuntimeError("message length exceeds input size");
    }
    return end;
  }

  readDynamicArray<T>(readElement: (reader: BebopReader) => T): T[] {
    const count = this.readCollectionLength();
    const values: T[] = new Array(Math.min(count, this.buffer.length - this.index));
    for (let i = 0; i < count; i++) {
      values[i] = readElement(this);
    }
    return values;
  }

  readDynamicMap<K, V>(
    readKey: (reader: BebopReader) => K,
    readValue: (reader: BebopReader) => V,
  ): Map<K, V> {
    const count = this.readCollectionLength();
    const values = new Map<K, V>();
    for (let i = 0; i < count; i++) {
      const key = readKey(this);
      values.set(key, readValue(this));
    }
    return values;
  }

  private readTypedArray<T extends BebopTypedArray>(
    count: number,
    ctor: BebopTypedArrayConstructor<T>,
  ): T {
    requireLength(count);
    const byteCount = count * ctor.BYTES_PER_ELEMENT;
    if (byteCount === 0) {
      return new ctor(emptyArrayBuffer, 0, 0);
    }
    this.require(byteCount);

    const start = this.index;
    this.index += byteCount;

    if (this.copyArrays) {
      const bytes = new Uint8Array(this.buffer.subarray(start, start + byteCount));
      return new ctor(bytes.buffer, 0, count);
    }
    const byteOffset = this.buffer.byteOffset + start;
    if (byteOffset % ctor.BYTES_PER_ELEMENT === 0) {
      return new ctor(this.buffer.buffer, byteOffset, count);
    }
    const bytes = new Uint8Array(this.buffer.subarray(start, start + byteCount));
    return new ctor(bytes.buffer, 0, count);
  }

  private readCollectionLength(): number {
    const count = this.readUint32();
    if (count > this.maxCollectionLength) {
      throw new BebopRuntimeError("collection exceeds configured limit");
    }
    return count;
  }

  private readContinuationByte(end: number): number {
    if (this.index >= end) invalidUtf8();
    const byte = this.buffer[this.index++]!;
    if ((byte & 0xc0) !== 0x80) invalidUtf8();
    return byte;
  }
}

function validCollectionLength(value: number | undefined): number {
  if (value === undefined || value === Number.POSITIVE_INFINITY) return Number.POSITIVE_INFINITY;
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new RangeError(`maxCollectionLength must be a non-negative safe integer: ${value}`);
  }
  return value;
}

export class BebopWriter {
  private buffer: Uint8Array;
  private view: DataView;
  private readonly origin: number;
  private readonly fixed: boolean;
  length: number;

  /**
   * @param storage Initial capacity (number), or a caller-owned buffer to write
   *   into. When a buffer is supplied, the writer never reallocates: a message
   *   that exceeds the supplied capacity throws instead of growing.
   */
  constructor(storage: number | Uint8Array | ArrayBuffer = 256) {
    if (typeof storage === "number") {
      this.buffer = new Uint8Array(Math.max(16, storage));
      this.origin = 0;
      this.fixed = false;
    } else if (storage instanceof Uint8Array) {
      this.buffer = storage;
      this.origin = 0;
      this.fixed = true;
    } else {
      this.buffer = new Uint8Array(storage);
      this.origin = 0;
      this.fixed = true;
    }
    this.view = new DataView(this.buffer.buffer, this.buffer.byteOffset, this.buffer.byteLength);
    this.length = this.origin;
  }

  /** Creates a writer that writes into a caller-owned buffer and never reallocates. */
  static fromBuffer(buffer: Uint8Array): BebopWriter {
    return new BebopWriter(buffer);
  }

  private reserve(byteCount: number): number {
    const index = this.length;
    this.length += byteCount;
    this.guaranteeBufferLength(this.length);
    return index;
  }

  private guaranteeBufferLength(length: number): void {
    if (length <= this.buffer.length) {
      return;
    }
    if (this.fixed) {
      throw new BebopRuntimeError(
        `supplied buffer is too small: need ${length} bytes, have ${this.buffer.length}`,
      );
    }
    let nextLength = this.buffer.length;
    while (nextLength < length) nextLength *= 2;
    const next = new Uint8Array(nextLength);
    next.set(this.buffer);
    this.buffer = next;
    this.view = new DataView(next.buffer);
  }

  /** Returns a copy of the written bytes. Safe to retain and reuse. */
  toArray(): Uint8Array {
    return this.buffer.slice(this.origin, this.length);
  }

  /**
   * Returns a no-copy view over the written bytes.
   *
   * @remarks Valid only until the next write or {@link reset} on this writer.
   * Do not store it, hold it across `await`, or use it as a byte-array field of
   * another message encoded with this or a pooled writer.
   */
  toArrayView(): Uint8Array {
    return this.buffer.subarray(this.origin, this.length);
  }

  /**
   * Resets the write cursor to reuse this writer.
   *
   * @remarks Any view from {@link toArrayView} is unsafe to retain after this.
   */
  reset(): void {
    this.length = this.origin;
  }

  writeByte(value: number): void {
    this.buffer[this.reserve(1)] = value;
  }

  writeTag(value: number): void {
    this.writeByte(value);
  }

  writeBool(value: boolean): void {
    this.writeByte(value ? 1 : 0);
  }

  writeInt8(value: number): void {
    this.view.setInt8(this.reserve(1), value);
  }

  writeUint16(value: number): void {
    this.view.setUint16(this.reserve(2), value, true);
  }

  writeInt16(value: number): void {
    this.view.setInt16(this.reserve(2), value, true);
  }

  writeUint32(value: number): void {
    this.view.setUint32(this.reserve(4), value, true);
  }

  writeInt32(value: number): void {
    this.view.setInt32(this.reserve(4), value, true);
  }

  writeUint64(value: bigint): void {
    this.view.setBigUint64(this.reserve(8), BigInt.asUintN(64, value), true);
  }

  writeInt64(value: bigint): void {
    this.view.setBigInt64(this.reserve(8), BigInt.asIntN(64, value), true);
  }

  writeUint128(value: bigint): void {
    const normalized = BigInt.asUintN(128, value);
    this.writeUint64(normalized & 0xffff_ffff_ffff_ffffn);
    this.writeUint64(normalized >> 64n);
  }

  writeInt128(value: bigint): void {
    this.writeUint128(BigInt.asUintN(128, value));
  }

  writeFloat16(value: number): void {
    if (typeof this.view.setFloat16 !== "function") {
      throw new BebopRuntimeError("DataView.setFloat16 is not available in this JavaScript runtime");
    }
    this.view.setFloat16(this.reserve(2), value, true);
  }

  writeFloat32(value: number): void {
    this.view.setFloat32(this.reserve(4), value, true);
  }

  writeFloat64(value: number): void {
    this.view.setFloat64(this.reserve(8), value, true);
  }

  writeBFloat16(value: BFloat16): void {
    BFloat16.writeInto(this, value);
  }

  writeTimestamp(value: BebopTimestamp): void {
    const wire = BebopTimestamp.toWire(value);
    this.writeInt64(wire.seconds);
    this.writeInt32(wire.nanoseconds);
    this.writeInt32(wire.offsetMs);
  }

  writeDuration(value: BebopDuration): void {
    const wire = BebopDuration.toWire(value);
    this.writeInt64(wire.seconds);
    this.writeInt32(wire.nanoseconds);
  }

  writeBytes(value: Uint8Array): void {
    if (value.length === 0) return;
    const index = this.reserve(value.length);
    this.buffer.set(value, index);
  }

  /** Writes a length-prefixed byte array, or exactly `length` bytes for fixed arrays. */
  writeUint8Array(value: Uint8Array, length?: number): void {
    if (length === undefined) {
      this.writeUint32(value.length);
    } else {
      requireLength(length);
      if (value.length !== length) {
        throw new BebopRuntimeError(`expected fixed length ${length}, got ${value.length}`);
      }
    }
    this.writeBytes(value);
  }

  writeInt8Array(value: Int8Array, length?: number): void { this.writeTypedArray(value, length); }
  writeUint16Array(value: Uint16Array, length?: number): void { this.writeTypedArray(value, length); }
  writeInt16Array(value: Int16Array, length?: number): void { this.writeTypedArray(value, length); }
  writeUint32Array(value: Uint32Array, length?: number): void { this.writeTypedArray(value, length); }
  writeInt32Array(value: Int32Array, length?: number): void { this.writeTypedArray(value, length); }
  writeBigUint64Array(value: BigUint64Array, length?: number): void { this.writeTypedArray(value, length); }
  writeBigInt64Array(value: BigInt64Array, length?: number): void { this.writeTypedArray(value, length); }
  writeFloat16Array(value: Float16Array, length?: number): void { this.writeTypedArray(value, length); }
  writeFloat32Array(value: Float32Array, length?: number): void { this.writeTypedArray(value, length); }
  writeFloat64Array(value: Float64Array, length?: number): void { this.writeTypedArray(value, length); }

  writeBFloat16Array(value: BFloat16Array, length?: number): void {
    this.writeUint16Array(value.toBitPatterns({ copy: false }), length);
  }

  writeString(value: string): void {
    const stringLength = value.length;
    const start = this.reserve(4);
    const writeStart = start + 4;
    let writeIndex = writeStart;

    for (let i = 0; i < stringLength; i++) {
      const a = value.charCodeAt(i);
      let codePoint = a;
      if (a >= 0xd800 && a <= 0xdbff) {
        const b = i + 1 < stringLength ? value.charCodeAt(i + 1) : 0;
        if (b >= 0xdc00 && b <= 0xdfff) {
          i++;
          codePoint = (a << 10) + b + (0x10000 - (0xd800 << 10) - 0xdc00);
        } else {
          codePoint = 0xfffd;
        }
      } else if (a >= 0xdc00 && a <= 0xdfff) {
        codePoint = 0xfffd;
      }

      if (codePoint < 0x80) {
        if (writeIndex === this.buffer.length) this.guaranteeBufferLength(writeIndex + 1);
        this.buffer[writeIndex++] = codePoint;
      } else if (codePoint < 0x800) {
        if (writeIndex + 2 > this.buffer.length) this.guaranteeBufferLength(writeIndex + 2);
        this.buffer[writeIndex++] = ((codePoint >> 6) & 0x1f) | 0xc0;
        this.buffer[writeIndex++] = (codePoint & 0x3f) | 0x80;
      } else if (codePoint < 0x10000) {
        if (writeIndex + 3 > this.buffer.length) this.guaranteeBufferLength(writeIndex + 3);
        this.buffer[writeIndex++] = ((codePoint >> 12) & 0x0f) | 0xe0;
        this.buffer[writeIndex++] = ((codePoint >> 6) & 0x3f) | 0x80;
        this.buffer[writeIndex++] = (codePoint & 0x3f) | 0x80;
      } else {
        if (writeIndex + 4 > this.buffer.length) this.guaranteeBufferLength(writeIndex + 4);
        this.buffer[writeIndex++] = ((codePoint >> 18) & 0x07) | 0xf0;
        this.buffer[writeIndex++] = ((codePoint >> 12) & 0x3f) | 0x80;
        this.buffer[writeIndex++] = ((codePoint >> 6) & 0x3f) | 0x80;
        this.buffer[writeIndex++] = (codePoint & 0x3f) | 0x80;
      }
    }

    if (writeIndex === this.buffer.length) this.guaranteeBufferLength(writeIndex + 1);
    this.view.setUint32(start, writeIndex - writeStart, true);
    this.buffer[writeIndex++] = 0;
    this.length = writeIndex;
  }

  reserveMessageLength(): number {
    return this.reserve(4);
  }

  fillMessageLength(position: number): void {
    this.view.setUint32(position, this.length - position - 4, true);
  }

  writeEndMarker(): void {
    this.writeByte(0);
  }

  writeDynamicArray<T>(values: readonly T[], writeElement: (writer: BebopWriter, value: T) => void): void {
    this.writeUint32(values.length);
    for (let i = 0; i < values.length; i++) {
      writeElement(this, values[i]!);
    }
  }

  writeDynamicMap<K, V>(
    values: ReadonlyMap<K, V>,
    writeEntry: (writer: BebopWriter, key: K, value: V) => void,
  ): void {
    this.writeUint32(values.size);
    values.forEach((value, key) => writeEntry(this, key, value));
  }

  private writeTypedArray(value: BebopTypedArray, length?: number): void {
    if (length === undefined) {
      this.writeUint32(value.length);
    } else {
      requireLength(length);
      if (value.length !== length) {
        throw new BebopRuntimeError(`expected fixed length ${length}, got ${value.length}`);
      }
    }
    this.writeBytes(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
  }
}
