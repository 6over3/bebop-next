import type { BebopCodec } from "./codec";
import type { BebopReader, BebopWriter } from "./wire";

export interface BebopTimestamp {
  readonly seconds: bigint;
  readonly nanoseconds: number;
  readonly offsetMs: number;
}

export const BebopTimestamp = {
  readFrom(reader: BebopReader): BebopTimestamp {
    return reader.readTimestamp();
  },

  writeInto(writer: BebopWriter, value: BebopTimestamp): void {
    writer.writeTimestamp(value);
  },

  encodedSize(_value: BebopTimestamp): number {
    return 16;
  },
} satisfies BebopCodec<BebopTimestamp>;

export interface BebopDuration {
  readonly seconds: bigint;
  readonly nanoseconds: number;
}

export const BebopDuration = {
  readFrom(reader: BebopReader): BebopDuration {
    return reader.readDuration();
  },

  writeInto(writer: BebopWriter, value: BebopDuration): void {
    writer.writeDuration(value);
  },

  encodedSize(_value: BebopDuration): number {
    return 12;
  },
} satisfies BebopCodec<BebopDuration>;
