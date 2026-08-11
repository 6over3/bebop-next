import {
  CodeGeneratorRequest,
  CodeGeneratorResponse,
  DefinitionKind,
  MethodType as DescriptorMethodType,
  TypeKind,
  type DefinitionDescriptor,
  type FieldDescriptor,
  type GeneratedFile,
  type LiteralValue,
  type SchemaDescriptor,
  type TypeDescriptor,
  type UnionBranchDescriptor,
  LiteralKind,
} from "@bebop/plugin";
import { decode, encode } from "@bebop/runtime/codec";
import { IndentedStringBuilder } from "./indented-string-builder.js";

type DefinitionInfo = {
  readonly schema: SchemaDescriptor;
  readonly typeName: string;
  readonly kind: DefinitionKind;
  readonly enumBaseType?: TypeKind;
};

type FieldInfo = {
  readonly originalName: string;
  readonly name: string;
  readonly variable: string;
  readonly type: TypeDescriptor;
  readonly typeName: string;
  readonly index: number;
};

type ServiceMethodInfo = {
  readonly originalName: string;
  readonly name: string;
  readonly id: number;
  readonly methodType: DescriptorMethodType;
  readonly runtimeMethodType: "UNARY" | "SERVER_STREAM" | "CLIENT_STREAM" | "DUPLEX_STREAM";
  readonly requestType: string;
  readonly requestView: string;
  readonly requestCodec: string;
  readonly requestTypeUrl: string;
  readonly responseType: string;
  readonly responseCodec: string;
  readonly responseTypeUrl: string;
};

type ImportState = {
  readonly state: GeneratorState;
  readonly schema: SchemaDescriptor;
  readonly runtimeImport: RuntimeImport;
  readonly runtimeValues: Set<string>;
  readonly runtimeTypes: Set<string>;
  readonly localValues: Map<string, Set<string>>;
  nextTemporary: number;
};

type GeneratorState = {
  readonly definitions: Map<string, DefinitionInfo>;
};

type RuntimeImport =
  | { readonly kind: "module"; readonly module: string }
  | { readonly kind: "source" };

type GeneratorOptions = {
  readonly runtimeImport: RuntimeImport;
};

const reservedIdentifiers = new Set([
  "break",
  "case",
  "catch",
  "class",
  "const",
  "continue",
  "debugger",
  "default",
  "delete",
  "do",
  "else",
  "enum",
  "export",
  "extends",
  "false",
  "finally",
  "for",
  "function",
  "if",
  "import",
  "in",
  "instanceof",
  "new",
  "null",
  "package",
  "return",
  "super",
  "switch",
  "this",
  "throw",
  "true",
  "try",
  "typeof",
  "undefined",
  "var",
  "void",
  "while",
  "with",
  "yield",
  "abstract",
  "any",
  "as",
  "asserts",
  "async",
  "await",
  "bigint",
  "boolean",
  "constructor",
  "declare",
  "from",
  "get",
  "infer",
  "is",
  "keyof",
  "let",
  "module",
  "namespace",
  "never",
  "number",
  "object",
  "of",
  "readonly",
  "require",
  "set",
  "static",
  "string",
  "symbol",
  "type",
  "unknown",
  "implements",
  "interface",
  "private",
  "protected",
  "public",
]);

const reservedClientMembers = new Set(["batch", "channel", "futures"]);

export function runGenerator(input: Uint8Array): Uint8Array {
  if (input.length === 0) {
    return encode(CodeGeneratorResponse, { error: "bebopc-gen-typescript: empty input" });
  }

  try {
    const request = decode(CodeGeneratorRequest, input);
    return encode(CodeGeneratorResponse, generate(request));
  } catch (error) {
    return encode(CodeGeneratorResponse, { error: `bebopc-gen-typescript: ${errorMessage(error)}` });
  }
}

export function generate(request: CodeGeneratorRequest): CodeGeneratorResponse {
  const filesToGenerate = request.filesToGenerate;
  const schemas = request.schemas;
  if (filesToGenerate === undefined) {
    throw new CodegenError("request missing filesToGenerate");
  }
  if (schemas === undefined) {
    throw new CodegenError("request missing schemas");
  }

  const state = buildState(schemas);
  const options = parseGeneratorOptions(request.hostOptions);
  const fileSet = new Set(filesToGenerate);
  if (fileSet.size !== filesToGenerate.length) {
    throw new CodegenError("filesToGenerate contains duplicate paths");
  }
  const files: GeneratedFile[] = [];
  const generated = new Set<string>();
  for (const schema of schemas) {
    const path = required(schema.path, "schema missing path");
    if (!fileSet.has(path)) continue;
    files.push({
      name: outputFileName(schema),
      content: generateSchema(schema, state, options, request.compilerVersion),
    });
    generated.add(path);
  }
  for (const path of fileSet) {
    if (!generated.has(path)) throw new CodegenError(`requested schema not found: ${path}`);
  }
  return { files };
}

function parseGeneratorOptions(hostOptions: ReadonlyMap<string, string> | undefined): GeneratorOptions {
  let runtimeImport: RuntimeImport = { kind: "module", module: "@bebop/runtime" };
  const option = hostOptions?.get("runtime-import");
  if (option !== undefined) {
    const value = option.trim();
    if (value.length === 0) {
      throw new CodegenError("runtime-import must not be empty");
    }
    runtimeImport = value === "source" ? { kind: "source" } : { kind: "module", module: value };
  }
  return { runtimeImport };
}

function buildState(schemas: readonly SchemaDescriptor[]): GeneratorState {
  const definitions = new Map<string, DefinitionInfo>();
  for (const schema of schemas) {
    required(schema.path, "schema missing path");
    for (const def of schema.definitions ?? []) {
      collectDefinition(def, schema, definitions);
    }
  }
  return { definitions };
}

function collectDefinition(
  def: DefinitionDescriptor,
  schema: SchemaDescriptor,
  definitions: Map<string, DefinitionInfo>,
): void {
  const pending = [def];
  while (pending.length !== 0) {
    const current = pending.pop()!;
    if (current.fqn !== undefined) {
      if (definitions.has(current.fqn)) {
        throw new CodegenError(`definition ${current.fqn} appears more than once`);
      }
      const typeName = exportedTypeName(current, schema);
      const kind = required(current.kind, `definition ${typeName} missing kind`);
      if (kind === DefinitionKind.ENUM) {
        const enumBaseType = required(required(current.enumDef, `enum ${typeName} missing body`).baseType, `enum ${typeName} missing base type`);
        definitions.set(current.fqn, { schema, typeName, kind, enumBaseType });
      } else {
        definitions.set(current.fqn, { schema, typeName, kind });
      }
    }
    const nested = current.nested ?? [];
    for (let index = nested.length - 1; index >= 0; index--) pending.push(nested[index]!);
  }
}

function generateSchema(
  schema: SchemaDescriptor,
  state: GeneratorState,
  options: GeneratorOptions,
  compilerVersion: { readonly major: number; readonly minor: number; readonly patch: number; readonly suffix: string } | undefined,
): string {
  const imports: ImportState = {
    state,
    schema,
    runtimeImport: options.runtimeImport,
    runtimeValues: new Set(),
    runtimeTypes: new Set(["BebopReader", "BebopWriter"]),
    localValues: new Map(),
    nextTemporary: 0,
  };
  const declarations: string[] = [];
  for (const def of schema.definitions ?? []) {
    declarations.push(...generateDefinition(def, schema, state, imports));
  }

  const out = new IndentedStringBuilder();
  out.line("// Code generated by bebopc-gen-typescript. DO NOT EDIT.");
  out.line(`// source: ${required(schema.path, "schema missing path")}`);
  if (compilerVersion !== undefined) {
    const suffix = compilerVersion.suffix.length === 0 ? "" : `-${compilerVersion.suffix}`;
    out.line(`// bebopc ${compilerVersion.major}.${compilerVersion.minor}.${compilerVersion.patch}${suffix}`);
  }
  out.line();
  writeImports(out, imports);
  for (let index = 0; index < declarations.length; index++) {
    if (index !== 0) out.line();
    out.append(declarations[index]!);
    out.line();
  }
  return out.toString();
}

function generateDefinition(
  def: DefinitionDescriptor,
  schema: SchemaDescriptor,
  state: GeneratorState,
  imports: ImportState,
): string[] {
  const result: string[] = [];
  const pending: { readonly definition: DefinitionDescriptor; readonly expanded: boolean }[] = [
    { definition: def, expanded: false },
  ];
  while (pending.length !== 0) {
    const { definition, expanded } = pending.pop()!;
    if (!expanded) {
      pending.push({ definition, expanded: true });
      const nested = definition.nested ?? [];
      for (let index = nested.length - 1; index >= 0; index--) {
        pending.push({ definition: nested[index]!, expanded: false });
      }
      continue;
    }
    switch (definition.kind) {
      case DefinitionKind.ENUM:
        result.push(generateEnum(definition, schema, imports));
        break;
      case DefinitionKind.STRUCT:
        result.push(generateRecord(definition, schema, state, imports, "struct"));
        break;
      case DefinitionKind.MESSAGE:
        result.push(generateRecord(definition, schema, state, imports, "message"));
        break;
      case DefinitionKind.UNION:
        result.push(generateUnion(definition, schema, state, imports));
        break;
      case DefinitionKind.CONST:
        result.push(generateConst(definition, state, imports));
        break;
      case DefinitionKind.SERVICE:
        result.push(generateService(definition, schema, state, imports));
        break;
      case DefinitionKind.DECORATOR:
        break;
      default:
        throw new CodegenError(`unknown definition kind ${String(definition.kind)}`);
    }
  }
  return result;
}

