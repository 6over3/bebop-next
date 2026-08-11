import type { BebopCodec } from "./codec.js";
import { BebopRuntimeError } from "./error.js";
import type { BebopReader, BebopWriter } from "./wire.js";

export type BFloat16ArrayFromOptions =
  | { readonly as?: "values" }
  | { readonly as: "bit-patterns"; readonly copy?: boolean };

type BFloat16ArrayValueSource = Iterable<BFloat16 | number> | ArrayLike<BFloat16 | number>;
type BFloat16ArrayValueOptions = Extract<BFloat16ArrayFromOptions, { readonly as?: "values" }>;
type BFloat16ArrayBitPatternOptions = Extract<BFloat16ArrayFromOptions, { readonly as: "bit-patterns" }>;

const float32Scratch = new Float32Array(1);
const uint32Scratch = new Uint32Array(float32Scratch.buffer);
const F32_EXPONENT_MASK = 0x7f80_0000;
const F32_SIGNIFICAND_MASK = 0x007f_ffff;
const BF16_QUIET_NAN_BIT = 0x0040;
const F32_TO_BF16_ROUND_BIAS = 0x7fff;

function float32Bits(value: number): number {
  float32Scratch[0] = value;
  return uint32Scratch[0]!;
}

function float32FromBits(bits: number): number {
  uint32Scratch[0] = (bits << 16) >>> 0;
  return float32Scratch[0]!;
}

function bfloat16BitsFromNumber(value: number): number {
  const bits = float32Bits(value);
  if ((bits & F32_EXPONENT_MASK) === F32_EXPONENT_MASK && (bits & F32_SIGNIFICAND_MASK) !== 0) {
    // Preserve NaNs as NaNs. Without quieting, a payload only in the low
    // float32 bits could truncate to infinity.
    return ((bits >>> 16) | BF16_QUIET_NAN_BIT) & 0xffff;
  }

  // Round to nearest even. The retained low bit adjusts exact half-way cases.
  const lsb = (bits >>> 16) & 1;
  return (((bits + F32_TO_BF16_ROUND_BIAS + lsb) >>> 0) >>> 16) & 0xffff;
}

function bfloat16BitsFromValue(value: BFloat16 | number): number {
  return value instanceof BFloat16Value ? value.bitPattern : bfloat16BitsFromNumber(value);
}

declare const bfloat16Brand: unique symbol;

class BFloat16Value {
  declare private readonly [bfloat16Brand]: void;

  constructor(readonly bitPattern: number) {
    if (!Number.isInteger(bitPattern) || bitPattern < 0 || bitPattern > 0xffff) {
      throw new BebopRuntimeError("BFloat16 bit pattern must be a uint16");
    }
  }

  valueOf(): number {
    return float32FromBits(this.bitPattern);
  }

  toString(): string {
    return String(this.valueOf());
  }

  toJSON(): number {
    return this.valueOf();
  }
}

export type BFloat16 = BFloat16Value;

export const BFloat16 = {
  fromBitPattern(bitPattern: number): BFloat16 {
    return new BFloat16Value(bitPattern);
  },

  fromNumber(value: number): BFloat16 {
    return new BFloat16Value(bfloat16BitsFromNumber(value));
  },

  toBitPattern(value: BFloat16): number {
    return value.bitPattern;
  },

  toNumber(value: BFloat16): number {
    return value.valueOf();
  },

  is(value: unknown): value is BFloat16 {
    return value instanceof BFloat16Value;
  },

  readFrom(reader: BebopReader): BFloat16 {
    return new BFloat16Value(reader.readUint16());
  },

  writeInto(writer: BebopWriter, value: BFloat16): void {
    writer.writeUint16(value.bitPattern);
  },

  encodedSize(_value: BFloat16): number {
    return 2;
  },
} satisfies BebopCodec<BFloat16> & {
  fromBitPattern(bitPattern: number): BFloat16;
  fromNumber(value: number): BFloat16;
  toBitPattern(value: BFloat16): number;
  toNumber(value: BFloat16): number;
  is(value: unknown): value is BFloat16;
};

export class BFloat16Array implements Iterable<BFloat16> {
  private readonly bits: Uint16Array;

  constructor(length: number);
  /**
   * Creates a BFloat16Array by copying and converting numeric values.
   */
  constructor(values: BFloat16ArrayValueSource);
  /**
   * Creates a BFloat16Array view over an ArrayBuffer.
   *
   * Like native typed arrays, this is a live view over the buffer.
   */
  constructor(buffer: ArrayBufferLike, byteOffset?: number, length?: number);
  constructor(
    lengthOrSource: number | BFloat16ArrayValueSource | ArrayBufferLike,
    byteOffset?: number,
    length?: number,
  ) {
    if (typeof lengthOrSource === "number") {
      if (!Number.isInteger(lengthOrSource) || lengthOrSource < 0) {
        throw new BebopRuntimeError("BFloat16Array length must be a non-negative integer");
      }
      this.bits = new Uint16Array(lengthOrSource);
    } else if (isArrayBufferLike(lengthOrSource)) {
      this.bits =
        length === undefined
          ? new Uint16Array(lengthOrSource, byteOffset)
          : new Uint16Array(lengthOrSource, byteOffset, length);
    } else if (length === undefined) {
      this.bits = BFloat16Array.from(lengthOrSource).bits;
    } else {
      throw new BebopRuntimeError("byteOffset and length are only valid with an ArrayBuffer source");
    }
  }

