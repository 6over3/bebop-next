import { describe, expect, test } from "vitest";
import {
  BebopReader,
  BebopMessageView,
  BebopRuntimeError,
  BebopDuration,
  BebopTimestamp,
  BebopAny,
  BebopEmpty,
  BebopUUID,
  BebopViewReader,
  BebopWriter,
  BFloat16,
  BFloat16Array,
  decode,
  encode,
  encodeInto,
  messageEncodedSize,
  utf8ByteLength,
} from "../src/index.js";

describe("Bebop wire primitives", () => {
  test("computes UTF-8 sizes without allocating an encoded buffer", () => {
    const encoder = new TextEncoder();
    for (const value of ["", "ascii", "café", "東京", "😀", "\ud800", "\udc00", "a😀東京z"]) {
      expect(utf8ByteLength(value)).toBe(encoder.encode(value).byteLength);
    }
  });

  test("round-trips integer and floating primitives", () => {
    const writer = new BebopWriter();
    writer.writeBool(true);
    writer.writeByte(255);
    writer.writeInt8(-12);
    writer.writeUint16(0xabcd);
    writer.writeInt16(-1234);
    writer.writeUint32(0xfedc_ba98);
    writer.writeInt32(-123_456);
    writer.writeUint64(0xffff_ffff_ffff_ffffn);
    writer.writeInt64(-9223372036854775808n);
    writer.writeUint128(0x1122_3344_5566_7788_99aa_bbcc_ddee_ff00n);
    writer.writeInt128(-1n);
    writer.writeFloat16(1.5);
    writer.writeFloat32(3.5);
    writer.writeFloat64(Math.PI);
    writer.writeBFloat16(BFloat16.fromNumber(-2.25));
    const temporalAvailable = typeof Temporal !== "undefined";
    const timestamp = temporalAvailable
      ? BebopTimestamp.fromWire(-1n, -500_000_000, 540_000)
      : undefined;
    const duration = temporalAvailable ? BebopDuration.fromWire(12n, 345) : undefined;
    if (timestamp !== undefined && duration !== undefined) {
      writer.writeTimestamp(timestamp);
      writer.writeDuration(duration);
    }

    const reader = new BebopReader(writer.toArray());
    expect(reader.readBool()).toBe(true);
    expect(reader.readByte()).toBe(255);
    expect(reader.readInt8()).toBe(-12);
    expect(reader.readUint16()).toBe(0xabcd);
    expect(reader.readInt16()).toBe(-1234);
    expect(reader.readUint32()).toBe(0xfedc_ba98);
    expect(reader.readInt32()).toBe(-123_456);
    expect(reader.readUint64()).toBe(0xffff_ffff_ffff_ffffn);
    expect(reader.readInt64()).toBe(-9223372036854775808n);
    expect(reader.readUint128()).toBe(0x1122_3344_5566_7788_99aa_bbcc_ddee_ff00n);
    expect(reader.readInt128()).toBe(-1n);
    expect(reader.readFloat16()).toBe(1.5);
    expect(reader.readFloat32()).toBe(3.5);
    expect(reader.readFloat64()).toBe(Math.PI);
    const bfloat16 = reader.readBFloat16();
    expect(BFloat16.toBitPattern(bfloat16)).toBe(0xc010);
    expect(BFloat16.toNumber(bfloat16)).toBe(-2.25);
    if (timestamp !== undefined) {
      const decodedTimestamp = reader.readTimestamp();
      expect(decodedTimestamp.epochNanoseconds).toBe(timestamp.epochNanoseconds);
      expect(decodedTimestamp.offsetNanoseconds).toBe(540_000_000_000);
      expect(BebopTimestamp.toWire(decodedTimestamp)).toEqual({
        seconds: -1n,
        nanoseconds: -500_000_000,
        offsetMs: 540_000,
      });
      expect(BebopDuration.toWire(reader.readDuration())).toEqual({ seconds: 12n, nanoseconds: 345 });
      expect(BebopDuration.toWire(Temporal.Duration.from({ milliseconds: 2_500 })))
        .toEqual({ seconds: 2n, nanoseconds: 500_000_000 });
      expect(() => BebopTimestamp.fromWire(1n, -1, 0)).toThrow("must have the same sign");
      expect(() => BebopTimestamp.fromWire(0n, 1_000_000_000, 0))
        .toThrow("invalid Bebop timestamp nanoseconds");
      expect(() => BebopDuration.fromWire(1n, -1)).toThrow("must have the same sign");
      expect(() => BebopDuration.fromWire(0n, 1_000_000_000)).toThrow("invalid Bebop duration nanoseconds");
    }
  });

  test("rejects integer values that JavaScript DataView would silently truncate", () => {
    const writer = new BebopWriter();
    expect(() => writer.writeByte(-1)).toThrow("byte value is out of range");
    expect(() => writer.writeInt32(0x8000_0000)).toThrow("int32 value is out of range");
    expect(() => writer.writeUint32(1.5)).toThrow("uint32 value is out of range");
    expect(() => writer.writeUint64(-1n)).toThrow("uint64 value is out of range");
    expect(() => writer.writeInt64(0x8000_0000_0000_0000n)).toThrow("int64 value is out of range");
  });

  test("round-trips typed scalar arrays", () => {
    const writer = new BebopWriter();
    const bytes = new Uint8Array([1, 2, 3, 255]);
    const ints = new Int32Array([-1, 0, 42]);
    const bigs = new BigInt64Array([-1n, 2n]);
    const float16s = new Float16Array([1.5, -2.25]);
    const float32s = new Float32Array([Math.PI, -0]);
    const bfloat16s = BFloat16Array.of(1.5, -2.25);

    writer.writeUint8Array(bytes);
    writer.writeInt32Array(ints);
    writer.writeBigInt64Array(bigs);
    writer.writeFloat16Array(float16s);
    writer.writeFloat32Array(float32s);
    writer.writeBFloat16Array(bfloat16s);

    const reader = new BebopReader(writer.toArray());
    expect([...reader.readUint8Array()]).toEqual([...bytes]);
    expect([...reader.readInt32Array()]).toEqual([...ints]);
    expect([...reader.readBigInt64Array()]).toEqual([...bigs]);
    expect([...reader.readFloat16Array()]).toEqual([...float16s]);
    expect([...reader.readFloat32Array()]).toEqual([...float32s]);

    const decodedBFloat16s = reader.readBFloat16Array();
    expect([...decodedBFloat16s.toBitPatterns()]).toEqual([...bfloat16s.toBitPatterns()]);
    expect([...decodedBFloat16s.values()]).toEqual([1.5, -2.25]);
  });

  test("rejects duplicate map keys instead of silently overwriting values", () => {
    const writer = new BebopWriter();
    writer.writeUint32(2);
    writer.writeByte(7);
    writer.writeByte(1);
    writer.writeByte(7);
    writer.writeByte(2);

    const reader = new BebopReader(writer.toArray());
    expect(() => reader.readDynamicMap(
      (entry) => entry.readByte(),
      (entry) => entry.readByte(),
    )).toThrow("map contains a duplicate key");

    const viewReader = new BebopViewReader(writer.toArray());
    const view = viewReader.readMapView(
      (entry) => entry.readByte(),
      (entry) => entry.readByte(),
    );
    expect(() => view.toMap()).toThrow("map contains a duplicate key");

    const stringWriter = new BebopWriter();
    stringWriter.writeUint32(2);
    stringWriter.writeString("duplicate");
    stringWriter.writeByte(1);
    stringWriter.writeString("duplicate");
    stringWriter.writeByte(2);
    const stringReader = new BebopViewReader(stringWriter.toArray());
    const stringView = stringReader.readMapView(
      (entry) => entry.readStringView(),
      (entry) => entry.readByte(),
    );
    expect(stringView.get("duplicate")).toBe(1);
    expect(() => stringView.toMap()).toThrow("map contains a duplicate key");
  });

  test("applies production-safe collection limits to every array path", () => {
    const writer = new BebopWriter();
    writer.writeUint32(1_000_001);
    const encoded = writer.toArray();

    expect(() => new BebopReader(encoded).readDynamicArray(() => ({})))
      .toThrow("collection exceeds configured limit");
    expect(() => new BebopReader(encoded).readUint8Array())
      .toThrow("collection exceeds configured limit");
    expect(() => new BebopReader(new Uint8Array()).readFixedArray(1_000_001, () => ({})))
      .toThrow("collection exceeds configured limit");
  });

  test("round-trips fixed typed scalar arrays without length prefixes", () => {
    const writer = new BebopWriter();
    const bytes = new Uint8Array([1, 2, 3, 255]);
    const ints = new Int32Array([-1, 0, 42]);
    const float32s = new Float32Array([Math.PI, -0]);
    const bfloat16s = BFloat16Array.of(1.5, -2.25);

    writer.writeUint8Array(bytes, 4);
    writer.writeInt32Array(ints, 3);
    writer.writeFloat32Array(float32s, 2);
    writer.writeBFloat16Array(bfloat16s, 2);

    const encoded = writer.toArray();
    expect([...encoded.slice(0, 4)]).toEqual([...bytes]);

    const reader = new BebopReader(encoded);
    expect([...reader.readUint8Array(4)]).toEqual([...bytes]);
    expect([...reader.readInt32Array(3)]).toEqual([...ints]);
    expect([...reader.readFloat32Array(2)]).toEqual([...float32s]);

    const decodedBFloat16s = reader.readBFloat16Array(2);
    expect([...decodedBFloat16s.toBitPatterns()]).toEqual([...bfloat16s.toBitPatterns()]);
    expect(reader.index).toBe(encoded.length);

    expect(() => new BebopWriter().writeInt32Array(ints, 4)).toThrow(BebopRuntimeError);
    expect(() => new BebopWriter().writeUint8Array(bytes, 3)).toThrow(BebopRuntimeError);
  });

  test("copies decoded arrays by default and aliases only with explicit opt-in", () => {
    const byteWriter = new BebopWriter();
    byteWriter.writeUint8Array(new Uint8Array([10, 20, 30]));
    const byteInput = byteWriter.toArray();
    const copiedBytes = new BebopReader(byteInput).readUint8Array();
    const viewedBytes = new BebopReader(byteInput, { copyArrays: false }).readUint8Array();

    byteInput[4] = 99;
    expect([...copiedBytes]).toEqual([10, 20, 30]);
    expect([...viewedBytes]).toEqual([99, 20, 30]);

    const intWriter = new BebopWriter();
    intWriter.writeInt32Array(new Int32Array([1, 2]));
    const intInput = intWriter.toArray();
    const copiedInts = new BebopReader(intInput).readInt32Array();
    const viewedInts = new BebopReader(intInput, { copyArrays: false }).readInt32Array();

    new DataView(intInput.buffer, intInput.byteOffset, intInput.byteLength).setInt32(4, 42, true);
    expect([...copiedInts]).toEqual([1, 2]);
    expect([...viewedInts]).toEqual([42, 2]);
  });

  test("copies Node Buffer inputs and handles unaligned Buffer slabs", () => {
    const writer = new BebopWriter();
    writer.writeUint8Array(new Uint8Array([10, 20, 30]));
    writer.writeUint32Array(new Uint32Array([0x1122_3344, 0xaabb_ccdd]));
    const encoded = writer.toArrayView();
    const slab = Buffer.allocUnsafe(encoded.byteLength + 7);
    const input = slab.subarray(7);
    input.set(encoded);

    const reader = new BebopReader(input);
    const bytes = reader.readUint8Array();
    const integers = reader.readUint32Array();
    input.fill(0);

    expect(bytes).toBeInstanceOf(Uint8Array);
    expect(Buffer.isBuffer(bytes)).toBe(false);
    expect([...bytes]).toEqual([10, 20, 30]);
    expect([...integers]).toEqual([0x1122_3344, 0xaabb_ccdd]);
  });

  test("reusing a writer invalidates unsafe views but not copied arrays", () => {
    const writer = new BebopWriter();
    writer.writeByte(1);
    const copy = writer.toArray();
    const view = writer.toArrayView();

    writer.reset();
    writer.writeByte(2);

    expect([...copy]).toEqual([1]);
    expect([...view]).toEqual([2]);
  });

  test("encodes into caller-owned writers", () => {
    const writer = new BebopWriter();
    const uuid = "00112233-4455-6677-8899-aabbccddeeff";

    expect(encodeInto(BebopUUID, uuid, writer)).toBe(16);
    expect(decode(BebopUUID, writer.toArray())).toBe(uuid);
  });

  test("round-trips dynamic maps without tuple entry callbacks", () => {
    const values = new Map<string, number>([
      ["one", 1],
      ["two", 2],
    ]);
    const writer = new BebopWriter();
    writer.writeDynamicMap(values, (mapWriter, key, value) => {
      mapWriter.writeString(key);
      mapWriter.writeInt32(value);
    });

    const decoded = new BebopReader(writer.toArray()).readDynamicMap(
      (mapReader) => mapReader.readString(),
      (mapReader) => mapReader.readInt32(),
    );

    expect([...decoded]).toEqual([...values]);
  });

  test("enforces configured buffered collection limits", () => {
    const writer = new BebopWriter();
    writer.writeUint32(2);
    writer.writeByte(1);
    writer.writeByte(2);

    expect(() => new BebopReader(writer.toArrayView(), { maxCollectionLength: 1 })
      .readDynamicArray((reader) => reader.readByte()))
      .toThrow("collection exceeds configured limit");
  });

  test("writes and randomly accesses every indexed-message directory shape", () => {
    const tagSets = [
      [],
      [5],
      [1, 8],
      [1, 9, 16],
      [1, 16, 32],
      [1, 33, 97, 255],
    ] as const;

    for (const tags of tagSets) {
      const writer = new BebopWriter();
      const message = writer.beginMessage();
      for (const tag of tags) {
        writer.markMessageField(message, tag);
        writer.writeUint32(tag * 10);
      }
      writer.endMessage(message);
      const encoded = writer.toArray();
      const blockMask = tags.reduce((mask, tag) => mask | (1 << ((tag - 1) >>> 5)), 0);

      expect(encoded.length).toBe(messageEncodedSize(
        tags.length * 4,
        tags.length,
        tags.at(-1) ?? 0,
        blockMask,
      ));
      const view = new BebopMessageView(encoded);
      for (const tag of tags) {
        expect(view.read(tag, (reader) => reader.readUint32())).toBe(tag * 10);
      }
      expect(view.field(254)).toBeUndefined();
    }
  });

  test("nests indexed messages without corrupting writer metadata", () => {
    const writer = new BebopWriter();
    const outer = writer.beginMessage();
    writer.markMessageField(outer, 1);
    const inner = writer.beginMessage();
    writer.markMessageField(inner, 200);
    writer.writeString("nested");
    writer.endMessage(inner);
    writer.markMessageField(outer, 9);
    writer.writeInt32(42);
    writer.endMessage(outer);

    const outerView = new BebopMessageView(writer.toArray());
    const innerBytes = outerView.field(1);
    expect(innerBytes).toBeDefined();
    const innerView = new BebopMessageView(innerBytes!);
    expect(innerView.read(200, (reader) => reader.readString())).toBe("nested");
    expect(outerView.read(9, (reader) => reader.readInt32())).toBe(42);
  });

  test("rejects malformed indexed-message metadata", () => {
    const writer = new BebopWriter();
    const message = writer.beginMessage();
    writer.markMessageField(message, 1);
    writer.writeByte(7);
    writer.endMessage(message);
    const encoded = writer.toArray();
    encoded[encoded.length - 1] = 0xff;

    expect(() => new BebopMessageView(encoded)).toThrow("malformed indexed message");
  });

  test("writes vnext strings with a NUL terminator", () => {
    const writer = new BebopWriter();
    writer.writeString("bebop");

    expect([...writer.toArray()]).toEqual([5, 0, 0, 0, 98, 101, 98, 111, 112, 0]);
    expect(new BebopReader(writer.toArray()).readString()).toBe("bebop");
  });

  test("round-trips short and long UTF-8 strings", () => {
    const short = "hello, 世界";
    const long = "swordfish ".repeat(80);
    const writer = new BebopWriter();
    writer.writeString(short);
    writer.writeString(long);

    const reader = new BebopReader(writer.toArray());
    expect(reader.readString()).toBe(short);
    expect(reader.readString()).toBe(long);
  });

  test("writes strings into exact caller-owned buffers", () => {
    for (const value of ["", "plain ascii", "café 東京 😀", "\ud800"]) {
      const writer = new BebopWriter(new Uint8Array(utf8ByteLength(value) + 5));
      writer.writeString(value);
      expect(new BebopReader(writer.toArrayView()).readString()).toBe(
        new TextDecoder().decode(new TextEncoder().encode(value)),
      );
    }
  });

  test("does not corrupt a fixed writer after a capacity error", () => {
    const writer = new BebopWriter(new Uint8Array(4));
    writer.writeUint32(7);
    expect(() => writer.writeByte(1)).toThrow("supplied buffer is too small");
    expect(writer.length).toBe(4);
    expect([...writer.toArrayView()]).toEqual([7, 0, 0, 0]);
  });

  test("rejects strings missing their NUL terminator", () => {
    const bytes = new Uint8Array([1, 0, 0, 0, 65, 1]);
    expect(() => new BebopReader(bytes).readString()).toThrow(BebopRuntimeError);
  });

  test("rejects malformed UTF-8 strings", () => {
    const continuationWithoutLead = new Uint8Array([1, 0, 0, 0, 0x80, 0]);
    const truncatedSequence = new Uint8Array([2, 0, 0, 0, 0xe2, 0x82, 0]);

    expect(() => new BebopReader(continuationWithoutLead).readString()).toThrow(BebopRuntimeError);
    expect(() => new BebopReader(truncatedSequence).readString()).toThrow(BebopRuntimeError);
  });

  test("preserves native decoder errors as BebopRuntimeError causes", () => {
    const invalidLongUtf8 = new Uint8Array(305);
    new DataView(invalidLongUtf8.buffer).setUint32(0, 300, true);
    invalidLongUtf8.fill(0x80, 4, 304);
    const readInvalidString = (): string => new BebopReader(invalidLongUtf8).readString();

    expect(readInvalidString).toThrow(BebopRuntimeError);
    expect(readInvalidString).toThrow("malformed UTF-8 string");
    expect(readInvalidString).toThrow(
      expect.objectContaining({
        cause: expect.any(TypeError),
      }),
    );
  });
});

