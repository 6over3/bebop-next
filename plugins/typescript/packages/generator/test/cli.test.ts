import { spawnSync } from "node:child_process";
import { afterEach, describe, expect, test } from "vitest";
import { mkdir, rm, writeFile } from "node:fs/promises";
import { decode, encode } from "@bebop/runtime";
import {
  CodeGeneratorRequest,
  CodeGeneratorResponse,
  DefinitionKind,
  LiteralKind,
  MethodType,
  TypeKind,
  type CodeGeneratorRequest as CodeGeneratorRequestValue,
} from "@bebop/plugin";
import { generate, runGenerator } from "../src/generator";

describe("bebopc-gen-typescript bootstrap", () => {
  const tempDir = new URL("./.tmp-generated/", import.meta.url);

  afterEach(async () => {
    await rm(tempDir, { recursive: true, force: true });
  });

  test("generates compilable TypeScript from plugin descriptors", async () => {
    const request = bootstrapRequest();
    const response = generate(request);

    expect(response.error).toBeUndefined();
    expect(response.files?.map((file) => file.name)).toEqual(["base.bb.ts", "main.bb.ts"]);

    await mkdir(tempDir, { recursive: true });
    for (const file of response.files ?? []) {
      await writeFile(new URL(file.name ?? "unknown.ts", tempDir), file.content ?? "");
    }
    await writeFile(new URL("tsconfig.json", tempDir), JSON.stringify({
      compilerOptions: {
        target: "ESNext",
        module: "Preserve",
        moduleResolution: "bundler",
        strict: true,
        noUncheckedIndexedAccess: true,
        exactOptionalPropertyTypes: true,
        skipLibCheck: true,
        noEmit: true,
      },
      include: ["*.ts"],
    }, null, 2));

    const result = spawnSync(
      process.execPath,
      [new URL("../../../node_modules/typescript/bin/tsc", import.meta.url).pathname, "-p", new URL("tsconfig.json", tempDir).pathname],
      {
        cwd: new URL("../../..", import.meta.url),
        encoding: "utf8",
      },
    );

    expect(result.status, `${result.stdout}\n${result.stderr}`).toBe(0);
    expect(response.files?.[1]?.content).toContain(
      `import { Paint, Point } from "./base.bb.js";`,
    );
    expect(response.files?.[0]?.content).toContain("export const Color =");
    expect(response.files?.[0]?.content).toContain("} as const;");
    expect(response.files?.[0]?.content).toContain("export type Color = (typeof Color)[keyof typeof Color];");
    expect(response.files?.[0]?.content).toContain("export const ColorReflection =");
    expect(response.files?.[0]?.content).toContain('kind: "enum",');
    expect(response.files?.[0]?.content).toContain("encode(value: Point): Uint8Array");
    expect(response.files?.[0]?.content).toContain("decode(bytes: Uint8Array, options?: BebopReaderOptions): Point");
    expect(response.files?.[0]?.content).toContain("return encode(Point, value);");
    expect(response.files?.[0]?.content).toContain("return decode(Point, bytes, options);");
    expect(response.files?.[0]?.content).not.toContain("export const ColorCodec");
    expect(response.files?.[0]?.content).toContain("export const ANSWER: number = 42;");
    expect(response.files?.[0]?.content).toContain("export const LARGE: bigint = 42n;");
    expect(response.files?.[1]?.content).toContain(`export type Shape =`);
    expect(response.files?.[1]?.content).not.toContain("ShapeKind");
    expect(response.files?.[1]?.content).toContain(`readonly kind: "circle"`);
    expect(response.files?.[1]?.content).toContain(`case "circle":`);
    expect(response.files?.[1]?.content).toContain("export interface CanvasServiceHandler");
    expect(response.files?.[1]?.content).toContain("export const CanvasService = defineService(");
    expect(response.files?.[1]?.content).toContain(
      "export class CanvasServiceClient<Payload = Uint8Array, Metadata = RpcMetadata>",
    );
    expect(response.files?.[1]?.content).toContain("export class CanvasServiceBatch<Metadata = RpcMetadata>");
    expect(response.files?.[1]?.content).toContain("export class CanvasServiceFutures<Metadata = RpcMetadata>");
    expect(response.files?.[1]?.content).toContain(
      "batch(this: CanvasServiceClient<Uint8Array, Metadata>, metadata: RpcMetadata = new Map()): CanvasServiceBatch<Metadata>",
    );
    expect(response.files?.[1]?.content).toContain(
      "futures(this: CanvasServiceClient<Uint8Array, Metadata>): CanvasServiceFutures<Metadata>",
    );
    expect(response.files?.[1]?.content).toContain("methodType: MethodType.DUPLEX_STREAM");
    expect(response.files?.[1]?.content).toContain(
      "sync(requests: StreamSource<Paint>, context = new RpcContext()): Promise<StreamResponse<Canvas, Metadata>>",
    );
    expect(response.files?.[1]?.content).toContain(
      "return duplexStreamCall(this.channel, CanvasService.methods.sync, requests, context);",
    );
  });

  test("reads and writes encoded plugin protocol messages", () => {
    const encodedRequest = encode(CodeGeneratorRequest, bootstrapRequest());
    const encodedResponse = runGenerator(encodedRequest);
    const response = decode(CodeGeneratorResponse, encodedResponse);

    expect(response.error).toBeUndefined();
    expect(response.files?.length).toBe(2);
  });

  test("can target a custom runtime import module", () => {
    const response = generate({
      ...bootstrapRequest(),
      parameter: "generated/runtime",
      hostOptions: new Map([["runtime-import", "source"]]),
    });

    expect(response.error).toBeUndefined();
    expect(response.files?.[0]?.content).toContain(`from "./error.js";`);
    expect(response.files?.[0]?.content).toContain(`from "./wire.js";`);
    expect(response.files?.[0]?.content).not.toContain("function stringSize");
    expect(response.files?.[0]?.content).not.toContain("function readFixedArray");
    expect(response.files?.[0]?.content).not.toContain("package_");
  });

  test("returns protocol errors instead of throwing", () => {
    const response = decode(CodeGeneratorResponse, runGenerator(new Uint8Array()));

    expect(response.error).toContain("empty input");
  });
});

