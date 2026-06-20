import { decode, encode } from "./codec";
import { BebopRuntimeError } from "./error";
import type { BebopReflectableCodec } from "./reflection";
import { BebopDefinitionKind } from "./reflection";
import type { BebopReader, BebopWriter } from "./wire";

export const BEBOP_TYPE_URL_PREFIX = "type.bebop.sh/";

export type BebopAny = {
  readonly typeUrl: string;
  readonly value: Uint8Array;
};

function stringSize(value: string): number {
  return 4 + new TextEncoder().encode(value).length + 1;
}

export const BebopAny = {
  typeUrlPrefix: BEBOP_TYPE_URL_PREFIX,

  readFrom(reader: BebopReader): BebopAny {
    return {
      typeUrl: reader.readString(),
      value: reader.readUint8Array(),
    };
  },

  writeInto(writer: BebopWriter, value: BebopAny): void {
    writer.writeString(value.typeUrl);
    writer.writeUint8Array(value.value);
  },

  encodedSize(value: BebopAny): number {
    return stringSize(value.typeUrl) + 4 + value.value.length;
  },

  pack<T>(
    value: T,
    codec: BebopReflectableCodec<T>,
    prefix = BEBOP_TYPE_URL_PREFIX,
  ): BebopAny {
    return {
      typeUrl: prefix + codec.reflection.fqn,
      value: encode(codec, value),
    };
  },

  unpack<T>(value: BebopAny, codec: BebopReflectableCodec<T>): T {
    if (!this.is(value, codec)) {
      throw new BebopRuntimeError(
        `BebopAny type mismatch: expected ${codec.reflection.fqn}, got ${this.typeName(value) ?? value.typeUrl}`,
      );
    }
    return decode(codec, value.value);
  },

  is<T>(value: BebopAny, codec: BebopReflectableCodec<T>): boolean {
    return this.typeName(value) === codec.reflection.fqn;
  },

  typeName(value: BebopAny): string | undefined {
    const slash = value.typeUrl.lastIndexOf("/");
    return slash < 0 ? undefined : value.typeUrl.slice(slash + 1);
  },

  reflection: {
    name: "BebopAny",
    fqn: "bebop.Any",
    kind: BebopDefinitionKind.struct,
    detail: {
      fields: [
        { name: "type_url", index: 0, typeName: "string" },
        { name: "value", index: 0, typeName: "byte[]" },
      ],
    },
  },
} satisfies BebopReflectableCodec<BebopAny> & {
  readonly typeUrlPrefix: string;
  pack<T>(value: T, codec: BebopReflectableCodec<T>, prefix?: string): BebopAny;
  unpack<T>(value: BebopAny, codec: BebopReflectableCodec<T>): T;
  is<T>(value: BebopAny, codec: BebopReflectableCodec<T>): boolean;
  typeName(value: BebopAny): string | undefined;
};
