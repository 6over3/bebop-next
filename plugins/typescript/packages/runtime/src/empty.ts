import type { BebopReflectableCodec } from "./reflection.js";
import type { BebopViewReader } from "./view.js";

export type BebopEmpty = Record<string, never>;
const emptyBytes = new Uint8Array();

export class BebopEmptyView {
  static readonly instance = new BebopEmptyView();
  readonly encoded = emptyBytes;

  private constructor() {}

  static readFrom(_reader: BebopViewReader): BebopEmptyView {
    return BebopEmptyView.instance;
  }

  static skip(_reader: BebopViewReader): void {}

  decoded(): BebopEmpty { return {}; }
}

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