function bootstrapRequest(): CodeGeneratorRequestValue {
  return {
    filesToGenerate: ["base.bop", "main.bop"],
    compilerVersion: { major: 2026, minor: 0, patch: 0, suffix: "" },
    schemas: [
      {
        path: "base.bop",
        package: "base",
        definitions: [
          {
            kind: DefinitionKind.ENUM,
            name: "Color",
            fqn: "base.Color",
            enumDef: {
              baseType: TypeKind.BYTE,
              isFlags: false,
              members: [
                { name: "RED", value: 1n },
                { name: "BLUE", value: 2n },
              ],
            },
          },
          {
            kind: DefinitionKind.STRUCT,
            name: "Point",
            fqn: "base.Point",
            structDef: {
              fields: [
                { name: "x", type: { kind: TypeKind.INT32 } },
                { name: "y", type: { kind: TypeKind.INT32 } },
                { name: "shade", type: { kind: TypeKind.DEFINED, definedFqn: "base.Color" } },
              ],
            },
          },
          {
            kind: DefinitionKind.MESSAGE,
            name: "Paint",
            fqn: "base.Paint",
            messageDef: {
              fields: [
                { name: "name", index: 1, type: { kind: TypeKind.STRING } },
                { name: "point", index: 2, type: { kind: TypeKind.DEFINED, definedFqn: "base.Point" } },
                { name: "samples", index: 3, type: { kind: TypeKind.ARRAY, arrayElement: { kind: TypeKind.FLOAT32 } } },
                { name: "stamp", index: 4, type: { kind: TypeKind.TIMESTAMP } },
                { name: "id", index: 5, type: { kind: TypeKind.UUID } },
                { name: "rgba", index: 6, type: { kind: TypeKind.FIXED_ARRAY, fixedArrayElement: { kind: TypeKind.BYTE }, fixedArraySize: 4 } },
              ],
            },
          },
          {
            kind: DefinitionKind.CONST,
            name: "GREETING",
            fqn: "base.GREETING",
            constDef: {
              type: { kind: TypeKind.STRING },
              value: { kind: LiteralKind.STRING, stringValue: "hello" },
            },
          },
          {
            kind: DefinitionKind.CONST,
            name: "ANSWER",
            fqn: "base.ANSWER",
            constDef: {
              type: { kind: TypeKind.INT32 },
              value: { kind: LiteralKind.INT, intValue: 42n },
            },
          },
          {
            kind: DefinitionKind.CONST,
            name: "LARGE",
            fqn: "base.LARGE",
            constDef: {
              type: { kind: TypeKind.INT64 },
              value: { kind: LiteralKind.INT, intValue: 42n },
            },
          },
        ],
      },
      {
        path: "main.bop",
        package: "main",
        imports: ["base.bop"],
        definitions: [
          {
            kind: DefinitionKind.UNION,
            name: "Shape",
            fqn: "main.Shape",
            nested: [
              {
                kind: DefinitionKind.MESSAGE,
                name: "Circle",
                fqn: "main.Shape.Circle",
                messageDef: {
                  fields: [
                    { name: "radius", index: 1, type: { kind: TypeKind.FLOAT64 } },
                  ],
                },
              },
            ],
            unionDef: {
              branches: [
                { name: "point", discriminator: 1, typeRefFqn: "base.Point" },
                { name: "circle", discriminator: 2, inlineFqn: "main.Shape.Circle" },
              ],
            },
          },
          {
            kind: DefinitionKind.MESSAGE,
            name: "Canvas",
            fqn: "main.Canvas",
            messageDef: {
              fields: [
                { name: "shapes", index: 1, type: { kind: TypeKind.ARRAY, arrayElement: { kind: TypeKind.DEFINED, definedFqn: "main.Shape" } } },
                { name: "paints", index: 2, type: { kind: TypeKind.MAP, mapKey: { kind: TypeKind.STRING }, mapValue: { kind: TypeKind.DEFINED, definedFqn: "base.Paint" } } },
              ],
            },
          },
          {
            kind: DefinitionKind.SERVICE,
            name: "CanvasService",
            fqn: "main.CanvasService",
            serviceDef: {
              methods: [
                {
                  name: "Get",
                  id: 0xA3F7_3AA7,
                  methodType: MethodType.UNARY,
                  requestType: { kind: TypeKind.DEFINED, definedFqn: "base.Paint" },
                  responseType: { kind: TypeKind.DEFINED, definedFqn: "main.Canvas" },
                },
                {
                  name: "Watch",
                  id: 0xBB7F_3698,
                  methodType: MethodType.SERVER_STREAM,
                  requestType: { kind: TypeKind.DEFINED, definedFqn: "base.Paint" },
                  responseType: { kind: TypeKind.DEFINED, definedFqn: "main.Canvas" },
                },
                {
                  name: "Upload",
                  id: 0xC1A3_0C3B,
                  methodType: MethodType.CLIENT_STREAM,
                  requestType: { kind: TypeKind.DEFINED, definedFqn: "base.Paint" },
                  responseType: { kind: TypeKind.DEFINED, definedFqn: "main.Canvas" },
                },
                {
                  name: "Sync",
                  id: 0x317F_5C17,
                  methodType: MethodType.DUPLEX_STREAM,
                  requestType: { kind: TypeKind.DEFINED, definedFqn: "base.Paint" },
                  responseType: { kind: TypeKind.DEFINED, definedFqn: "main.Canvas" },
                },
              ],
            },
          },
        ],
      },
    ],
  };
}
