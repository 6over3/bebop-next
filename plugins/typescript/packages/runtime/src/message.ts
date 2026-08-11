import { BebopRuntimeError } from "./error.js";
import { BebopReader, type BebopReaderOptions } from "./wire.js";

const MessageDirectory = {
  empty: 0,
  tiny1: 1,
  tiny2: 2,
  tiny3: 3,
  mask8: 4,
  mask16: 5,
  mask32: 6,
  blocks: 7,
} as const;

type ParsedMessageIndex = {
  readonly payloadEnd: number;
  readonly boundariesOffset: number;
  readonly directoryOffset: number;
  readonly fieldCount: number;
  readonly offsetWidth: 1 | 2 | 4;
  readonly directoryKind: number;
};

type MessageViewContext = {
  readonly options: BebopReaderOptions;
  readonly depth: number;
};

type MessageDirectoryLayout = {
  readonly kind: number;
  readonly size: number;
  readonly blockMask: number;
};

function malformedMessage(): never {
  throw new BebopRuntimeError("malformed indexed message");
}

function loadLittleEndian(bytes: Uint8Array, offset: number, width: number): number {
  switch (width) {
    case 1: return bytes[offset]!;
    case 2: return bytes[offset]! | (bytes[offset + 1]! << 8);
    case 4: return (
      bytes[offset]!
      | (bytes[offset + 1]! << 8)
      | (bytes[offset + 2]! << 16)
      | (bytes[offset + 3]! << 24)
    ) >>> 0;
    default: return malformedMessage();
  }
}

/** Population count for one unsigned 32-bit word. */
export function popcount32(value: number): number {
  value >>>= 0;
  value -= (value >>> 1) & 0x5555_5555;
  value = (value & 0x3333_3333) + ((value >>> 2) & 0x3333_3333);
  return (((value + (value >>> 4)) & 0x0f0f_0f0f) * 0x0101_0101) >>> 24;
}

function parseMessageIndex(encoded: Uint8Array): ParsedMessageIndex {
  if (encoded.length < 5) malformedMessage();
  const bodyLength = loadLittleEndian(encoded, 0, 4);
  if (bodyLength !== encoded.length - 4 || bodyLength === 0) malformedMessage();

  const control = encoded[encoded.length - 1]!;
  const widthCode = control & 3;
  const directoryKind = (control >>> 2) & 7;
  if ((control & 0xe0) !== 0 || widthCode === 3) malformedMessage();
  const offsetWidth = (1 << widthCode) as 1 | 2 | 4;

  let directorySize: number;
  let fieldCount = 0;
  switch (directoryKind) {
    case MessageDirectory.empty:
      directorySize = 0;
      break;
    case MessageDirectory.tiny1:
    case MessageDirectory.tiny2:
    case MessageDirectory.tiny3:
      directorySize = directoryKind;
      fieldCount = directoryKind;
      break;
    case MessageDirectory.mask8:
      directorySize = 1;
      break;
    case MessageDirectory.mask16:
      directorySize = 2;
      break;
    case MessageDirectory.mask32:
      directorySize = 4;
      break;
    case MessageDirectory.blocks: {
      if (bodyLength < 2) malformedMessage();
      const blockMask = encoded[encoded.length - 2]!;
      directorySize = 1 + 5 * popcount32(blockMask);
      break;
    }
    default:
      return malformedMessage();
  }
  if (directorySize + 1 > bodyLength) malformedMessage();

  const directoryOffset = encoded.length - 1 - directorySize;
  switch (directoryKind) {
    case MessageDirectory.mask8:
    case MessageDirectory.mask16:
    case MessageDirectory.mask32:
      fieldCount = popcount32(loadLittleEndian(encoded, directoryOffset, directorySize));
      break;
    case MessageDirectory.blocks: {
      const blockMask = encoded[directoryOffset + directorySize - 1]!;
      if (blockMask === 0) malformedMessage();
      let entry = directoryOffset;
      let rank = 0;
      for (let block = 0; block < 8; block++) {
        if ((blockMask & (1 << block)) === 0) continue;
        if (encoded[entry] !== rank) malformedMessage();
        const mask = loadLittleEndian(encoded, entry + 1, 4);
        if (mask === 0 || (block === 7 && (mask & 0x8000_0000) !== 0)) malformedMessage();
        rank += popcount32(mask);
        entry += 5;
      }
      fieldCount = rank;
      break;
    }
  }

  if ((fieldCount === 0) !== (directoryKind === MessageDirectory.empty)) malformedMessage();
  if (directoryKind >= MessageDirectory.tiny1 && directoryKind <= MessageDirectory.tiny3) {
    if (encoded[directoryOffset] === 0) malformedMessage();
    for (let index = 1; index < fieldCount; index++) {
      if (encoded[directoryOffset + index]! <= encoded[directoryOffset + index - 1]!) {
        malformedMessage();
      }
    }
  }

  const boundaryCount = Math.max(0, fieldCount - 1);
  const boundarySize = boundaryCount * offsetWidth;
  if (boundarySize + directorySize + 1 > bodyLength) malformedMessage();
  const boundariesOffset = directoryOffset - boundarySize;
  const payloadEnd = boundariesOffset;
  let previous = 0;
  for (let rank = 0; rank < boundaryCount; rank++) {
    const boundary = loadLittleEndian(encoded, boundariesOffset + rank * offsetWidth, offsetWidth);
    if (boundary < previous || boundary > payloadEnd - 4) malformedMessage();
    previous = boundary;
  }
  if (fieldCount === 0 && payloadEnd !== 4) malformedMessage();
  return {
    payloadEnd,
    boundariesOffset,
    directoryOffset,
    fieldCount,
    offsetWidth,
    directoryKind,
  };
}

