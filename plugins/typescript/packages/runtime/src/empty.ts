import type { BebopReflectableCodec } from "./reflection.js";

export type BebopEmpty = Record<string, never>;

export const BebopEmpty = {
  readFrom(): BebopEmpty {
    return {};
  },

  writeInto(): void {},

  encodedSize(): number {
    return 0;
  },

  reflection: {
    name: "BebopEmpty",
    fqn: "bebop.Empty",
    kind: "struct",
    detail: { fields: [] },
  },
} satisfies BebopReflectableCodec<BebopEmpty>;
