import { BebopRuntimeError } from "./error";
import type { BebopReflectableCodec } from "./reflection";
import { BebopDefinitionKind } from "./reflection";
import type { BebopReader, BebopWriter } from "./wire";

export type BebopUUID = string;

const guidDelimiter = "-";
const hexDigits = "0123456789abcdef";
const byteToHex: string[] = [];
for (const x of hexDigits) {
  for (const y of hexDigits) {
    byteToHex.push(x + y);
  }
}

const asciiToHex = new Int8Array(128).fill(-1);
for (let i = 0; i < 10; i++) {
  asciiToHex[48 + i] = i;
}
for (let i = 0; i < 6; i++) {
  asciiToHex[65 + i] = 10 + i;
  asciiToHex[97 + i] = 10 + i;
}

const uuidByteOffsets = [0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34] as const;

function uuidByte(value: string, offset: number): number {
  const high = asciiToHex[value.charCodeAt(offset)] ?? -1;
  const low = asciiToHex[value.charCodeAt(offset + 1)] ?? -1;
  if (high < 0 || low < 0) {
    throw new BebopRuntimeError("UUID contains invalid hex");
  }
  return (high << 4) | low;
}

export const BebopUUID = {
  readFrom(reader: BebopReader): BebopUUID {
    const start = reader.index;
    reader.skip(16);
    const buffer = reader.buffer;

    let s = byteToHex[buffer[start]!]!;
    s += byteToHex[buffer[start + 1]!]!;
    s += byteToHex[buffer[start + 2]!]!;
    s += byteToHex[buffer[start + 3]!]!;
    s += guidDelimiter;
    s += byteToHex[buffer[start + 4]!]!;
    s += byteToHex[buffer[start + 5]!]!;
    s += guidDelimiter;
    s += byteToHex[buffer[start + 6]!]!;
    s += byteToHex[buffer[start + 7]!]!;
    s += guidDelimiter;
    s += byteToHex[buffer[start + 8]!]!;
    s += byteToHex[buffer[start + 9]!]!;
    s += guidDelimiter;
    s += byteToHex[buffer[start + 10]!]!;
    s += byteToHex[buffer[start + 11]!]!;
    s += byteToHex[buffer[start + 12]!]!;
    s += byteToHex[buffer[start + 13]!]!;
    s += byteToHex[buffer[start + 14]!]!;
    s += byteToHex[buffer[start + 15]!]!;
    return s;
  },

  writeInto(writer: BebopWriter, value: BebopUUID): void {
    if (value.length !== 36) {
      throw new BebopRuntimeError("UUID must use canonical 36-character form");
    }
    if (
      value.charCodeAt(8) !== 45 ||
      value.charCodeAt(13) !== 45 ||
      value.charCodeAt(18) !== 45 ||
      value.charCodeAt(23) !== 45
    ) {
      throw new BebopRuntimeError("UUID has invalid delimiter positions");
    }

    for (const offset of uuidByteOffsets) {
      writer.writeByte(uuidByte(value, offset));
    }
  },

  encodedSize(_value: BebopUUID): number {
    return 16;
  },

  reflection: {
    name: "BebopUUID",
    fqn: "bebop.UUID",
    kind: BebopDefinitionKind.struct,
    detail: { fields: [] },
  },
} satisfies BebopReflectableCodec<BebopUUID>;