function fieldRank(tag: number, index: ParsedMessageIndex, encoded: Uint8Array): number | undefined {
  if (!Number.isInteger(tag) || tag < 1 || tag > 255 || index.fieldCount === 0) return undefined;
  switch (index.directoryKind) {
    case MessageDirectory.tiny1:
    case MessageDirectory.tiny2:
    case MessageDirectory.tiny3:
      for (let rank = 0; rank < index.fieldCount; rank++) {
        const candidate = encoded[index.directoryOffset + rank]!;
        if (candidate === tag) return rank;
        if (candidate > tag) return undefined;
      }
      return undefined;
    case MessageDirectory.mask8:
    case MessageDirectory.mask16:
    case MessageDirectory.mask32: {
      const maskWidth = 1 << (index.directoryKind - MessageDirectory.mask8);
      if (tag > maskWidth * 8) return undefined;
      const mask = loadLittleEndian(encoded, index.directoryOffset, maskWidth);
      const bit = (1 << (tag - 1)) >>> 0;
      return (mask & bit) === 0 ? undefined : popcount32(mask & (bit - 1));
    }
    case MessageDirectory.blocks: {
      const block = (tag - 1) >>> 5;
      const blockBit = 1 << block;
      const topMask = encoded[encoded.length - 2]!;
      if ((topMask & blockBit) === 0) return undefined;
      const preceding = popcount32(topMask & (blockBit - 1));
      const entry = index.directoryOffset + 5 * preceding;
      const mask = loadLittleEndian(encoded, entry + 1, 4);
      const bit = (1 << ((tag - 1) & 31)) >>> 0;
      return (mask & bit) === 0
        ? undefined
        : encoded[entry]! + popcount32(mask & (bit - 1));
    }
    default:
      return undefined;
  }
}

function fieldRange(rank: number, index: ParsedMessageIndex, encoded: Uint8Array): [number, number] {
  const start = rank === 0
    ? 0
    : loadLittleEndian(
        encoded,
        index.boundariesOffset + (rank - 1) * index.offsetWidth,
        index.offsetWidth,
      );
  const end = rank + 1 === index.fieldCount
    ? index.payloadEnd - 4
    : loadLittleEndian(
        encoded,
        index.boundariesOffset + rank * index.offsetWidth,
        index.offsetWidth,
      );
  return [4 + start, 4 + end];
}