describe("BFloat16Array", () => {
  test("preserves bit patterns and exposes numeric iteration", () => {
    const values = BFloat16Array.of(1.5, BFloat16.fromBitPattern(0x7fc1));

    expect(Object.prototype.toString.call(values)).toBe("[object BFloat16Array]");
    expect([...values].map(BFloat16.toBitPattern)).toEqual([0x3fc0, 0x7fc1]);
    expect([...values.values()]).toEqual([1.5, Number.NaN]);
  });

  test("matches native constructor copy and ArrayBuffer view behavior", () => {
    const buffer = new ArrayBuffer(4);
    const bits = new Uint16Array(buffer);
    bits.set([0x3f80, 0x4000]);

    const view = new BFloat16Array(buffer);
    bits[0] = 0x4040;
    expect(view.getBitPattern(0)).toBe(0x4040);

    const copiedAndConverted = new BFloat16Array(bits);
    bits[0] = 0x3f80;
    expect(copiedAndConverted.getBitPattern(0)).toBe(BFloat16.toBitPattern(BFloat16.fromNumber(0x4040)));
  });

  test("copies bit patterns by default but supports explicit live bit-pattern views", () => {
    const bits = new Uint16Array([0x3f80, 0x4000]);
    const copied = BFloat16Array.from(bits, { as: "bit-patterns" });
    const live = BFloat16Array.from(bits, { as: "bit-patterns", copy: false });

    bits[0] = 0x4040;
    expect(copied.getBitPattern(0)).toBe(0x3f80);
    expect(live.getBitPattern(0)).toBe(0x4040);

    const liveBits = live.toBitPatterns({ copy: false });
    liveBits[1] = 0xbf80;
    expect(live.getBitPattern(1)).toBe(0xbf80);
    expect([...live.values()]).toEqual([3, -1]);
  });
});