function generateEnum(def: DefinitionDescriptor, schema: SchemaDescriptor, imports: ImportState): string {
  const name = exportedTypeName(def, schema);
  const enumDef = required(def.enumDef, `enum ${name} missing body`);
  const baseType = required(enumDef.baseType, `enum ${name} missing base type`);
  const members = enumDef.members ?? [];
  imports.runtimeTypes.add("BebopTypeReflection");

  const out = new IndentedStringBuilder();
  out.block(`export const ${name} =`, (b) => {
    for (const member of members) {
      b.line(`${propertyKey(required(member.name, `enum ${name} member missing name`))}: ${enumMemberLiteral(required(member.value, `enum ${name} member missing value`), baseType)},`);
    }
  }, "} as const;");
  out.line(`export type ${name} = ${enumTypeExpression(name, baseType, enumDef.isFlags === true)};`);
  out.line();
  out.block(`export const ${name}Reflection =`, (reflection) => {
    reflection.line(`name: ${stringLiteral(required(def.name, "enum missing name"))},`);
    reflection.line(`fqn: ${stringLiteral(required(def.fqn, `enum ${name} missing fqn`))},`);
    reflection.line('kind: "enum",');
    reflection.block("detail:", (detail) => {
      detail.line("members: [");
      detail.indented((array) => {
        for (const member of members) {
          array.line(`{ name: ${stringLiteral(required(member.name, `enum ${name} member missing name`))}, value: ${enumMemberLiteral(required(member.value, `enum ${name} member missing value`), baseType)} },`);
        }
      });
      detail.line("],");
      detail.line(`isFlags: ${enumDef.isFlags === true},`);
    }, "},");
  }, "} as const satisfies BebopTypeReflection;");
  return out.trimEnd().toString();
}

function generateRecord(
  def: DefinitionDescriptor,
  schema: SchemaDescriptor,
  state: GeneratorState,
  imports: ImportState,
  kind: "struct" | "message",
): string {
  const name = exportedTypeName(def, schema);
  requireGeneratedCodecImports(imports);
  const fields = recordFields(def, kind).map((field) => fieldInfo(field, kind, state, imports));
  if (kind === "message") {
    fields.sort((left, right) => left.index - right.index);
    for (let index = 0; index < fields.length; index++) {
      const field = fields[index]!;
      if (field.index < 1 || field.index > 255) {
        throw new CodegenError(`message ${name} field ${field.originalName} has invalid index ${field.index}`);
      }
      if (index !== 0 && fields[index - 1]!.index === field.index) {
        throw new CodegenError(`message ${name} has duplicate field index ${field.index}`);
      }
    }
  }
  requireUniqueNames(fields.map((field) => field.name), `${kind} ${name} field`);
  const out = new IndentedStringBuilder();
  out.block(`export type ${name} =`, (type) => {
    for (const field of fields) {
      const optional = kind === "message" ? "?" : "";
      const undefinedType = kind === "message" ? " | undefined" : "";
      type.line(`readonly ${propertyKey(field.name)}${optional}: ${field.typeName}${undefinedType};`);
    }
  }, "};");
  out.line();
  writeRecordView(out, name, fields, kind, imports);
  out.line();
  out.block(`export const ${name} =`, (codec) => {
    writeGeneratedCodecConvenience(codec, name, `${name}View`);
    if (kind === "struct") {
      writeStructRead(codec, name, fields, imports);
      writeStructWrite(codec, name, fields, imports);
      writeStructSize(codec, name, fields, imports);
    } else {
      writeMessageRead(codec, name, fields, imports);
      writeMessageWrite(codec, name, fields, imports);
      writeMessageSize(codec, name, fields, imports);
    }
    writeReflection(codec, def, name, kind, fields);
  }, `} satisfies BebopGeneratedCodec<${name}, ${name}View>;`);
  return out.trimEnd().toString();
}

function writeGeneratedCodecConvenience(
  codec: IndentedStringBuilder,
  name: string,
  viewName?: string,
): void {
  codec.block(`encode(value: ${name}): Uint8Array`, (body) => {
    body.line(`return encode(${name}, value);`);
  }, "},");
  codec.block(`decode(bytes: Uint8Array, options?: BebopReaderOptions): ${name}`, (body) => {
    body.line(`return decode(${name}, bytes, options);`);
  }, "},");
  if (viewName !== undefined) {
    codec.block(`view(bytes: Uint8Array, options?: BebopReaderOptions): ${viewName}`, (body) => {
      body.line("const reader = new BebopViewReader(bytes, options);");
      body.line(`const view = ${viewName}.readFrom(reader);`);
      body.line("reader.finish();");
      body.line("return view;");
    }, "},");
    codec.block(`readView(reader: BebopViewReader): ${viewName}`, (body) => {
      body.line(`return ${viewName}.readFrom(reader);`);
    }, "},");
  }
}

function requireGeneratedCodecImports(imports: ImportState): void {
  imports.runtimeValues.add("decode");
  imports.runtimeValues.add("encode");
  imports.runtimeTypes.add("BebopGeneratedCodec");
  imports.runtimeTypes.add("BebopReaderOptions");
}

function writeRecordView(
  out: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  kind: "struct" | "message",
  imports: ImportState,
): void {
  imports.runtimeValues.add("BebopViewReader");
  if (kind === "message") {
    imports.runtimeValues.add("readMessageField");
    imports.runtimeTypes.add("BebopMessageView");
  }
  out.block(`export class ${name}View`, (view) => {
    if (kind === "message") {
      view.line("private constructor(private readonly message: BebopMessageView) {}");
      view.line();
      view.line("get encoded(): Uint8Array { return this.message.encoded; }");
      view.line();
      view.block("field(tag: number): Uint8Array | undefined", (body) => {
        body.line("return this.message.field(tag);");
      });
      for (const field of fields) {
        view.line();
        view.block(`get ${propertyKey(field.name)}(): ${viewTypeName(field.type, imports)} | undefined`, (getter) => {
          getter.line(`return readMessageField(this.message, ${field.index}, (_r) => ${viewReadExpression(field.type, "_r", imports)});`);
        });
      }
      view.line();
      view.block(`static readFrom(reader: BebopViewReader): ${name}View`, (body) => {
        body.line(`return new ${name}View(reader.readMessage());`);
      });
      view.line();
      view.block("static skip(reader: BebopViewReader): void", (body) => {
        body.line("reader.skipMessage();");
      });
    } else {
      view.line("readonly encoded: Uint8Array;");
      for (const field of fields) {
        view.line(`readonly ${propertyKey(field.name)}: ${viewTypeName(field.type, imports)};`);
      }
      view.line();
      const parameters = ["encoded: Uint8Array", ...fields.map(
        (field) => `${field.variable}: ${viewTypeName(field.type, imports)}`,
      )].join(", ");
      view.block(`private constructor(${parameters})`, (body) => {
        body.line("this.encoded = encoded;");
        for (const field of fields) body.line(`${memberAccess("this", field.name)} = ${field.variable};`);
      });
      view.line();
      view.block(`static readFrom(reader: BebopViewReader): ${name}View`, (body) => {
        body.line("const start = reader.position;");
        for (const field of fields) {
          body.line(`const ${field.variable} = ${viewReadExpression(field.type, "reader", imports)};`);
        }
        const args = ["reader.encodedFrom(start)", ...fields.map(({ variable }) => variable)].join(", ");
        body.line(`return new ${name}View(${args});`);
      });
      view.line();
      const skipReader = fields.length === 0 ? "_reader" : "reader";
      view.block(`static skip(${skipReader}: BebopViewReader): void`, (body) => {
        for (const field of fields) body.line(`${viewSkipExpression(field.type, skipReader, imports)};`);
      });
    }
    view.line();
    view.block(`decoded(options?: BebopReaderOptions): ${name}`, (body) => {
      body.line(`return ${name}.decode(this.encoded, options);`);
    });
  });
}

function writeStructRead(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  const reader = fields.length === 0 ? "_reader" : "reader";
  codec.block(`readFrom(${reader}: BebopReader): ${name}`, (body) => {
    if (fields.length === 0) {
      body.line("return {};");
      return;
    }
    body.line("return {");
    body.indented((object) => {
      for (const field of fields) {
        object.line(`${propertyKey(field.name)}: ${readExpression(field.type, reader, imports)},`);
      }
    });
    body.line("};");
  }, "},");
}

function writeStructWrite(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  const writer = fields.length === 0 ? "_writer" : "writer";
  const value = fields.length === 0 ? "_value" : "value";
  codec.block(`writeInto(${writer}: BebopWriter, ${value}: ${name}): void`, (body) => {
    for (const field of fields) {
      writeStatement(body, field.type, writer, memberAccess(value, field.name), imports);
    }
  }, "},");
}

function writeStructSize(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  const usesValue = fields.some((field) => encodedSizeUsesValue(field.type, imports));
  const value = usesValue ? "value" : "_value";
  codec.block(`encodedSize(${value}: ${name}): number`, (body) => {
    if (fields.length === 0) {
      body.line("return 0;");
      return;
    }
    body.line("let size = 0;");
    for (const field of fields) {
      writeAddSizeStatements(body, field.type, memberAccess(value, field.name), imports);
    }
    body.line("return size;");
  }, "},");
}

function writeMessageRead(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  codec.block(`readFrom(reader: BebopReader): ${name}`, (body) => {
    body.line("const message = reader.readMessage();");
    body.line("return {");
    body.indented((object) => {
      for (const field of fields) {
        object.line(`${propertyKey(field.name)}: message.read(${field.index}, (_r) => ${readExpression(field.type, "_r", imports)}),`);
      }
    });
    body.line("};");
  }, "},");
}

function writeMessageWrite(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  codec.block(`writeInto(writer: BebopWriter, value: ${name}): void`, (body) => {
    body.line("const message = writer.beginMessage();");
    for (const field of fields) {
      const valueAccess = memberAccess("value", field.name);
      body.block(`if (${valueAccess} !== undefined)`, (ifBody) => {
        ifBody.line(`writer.markMessageField(message, ${field.index});`);
        writeStatement(ifBody, field.type, "writer", valueAccess, imports);
      });
    }
    body.line("writer.endMessage(message);");
  }, "},");
}

function writeMessageSize(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  codec.block(`encodedSize(value: ${name}): number`, (body) => {
    imports.runtimeValues.add("messageEncodedSize");
    body.line("let size = 0;");
    body.line("let fieldCount = 0;");
    body.line("let maxTag = 0;");
    body.line("let blockMask = 0;");
    for (const field of fields) {
      const valueAccess = memberAccess("value", field.name);
      body.block(`if (${valueAccess} !== undefined)`, (ifBody) => {
        ifBody.line("fieldCount++;");
        ifBody.line(`maxTag = ${field.index};`);
        ifBody.line(`blockMask |= ${1 << ((field.index - 1) >>> 5)};`);
        writeAddSizeStatements(ifBody, field.type, valueAccess, imports);
      });
    }
    body.line("return messageEncodedSize(size, fieldCount, maxTag, blockMask);");
  }, "},");
}

