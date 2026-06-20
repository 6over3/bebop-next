import { describe, expect, test } from "bun:test";
import { decode, encode } from "@bebop/runtime";
import {
  CodeGeneratorRequest,
  CodeGeneratorResponse,
  DefinitionKind,
  DescriptorSet,
  Diagnostic,
  DiagnosticSeverity,
  Edition,
  TypeKind,
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