/** A validated, immutable, random-access view over one indexed message. */
export class BebopMessageView {
  readonly encoded: Uint8Array;
  private readonly index: ParsedMessageIndex;
  private readonly context: MessageViewContext;

  constructor(encoded: Uint8Array, options: BebopReaderOptions = {}, depth = 0) {
    this.encoded = encoded;
    this.index = parseMessageIndex(encoded);
    this.context = { options, depth };
  }

  get readerOptions(): BebopReaderOptions {
    return this.context.options;
  }

  get readerDepth(): number {
    return this.context.depth;
  }

  field(tag: number): Uint8Array | undefined {
    const rank = fieldRank(tag, this.index, this.encoded);
    if (rank === undefined) return undefined;
    const [start, end] = fieldRange(rank, this.index, this.encoded);
    return this.encoded.subarray(start, end);
  }

  read<Value>(tag: number, decode: (reader: BebopReader) => Value): Value | undefined {
    const encoded = this.field(tag);
    if (encoded === undefined) return undefined;
    const reader = new BebopReader(encoded, this.context.options, this.context.depth);
    const value = decode(reader);
    if (reader.index !== reader.length) {
      throw new BebopRuntimeError(`field ${tag} contains trailing data`);
    }
    return value;
  }
}

export function messageDirectoryLayout(
  fieldCount: number,
  maxTag: number,
  blockMask: number,
): MessageDirectoryLayout {
  if (fieldCount === 0) return { kind: MessageDirectory.empty, size: 0, blockMask: 0 };
  let kind: number = MessageDirectory.blocks;
  let size = 1 + 5 * popcount32(blockMask);
  if (maxTag <= 8 && size >= 1) {
    kind = MessageDirectory.mask8;
    size = 1;
  } else if (maxTag <= 16 && size >= 2) {
    kind = MessageDirectory.mask16;
    size = 2;
  } else if (maxTag <= 32 && size >= 4) {
    kind = MessageDirectory.mask32;
    size = 4;
  }
  if (fieldCount <= 3 && fieldCount < size) {
    kind = fieldCount;
    size = fieldCount;
  }
  return { kind, size, blockMask };
}

/** Computes the complete indexed-message size without allocating tag arrays. */
export function messageEncodedSize(
  payloadSize: number,
  fieldCount: number,
  maxTag: number,
  blockMask: number,
): number {
  if (!Number.isSafeInteger(payloadSize) || payloadSize < 0 || payloadSize > 0xffff_ffff) {
    throw new RangeError(`message payload size is out of range: ${payloadSize}`);
  }
  if (!Number.isInteger(fieldCount) || fieldCount < 0 || fieldCount > 255) {
    throw new RangeError(`message field count is out of range: ${fieldCount}`);
  }
  if (!Number.isInteger(maxTag) || maxTag < 0 || maxTag > 255) {
    throw new RangeError(`message maximum tag is out of range: ${maxTag}`);
  }
  if (!Number.isInteger(blockMask) || blockMask < 0 || blockMask > 0xff) {
    throw new RangeError(`message block mask is out of range: ${blockMask}`);
  }
  if (fieldCount === 0) {
    if (maxTag !== 0 || blockMask !== 0) {
      throw new RangeError("empty message metadata must not contain tags");
    }
  } else {
    const maxTagBlock = 1 << ((maxTag - 1) >>> 5);
    if (maxTag === 0 || blockMask === 0 || (blockMask & maxTagBlock) === 0) {
      throw new RangeError("message metadata does not contain its maximum tag");
    }
  }
  const width = payloadSize <= 0xff ? 1 : payloadSize <= 0xffff ? 2 : 4;
  const directory = messageDirectoryLayout(fieldCount, maxTag, blockMask);
  const bodySize = payloadSize + Math.max(0, fieldCount - 1) * width + directory.size + 1;
  if (bodySize > 0xffff_ffff) {
    throw new RangeError(`message body size is out of range: ${bodySize}`);
  }
  return 4 + bodySize;
}