function writeReflection(
  codec: IndentedStringBuilder,
  def: DefinitionDescriptor,
  name: string,
  kind: "struct" | "message",
  fields: readonly FieldInfo[],
): void {
  codec.block("reflection:", (reflection) => {
    reflection.line(`name: ${stringLiteral(required(def.name, `${kind} missing name`))},`);
    reflection.line(`fqn: ${stringLiteral(required(def.fqn, `${kind} ${name} missing fqn`))},`);
    reflection.line(`kind: ${stringLiteral(kind)},`);
    reflection.block("detail:", (detail) => {
      detail.line("fields: [");
      detail.indented((array) => {
        for (const field of fields) {
          array.line(`{ name: ${stringLiteral(field.originalName)}, index: ${kind === "message" ? field.index : 0}, typeName: ${stringLiteral(field.typeName)} },`);
        }
      });
      detail.line("],");
    }, "},");
  }, "},");
}

function generateUnion(
  def: DefinitionDescriptor,
  schema: SchemaDescriptor,
  state: GeneratorState,
  imports: ImportState,
): string {
  const name = exportedTypeName(def, schema);
  requireGeneratedCodecImports(imports);
  imports.runtimeValues.add("BebopReader");
  const branches = required(def.unionDef, `union ${name} missing body`).branches ?? [];
  const branchInfos = branches.map((branch) => unionBranchInfo(branch, name, state, imports));
  requireUniqueNames(branchInfos.map((branch) => branch.caseName), `union ${name} branch`);
  const out = new IndentedStringBuilder();
  out.line(`export type ${name} =`);
  out.indented((type) => {
    const variants = [
      '{ readonly kind: "unknown"; readonly discriminator: number; readonly data: Uint8Array }',
      ...branchInfos.map(
        (branch) => `{ readonly kind: ${stringLiteral(branch.caseName)}; readonly value: ${branch.typeName} }`,
      ),
    ];
    for (let index = 0; index < variants.length; index++) {
      type.line(`| ${variants[index]!}${index === variants.length - 1 ? ";" : ""}`);
    }
  });
  out.line();
  out.line(`export type ${name}View =`);
  out.indented((type) => {
    type.line("| {");
    type.indented((variant) => {
      variant.line("readonly encoded: Uint8Array;");
      variant.line('readonly kind: "unknown";');
      variant.line("readonly discriminator: number;");
      variant.line("readonly data: Uint8Array;");
      variant.line(`decoded(options?: BebopReaderOptions): ${name};`);
    });
    type.line("}");
    for (let index = 0; index < branchInfos.length; index++) {
      const branch = branchInfos[index]!;
      type.line("| {");
      type.indented((variant) => {
        variant.line("readonly encoded: Uint8Array;");
        variant.line(`readonly kind: ${stringLiteral(branch.caseName)};`);
        variant.line(`readonly value: ${viewTypeName({ kind: TypeKind.DEFINED, definedFqn: branch.fqn }, imports)};`);
        variant.line(`decoded(options?: BebopReaderOptions): ${name};`);
      });
      type.line(`}${index === branchInfos.length - 1 ? ";" : ""}`);
    }
  });
  out.line();
  imports.runtimeValues.add("BebopViewReader");
  out.block(`export const ${name}View =`, (view) => {
    view.block(`readFrom(reader: BebopViewReader): ${name}View`, (body) => {
      body.line("const result = reader.readLengthPrefixedValue((_r) => {");
      body.indented((decodeBody) => {
        decodeBody.line("const discriminator = _r.readByte();");
        decodeBody.block("switch (discriminator)", (sw) => {
          for (const branch of branchInfos) {
            sw.line(`case ${branch.discriminator}: return { kind: ${stringLiteral(branch.caseName)} as const, value: ${viewReadExpression({ kind: TypeKind.DEFINED, definedFqn: branch.fqn }, "_r", imports)} };`);
          }
          sw.line('default: return { kind: "unknown" as const, discriminator, data: _r.readView(_r.remaining) };');
        });
      });
      body.line("});");
      body.line("return {");
      body.indented((value) => {
        value.line("encoded: result.encoded,");
        value.line("...result.value,");
        value.block(`decoded(options?: BebopReaderOptions): ${name}`, (decoded) => {
          decoded.line(`return ${name}.decode(this.encoded, options);`);
        }, "},");
      });
      body.line("};");
    }, "},");
    view.block("skip(reader: BebopViewReader): void", (body) => {
      body.line("reader.skipLengthPrefixed();");
    }, "},");
  }, "} as const;");
  out.line();
  out.block(`export const ${name} =`, (codec) => {
    writeGeneratedCodecConvenience(codec, name, `${name}View`);
    codec.block(`readFrom(reader: BebopReader): ${name}`, (body) => {
      body.line("return reader.readLengthPrefixed((valueReader) => {");
      body.indented((decodeBody) => {
        decodeBody.line("const discriminator = valueReader.readByte();");
        decodeBody.block("switch (discriminator)", (sw) => {
          for (const branch of branchInfos) {
            sw.line(`case ${branch.discriminator}: return { kind: ${stringLiteral(branch.caseName)}, value: valueReader.readNested(${branch.typeName}) };`);
          }
          sw.line('default: return { kind: "unknown", discriminator, data: valueReader.readBytes(valueReader.length - valueReader.index) };');
        });
      });
      body.line("});");
    }, "},");
    codec.block(`writeInto(writer: BebopWriter, value: ${name}): void`, (body) => {
      body.line("const position = writer.beginLengthPrefixed();");
      body.block("switch (value.kind)", (sw) => {
        for (const branch of branchInfos) {
          sw.line(`case ${stringLiteral(branch.caseName)}:`);
          sw.indented((caseBody) => {
            caseBody.line(`writer.writeByte(${branch.discriminator});`);
            caseBody.line(`${branch.typeName}.writeInto(writer, value.value);`);
            caseBody.line("break;");
          });
        }
        sw.line('case "unknown":');
        sw.indented((caseBody) => {
          caseBody.line("writer.writeByte(value.discriminator);");
          caseBody.line("writer.writeBytes(value.data);");
          caseBody.line("break;");
        });
      });
      body.line("writer.endLengthPrefixed(position);");
    }, "},");
    codec.block(`encodedSize(value: ${name}): number`, (body) => {
      body.block("switch (value.kind)", (sw) => {
        for (const branch of branchInfos) {
          sw.line(`case ${stringLiteral(branch.caseName)}: return 5 + ${branch.typeName}.encodedSize(value.value);`);
        }
        sw.line('case "unknown": return 5 + value.data.length;');
      });
    }, "},");
    codec.block("reflection:", (reflection) => {
      reflection.line(`name: ${stringLiteral(required(def.name, "union missing name"))},`);
      reflection.line(`fqn: ${stringLiteral(required(def.fqn, `union ${name} missing fqn`))},`);
      reflection.line('kind: "union",');
      reflection.block("detail:", (detail) => {
        detail.line("branches: [");
        detail.indented((array) => {
          for (const branch of branchInfos) {
            array.line(`{ discriminator: ${branch.discriminator}, name: ${stringLiteral(branch.originalName)}, typeName: ${stringLiteral(branch.typeName)} },`);
          }
        });
        detail.line("],");
      }, "},");
    }, "},");
  }, `} satisfies BebopGeneratedCodec<${name}, ${name}View>;`);
  return out.trimEnd().toString();
}

function generateConst(def: DefinitionDescriptor, state: GeneratorState, imports: ImportState): string {
  const name = identifier(required(def.name, "const missing name"));
  const body = required(def.constDef, `const ${name} missing body`);
  const type = required(body.type, `const ${name} missing type`);
  const value = required(body.value, `const ${name} missing value`);
  return `export const ${name}: ${typeName(type, state, imports)} = ${literalExpression(value, type)};`;
}

