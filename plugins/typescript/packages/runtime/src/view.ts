import { BebopRuntimeError } from "./error.js";
import { BebopMessageView } from "./message.js";
import {
  BebopReader,
  decodeUtf8,
  resolveBebopReaderOptions,
  validateUtf8,
  type BebopReaderOptions,
  type ResolvedBebopReaderOptions,
} from "./wire.js";
import { BebopUUID, type BebopUUID as BebopUUIDValue } from "./uuid.js";

/** A validated UTF-8 value that remains backed by the encoded input. */
export class BebopStringView {
  private decoded: string | undefined;

  /** The bytes remain owned by the input buffer and must not be mutated. */
  constructor(readonly bytes: Uint8Array) {
    validateUtf8(bytes);
  }

  toString(): string {
    return (this.decoded ??= decodeUtf8(this.bytes));
  }

  get string(): string {
    return this.toString();
  }

  equals(other: string | BebopStringView): boolean {
    if (typeof other === "string") return this.string === other;
    if (this.bytes.length !== other.bytes.length) return false;
    for (let index = 0; index < this.bytes.length; index++) {
      if (this.bytes[index] !== other.bytes[index]) return false;
    }
    return true;
  }
}

type ViewDecoder<Value> = (reader: BebopViewReader) => Value;
type ViewCodec<Value> = { readFrom(reader: BebopViewReader): Value };
type ViewSkipper = { skip(reader: BebopViewReader): void };
type BebopViewContext = {
  readonly options: ResolvedBebopReaderOptions;
  readonly depth: number;
};

/** A zero-copy cursor used by generated aggregate views. */
export class BebopViewReader extends BebopReader {
  readonly encoded: Uint8Array;

  constructor(encoded: Uint8Array, options: BebopReaderOptions = {}, depth = 0) {
    super(encoded, resolveViewOptions(options), depth);
    this.encoded = encoded;
  }

  get position(): number { return this.index; }
  get remaining(): number { return this.length - this.index; }

  finish(): void {
    if (this.remaining !== 0) throw new BebopRuntimeError("Bebop value contains trailing data");
  }

  encodedFrom(from: number): Uint8Array {
    return this.encoded.subarray(from, this.position);
  }

  readUUID(): BebopUUIDValue { return BebopUUID.readFrom(this); }

  skipString(): void {
    validateUtf8(this.readStringBytes());
  }

  skipMessage(): void {
    const bodyLength = this.readUint32();
    if (bodyLength === 0) throw new BebopRuntimeError("message body is empty");
    this.skip(bodyLength);
  }

  skipLengthPrefixed(): void {
    this.skip(this.readUint32());
  }

  readStringView(): BebopStringView {
    return new BebopStringView(this.readStringBytes());
  }

  readNestedView<Value>(codec: ViewCodec<Value>): Value {
    return super.readNested({ readFrom: () => codec.readFrom(this) });
  }

  skipNestedView(codec: ViewSkipper): void {
    super.readNested({
      readFrom: () => {
        codec.skip(this);
      },
    });
  }

  readLengthPrefixedValue<Value>(decode: ViewDecoder<Value>): {
    readonly encoded: Uint8Array;
    readonly value: Value;
  } {
    const start = this.position;
    const body = this.readLengthPrefixedView();
    const nested = new BebopViewReader(body, this.options, this.nestingDepth);
    const value = decode(nested);
    nested.finish();
    return { encoded: this.encoded.subarray(start, this.position), value };
  }

  readArrayView<Value>(
    decode: ViewDecoder<Value>,
    elementSize?: number,
    skip: (reader: BebopViewReader) => void = decode,
  ): BebopArrayView<Value> {
    const start = this.position;
    const count = this.readCollectionCount();
    const bodyStart = this.position;
    if (elementSize === undefined) {
      for (let index = 0; index < count; index++) skip(this);
    } else {
      this.skip(checkedByteCount(count, elementSize));
    }
    return new ArrayView(
      this.encoded.subarray(start, this.position),
      this.encoded.subarray(bodyStart, this.position),
      count,
      decode,
      { options: this.options, depth: this.nestingDepth },
      elementSize,
    );
  }

