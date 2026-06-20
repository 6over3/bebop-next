import type { BebopCodec } from "./codec";

export const BebopDefinitionKind = {
  struct: "struct",
  message: "message",
  enum: "enum",
  union: "union",
} as const;

export type BebopDefinitionKind =
  (typeof BebopDefinitionKind)[keyof typeof BebopDefinitionKind];

export type BebopFieldReflection = {
  readonly name: string;
  readonly index: number;
  readonly typeName: string;
};

export type BebopTypeReflection =
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: typeof BebopDefinitionKind.struct;
      readonly detail: { readonly fields: readonly BebopFieldReflection[] };
    }
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: typeof BebopDefinitionKind.message;
      readonly detail: { readonly fields: readonly BebopFieldReflection[] };
    }
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: typeof BebopDefinitionKind.enum;
      readonly detail: {
        readonly members: readonly { readonly name: string; readonly value: bigint | number }[];
        readonly isFlags: boolean;
      };
    }
  | {
      readonly name: string;
      readonly fqn: string;
      readonly kind: typeof BebopDefinitionKind.union;
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