function generateService(
  def: DefinitionDescriptor,
  schema: SchemaDescriptor,
  state: GeneratorState,
  imports: ImportState,
): string {
  const name = exportedTypeName(def, schema);
  const service = required(def.serviceDef, `service ${name} missing body`);
  const methods = (service.methods ?? []).map((method) => serviceMethodInfo(method, name, state, imports));
  requireUniqueNames(methods.map((method) => method.name), `service ${name} method`);
  imports.runtimeValues.add("defineService");
  imports.runtimeValues.add("MethodType");
  imports.runtimeValues.add("RpcContext");
  imports.runtimeTypes.add("Awaitable");
  imports.runtimeTypes.add("BebopChannel");
  imports.runtimeTypes.add("RpcMetadata");
  imports.runtimeTypes.add("RpcResponse");
  imports.runtimeTypes.add("StreamResponse");
  imports.runtimeTypes.add("StreamSource");
  for (const method of methods) imports.runtimeValues.add(clientHelper(method.methodType));
  const batchMethods = methods.filter(({ methodType }) =>
    methodType === DescriptorMethodType.UNARY || methodType === DescriptorMethodType.SERVER_STREAM);
  const futureMethods = methods.filter(({ methodType }) => methodType === DescriptorMethodType.UNARY);
  if (batchMethods.length > 0) {
    imports.runtimeValues.add("Batch");
    imports.runtimeTypes.add("BatchResults");
    imports.runtimeTypes.add("CallRef");
    imports.runtimeTypes.add("StreamRef");
  }
  if (futureMethods.length > 0) {
    imports.runtimeValues.add("FutureDispatcher");
    imports.runtimeTypes.add("BebopFuture");
    imports.runtimeTypes.add("DispatchOptions");
  }
  const out = new IndentedStringBuilder();
  out.block(`export interface ${name}Handler`, (handler) => {
    for (const method of methods) {
      handler.line(`${method.name}${handlerSignature(method)};`);
    }
  });
  out.line();
  out.line(`export const ${name} = defineService(`);
  out.indented((definition) => {
    definition.line(`${stringLiteral(required(def.name, "service missing name"))},`);
    definition.block("", (methodDefinitions) => {
      for (const method of methods) {
        methodDefinitions.block(`${method.name}:`, (descriptor) => {
          descriptor.line(`id: ${hexUint32(method.id)},`);
          descriptor.line(`name: ${stringLiteral(method.originalName)},`);
          descriptor.line(`methodType: MethodType.${method.runtimeMethodType},`);
          descriptor.line(`request: ${method.requestCodec},`);
          descriptor.line(`response: ${method.responseCodec},`);
          descriptor.line(`requestTypeUrl: ${stringLiteral(method.requestTypeUrl)},`);
          descriptor.line(`responseTypeUrl: ${stringLiteral(method.responseTypeUrl)},`);
        }, "},");
      }
    }, "},");
    definition.block(`(builder, handler: ${name}Handler, methods) =>`, (register) => {
      for (const method of methods) {
        const registration = registrationMethod(method.methodType);
        const args = method.methodType === DescriptorMethodType.UNARY || method.methodType === DescriptorMethodType.SERVER_STREAM
          ? "request, context"
          : "requests, context";
        register.line(`builder.${registration}(methods.${method.name}, (${args}) => handler.${method.name}(${args}));`);
      }
      register.line("return builder;");
    }, "},");
  });
  out.line(");");
  out.line();
  if (batchMethods.length > 0) {
    out.block(`export class ${name}Batch<Metadata = RpcMetadata>`, (batch) => {
      batch.line("constructor(private readonly batch: Batch<Metadata>) {}");
      for (const method of batchMethods) {
        batch.line();
        const reference = method.methodType === DescriptorMethodType.UNARY
          ? `CallRef<${method.responseType}>`
          : `StreamRef<${method.responseType}>`;
        const add = method.methodType === DescriptorMethodType.UNARY ? "addUnary" : "addServerStream";
        batch.block(`${method.name}(request: ${method.requestType} | CallRef<${method.requestType}>): ${reference}`, (body) => {
          body.line(`return this.batch.${add}(${name}.methods.${method.name}, request);`);
        });
      }
      batch.line();
      batch.block("execute(context = new RpcContext()): Promise<BatchResults>", (body) => {
        body.line("return this.batch.execute(context);");
      });
    });
    out.line();
  }
  if (futureMethods.length > 0) {
    out.block(`export class ${name}Futures<Metadata = RpcMetadata> implements AsyncDisposable`, (futures) => {
      futures.line("constructor(private readonly dispatcher: FutureDispatcher<Metadata>) {}");
      for (const method of futureMethods) {
        futures.line();
        futures.block(
          `${method.name}(request: ${method.requestType}, options: DispatchOptions = {}, context = new RpcContext()): Promise<BebopFuture<${method.responseType}, Metadata>>`,
          (body) => {
            body.line(`return this.dispatcher.dispatch(${name}.methods.${method.name}, request, options, context);`);
          },
        );
      }
      futures.line();
      futures.block("[Symbol.asyncDispose](): Promise<void>", (body) => {
        body.line("return this.dispatcher.close();");
      });
    });
    out.line();
  }
  out.block(`export class ${name}Client<Payload = Uint8Array, Metadata = RpcMetadata>`, (client) => {
    client.line("constructor(readonly channel: BebopChannel<Payload, Metadata>) {}");
    for (const method of methods) {
      client.line();
      client.block(`${clientSignature(method)}`, (body) => {
        const helper = clientHelper(method.methodType);
        const request = method.methodType === DescriptorMethodType.UNARY || method.methodType === DescriptorMethodType.SERVER_STREAM
          ? "request, "
          : "requests, ";
        body.line(`return ${helper}(this.channel, ${name}.methods.${method.name}, ${request}context);`);
      });
    }
    if (batchMethods.length > 0) {
      client.line();
      client.block(`batch(this: ${name}Client<Uint8Array, Metadata>, metadata: RpcMetadata = new Map()): ${name}Batch<Metadata>`, (body) => {
        body.line(`return new ${name}Batch(new Batch(this.channel, metadata));`);
      });
    }
    if (futureMethods.length > 0) {
      client.line();
      client.block(`futures(this: ${name}Client<Uint8Array, Metadata>): ${name}Futures<Metadata>`, (body) => {
        body.line(`return new ${name}Futures(new FutureDispatcher(this.channel));`);
      });
    }
  });
  return out.trimEnd().toString();
}

function serviceMethodInfo(
  method: NonNullable<NonNullable<DefinitionDescriptor["serviceDef"]>["methods"]>[number],
  serviceName: string,
  state: GeneratorState,
  imports: ImportState,
): ServiceMethodInfo {
  const originalName = required(method.name, `service ${serviceName} method missing name`);
  const methodType = required(method.methodType, `service ${serviceName} method ${originalName} missing type`);
  const request = serviceTypeInfo(
    required(method.requestType, `service ${serviceName} method ${originalName} missing request`),
    state,
    imports,
  );
  const response = serviceTypeInfo(
    required(method.responseType, `service ${serviceName} method ${originalName} missing response`),
    state,
    imports,
  );
  return {
    originalName,
    name: serviceMethodName(originalName),
    id: required(method.id, `service ${serviceName} method ${originalName} missing id`),
    methodType,
    runtimeMethodType: runtimeMethodType(methodType),
    requestType: request.type,
    requestView: request.view,
    requestCodec: request.codec,
    requestTypeUrl: request.typeUrl,
    responseType: response.type,
    responseCodec: response.codec,
    responseTypeUrl: response.typeUrl,
  };
}

function serviceTypeInfo(
  type: TypeDescriptor,
  state: GeneratorState,
  imports: ImportState,
): { readonly type: string; readonly view: string; readonly codec: string; readonly typeUrl: string } {
  if (type.kind !== TypeKind.DEFINED) {
    throw new CodegenError("service request and response types must be defined Bebop records");
  }
  const fqn = required(type.definedFqn, "service type missing fqn");
  const name = definedTypeName(fqn, state, imports);
  return {
    type: name,
    view: viewTypeName(type, imports),
    codec: name,
    typeUrl: `type.bebop.sh/${fqn}`,
  };
}

function runtimeMethodType(methodType: DescriptorMethodType): ServiceMethodInfo["runtimeMethodType"] {
  switch (methodType) {
    case DescriptorMethodType.UNARY: return "UNARY";
    case DescriptorMethodType.SERVER_STREAM: return "SERVER_STREAM";
    case DescriptorMethodType.CLIENT_STREAM: return "CLIENT_STREAM";
    case DescriptorMethodType.DUPLEX_STREAM: return "DUPLEX_STREAM";
    case DescriptorMethodType.UNKNOWN: throw new CodegenError("service method type must not be unknown");
    default: return assertNever(methodType);
  }
}

function registrationMethod(methodType: DescriptorMethodType): string {
  switch (methodType) {
    case DescriptorMethodType.UNARY: return "registerUnary";
    case DescriptorMethodType.SERVER_STREAM: return "registerServerStream";
    case DescriptorMethodType.CLIENT_STREAM: return "registerClientStream";
    case DescriptorMethodType.DUPLEX_STREAM: return "registerDuplexStream";
    case DescriptorMethodType.UNKNOWN: throw new CodegenError("service method type must not be unknown");
    default: return assertNever(methodType);
  }
}

function handlerSignature(method: ServiceMethodInfo): string {
  switch (method.methodType) {
    case DescriptorMethodType.UNARY:
      return `(request: ${method.requestView}, context: RpcContext): Awaitable<${method.responseType}>`;
    case DescriptorMethodType.SERVER_STREAM:
      return `(request: ${method.requestView}, context: RpcContext): Awaitable<StreamSource<${method.responseType}>>`;
    case DescriptorMethodType.CLIENT_STREAM:
      return `(requests: ReadableStream<${method.requestView}>, context: RpcContext): Awaitable<${method.responseType}>`;
    case DescriptorMethodType.DUPLEX_STREAM:
      return `(requests: ReadableStream<${method.requestView}>, context: RpcContext): Awaitable<StreamSource<${method.responseType}>>`;
    case DescriptorMethodType.UNKNOWN: throw new CodegenError("service method type must not be unknown");
    default: return assertNever(method.methodType);
  }
}

function clientSignature(method: ServiceMethodInfo): string {
  switch (method.methodType) {
    case DescriptorMethodType.UNARY:
      return `${method.name}(request: ${method.requestType}, context = new RpcContext()): Promise<RpcResponse<${method.responseType}, Metadata>>`;
    case DescriptorMethodType.SERVER_STREAM:
      return `${method.name}(request: ${method.requestType}, context = new RpcContext()): Promise<StreamResponse<${method.responseType}, Metadata>>`;
    case DescriptorMethodType.CLIENT_STREAM:
      return `${method.name}(requests: StreamSource<${method.requestType}>, context = new RpcContext()): Promise<RpcResponse<${method.responseType}, Metadata>>`;
    case DescriptorMethodType.DUPLEX_STREAM:
      return `${method.name}(requests: StreamSource<${method.requestType}>, context = new RpcContext()): Promise<StreamResponse<${method.responseType}, Metadata>>`;
    case DescriptorMethodType.UNKNOWN: throw new CodegenError("service method type must not be unknown");
    default: return assertNever(method.methodType);
  }
}

function clientHelper(methodType: DescriptorMethodType): string {
  switch (methodType) {
    case DescriptorMethodType.UNARY: return "unaryCall";
    case DescriptorMethodType.SERVER_STREAM: return "serverStreamCall";
    case DescriptorMethodType.CLIENT_STREAM: return "clientStreamCall";
    case DescriptorMethodType.DUPLEX_STREAM: return "duplexStreamCall";
    case DescriptorMethodType.UNKNOWN: throw new CodegenError("service method type must not be unknown");
    default: return assertNever(methodType);
  }
}

function recordFields(def: DefinitionDescriptor, kind: "struct" | "message"): readonly FieldDescriptor[] {
  if (kind === "struct") {
    return required(def.structDef, `struct ${def.name ?? ""} missing body`).fields ?? [];
  }
  return required(def.messageDef, `message ${def.name ?? ""} missing body`).fields ?? [];
}

function fieldInfo(
  field: FieldDescriptor,
  recordKind: "struct" | "message",
  state: GeneratorState,
  imports: ImportState,
): FieldInfo {
  const originalName = required(field.name, "field missing name");
  const name = fieldName(originalName);
  const type = required(field.type, `field ${originalName} missing type`);
  return {
    originalName,
    name,
    variable: `${identifier(name)}Value`,
    type,
    typeName: typeName(type, state, imports),
    index: recordKind === "message"
      ? required(field.index, `message field ${originalName} missing index`)
      : 0,
  };
}