describe("BebopUUID codec", () => {
  test("uses RFC 4122 byte order", () => {
    const uuid = "00112233-4455-6677-8899-aabbccddeeff";
    const bytes = encode(BebopUUID, uuid);

    expect([...bytes]).toEqual([
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    ]);
    expect(decode(BebopUUID, bytes)).toBe(uuid);
  });

  test("accepts uppercase UUID input and normalizes reads to lowercase", () => {
    const uuid = "00112233-4455-6677-8899-AABBCCDDEEFF";
    expect(decode(BebopUUID, encode(BebopUUID, uuid))).toBe(
      "00112233-4455-6677-8899-aabbccddeeff",
    );
  });

  test("rejects invalid UUID text", () => {
    expect(() => encode(BebopUUID, "00112233-4455-6677-8899-aabbccddeezz")).toThrow(
      BebopRuntimeError,
    );
    expect(() => encode(BebopUUID, "00112233445566778899aabbccddeeff")).toThrow(
      BebopRuntimeError,
    );
  });
});

describe("Bebop runtime records", () => {
  test("round-trips BebopEmpty as zero bytes", () => {
    const encoded = encode(BebopEmpty, {});

    expect([...encoded]).toEqual([]);
    expect(decode(BebopEmpty, encoded)).toEqual({});
  });

  test("packs and unpacks BebopAny", () => {
    const uuid = "00112233-4455-6677-8899-aabbccddeeff";
    const packed = BebopAny.pack(uuid, BebopUUID);

    expect(packed.typeUrl).toBe("type.bebop.sh/bebop.UUID");
    expect(BebopAny.typeName(packed)).toBe("bebop.UUID");
    expect(BebopAny.is(packed, BebopUUID)).toBe(true);
    expect(BebopAny.unpack(packed, BebopUUID)).toBe(uuid);
    expect(decode(BebopAny, encode(BebopAny, packed))).toEqual(packed);
  });
});