  readFixedArrayView<Value>(
    count: number,
    decode: ViewDecoder<Value>,
    elementSize?: number,
    skip: (reader: BebopViewReader) => void = decode,
  ): BebopArrayView<Value> {
    this.validateCollectionLength(count);
    const start = this.position;
    if (elementSize === undefined) {
      for (let index = 0; index < count; index++) skip(this);
    } else {
      this.skip(checkedByteCount(count, elementSize));
    }
    const encoded = this.encoded.subarray(start, this.position);
    return new ArrayView(
      encoded,
      encoded,
      count,
      decode,
      { options: this.options, depth: this.nestingDepth },
      elementSize,
    );
  }

  readMapView<Key, Value>(
    decodeKey: ViewDecoder<Key>,
    decodeValue: ViewDecoder<Value>,
    skipKey: (reader: BebopViewReader) => void = decodeKey,
    skipValue: (reader: BebopViewReader) => void = decodeValue,
  ): BebopMapView<Key, Value> {
    const start = this.position;
    const count = this.readCollectionCount();
    const bodyStart = this.position;
    for (let index = 0; index < count; index++) {
      skipKey(this);
      skipValue(this);
    }
    return new MapView(
      this.encoded.subarray(start, this.position),
      this.encoded.subarray(bodyStart, this.position),
      count,
      decodeKey,
      decodeValue,
      { options: this.options, depth: this.nestingDepth },
    );
  }

  skipArray(skip: (reader: BebopViewReader) => void, elementSize?: number): void {
    const count = this.readCollectionCount();
    if (elementSize === undefined) {
      for (let index = 0; index < count; index++) skip(this);
    } else {
      this.skip(checkedByteCount(count, elementSize));
    }
  }

  skipFixedArray(
    count: number,
    skip: (reader: BebopViewReader) => void,
    elementSize?: number,
  ): void {
    this.validateCollectionLength(count);
    if (elementSize === undefined) {
      for (let index = 0; index < count; index++) skip(this);
    } else {
      this.skip(checkedByteCount(count, elementSize));
    }
  }

  skipMap(
    skipKey: (reader: BebopViewReader) => void,
    skipValue: (reader: BebopViewReader) => void,
  ): void {
    const count = this.readCollectionCount();
    for (let index = 0; index < count; index++) {
      skipKey(this);
      skipValue(this);
    }
  }
}

/** An immutable, lazy view over an encoded Bebop array. */
export interface BebopArrayView<Value> extends Iterable<Value> {
  readonly encoded: Uint8Array;
  readonly length: number;
  readonly size: number;
  at(index: number): Value | undefined;
  get(index: number): Value;
  toArray(): Value[];
}

class ArrayView<Value> implements BebopArrayView<Value> {
  constructor(
    readonly encoded: Uint8Array,
    private readonly body: Uint8Array,
    readonly length: number,
    private readonly decode: ViewDecoder<Value>,
    private readonly context: BebopViewContext,
    private readonly elementSize?: number,
  ) {}

  get size(): number { return this.length; }

  at(index: number): Value | undefined {
    const normalized = index < 0 ? this.length + index : index;
    if (!Number.isInteger(normalized) || normalized < 0 || normalized >= this.length) return undefined;
    return this.get(normalized);
  }

  get(index: number): Value {
    if (!Number.isInteger(index) || index < 0 || index >= this.length) {
      throw new RangeError(`array index is out of bounds: ${index}`);
    }
    if (this.elementSize !== undefined) {
      const offset = checkedByteCount(index, this.elementSize);
      return this.decode(new BebopViewReader(
        this.body.subarray(offset, offset + this.elementSize),
        this.context.options,
        this.context.depth,
      ));
    }
    const reader = new BebopViewReader(
      this.body,
      this.context.options,
      this.context.depth,
    );
    for (let current = 0; current < index; current++) this.decode(reader);
    return this.decode(reader);
  }