function unionBranchInfo(
  branch: UnionBranchDescriptor,
  unionName: string,
  state: GeneratorState,
  imports: ImportState,
): {
  readonly originalName: string;
  readonly caseName: string;
  readonly typeName: string;
  readonly fqn: string;
  readonly discriminator: number;
} {
  const fqn = branch.typeRefFqn ?? branch.inlineFqn;
  if (fqn === undefined) {
    throw new CodegenError(`union ${unionName} branch missing type`);
  }
  const originalName = branch.name ?? fqn.slice(fqn.lastIndexOf(".") + 1);
  return {
    originalName,
    caseName: fieldName(originalName),
    typeName: definedTypeName(fqn, state, imports),
    fqn,
    discriminator: required(branch.discriminator, `union ${unionName} branch missing discriminator`),
  };
}

function typeName(type: TypeDescriptor, state: GeneratorState, imports: ImportState): string {
  switch (type.kind) {
    case TypeKind.BOOL: return "boolean";
    case TypeKind.BYTE: return "number";
    case TypeKind.INT8:
    case TypeKind.INT16:
    case TypeKind.UINT16:
    case TypeKind.INT32:
    case TypeKind.UINT32:
    case TypeKind.FLOAT16:
    case TypeKind.FLOAT32:
    case TypeKind.FLOAT64:
      return "number";
    case TypeKind.INT64:
    case TypeKind.UINT64:
    case TypeKind.INT128:
    case TypeKind.UINT128:
      return "bigint";
    case TypeKind.BFLOAT16:
      imports.runtimeValues.add("BFloat16");
      return "BFloat16";
    case TypeKind.STRING:
      return "string";
    case TypeKind.UUID:
      imports.runtimeValues.add("BebopUUID");
      return "BebopUUID";
    case TypeKind.TIMESTAMP:
      imports.runtimeValues.add("BebopTimestamp");
      return "BebopTimestamp";
    case TypeKind.DURATION:
      imports.runtimeValues.add("BebopDuration");
      return "BebopDuration";
    case TypeKind.ARRAY:
      return arrayType(required(type.arrayElement, "array missing element"), state, imports);
    case TypeKind.FIXED_ARRAY:
      return arrayType(required(type.fixedArrayElement, "fixed array missing element"), state, imports);
    case TypeKind.MAP:
      return `ReadonlyMap<${typeName(required(type.mapKey, "map missing key"), state, imports)}, ${typeName(required(type.mapValue, "map missing value"), state, imports)}>`;
    case TypeKind.DEFINED:
      return definedTypeName(required(type.definedFqn, "defined type missing fqn"), state, imports);
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
}

function viewTypeName(type: TypeDescriptor, imports: ImportState): string {
  switch (type.kind) {
    case TypeKind.STRING:
      imports.runtimeValues.add("BebopStringView");
      return "BebopStringView";
    case TypeKind.ARRAY:
    case TypeKind.FIXED_ARRAY: {
      imports.runtimeTypes.add("BebopArrayView");
      const element = type.kind === TypeKind.ARRAY
        ? required(type.arrayElement, "array missing element")
        : required(type.fixedArrayElement, "fixed array missing element");
      return `BebopArrayView<${viewTypeName(element, imports)}>`;
    }
    case TypeKind.MAP:
      imports.runtimeTypes.add("BebopMapView");
      return `BebopMapView<${viewTypeName(required(type.mapKey, "map missing key"), imports)}, ${viewTypeName(required(type.mapValue, "map missing value"), imports)}>`;
    case TypeKind.DEFINED: {
      const fqn = required(type.definedFqn, "defined type missing fqn");
      const info = definedInfo(fqn, imports.state, imports);
      if (info === undefined) {
        if (fqn === "bebop.Any") {
          imports.runtimeValues.add("BebopAnyView");
          return "BebopAnyView";
        }
        if (fqn === "bebop.Empty") {
          imports.runtimeValues.add("BebopEmptyView");
          return "BebopEmptyView";
        }
        throw new CodegenError(`unknown type ${fqn}`);
      }
      if (info.kind === DefinitionKind.ENUM) return info.typeName;
      const viewName = `${info.typeName}View`;
      if (info.schema.path !== imports.schema.path) {
        addSymbolImport(imports.localValues, `./${baseNameWithoutExtension(required(info.schema.path, "schema missing path"))}.bb.js`, viewName);
      }
      return viewName;
    }
    default:
      return typeName(type, imports.state, imports);
  }
}

function viewReadExpression(type: TypeDescriptor, reader: string, imports: ImportState): string {
  switch (type.kind) {
    case TypeKind.BOOL: return `${reader}.readBool()`;
    case TypeKind.BYTE: return `${reader}.readByte()`;
    case TypeKind.INT8: return `${reader}.readInt8()`;
    case TypeKind.INT16: return `${reader}.readInt16()`;
    case TypeKind.UINT16: return `${reader}.readUint16()`;
    case TypeKind.INT32: return `${reader}.readInt32()`;
    case TypeKind.UINT32: return `${reader}.readUint32()`;
    case TypeKind.INT64: return `${reader}.readInt64()`;
    case TypeKind.UINT64: return `${reader}.readUint64()`;
    case TypeKind.INT128: return `${reader}.readInt128()`;
    case TypeKind.UINT128: return `${reader}.readUint128()`;
    case TypeKind.FLOAT16: return `${reader}.readFloat16()`;
    case TypeKind.FLOAT32: return `${reader}.readFloat32()`;
    case TypeKind.FLOAT64: return `${reader}.readFloat64()`;
    case TypeKind.BFLOAT16: return `${reader}.readBFloat16()`;
    case TypeKind.STRING: return `${reader}.readStringView()`;
    case TypeKind.UUID: return `${reader}.readUUID()`;
    case TypeKind.TIMESTAMP: return `${reader}.readTimestamp()`;
    case TypeKind.DURATION: return `${reader}.readDuration()`;
    case TypeKind.ARRAY:
      return viewArrayReadExpression(
        required(type.arrayElement, "array missing element"),
        reader,
        undefined,
        imports,
      );
    case TypeKind.FIXED_ARRAY:
      return viewArrayReadExpression(
        required(type.fixedArrayElement, "fixed array missing element"),
        reader,
        required(type.fixedArraySize, "fixed array missing size"),
        imports,
      );
    case TypeKind.MAP:
      return `${reader}.readMapView((_r) => ${viewReadExpression(required(type.mapKey, "map missing key"), "_r", imports)}, (_r) => ${viewReadExpression(required(type.mapValue, "map missing value"), "_r", imports)}, (_r) => ${viewSkipExpression(required(type.mapKey, "map missing key"), "_r", imports)}, (_r) => ${viewSkipExpression(required(type.mapValue, "map missing value"), "_r", imports)})`;
    case TypeKind.DEFINED:
      return viewDefinedReadExpression(required(type.definedFqn, "defined type missing fqn"), reader, imports);
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
}

function viewArrayReadExpression(
  element: TypeDescriptor,
  reader: string,
  count: number | undefined,
  imports: ImportState,
): string {
  const method = count === undefined ? "readArrayView" : "readFixedArrayView";
  const countArgument = count === undefined ? "" : `${count}, `;
  const size = viewElementSize(element, imports);
  const sizeArgument = size === undefined ? "" : `, ${size}`;
  const skipArgument = size === undefined
    ? `, undefined, (_r) => ${viewSkipExpression(element, "_r", imports)}`
    : sizeArgument;
  return `${reader}.${method}(${countArgument}(_r) => ${viewReadExpression(element, "_r", imports)}${skipArgument})`;
}

function viewSkipExpression(type: TypeDescriptor, reader: string, imports: ImportState): string {
  const size = viewElementSize(type, imports);
  if (size !== undefined && type.kind !== TypeKind.DEFINED) return `${reader}.skip(${size})`;
  switch (type.kind) {
    case TypeKind.STRING: return `${reader}.skipString()`;
    case TypeKind.ARRAY: {
      const element = required(type.arrayElement, "array missing element");
      const elementSize = viewElementSize(element, imports);
      return `${reader}.skipArray((_r) => ${viewSkipExpression(element, "_r", imports)}${elementSize === undefined ? "" : `, ${elementSize}`})`;
    }
    case TypeKind.FIXED_ARRAY: {
      const element = required(type.fixedArrayElement, "fixed array missing element");
      const elementSize = viewElementSize(element, imports);
      return `${reader}.skipFixedArray(${required(type.fixedArraySize, "fixed array missing size")}, (_r) => ${viewSkipExpression(element, "_r", imports)}${elementSize === undefined ? "" : `, ${elementSize}`})`;
    }
    case TypeKind.MAP:
      return `${reader}.skipMap((_r) => ${viewSkipExpression(required(type.mapKey, "map missing key"), "_r", imports)}, (_r) => ${viewSkipExpression(required(type.mapValue, "map missing value"), "_r", imports)})`;
    case TypeKind.DEFINED: {
      const fqn = required(type.definedFqn, "defined type missing fqn");
      const info = definedInfo(fqn, imports.state, imports);
      if (info?.kind === DefinitionKind.ENUM) {
        return viewSkipExpression({ kind: required(info.enumBaseType, `enum ${info.typeName} missing base type`) }, reader, imports);
      }
      if (info === undefined) {
        const viewName = fqn === "bebop.Any" ? "BebopAnyView" : "BebopEmptyView";
        imports.runtimeValues.add(viewName);
        return `${reader}.skipNestedView(${viewName})`;
      }
      const viewName = `${info.typeName}View`;
      if (info.schema.path !== imports.schema.path) {
        addSymbolImport(imports.localValues, `./${baseNameWithoutExtension(required(info.schema.path, "schema missing path"))}.bb.js`, viewName);
      }
      return `${reader}.skipNestedView(${viewName})`;
    }
    default:
      return `${reader}.skip(${required(size, `type ${String(type.kind)} has no view skip`)})`;
  }
}

function viewElementSize(type: TypeDescriptor, imports: ImportState): number | undefined {
  const scalar = fixedScalarSize(type.kind);
  if (scalar !== undefined) return scalar;
  switch (type.kind) {
    case TypeKind.UUID: return 16;
    case TypeKind.TIMESTAMP: return 16;
    case TypeKind.DURATION: return 12;
    case TypeKind.DEFINED: {
      const info = definedInfo(required(type.definedFqn, "defined type missing fqn"), imports.state, imports);
      return info?.kind === DefinitionKind.ENUM
        ? fixedScalarSize(required(info.enumBaseType, `enum ${info.typeName} missing base type`))
        : undefined;
    }
    default: return undefined;
  }
}

function viewDefinedReadExpression(fqn: string, reader: string, imports: ImportState): string {
  const info = definedInfo(fqn, imports.state, imports);
  if (info === undefined) {
    if (fqn === "bebop.Any") {
      imports.runtimeValues.add("BebopAnyView");
      return `${reader}.readNestedView(BebopAnyView)`;
    }
    if (fqn === "bebop.Empty") {
      imports.runtimeValues.add("BebopEmptyView");
      return `${reader}.readNestedView(BebopEmptyView)`;
    }
    throw new CodegenError(`unknown type ${fqn}`);
  }
  if (info.kind === DefinitionKind.ENUM) {
    return `${viewReadExpression({ kind: required(info.enumBaseType, `enum ${info.typeName} missing base type`) }, reader, imports)} as ${info.typeName}`;
  }
  const viewName = `${info.typeName}View`;
  if (info.schema.path !== imports.schema.path) {
    addSymbolImport(imports.localValues, `./${baseNameWithoutExtension(required(info.schema.path, "schema missing path"))}.bb.js`, viewName);
  }
  return `${reader}.readNestedView(${viewName})`;
}

function arrayType(element: TypeDescriptor, state: GeneratorState, imports: ImportState): string {
  switch (element.kind) {
    case TypeKind.BYTE: return "Uint8Array";
    case TypeKind.INT8: return "Int8Array";
    case TypeKind.INT16: return "Int16Array";
    case TypeKind.UINT16: return "Uint16Array";
    case TypeKind.INT32: return "Int32Array";
    case TypeKind.UINT32: return "Uint32Array";
    case TypeKind.INT64: return "BigInt64Array";
    case TypeKind.UINT64: return "BigUint64Array";
    case TypeKind.FLOAT16: return "Float16Array";
    case TypeKind.FLOAT32: return "Float32Array";
    case TypeKind.FLOAT64: return "Float64Array";
    case TypeKind.BFLOAT16:
      imports.runtimeValues.add("BFloat16Array");
      return "BFloat16Array";
    default: {
      const elementType = typeName(element, state, imports);
      return elementType.startsWith("readonly ")
        ? `readonly (${elementType})[]`
        : `readonly ${elementType}[]`;
    }
  }
}

function definedTypeName(fqn: string, state: GeneratorState, imports: ImportState): string {
  const info = state.definitions.get(fqn);
  if (info === undefined) {
    if (fqn === "bebop.Any") {
      imports.runtimeValues.add("BebopAny");
      return "BebopAny";
    }
    if (fqn === "bebop.Empty") {
      imports.runtimeValues.add("BebopEmpty");
      return "BebopEmpty";
    }
    throw new CodegenError(`unknown type ${fqn}`);
  }
  if (info.schema.path !== imports.schema.path) {
    addSymbolImport(imports.localValues, `./${baseNameWithoutExtension(required(info.schema.path, "schema missing path"))}.bb.js`, info.typeName);
  }
  return info.typeName;
}

function definedInfo(fqn: string, state: GeneratorState, imports: ImportState): DefinitionInfo | undefined {
  const info = state.definitions.get(fqn);
  if (info === undefined) {
    return undefined;
  }
  if (info.schema.path !== imports.schema.path) {
    addSymbolImport(imports.localValues, `./${baseNameWithoutExtension(required(info.schema.path, "schema missing path"))}.bb.js`, info.typeName);
  }
  return info;
}

function readExpression(type: TypeDescriptor, reader: string, imports: ImportState): string {
  switch (type.kind) {
    case TypeKind.BOOL: return `${reader}.readBool()`;
    case TypeKind.BYTE: return `${reader}.readByte()`;
    case TypeKind.INT8: return `${reader}.readInt8()`;
    case TypeKind.INT16: return `${reader}.readInt16()`;
    case TypeKind.UINT16: return `${reader}.readUint16()`;
    case TypeKind.INT32: return `${reader}.readInt32()`;
    case TypeKind.UINT32: return `${reader}.readUint32()`;
    case TypeKind.INT64: return `${reader}.readInt64()`;
    case TypeKind.UINT64: return `${reader}.readUint64()`;
    case TypeKind.INT128: return `${reader}.readInt128()`;
    case TypeKind.UINT128: return `${reader}.readUint128()`;
    case TypeKind.FLOAT16: return `${reader}.readFloat16()`;
    case TypeKind.FLOAT32: return `${reader}.readFloat32()`;
    case TypeKind.FLOAT64: return `${reader}.readFloat64()`;
    case TypeKind.BFLOAT16: return `${reader}.readBFloat16()`;
    case TypeKind.STRING: return `${reader}.readString()`;
    case TypeKind.UUID:
      imports.runtimeValues.add("BebopUUID");
      return `BebopUUID.readFrom(${reader})`;
    case TypeKind.TIMESTAMP: return `${reader}.readTimestamp()`;
    case TypeKind.DURATION: return `${reader}.readDuration()`;
    case TypeKind.ARRAY:
      return readArrayExpression(required(type.arrayElement, "array missing element"), reader, imports);
    case TypeKind.FIXED_ARRAY:
      return readFixedArrayExpression(
        required(type.fixedArrayElement, "fixed array missing element"),
        required(type.fixedArraySize, "fixed array missing size"),
        reader,
        imports,
      );
    case TypeKind.MAP:
      return `${reader}.readDynamicMap((_r) => ${readExpression(required(type.mapKey, "map missing key"), "_r", imports)}, (_r) => ${readExpression(required(type.mapValue, "map missing value"), "_r", imports)})`;
    case TypeKind.DEFINED:
      return readDefinedExpression(required(type.definedFqn, "defined type missing fqn"), reader, imports);
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
}

function readDefinedExpression(fqn: string, reader: string, imports: ImportState): string {
  const info = definedInfo(fqn, imports.state, imports);
  if (info === undefined) {
    if (fqn === "bebop.Any") {
      imports.runtimeValues.add("BebopAny");
      return `${reader}.readNested(BebopAny)`;
    }
    if (fqn === "bebop.Empty") {
      imports.runtimeValues.add("BebopEmpty");
      return `${reader}.readNested(BebopEmpty)`;
    }
    throw new CodegenError(`unknown type ${fqn}`);
  }
  if (info.kind === DefinitionKind.ENUM) {
    return `${readExpression({ kind: required(info.enumBaseType, `enum ${info.typeName} missing base type`) }, reader, imports)} as ${info.typeName}`;
  }
  return `${reader}.readNested(${info.typeName})`;
}

function readArrayExpression(element: TypeDescriptor, reader: string, imports: ImportState): string {
  switch (element.kind) {
    case TypeKind.BYTE: return `${reader}.readUint8Array()`;
    case TypeKind.INT8: return `${reader}.readInt8Array()`;
    case TypeKind.INT16: return `${reader}.readInt16Array()`;
    case TypeKind.UINT16: return `${reader}.readUint16Array()`;
    case TypeKind.INT32: return `${reader}.readInt32Array()`;
    case TypeKind.UINT32: return `${reader}.readUint32Array()`;
    case TypeKind.INT64: return `${reader}.readBigInt64Array()`;
    case TypeKind.UINT64: return `${reader}.readBigUint64Array()`;
    case TypeKind.FLOAT16: return `${reader}.readFloat16Array()`;
    case TypeKind.FLOAT32: return `${reader}.readFloat32Array()`;
    case TypeKind.FLOAT64: return `${reader}.readFloat64Array()`;
    case TypeKind.BFLOAT16: return `${reader}.readBFloat16Array()`;
    default:
      return `${reader}.readDynamicArray((_r) => ${readExpression(element, "_r", imports)})`;
  }
}

function readFixedArrayExpression(
  element: TypeDescriptor,
  length: number,
  reader: string,
  imports: ImportState,
): string {
  switch (element.kind) {
    case TypeKind.BYTE: return `${reader}.readUint8Array(${length})`;
    case TypeKind.INT8: return `${reader}.readInt8Array(${length})`;
    case TypeKind.INT16: return `${reader}.readInt16Array(${length})`;
    case TypeKind.UINT16: return `${reader}.readUint16Array(${length})`;
    case TypeKind.INT32: return `${reader}.readInt32Array(${length})`;
    case TypeKind.UINT32: return `${reader}.readUint32Array(${length})`;
    case TypeKind.INT64: return `${reader}.readBigInt64Array(${length})`;
    case TypeKind.UINT64: return `${reader}.readBigUint64Array(${length})`;
    case TypeKind.FLOAT16: return `${reader}.readFloat16Array(${length})`;
    case TypeKind.FLOAT32: return `${reader}.readFloat32Array(${length})`;
    case TypeKind.FLOAT64: return `${reader}.readFloat64Array(${length})`;
    case TypeKind.BFLOAT16: return `${reader}.readBFloat16Array(${length})`;
    default:
      return `${reader}.readFixedArray(${length}, (_r) => ${readExpression(element, "_r", imports)})`;
  }
}

function writeStatement(
  out: IndentedStringBuilder,
  type: TypeDescriptor,
  writer: string,
  value: string,
  imports: ImportState,
): void {
  switch (type.kind) {
    case TypeKind.BOOL: out.line(`${writer}.writeBool(${value});`); return;
    case TypeKind.BYTE: out.line(`${writer}.writeByte(${value});`); return;
    case TypeKind.INT8: out.line(`${writer}.writeInt8(${value});`); return;
    case TypeKind.INT16: out.line(`${writer}.writeInt16(${value});`); return;
    case TypeKind.UINT16: out.line(`${writer}.writeUint16(${value});`); return;
    case TypeKind.INT32: out.line(`${writer}.writeInt32(${value});`); return;
    case TypeKind.UINT32: out.line(`${writer}.writeUint32(${value});`); return;
    case TypeKind.INT64: out.line(`${writer}.writeInt64(${value});`); return;
    case TypeKind.UINT64: out.line(`${writer}.writeUint64(${value});`); return;
    case TypeKind.INT128: out.line(`${writer}.writeInt128(${value});`); return;
    case TypeKind.UINT128: out.line(`${writer}.writeUint128(${value});`); return;
    case TypeKind.FLOAT16: out.line(`${writer}.writeFloat16(${value});`); return;
    case TypeKind.FLOAT32: out.line(`${writer}.writeFloat32(${value});`); return;
    case TypeKind.FLOAT64: out.line(`${writer}.writeFloat64(${value});`); return;
    case TypeKind.BFLOAT16: out.line(`${writer}.writeBFloat16(${value});`); return;
    case TypeKind.STRING: out.line(`${writer}.writeString(${value});`); return;
    case TypeKind.UUID:
      imports.runtimeValues.add("BebopUUID");
      out.line(`BebopUUID.writeInto(${writer}, ${value});`);
      return;
    case TypeKind.TIMESTAMP: out.line(`${writer}.writeTimestamp(${value});`); return;
    case TypeKind.DURATION: out.line(`${writer}.writeDuration(${value});`); return;
    case TypeKind.ARRAY:
      writeArrayStatement(out, required(type.arrayElement, "array missing element"), writer, value, imports);
      return;
    case TypeKind.FIXED_ARRAY:
      writeFixedArrayStatement(
        out,
        required(type.fixedArrayElement, "fixed array missing element"),
        required(type.fixedArraySize, "fixed array missing size"),
        writer,
        value,
        imports,
      );
      return;
    case TypeKind.MAP:
      out.block(`${writer}.writeDynamicMap(${value}, (_w, _k, _v) =>`, (body) => {
        writeStatement(body, required(type.mapKey, "map missing key"), "_w", "_k", imports);
        writeStatement(body, required(type.mapValue, "map missing value"), "_w", "_v", imports);
      }, "});");
      return;
    case TypeKind.DEFINED:
      writeDefinedStatement(out, required(type.definedFqn, "defined type missing fqn"), writer, value, imports);
      return;
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
}

function writeDefinedStatement(
  out: IndentedStringBuilder,
  fqn: string,
  writer: string,
  value: string,
  imports: ImportState,
): void {
  const info = definedInfo(fqn, imports.state, imports);
  if (info === undefined) {
    if (fqn === "bebop.Any") {
      imports.runtimeValues.add("BebopAny");
      out.line(`BebopAny.writeInto(${writer}, ${value});`);
      return;
    }
    if (fqn === "bebop.Empty") {
      imports.runtimeValues.add("BebopEmpty");
      out.line(`BebopEmpty.writeInto(${writer}, ${value});`);
      return;
    }
    throw new CodegenError(`unknown type ${fqn}`);
  }
  if (info.kind === DefinitionKind.ENUM) {
    writeStatement(out, { kind: required(info.enumBaseType, `enum ${info.typeName} missing base type`) }, writer, value, imports);
    return;
  }
  out.line(`${info.typeName}.writeInto(${writer}, ${value});`);
}

function writeArrayStatement(
  out: IndentedStringBuilder,
  element: TypeDescriptor,
  writer: string,
  value: string,
  imports: ImportState,
): void {
  const method = arrayWriteMethod(element.kind);
  if (method !== undefined) {
    out.line(`${writer}.${method}(${value});`);
    return;
  }
  out.block(`${writer}.writeDynamicArray(${value}, (_w, _v) =>`, (body) => {
    writeStatement(body, element, "_w", "_v", imports);
  }, "});");
}

function writeFixedArrayStatement(
  out: IndentedStringBuilder,
  element: TypeDescriptor,
  length: number,
  writer: string,
  value: string,
  imports: ImportState,
): void {
  const method = arrayWriteMethod(element.kind);
  if (method !== undefined) {
    out.line(`${writer}.${method}(${value}, ${length});`);
    return;
  }
  imports.runtimeValues.add("BebopRuntimeError");
  out.line(`if (${value}.length !== ${length}) throw new BebopRuntimeError(\`expected fixed length ${length}, got \${${value}.length}\`);`);
  const index = temporary(imports, "i");
  const item = temporary(imports, "item");
  out.block(`for (let ${index} = 0; ${index} < ${value}.length; ${index}++)`, (body) => {
    body.line(`const ${item} = ${value}[${index}]!;`);
    writeStatement(body, element, writer, item, imports);
  });
}

function arrayWriteMethod(kind: TypeKind | undefined): string | undefined {
  switch (kind) {
    case TypeKind.BYTE: return "writeUint8Array";
    case TypeKind.INT8: return "writeInt8Array";
    case TypeKind.INT16: return "writeInt16Array";
    case TypeKind.UINT16: return "writeUint16Array";
    case TypeKind.INT32: return "writeInt32Array";
    case TypeKind.UINT32: return "writeUint32Array";
    case TypeKind.INT64: return "writeBigInt64Array";
    case TypeKind.UINT64: return "writeBigUint64Array";
    case TypeKind.FLOAT16: return "writeFloat16Array";
    case TypeKind.FLOAT32: return "writeFloat32Array";
    case TypeKind.FLOAT64: return "writeFloat64Array";
    case TypeKind.BFLOAT16: return "writeBFloat16Array";
    default: return undefined;
  }
}

function writeAddSizeStatements(
  out: IndentedStringBuilder,
  type: TypeDescriptor,
  value: string,
  imports: ImportState,
): void {
  const scalar = fixedScalarSize(type.kind);
  if (scalar !== undefined) {
    out.line(`size += ${scalar};`);
    return;
  }
  switch (type.kind) {
    case TypeKind.STRING:
      imports.runtimeValues.add("utf8ByteLength");
      out.line(`size += 5 + utf8ByteLength(${value});`);
      return;
    case TypeKind.UUID:
      out.line("size += 16;");
      return;
    case TypeKind.TIMESTAMP:
      out.line("size += 16;");
      return;
    case TypeKind.DURATION:
      out.line("size += 12;");
      return;
    case TypeKind.ARRAY:
      writeAddArraySizeStatements(out, required(type.arrayElement, "array missing element"), value, imports);
      return;
    case TypeKind.FIXED_ARRAY:
      writeAddFixedArraySizeStatements(
        out,
        required(type.fixedArrayElement, "fixed array missing element"),
        required(type.fixedArraySize, "fixed array missing size"),
        value,
        imports,
      );
      return;
    case TypeKind.MAP: {
      out.line("size += 4;");
      const key = temporary(imports, "key");
      const mapValue = temporary(imports, "value");
      out.block(`for (const [${key}, ${mapValue}] of ${value})`, (body) => {
        writeAddSizeStatements(body, required(type.mapKey, "map missing key"), key, imports);
        writeAddSizeStatements(body, required(type.mapValue, "map missing value"), mapValue, imports);
      });
      return;
    }
    case TypeKind.DEFINED:
      writeAddDefinedSizeStatement(out, required(type.definedFqn, "defined type missing fqn"), value, imports);
      return;
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
}

function writeAddDefinedSizeStatement(
  out: IndentedStringBuilder,
  fqn: string,
  value: string,
  imports: ImportState,
): void {
  const info = definedInfo(fqn, imports.state, imports);
  if (info === undefined) {
    if (fqn === "bebop.Any") {
      imports.runtimeValues.add("BebopAny");
      out.line(`size += BebopAny.encodedSize(${value});`);
      return;
    }
    if (fqn === "bebop.Empty") {
      imports.runtimeValues.add("BebopEmpty");
      out.line(`size += BebopEmpty.encodedSize(${value});`);
      return;
    }
    throw new CodegenError(`unknown type ${fqn}`);
  }
  if (info.kind === DefinitionKind.ENUM) {
    out.line(`size += ${fixedScalarSize(required(info.enumBaseType, `enum ${info.typeName} missing base type`))};`);
    return;
  }
  out.line(`size += ${info.typeName}.encodedSize(${value});`);
}

function writeAddArraySizeStatements(
  out: IndentedStringBuilder,
  element: TypeDescriptor,
  value: string,
  imports: ImportState,
): void {
  const scalar = fixedScalarSize(element.kind);
  if (element.kind === TypeKind.BYTE) {
    out.line(`size += 4 + ${value}.length;`);
    return;
  }
  if (scalar !== undefined) {
    out.line(`size += 4 + ${value}.length * ${scalar};`);
    return;
  }
  out.line("size += 4;");
  const index = temporary(imports, "i");
  const item = temporary(imports, "item");
  out.block(`for (let ${index} = 0; ${index} < ${value}.length; ${index}++)`, (body) => {
    body.line(`const ${item} = ${value}[${index}]!;`);
    writeAddSizeStatements(body, element, item, imports);
  });
}

function writeAddFixedArraySizeStatements(
  out: IndentedStringBuilder,
  element: TypeDescriptor,
  length: number,
  value: string,
  imports: ImportState,
): void {
  const scalar = fixedScalarSize(element.kind);
  imports.runtimeValues.add("BebopRuntimeError");
  out.line(`if (${value}.length !== ${length}) throw new BebopRuntimeError(\`expected fixed length ${length}, got \${${value}.length}\`);`);
  if (scalar !== undefined) {
    out.line(`size += ${length * scalar};`);
    return;
  }
  const index = temporary(imports, "i");
  const item = temporary(imports, "item");
  out.block(`for (let ${index} = 0; ${index} < ${value}.length; ${index}++)`, (body) => {
    body.line(`const ${item} = ${value}[${index}]!;`);
    writeAddSizeStatements(body, element, item, imports);
  });
}

function fixedScalarSize(kind: TypeKind | undefined): number | undefined {
  switch (kind) {
    case TypeKind.BOOL:
    case TypeKind.BYTE:
    case TypeKind.INT8:
      return 1;
    case TypeKind.INT16:
    case TypeKind.UINT16:
    case TypeKind.FLOAT16:
    case TypeKind.BFLOAT16:
      return 2;
    case TypeKind.INT32:
    case TypeKind.UINT32:
    case TypeKind.FLOAT32:
      return 4;
    case TypeKind.INT64:
    case TypeKind.UINT64:
    case TypeKind.FLOAT64:
      return 8;
    case TypeKind.INT128:
    case TypeKind.UINT128:
    case TypeKind.UUID:
    case TypeKind.TIMESTAMP:
      return 16;
    case TypeKind.DURATION:
      return 12;
    default:
      return undefined;
  }
}

function encodedSizeUsesValue(type: TypeDescriptor, imports: ImportState): boolean {
  if (fixedScalarSize(type.kind) !== undefined) return false;
  switch (type.kind) {
    case TypeKind.DEFINED: {
      const info = imports.state.definitions.get(required(type.definedFqn, "defined type missing fqn"));
      return info?.kind !== DefinitionKind.ENUM;
    }
    default:
      return true;
  }
}

function enumValueType(kind: TypeKind): string {
  switch (kind) {
    case TypeKind.INT64:
    case TypeKind.UINT64:
      return "bigint";
    default:
      return "number";
  }
}

function enumTypeExpression(name: string, baseType: TypeKind, isFlags: boolean): string {
  return isFlags ? enumValueType(baseType) : `(typeof ${name})[keyof typeof ${name}]`;
}

function enumMemberLiteral(value: bigint, kind: TypeKind): string {
  switch (kind) {
    case TypeKind.INT8: return String(Number(BigInt.asIntN(8, value)));
    case TypeKind.INT16: return String(Number(BigInt.asIntN(16, value)));
    case TypeKind.INT32: return String(Number(BigInt.asIntN(32, value)));
    case TypeKind.INT64: return `${BigInt.asIntN(64, value)}n`;
    case TypeKind.UINT64: return `${BigInt.asUintN(64, value)}n`;
    default: return String(Number(value));
  }
}

function literalExpression(value: LiteralValue, type: TypeDescriptor): string {
  switch (value.kind) {
    case LiteralKind.BOOL:
      return value.boolValue === true ? "true" : "false";
    case LiteralKind.INT:
      return integerLiteral(required(value.intValue, "int literal missing value"), type.kind);
    case LiteralKind.FLOAT:
      return numberLiteral(required(value.floatValue, "float literal missing value"));
    case LiteralKind.STRING:
      return stringLiteral(required(value.stringValue, "string literal missing value"));
    case LiteralKind.UUID:
      return stringLiteral(required(value.uuidValue, "uuid literal missing value"));
    case LiteralKind.BYTES:
      return `new Uint8Array([${[...(value.bytesValue ?? [])].join(", ")}])`;
    case LiteralKind.TIMESTAMP: {
      const v = required(value.timestampValue, "timestamp literal missing value");
      return `Temporal.ZonedDateTime.from(${stringLiteral(v.toString())})`;
    }
    case LiteralKind.DURATION: {
      const v = required(value.durationValue, "duration literal missing value");
      return `Temporal.Duration.from(${stringLiteral(v.toString())})`;
    }
    default:
      throw new CodegenError(`unsupported literal kind ${String(value.kind)}`);
  }
}

function integerLiteral(value: bigint, kind: TypeKind | undefined): string {
  switch (kind) {
    case TypeKind.INT64:
    case TypeKind.UINT64:
    case TypeKind.INT128:
    case TypeKind.UINT128:
      return `${value}n`;
    default:
      return String(Number(value));
  }
}

function writeImports(out: IndentedStringBuilder, imports: ImportState): void {
  const runtimeValueImports = groupRuntimeImports(imports.runtimeValues, imports.runtimeImport);
  const runtimeTypeImports = groupRuntimeImports(imports.runtimeTypes, imports.runtimeImport);
  const runtimeModules = new Set([...runtimeValueImports.keys(), ...runtimeTypeImports.keys()]);
  for (const module of [...runtimeModules].sort(compareString)) {
    const values = [...(runtimeValueImports.get(module) ?? [])].sort(compareString);
    const valueSet = new Set(values);
    const types = [...(runtimeTypeImports.get(module) ?? [])]
      .filter((symbol) => !valueSet.has(symbol))
      .sort(compareString);
    if (values.length === 0) {
      out.line(`import type { ${types.join(", ")} } from ${stringLiteral(module)};`);
    } else {
      const specifiers = [...values, ...types.map((symbol) => `type ${symbol}`)];
      out.line(`import { ${specifiers.join(", ")} } from ${stringLiteral(module)};`);
    }
  }
  for (const [module, symbols] of [...imports.localValues.entries()].sort(([a], [b]) => compareString(a, b))) {
    out.line(`import { ${[...symbols].sort().join(", ")} } from ${stringLiteral(module)};`);
  }
  if (runtimeValueImports.size > 0 || runtimeTypeImports.size > 0 || imports.localValues.size > 0) {
    out.line();
  }
}

function groupRuntimeImports(symbols: ReadonlySet<string>, runtimeImport: RuntimeImport): Map<string, Set<string>> {
  const grouped = new Map<string, Set<string>>();
  for (const symbol of symbols) {
    addSymbolImport(grouped, runtimeModuleForSymbol(symbol, runtimeImport), symbol);
  }
  return grouped;
}

const runtimeModuleSymbols = {
  any: ["BEBOP_TYPE_URL_PREFIX", "BebopAny", "BebopAnyView"],
  bfloat16: ["BFloat16", "BFloat16Array"],
  codec: ["decode", "encode"],
  empty: ["BebopEmpty", "BebopEmptyView"],
  error: ["BebopRuntimeError"],
  message: ["BebopMessageView", "messageEncodedSize"],
  reflection: ["BebopDefinitionKind", "BebopGeneratedCodec", "BebopReflectableCodec", "BebopTypeReflection"],
  "rpc/batch": ["Batch", "BatchResults", "CallRef", "StreamRef"],
  "rpc/channel": [
    "BebopChannel", "ClientStreamCall", "DuplexStreamCall", "RpcResponse", "StreamResponse",
    "clientStreamCall", "duplexStreamCall", "serverStreamCall", "unaryCall",
  ],
  "rpc/context": ["RpcContext"],
  "rpc/error": ["RpcMetadata"],
  "rpc/future": ["BebopFuture", "DispatchOptions", "FutureDispatcher"],
  "rpc/protocol": ["MethodType"],
  "rpc/service": ["Awaitable", "StreamSource", "defineService"],
  temporal: ["BebopDuration", "BebopTimestamp"],
  uuid: ["BebopUUID"],
  view: ["BebopArrayView", "BebopMapView", "BebopStringView", "BebopViewReader", "readMessageField"],
  wire: ["BebopReader", "BebopReaderOptions", "BebopWriter", "utf8ByteLength"],
} as const satisfies Readonly<Record<string, readonly string[]>>;

const runtimeModuleBySymbol = new Map<string, string>();
for (const [module, symbols] of Object.entries(runtimeModuleSymbols)) {
  for (const symbol of symbols) runtimeModuleBySymbol.set(symbol, module);
}

function runtimeModuleForSymbol(symbol: string, runtimeImport: RuntimeImport): string {
  if (runtimeImport.kind === "module" && runtimeImport.module !== "@bebop/runtime") {
    return runtimeImport.module;
  }
  const module = runtimeModuleBySymbol.get(symbol);
  if (module === undefined) throw new CodegenError(`unknown runtime symbol ${symbol}`);
  if (runtimeImport.kind === "module") return `@bebop/runtime/${module}`;
  return module === "rpc/protocol" ? "./rpc.bb.js" : `./${module}.js`;
}

function addSymbolImport(imports: Map<string, Set<string>>, module: string, symbol: string): void {
  const existing = imports.get(module);
  if (existing !== undefined) {
    existing.add(symbol);
    return;
  }
  imports.set(module, new Set([symbol]));
}

function outputFileName(schema: SchemaDescriptor): string {
  return `${baseNameWithoutExtension(required(schema.path, "schema missing path"))}.bb.ts`;
}

function exportedTypeName(def: DefinitionDescriptor, schema: SchemaDescriptor): string {
  const name = required(def.name, "definition missing name");
  const fqn = def.fqn;
  const pkg = schema.package;
  if (fqn !== undefined && pkg !== undefined && fqn.startsWith(`${pkg}.`)) {
    const local = fqn.slice(pkg.length + 1);
    return local.split(".").map(identifier).join("_");
  }
  return identifier(name);
}

function fieldName(value: string): string {
  const normalized = /^[A-Z0-9_]+$/u.test(value) ? value.toLowerCase() : value;
  const camel = normalized.replace(/_([a-zA-Z0-9])/gu, (_, char: string) => char.toUpperCase());
  return camel.length === 0 ? camel : camel[0]!.toLowerCase() + camel.slice(1);
}

function serviceMethodName(value: string): string {
  const name = identifier(fieldName(value));
  return reservedClientMembers.has(name) ? `${name}_` : name;
}

function identifier(value: string): string {
  const normalized = value.replace(/[^A-Za-z0-9_$]/gu, "_");
  const withStart = /^[$A-Z_a-z]/u.test(normalized) ? normalized : `_${normalized}`;
  return reservedIdentifiers.has(withStart) ? `${withStart}_` : withStart;
}

function memberAccess(receiver: string, name: string): string {
  return isIdentifier(name) ? `${receiver}.${name}` : `${receiver}[${stringLiteral(name)}]`;
}

function propertyKey(name: string): string {
  return isIdentifier(name) ? name : stringLiteral(name);
}

function isIdentifier(value: string): boolean {
  return /^[$A-Z_a-z][$\w]*$/u.test(value);
}

function stringLiteral(value: string): string {
  return JSON.stringify(value).replaceAll("\u2028", "\\u2028").replaceAll("\u2029", "\\u2029");
}

function numberLiteral(value: number): string {
  if (Number.isNaN(value)) return "Number.NaN";
  if (value === Number.POSITIVE_INFINITY) return "Number.POSITIVE_INFINITY";
  if (value === Number.NEGATIVE_INFINITY) return "Number.NEGATIVE_INFINITY";
  if (Object.is(value, -0)) return "-0";
  return String(value);
}

function baseNameWithoutExtension(path: string): string {
  const last = path.split(/[\\/]/u).at(-1) ?? path;
  const dot = last.lastIndexOf(".");
  return dot < 0 ? last : last.slice(0, dot);
}

function temporary(imports: ImportState, prefix: string): string {
  return `_${prefix}${imports.nextTemporary++}`;
}

function required<T>(value: T | undefined, message: string): T {
  if (value === undefined) {
    throw new CodegenError(message);
  }
  return value;
}

function hexUint32(value: number): string {
  return `0x${value.toString(16).padStart(8, "0").toUpperCase()}`;
}

function assertNever(value: never): never {
  throw new CodegenError(`unexpected value ${String(value)}`);
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function compareString(left: string, right: string): number {
  return left < right ? -1 : left > right ? 1 : 0;
}

function requireUniqueNames(names: readonly string[], context: string): void {
  const seen = new Set<string>();
  for (const name of names) {
    if (seen.has(name)) {
      throw new CodegenError(`${context} name collision after TypeScript normalization: ${name}`);
    }
    seen.add(name);
  }
}

class CodegenError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "CodegenError";
  }
}
