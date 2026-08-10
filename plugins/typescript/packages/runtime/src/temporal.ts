import type { BebopCodec } from "./codec.js";
import type { BebopReader, BebopWriter } from "./wire.js";

const nanosecondsPerSecond = 1_000_000_000n;
const nanosecondsPerMillisecond = 1_000_000n;

export type BebopTimestamp = Temporal.ZonedDateTime;

export const BebopTimestamp = {
  fromWire(seconds: bigint, nanoseconds: number, offsetMs: number): BebopTimestamp {
    const instant = Temporal.Instant.fromEpochNanoseconds(
      seconds * nanosecondsPerSecond + BigInt(nanoseconds),
    );
    return instant.toZonedDateTimeISO(formatOffset(offsetMs));
  },

  toWire(value: BebopTimestamp): {
    readonly seconds: bigint;
    readonly nanoseconds: number;
    readonly offsetMs: number;
  } {
    if (value.offsetNanoseconds % Number(nanosecondsPerMillisecond) !== 0) {
      throw new RangeError("Bebop timestamps require timezone offsets with millisecond precision");
    }
    const epochNanoseconds = value.epochNanoseconds;
    return {
      seconds: epochNanoseconds / nanosecondsPerSecond,
      nanoseconds: Number(epochNanoseconds % nanosecondsPerSecond),
      offsetMs: value.offsetNanoseconds / Number(nanosecondsPerMillisecond),
    };
  },

  fromInstant(value: Temporal.Instant): BebopTimestamp {
    return value.toZonedDateTimeISO("UTC");
  },

  readFrom(reader: BebopReader): BebopTimestamp {
    return reader.readTimestamp();
  },

  writeInto(writer: BebopWriter, value: BebopTimestamp): void {
    writer.writeTimestamp(value);
  },

  encodedSize(): number {
    return 16;
  },
} satisfies BebopCodec<BebopTimestamp> & {
  fromWire(seconds: bigint, nanoseconds: number, offsetMs: number): BebopTimestamp;
  toWire(value: BebopTimestamp): {
    readonly seconds: bigint;
    readonly nanoseconds: number;
    readonly offsetMs: number;
  };
  fromInstant(value: Temporal.Instant): BebopTimestamp;
};

export type BebopDuration = Temporal.Duration;

export const BebopDuration = {
  fromWire(seconds: bigint, nanoseconds: number): BebopDuration {
    const negative = seconds < 0n || nanoseconds < 0;
    const absoluteSeconds = seconds < 0n ? -seconds : seconds;
    const absoluteNanoseconds = Math.abs(nanoseconds);
    const fraction = absoluteNanoseconds === 0
      ? ""
      : `.${absoluteNanoseconds.toString().padStart(9, "0")}`;
    return Temporal.Duration.from(`${negative ? "-" : ""}PT${absoluteSeconds}${fraction}S`);
  },

  toWire(value: BebopDuration): { readonly seconds: bigint; readonly nanoseconds: number } {
    if (value.years !== 0 || value.months !== 0 || value.weeks !== 0 || value.days !== 0) {
      throw new RangeError("Bebop durations cannot encode calendar-relative units");
    }
    const seconds = BigInt(value.hours) * 3_600n
      + BigInt(value.minutes) * 60n
      + BigInt(value.seconds);
    const nanoseconds = value.milliseconds * 1_000_000
      + value.microseconds * 1_000
      + value.nanoseconds;
    return { seconds, nanoseconds };
  },

  readFrom(reader: BebopReader): BebopDuration {
    return reader.readDuration();
  },

  writeInto(writer: BebopWriter, value: BebopDuration): void {
    writer.writeDuration(value);
  },

  encodedSize(): number {
    return 12;
  },
} satisfies BebopCodec<BebopDuration> & {
  fromWire(seconds: bigint, nanoseconds: number): BebopDuration;
  toWire(value: BebopDuration): { readonly seconds: bigint; readonly nanoseconds: number };
};

function formatOffset(offsetMs: number): string {
  if (!Number.isInteger(offsetMs) || Math.abs(offsetMs) >= 86_400_000) {
    throw new RangeError(`invalid Bebop timezone offset: ${offsetMs}ms`);
  }
  if (offsetMs === 0) return "UTC";
  const sign = offsetMs < 0 ? "-" : "+";
  let remaining = Math.abs(offsetMs);
  const hours = Math.floor(remaining / 3_600_000);
  remaining -= hours * 3_600_000;
  const minutes = Math.floor(remaining / 60_000);
  remaining -= minutes * 60_000;
  const seconds = Math.floor(remaining / 1_000);
  const milliseconds = remaining - seconds * 1_000;
  const base = `${sign}${pad2(hours)}:${pad2(minutes)}`;
  if (seconds === 0 && milliseconds === 0) return base;
  return `${base}:${pad2(seconds)}${milliseconds === 0 ? "" : `.${milliseconds.toString().padStart(3, "0")}`}`;
}

function pad2(value: number): string {
  return value.toString().padStart(2, "0");
}
