import type { BebopCodec } from "./codec.js";
import type { BebopReaderOptions } from "./wire.js";
import type { BebopViewReader } from "./view.js";

export type BebopDefinitionKind =
  | "struct"
  | "message"
  | "enum"
  | "union";

export type BebopFieldReflection = {
  readonly name: string;
  readonly index: number;
  readonly typeName: string;
};

export type BebopTypeReflection =
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: "struct";
      readonly detail: { readonly fields: readonly BebopFieldReflection[] };
    }
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: "message";
      readonly detail: { readonly fields: readonly BebopFieldReflection[] };
    }
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: "enum";
      readonly detail: {
        readonly members: readonly { readonly name: string; readonly value: bigint | number }[];
        readonly isFlags: boolean;
      };
    }
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: "union";
      readonly detail: {
        readonly branches: readonly {
          readonly discriminator: number;
          readonly name: string;
          readonly typeName: string;
        }[];
      };
    };

export type BebopReflectableCodec<T> = BebopCodec<T> & {
  readonly reflection: BebopTypeReflection;
};

/** The self-contained codec surface exposed by generated Bebop types. */
export type BebopGeneratedCodec<T, View = never> = BebopReflectableCodec<T> & {
  encode(value: T): Uint8Array;
  decode(bytes: Uint8Array, options?: BebopReaderOptions): T;
} & ([View] extends [never] ? object : {
  view(bytes: Uint8Array, options?: BebopReaderOptions): View;
  readView(reader: BebopViewReader): View;
});

export type BebopViewCodec<T, View> = BebopCodec<T> & {
  view(bytes: Uint8Array, options?: BebopReaderOptions): View;
  readView(reader: BebopViewReader): View;
};