  /**
   * Creates a BFloat16Array from values, or from raw bit patterns when
   * `{ as: "bit-patterns" }` is provided.
   *
   * @remarks The default value path copies and converts inputs. The
   * bit-pattern path copies unless `{ copy: false }` is provided; that mode is
   * a live view and keeps the source ArrayBuffer alive.
   */
  static from(values: BFloat16ArrayValueSource, options?: BFloat16ArrayValueOptions): BFloat16Array;
  static from(bitPatterns: Uint16Array, options: BFloat16ArrayBitPatternOptions): BFloat16Array;
  static from(
    source: BFloat16ArrayValueSource | Uint16Array,
    options: BFloat16ArrayFromOptions = {},
  ): BFloat16Array {
    if (options.as === "bit-patterns") {
      if (!(source instanceof Uint16Array)) {
        throw new BebopRuntimeError('BFloat16Array.from(..., { as: "bit-patterns" }) requires Uint16Array');
      }
      return BFloat16Array.createFromBitPatternStorage(
        options.copy === false ? source : source.slice(),
      );
    }

    if (isArrayLike(source)) {
      const result = new BFloat16Array(source.length);
      for (let i = 0; i < source.length; i++) {
        const value = source[i];
        if (value === undefined) {
          throw new BebopRuntimeError(`missing BFloat16Array value at index ${i}`);
        }
        result.bits[i] = bfloat16BitsFromValue(value);
      }
      return result;
    }

    const bitPatterns: number[] = [];
    for (const value of source) {
      bitPatterns.push(bfloat16BitsFromValue(value));
    }
    const result = new BFloat16Array(bitPatterns.length);
    for (let i = 0; i < bitPatterns.length; i++) {
      result.bits[i] = bitPatterns[i]!;
    }
    return result;
  }

  static of(...values: (BFloat16 | number)[]): BFloat16Array {
    return BFloat16Array.from(values);
  }

  get length(): number {
    return this.bits.length;
  }

  get byteLength(): number {
    return this.bits.byteLength;
  }

  get [Symbol.toStringTag](): string {
    return "BFloat16Array";
  }

  getBitPattern(index: number): number {
    this.requireIndex(index);
    return this.bits[index]!;
  }

  /**
   * Returns a BFloat16 wrapper for one element.
   *
   * @remarks This allocates. Use {@link getBitPattern} or {@link values} in
   * hot loops.
   */
  get(index: number): BFloat16 {
    return new BFloat16Value(this.getBitPattern(index));
  }

  set(index: number, value: BFloat16 | number): void {
    this.requireIndex(index);
    this.bits[index] = bfloat16BitsFromValue(value);
  }

  /**
   * Returns raw bfloat16 bit patterns.
   *
   * By default this returns a copy. Pass `{ copy: false }` for an intentional
   * zero-copy live view of the array's internal storage.
   */
  toBitPatterns(options?: { readonly copy?: boolean }): Uint16Array {
    return options?.copy === false ? this.bits : this.bits.slice();
  }

  *values(): IterableIterator<number> {
    for (let i = 0; i < this.bits.length; i++) {
      yield float32FromBits(this.bits[i]!);
    }
  }

  toFloat32Array(): Float32Array {
    const values = new Float32Array(this.bits.length);
    for (let i = 0; i < this.bits.length; i++) {
      values[i] = float32FromBits(this.bits[i]!);
    }
    return values;
  }

  *[Symbol.iterator](): IterableIterator<BFloat16> {
    for (let i = 0; i < this.length; i++) {
      yield this.get(i);
    }
  }

  private requireIndex(index: number): void {
    if (!Number.isInteger(index) || index < 0 || index >= this.bits.length) {
      throw new BebopRuntimeError(`BFloat16Array index out of bounds: ${index}`);
    }
  }

  private static createFromBitPatternStorage(bitPatterns: Uint16Array): BFloat16Array {
    const values = new BFloat16Array(0);
    Object.defineProperty(values, "bits", {
      configurable: false,
      enumerable: false,
      value: bitPatterns,
      writable: false,
    });
    return values;
  }
}

function isArrayLike(value: unknown): value is ArrayLike<BFloat16 | number> {
  return typeof value === "object" && value !== null && "length" in value && typeof value.length === "number";
}

function isArrayBufferLike(value: unknown): value is ArrayBufferLike {
  return (
    value instanceof ArrayBuffer ||
    (typeof SharedArrayBuffer !== "undefined" && value instanceof SharedArrayBuffer)
  );
}