  *[Symbol.iterator](): Iterator<Value> {
    const reader = new BebopViewReader(
      this.body,
      this.context.options,
      this.context.depth,
    );
    for (let index = 0; index < this.length; index++) yield this.decode(reader);
  }

  toArray(): Value[] {
    return Array.from(this);
  }
}

/** A lazy, wire-order view over encoded Bebop map entries. */
export interface BebopMapView<Key, Value> extends Iterable<readonly [Key, Value]> {
  readonly encoded: Uint8Array;
  readonly size: number;
  get(key: Key | (Key extends BebopStringView ? string : never)): Value | undefined;
  has(key: Key | (Key extends BebopStringView ? string : never)): boolean;
  toMap(): Map<Key, Value>;
}

class MapView<Key, Value> implements BebopMapView<Key, Value> {
  constructor(
    readonly encoded: Uint8Array,
    private readonly body: Uint8Array,
    readonly size: number,
    private readonly decodeKey: ViewDecoder<Key>,
    private readonly decodeValue: ViewDecoder<Value>,
    private readonly context: BebopViewContext,
  ) {}

  get(key: Key | (Key extends BebopStringView ? string : never)): Value | undefined {
    for (const [candidate, value] of this) {
      if (viewKeysEqual(candidate, key)) return value;
    }
    return undefined;
  }

  has(key: Key | (Key extends BebopStringView ? string : never)): boolean {
    for (const [candidate] of this) {
      if (viewKeysEqual(candidate, key)) return true;
    }
    return false;
  }

  *[Symbol.iterator](): Iterator<readonly [Key, Value]> {
    const reader = new BebopViewReader(
      this.body,
      this.context.options,
      this.context.depth,
    );
    for (let index = 0; index < this.size; index++) {
      yield [this.decodeKey(reader), this.decodeValue(reader)] as const;
    }
  }

  toMap(): Map<Key, Value> {
    const values = new Map<Key, Value>();
    let stringKeys: Set<string> | undefined;
    for (const [key, value] of this) {
      if (key instanceof BebopStringView) {
        const keys = (stringKeys ??= new Set());
        if (keys.has(key.string)) throw new BebopRuntimeError("map contains a duplicate key");
        keys.add(key.string);
      } else if (values.has(key)) {
        throw new BebopRuntimeError("map contains a duplicate key");
      }
      values.set(key, value);
    }
    return values;
  }
}

function viewKeysEqual(left: unknown, right: unknown): boolean {
  if (Object.is(left, right) || left === right) return true;
  if (left instanceof BebopStringView) {
    return typeof right === "string" || right instanceof BebopStringView
      ? left.equals(right)
      : false;
  }
  return right instanceof BebopStringView && typeof left === "string" && right.equals(left);
}

export function readMessageField<Value>(
  message: BebopMessageView,
  tag: number,
  decode: ViewDecoder<Value>,
): Value | undefined {
  const field = message.field(tag);
  if (field === undefined) return undefined;
  const reader = new BebopViewReader(field, message.readerOptions, message.readerDepth);
  const value = decode(reader);
  reader.finish();
  return value;
}

function checkedByteCount(count: number, elementSize: number): number {
  if (!Number.isSafeInteger(count) || count < 0 || !Number.isSafeInteger(elementSize) || elementSize < 0) {
    throw new BebopRuntimeError("invalid array dimensions");
  }
  const byteCount = count * elementSize;
  if (!Number.isSafeInteger(byteCount)) throw new BebopRuntimeError("array byte length overflow");
  return byteCount;
}

function resolveViewOptions(options: BebopReaderOptions): ResolvedBebopReaderOptions {
  const resolved = resolveBebopReaderOptions(options);
  return resolved.copyArrays
    ? Object.freeze({ ...resolved, copyArrays: false })
    : resolved;
}
