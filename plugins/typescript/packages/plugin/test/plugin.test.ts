import { describe, expect, test } from "vitest";
import { decode, encode } from "@bebop/runtime";
import {
  CodeGeneratorRequest,
  CodeGeneratorResponse,
  DefinitionKind,
  DescriptorSet,
  Diagnostic,
  DiagnosticSeverity,
  Edition,
  ResponseBuilder,
  TypeKind,
  composePlugin,
  createContributorKey,
  defineContributor,
} from "../src";

describe("@bebop/plugin bootstrap codecs", () => {
  test("round-trips descriptor sets", () => {
    const descriptor: DescriptorSet = {
      schemas: [
        {
          path: "chat.bop",
          package: "myapp",
          edition: Edition.EDITION_2026,
          imports: ["bebop/empty.bop"],
          definitions: [
            {
              kind: DefinitionKind.STRUCT,
              name: "ChatMessage",
              fqn: "myapp.ChatMessage",
              visibility: 1,
              structDef: {
                fixedSize: 0,
                fields: [
                  {
                    name: "sender",
                    type: { kind: TypeKind.STRING },
                    index: 0,
                  },
                ],
              },
            },
          ],
          sourceCodeInfo: {
            locations: [
              {
                path: new Int32Array([5, 0]),
                span: new Int32Array([1, 1, 4, 2]),
                leadingComments: "hello",
              },
            ],
          },
        },
      ],
    };

    expect(decode(DescriptorSet, encode(DescriptorSet, descriptor))).toEqual(descriptor);
  });

  test("round-trips code generator requests and responses", () => {
    const request: CodeGeneratorRequest = {
      filesToGenerate: ["chat.bop"],
      parameter: "target=browser",
      compilerVersion: { major: 2026, minor: 0, patch: 0, suffix: "" },
      schemas: [{ path: "chat.bop", edition: Edition.EDITION_2026 }],
      hostOptions: new Map([["lang", "typescript"]]),
    };

    const response: CodeGeneratorResponse = {
      files: [
        {
          name: "chat.bb.ts",
          content: "export {};",
        },
      ],
      diagnostics: [
        {
          severity: DiagnosticSeverity.WARNING,
          text: "careful",
          span: new Int32Array([1, 2, 3, 4]),
        },
      ],
    };

    expect(decode(CodeGeneratorRequest, encode(CodeGeneratorRequest, request))).toEqual(request);
    expect(decode(CodeGeneratorResponse, encode(CodeGeneratorResponse, response))).toEqual(response);
  });

  test("encodes fixed diagnostic spans without a dynamic array prefix", () => {
    const diagnostic: Diagnostic = {
      span: new Int32Array([1, 2, 3, 4]),
    };
    const encoded = encode(Diagnostic, diagnostic);
    const view = new DataView(encoded.buffer, encoded.byteOffset, encoded.byteLength);

    expect(encoded.length).toBe(22);
    expect(encoded[4]).toBe(5);
    expect(view.getInt32(5, true)).toBe(1);
    expect(view.getInt32(9, true)).toBe(2);
    expect(view.getInt32(13, true)).toBe(3);
    expect(view.getInt32(17, true)).toBe(4);
    expect(encoded[21]).toBe(0);
    expect(decode(Diagnostic, encoded)).toEqual(diagnostic);
  });
});

describe("@bebop/plugin contributor composition", () => {
  test("orders dependencies and shares typed state", async () => {
    const namesKey = createContributorKey<string[]>("generated names");
    const extension = defineContributor({
      name: "extension",
      requires: ["base"],
      contribute({ response, state }) {
        response.insert("models.ts", "model_scope", `export const count = ${state.get(namesKey)?.length ?? 0};\n`);
      },
    });
    const base = defineContributor({
      name: "base",
      async contribute({ response, state }) {
        await Promise.resolve();
        state.set(namesKey, ["User"]);
        response.addFile("models.ts", "// @@bebopc_insertion_point(model_scope)\n");
      },
    });

    const result = await composePlugin(extension, base)({});

    expect(result.error).toBeUndefined();
    expect(result.files).toEqual([
      { name: "models.ts", content: "// @@bebopc_insertion_point(model_scope)\n" },
      { name: "models.ts", insertionPoint: "model_scope", content: "export const count = 1;\n" },
    ]);
  });

  test("reports dependency mistakes when composing", () => {
    const first = defineContributor({
      name: "first",
      after: ["second"],
      contribute() {},
    });
    const second = defineContributor({
      name: "second",
      after: ["first"],
      contribute() {},
    });

    expect(() => composePlugin(first, second)).toThrow("contributor dependency cycle: first, second");
    expect(() => composePlugin({
      name: "dependent",
      requires: ["missing"],
      contribute() {},
    })).toThrow("requires missing contributor 'missing'");
  });

  test("orders deeply composed contributors without recursive graph walks", () => {
    const contributors = Array.from({ length: 8_000 }, (_, index) => defineContributor({
      name: `contributor-${index}`,
      ...(index === 0 ? {} : { requires: [`contributor-${index - 1}`] }),
      contribute() {},
    })).reverse();

    expect(() => composePlugin(...contributors)).not.toThrow();
  });

  test("rejects ambiguous complete files while allowing insertion fragments", () => {
    const response = new ResponseBuilder()
      .addFile("models.ts", "export {};\n")
      .insert("models.ts", "scope", "export type Id = string;\n");

    expect(() => response.addFile("models.ts", "export {};\n"))
      .toThrow("generated file 'models.ts' was added more than once");
  });

  test("attributes contributor failures in protocol responses", async () => {
    const plugin = composePlugin({
      name: "broken",
      contribute() {
        throw new Error("invalid schema");
      },
    });

    await expect(plugin({})).resolves.toMatchObject({ error: "broken: invalid schema" });
  });
});
