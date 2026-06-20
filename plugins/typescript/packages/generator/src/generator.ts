import {
  CodeGeneratorRequest,
  CodeGeneratorResponse,
  DefinitionKind,
  TypeKind,
  type DefinitionDescriptor,
  type EnumMemberDescriptor,
  type FieldDescriptor,
  type GeneratedFile,
  type LiteralValue,
  type SchemaDescriptor,
  type TypeDescriptor,
  type UnionBranchDescriptor,
  LiteralKind,
} from "@bebop/plugin";
import { decode, encode } from "@bebop/runtime";
import { IndentedStringBuilder } from "./indented-string-builder";

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

type ImportState = {
  readonly runtimeImport: RuntimeImport;
  readonly runtimeValues: Set<string>;
  readonly runtimeTypes: Set<string>;
  readonly localValues: Map<string, Set<string>>;
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
  "let",
  "static",
  "implements",
  "interface",
  "private",
  "protected",
  "public",
]);

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
  const files: GeneratedFile[] = [];
  for (const schema of schemas) {
    if (schema.path === undefined || !fileSet.has(schema.path)) {
      continue;
    }
    files.push({
      name: outputFileName(schema),
      content: generateSchema(schema, state, options, request.compilerVersion),
    });
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
  if (def.fqn !== undefined) {
    const typeName = exportedTypeName(def, schema);
    const kind = required(def.kind, `definition ${typeName} missing kind`);
    if (kind === DefinitionKind.ENUM) {
      const enumBaseType = required(required(def.enumDef, `enum ${typeName} missing body`).baseType, `enum ${typeName} missing base type`);
      definitions.set(def.fqn, { schema, typeName, kind, enumBaseType });
    } else {
      definitions.set(def.fqn, { schema, typeName, kind });
    }
  }
  for (const child of def.nested ?? []) {
    collectDefinition(child, schema, definitions);
  }
}

function generateSchema(
  schema: SchemaDescriptor,
  state: GeneratorState,
  options: GeneratorOptions,
  compilerVersion: { readonly major: number; readonly minor: number; readonly patch: number; readonly suffix: string } | undefined,
): string {
  const imports: ImportState = {
    runtimeImport: options.runtimeImport,
    runtimeValues: new Set(),
    runtimeTypes: new Set(["BebopReader", "BebopWriter", "BebopReflectableCodec"]),
    localValues: new Map(),
  };
  const declarations: string[] = [];
  const previousState = activeState;
  const previousSchema = activeSchema;
  activeState = state;
  activeSchema = schema;
  try {
    for (const def of schema.definitions ?? []) {
      declarations.push(...generateDefinition(def, schema, state, imports));
    }
  } finally {
    activeState = previousState;
    activeSchema = previousSchema;
  }

  const out = new IndentedStringBuilder();
  out.line("// Code generated by bebopc-gen-typescript. DO NOT EDIT.");
  out.line(`// source: ${schema.path ?? "unknown"}`);
  if (compilerVersion !== undefined) {
    const suffix = compilerVersion.suffix.length === 0 ? "" : `-${compilerVersion.suffix}`;
    out.line(`// bebopc ${compilerVersion.major}.${compilerVersion.minor}.${compilerVersion.patch}${suffix}`);
  }
  out.line();
  writeImports(out, imports);
  for (const declaration of declarations) {
    out.line();
    out.append(declaration);
    out.line();
  }
  out.line();
  return out.toString();
}

function generateDefinition(
  def: DefinitionDescriptor,
  schema: SchemaDescriptor,
  state: GeneratorState,
  imports: ImportState,
): string[] {
  const nested = (def.nested ?? []).flatMap((child) => generateDefinition(child, schema, state, imports));
  switch (def.kind) {
    case DefinitionKind.ENUM:
      return [...nested, generateEnum(def, schema, imports)];
    case DefinitionKind.STRUCT:
      return [...nested, generateRecord(def, schema, state, imports, "struct")];
    case DefinitionKind.MESSAGE:
      return [...nested, generateRecord(def, schema, state, imports, "message")];
    case DefinitionKind.UNION:
      return [...nested, generateUnion(def, schema, state, imports)];
    case DefinitionKind.CONST:
      return [...nested, generateConst(def, state, imports)];
    case DefinitionKind.SERVICE:
      return [...nested, generateService(def, state, imports)];
    case DefinitionKind.DECORATOR:
      return nested;
    default:
      throw new CodegenError(`unknown definition kind ${String(def.kind)}`);
  }
}

function generateEnum(def: DefinitionDescriptor, schema: SchemaDescriptor, _imports: ImportState): string {
  const name = exportedTypeName(def, schema);
  const enumDef = required(def.enumDef, `enum ${name} missing body`);
  const baseType = required(enumDef.baseType, `enum ${name} missing base type`);
  const members = enumDef.members ?? [];

  const out = new IndentedStringBuilder();
  out.block(`export const ${name} =`, (b) => {
    for (const member of members) {
      b.line(`${enumMemberName(member)}: ${enumMemberLiteral(required(member.value, `enum ${name} member missing value`), baseType)},`);
    }
  }, "} as const;");
  out.line(`export type ${name} = ${enumTypeExpression(name, baseType, enumDef.isFlags === true)};`);
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
  const fields = recordFields(def, kind).map((field) => fieldInfo(field, state, imports));
  const out = new IndentedStringBuilder();
  out.block(`export type ${name} =`, (type) => {
    for (const field of fields) {
      const optional = kind === "message" ? "?" : "";
      const undefinedType = kind === "message" ? " | undefined" : "";
      type.line(`readonly ${propertyKey(field.name)}${optional}: ${field.typeName}${undefinedType};`);
    }
  }, "};");
  out.line();
  out.block(`export const ${name} =`, (codec) => {
    if (kind === "struct") {
      writeStructRead(codec, name, fields, imports);
      writeStructWrite(codec, name, fields, imports);
      writeStructSize(codec, name, fields, imports);
    } else {
      writeMessageRead(codec, name, fields, imports);
      writeMessageWrite(codec, name, fields, imports);
      writeMessageSize(codec, name, fields, imports);
    }
    writeReflection(codec, def, name, kind, fields, imports);
  }, `} satisfies BebopReflectableCodec<${name}>;`);
  return out.trimEnd().toString();
}

function writeStructRead(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  codec.block(`readFrom(reader: BebopReader): ${name}`, (body) => {
    if (fields.length === 0) {
      body.line("return {};");
      return;
    }
    body.line("return {");
    body.indented((object) => {
      for (const field of fields) {
        object.line(`${propertyKey(field.name)}: ${readExpression(field.type, "reader", imports)},`);
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
  codec.block(`writeInto(writer: BebopWriter, value: ${name}): void`, (body) => {
    for (const field of fields) {
      writeStatement(body, field.type, "writer", memberAccess("value", field.name), imports);
    }
  }, "},");
}

function writeStructSize(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  codec.block(`encodedSize(value: ${name}): number`, (body) => {
    if (fields.length === 0) {
      body.line("return 0;");
      return;
    }
    body.line("let size = 0;");
    for (const field of fields) {
      writeAddSizeStatements(body, field.type, memberAccess("value", field.name), imports);
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
    body.line("const end = reader.readMessageEnd();");
    for (const field of fields) {
      body.line(`let ${field.variable}: ${field.typeName} | undefined;`);
    }
    body.block("while (reader.index < end)", (loop) => {
      loop.line("const tag = reader.readTag();");
      loop.line("if (tag === 0) break;");
      loop.block("switch (tag)", (sw) => {
        for (const field of fields) {
          sw.line(`case ${field.index}: ${field.variable} = ${readExpression(field.type, "reader", imports)}; break;`);
        }
        sw.line("default: reader.skip(end - reader.index);");
      });
    });
    body.line("return {");
    body.indented((object) => {
      for (const field of fields) {
        object.line(`${propertyKey(field.name)}: ${field.variable},`);
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
    body.line("const pos = writer.reserveMessageLength();");
    for (const field of fields) {
      const valueAccess = memberAccess("value", field.name);
      body.block(`if (${valueAccess} !== undefined)`, (ifBody) => {
        ifBody.line(`writer.writeTag(${field.index});`);
        writeStatement(ifBody, field.type, "writer", valueAccess, imports);
      });
    }
    body.line("writer.writeEndMarker();");
    body.line("writer.fillMessageLength(pos);");
  }, "},");
}

function writeMessageSize(
  codec: IndentedStringBuilder,
  name: string,
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  codec.block(`encodedSize(value: ${name}): number`, (body) => {
    body.line("let size = 5;");
    for (const field of fields) {
      const valueAccess = memberAccess("value", field.name);
      body.block(`if (${valueAccess} !== undefined)`, (ifBody) => {
        ifBody.line("size += 1;");
        writeAddSizeStatements(ifBody, field.type, valueAccess, imports);
      });
    }
    body.line("return size;");
  }, "},");
}

function writeReflection(
  codec: IndentedStringBuilder,
  def: DefinitionDescriptor,
  name: string,
  kind: "struct" | "message",
  fields: readonly FieldInfo[],
  imports: ImportState,
): void {
  imports.runtimeValues.add("BebopDefinitionKind");
  codec.block("reflection:", (reflection) => {
    reflection.line(`name: ${stringLiteral(required(def.name, `${kind} missing name`))},`);
    reflection.line(`fqn: ${stringLiteral(required(def.fqn, `${kind} ${name} missing fqn`))},`);
    reflection.line(`kind: BebopDefinitionKind.${kind},`);
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
  const branches = required(def.unionDef, `union ${name} missing body`).branches ?? [];
  const branchInfos = branches.map((branch) => unionBranchInfo(branch, name, state, imports));
  const out = new IndentedStringBuilder();
  out.line(`export type ${name} =`);
  out.indented((type) => {
    type.line(`| { readonly kind: "unknown"; readonly discriminator: number; readonly data: Uint8Array }`);
    for (const branch of branchInfos) {
      type.line(`| { readonly kind: ${stringLiteral(branch.caseName)}; readonly value: ${branch.typeName} }`);
    }
  });
  out.line(";");
  out.line();
  out.block(`export const ${name} =`, (codec) => {
    codec.block(`readFrom(reader: BebopReader): ${name}`, (body) => {
      body.line("const end = reader.readMessageEnd();");
      body.line("const discriminator = reader.readByte();");
      body.block("switch (discriminator)", (sw) => {
        for (const branch of branchInfos) {
          sw.line(`case ${branch.discriminator}: return { kind: ${stringLiteral(branch.caseName)}, value: ${branch.typeName}.readFrom(reader) };`);
        }
        sw.line('default: return { kind: "unknown", discriminator, data: reader.readBytes(end - reader.index) };');
      });
    }, "},");
    codec.block(`writeInto(writer: BebopWriter, value: ${name}): void`, (body) => {
      body.line("const pos = writer.reserveMessageLength();");
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
      body.line("writer.fillMessageLength(pos);");
    }, "},");
    codec.block(`encodedSize(value: ${name}): number`, (body) => {
      body.block("switch (value.kind)", (sw) => {
        for (const branch of branchInfos) {
          sw.line(`case ${stringLiteral(branch.caseName)}: return 5 + ${branch.typeName}.encodedSize(value.value);`);
        }
        sw.line('case "unknown": return 5 + value.data.length;');
      });
    }, "},");
    imports.runtimeValues.add("BebopDefinitionKind");
    codec.block("reflection:", (reflection) => {
      reflection.line(`name: ${stringLiteral(required(def.name, "union missing name"))},`);
      reflection.line(`fqn: ${stringLiteral(required(def.fqn, `union ${name} missing fqn`))},`);
      reflection.line("kind: BebopDefinitionKind.union,");
      reflection.block("detail:", (detail) => {
        detail.line("branches: [");
        detail.indented((array) => {
          for (const branch of branchInfos) {
            array.line(`{ discriminator: ${branch.discriminator}, name: ${stringLiteral(branch.caseName)}, typeName: ${stringLiteral(branch.typeName)} },`);
          }
        });
        detail.line("],");
      }, "},");
    }, "},");
  }, `} satisfies BebopReflectableCodec<${name}>;`);
  return out.trimEnd().toString();
}

function generateConst(def: DefinitionDescriptor, state: GeneratorState, imports: ImportState): string {
  const name = identifier(required(def.name, "const missing name"));
  const body = required(def.constDef, `const ${name} missing body`);
  const type = required(body.type, `const ${name} missing type`);
  const value = required(body.value, `const ${name} missing value`);
  return `export const ${name}: ${typeName(type, state, imports)} = ${literalExpression(value)};`;
}

function generateService(def: DefinitionDescriptor, state: GeneratorState, imports: ImportState): string {
  const name = exportedTypeName(def, { package: packageFromFqn(def.fqn), definitions: [def] });
  const service = required(def.serviceDef, `service ${name} missing body`);
  const out = new IndentedStringBuilder();
  out.block(`export const ${name} =`, (svc) => {
    svc.line(`serviceName: ${stringLiteral(required(def.name, "service missing name"))},`);
    svc.line("methods: [");
    svc.indented((methods) => {
      for (const method of service.methods ?? []) {
        methods.line("{");
        methods.indented((m) => {
          m.line(`name: ${stringLiteral(required(method.name, `service ${name} method missing name`))},`);
          m.line(`methodId: ${required(method.id, `service ${name} method missing id`)},`);
          m.line(`methodType: ${required(method.methodType, `service ${name} method missing type`)},`);
          m.line(`requestType: ${stringLiteral(typeName(required(method.requestType, `service ${name} method missing request`), state, imports))},`);
          m.line(`responseType: ${stringLiteral(typeName(required(method.responseType, `service ${name} method missing response`), state, imports))},`);
        });
        methods.line("},");
      }
    });
    svc.line("],");
  }, "} as const;");
  return out.trimEnd().toString();
}

function recordFields(def: DefinitionDescriptor, kind: "struct" | "message"): readonly FieldDescriptor[] {
  if (kind === "struct") {
    return required(def.structDef, `struct ${def.name ?? ""} missing body`).fields ?? [];
  }
  return required(def.messageDef, `message ${def.name ?? ""} missing body`).fields ?? [];
}

function fieldInfo(field: FieldDescriptor, state: GeneratorState, imports: ImportState): FieldInfo {
  const originalName = required(field.name, "field missing name");
  const name = fieldName(originalName);
  return {
    originalName,
    name,
    variable: `${safeIdentifier(name)}Value`,
    type: required(field.type, `field ${originalName} missing type`),
    typeName: typeName(required(field.type, `field ${originalName} missing type`), state, imports),
    index: field.index ?? 0,
  };
}

function unionBranchInfo(
  branch: UnionBranchDescriptor,
  unionName: string,
  state: GeneratorState,
  imports: ImportState,
): { readonly caseName: string; readonly typeName: string; readonly discriminator: number } {
  const fqn = branch.typeRefFqn ?? branch.inlineFqn;
  if (fqn === undefined) {
    throw new CodegenError(`union ${unionName} branch missing type`);
  }
  return {
    caseName: fieldName(branch.name ?? lastFqnPart(fqn)),
    typeName: definedTypeName(fqn, state, imports),
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
      return fixedArrayType(required(type.fixedArrayElement, "fixed array missing element"), state, imports);
    case TypeKind.MAP:
      return `ReadonlyMap<${typeName(required(type.mapKey, "map missing key"), state, imports)}, ${typeName(required(type.mapValue, "map missing value"), state, imports)}>`;
    case TypeKind.DEFINED:
      return definedTypeName(required(type.definedFqn, "defined type missing fqn"), state, imports);
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
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
    default:
      return `readonly ${typeName(element, state, imports)}[]`;
  }
}

function fixedArrayType(element: TypeDescriptor, state: GeneratorState, imports: ImportState): string {
  return arrayType(element, state, imports);
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
  if (activeSchema !== undefined && info.schema.path !== activeSchema.path) {
    addLocalImport(imports, `./${baseNameWithoutExtension(info.schema.path ?? "schema")}.bb`, info.typeName);
  }
  return info.typeName;
}

function definedInfo(fqn: string, state: GeneratorState, imports: ImportState): DefinitionInfo | undefined {
  const info = state.definitions.get(fqn);
  if (info === undefined) {
    return undefined;
  }
  if (activeSchema !== undefined && info.schema.path !== activeSchema.path) {
    addLocalImport(imports, `./${baseNameWithoutExtension(info.schema.path ?? "schema")}.bb`, info.typeName);
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
      return `${reader}.readDynamicMap((${readerArg()}) => ${readExpression(required(type.mapKey, "map missing key"), "_r", imports)}, (${readerArg()}) => ${readExpression(required(type.mapValue, "map missing value"), "_r", imports)})`;
    case TypeKind.DEFINED:
      return readDefinedExpression(required(type.definedFqn, "defined type missing fqn"), reader, imports);
    default:
      throw new CodegenError(`unknown type kind ${String(type.kind)}`);
  }
}

function readDefinedExpression(fqn: string, reader: string, imports: ImportState): string {
  const info = definedInfo(fqn, globalStateForDefinedType(), imports);
  if (info === undefined) {
    if (fqn === "bebop.Any") {
      imports.runtimeValues.add("BebopAny");
      return `BebopAny.readFrom(${reader})`;
    }
    if (fqn === "bebop.Empty") {
      imports.runtimeValues.add("BebopEmpty");
      return `BebopEmpty.readFrom(${reader})`;
    }
    throw new CodegenError(`unknown type ${fqn}`);
  }
  if (info.kind === DefinitionKind.ENUM) {
    return `${scalarRead(required(info.enumBaseType, `enum ${info.typeName} missing base type`), reader, imports)} as ${info.typeName}`;
  }
  return `${info.typeName}.readFrom(${reader})`;
}

let activeState: GeneratorState | undefined;
let activeSchema: SchemaDescriptor | undefined;

function globalStateForDefinedType(): GeneratorState {
  if (activeState === undefined) {
    throw new CodegenError("generator state not active");
  }
  return activeState;
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
    default: {
      const elementType = typeName(element, globalStateForDefinedType(), imports);
      return `(() => {
  const values = new Array<${elementType}>(${length});
  for (let i = 0; i < ${length}; i++) values[i] = ${readExpression(element, reader, imports)};
  return values;
})()`;
    }
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
  const info = definedInfo(fqn, globalStateForDefinedType(), imports);
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
  out.block(`for (let i = 0; i < ${value}.length; i++)`, (body) => {
    body.line(`const item = ${value}[i]!;`);
    writeStatement(body, element, writer, "item", imports);
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

function scalarRead(kind: TypeKind, reader: string, imports: ImportState): string {
  return readExpression({ kind }, reader, imports);
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
      out.line(`size += 4 + new TextEncoder().encode(${value}).length + 1;`);
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
    case TypeKind.MAP:
      out.line("size += 4;");
      out.block(`${value}.forEach((_v, _k) =>`, (body) => {
        writeAddSizeStatements(body, required(type.mapKey, "map missing key"), "_k", imports);
        writeAddSizeStatements(body, required(type.mapValue, "map missing value"), "_v", imports);
      }, "});");
      return;
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
  const info = definedInfo(fqn, globalStateForDefinedType(), imports);
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
  out.block(`for (let i = 0; i < ${value}.length; i++)`, (body) => {
    body.line(`const item = ${value}[i]!;`);
    writeAddSizeStatements(body, element, "item", imports);
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
  out.block(`for (let i = 0; i < ${value}.length; i++)`, (body) => {
    body.line(`const item = ${value}[i]!;`);
    writeAddSizeStatements(body, element, "item", imports);
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
    case TypeKind.TIMESTAMP:
      return 16;
    case TypeKind.DURATION:
      return 12;
    default:
      return undefined;
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

function literalExpression(value: LiteralValue): string {
  switch (value.kind) {
    case LiteralKind.BOOL:
      return value.boolValue === true ? "true" : "false";
    case LiteralKind.INT:
      return `${required(value.intValue, "int literal missing value")}n`;
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
      return `{ seconds: ${v.seconds}n, nanoseconds: ${v.nanoseconds}, offsetMs: ${v.offsetMs} }`;
    }
    case LiteralKind.DURATION: {
      const v = required(value.durationValue, "duration literal missing value");
      return `{ seconds: ${v.seconds}n, nanoseconds: ${v.nanoseconds} }`;
    }
    default:
      throw new CodegenError(`unsupported literal kind ${String(value.kind)}`);
  }
}

function writeImports(out: IndentedStringBuilder, imports: ImportState): void {
  const runtimeValueImports = groupRuntimeImports(imports.runtimeValues, imports.runtimeImport);
  const runtimeTypeImports = groupRuntimeImports(imports.runtimeTypes, imports.runtimeImport);
  for (const [module, symbols] of [...runtimeValueImports.entries()].sort(([a], [b]) => compareString(a, b))) {
    out.line(`import { ${[...symbols].sort().join(", ")} } from ${stringLiteral(module)};`);
  }
  for (const [module, symbols] of [...runtimeTypeImports.entries()].sort(([a], [b]) => compareString(a, b))) {
    out.line(`import type { ${[...symbols].sort().join(", ")} } from ${stringLiteral(module)};`);
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

function runtimeModuleForSymbol(symbol: string, runtimeImport: RuntimeImport): string {
  if (runtimeImport.kind === "module") {
    return runtimeImport.module;
  }
  switch (symbol) {
    case "BEBOP_TYPE_URL_PREFIX":
    case "BebopAny":
      return "./any";
    case "BFloat16":
    case "BFloat16Array":
      return "./bfloat16";
    case "BebopEmpty":
      return "./empty";
    case "BebopRuntimeError":
      return "./error";
    case "BebopDefinitionKind":
    case "BebopReflectableCodec":
      return "./reflection";
    case "BebopDuration":
    case "BebopTimestamp":
      return "./temporal";
    case "BebopReader":
    case "BebopWriter":
      return "./wire";
    case "BebopUUID":
      return "./uuid";
    default:
      throw new CodegenError(`unknown runtime symbol ${symbol}`);
  }
}

function addSymbolImport(imports: Map<string, Set<string>>, module: string, symbol: string): void {
  const existing = imports.get(module);
  if (existing !== undefined) {
    existing.add(symbol);
    return;
  }
  imports.set(module, new Set([symbol]));
}

function addLocalImport(imports: ImportState, module: string, symbol: string): void {
  addSymbolImport(imports.localValues, module, symbol);
}

function outputFileName(schema: SchemaDescriptor): string {
  return `${baseNameWithoutExtension(schema.path ?? "schema")}.bb.ts`;
}

function exportedTypeName(def: DefinitionDescriptor, schema: SchemaDescriptor): string {
  const name = required(def.name, "definition missing name");
  const fqn = def.fqn;
  const pkg = schema.package;
  if (fqn !== undefined && pkg !== undefined && fqn.startsWith(`${pkg}.`)) {
    const local = fqn.slice(pkg.length + 1);
    return local.split(".").map(typeIdentifier).join("_");
  }
  return typeIdentifier(name);
}

function typeIdentifier(value: string): string {
  return identifier(value);
}

function fieldName(value: string): string {
  return value.replace(/_([a-zA-Z0-9])/gu, (_, char: string) => char.toUpperCase());
}

function enumMemberName(member: EnumMemberDescriptor): string {
  return propertyKey(required(member.name, "enum member missing name"));
}

function identifier(value: string): string {
  const normalized = value.replace(/[^A-Za-z0-9_$]/gu, "_");
  const withStart = /^[$A-Z_a-z]/u.test(normalized) ? normalized : `_${normalized}`;
  return reservedIdentifiers.has(withStart) ? `${withStart}_` : withStart;
}

function safeIdentifier(value: string): string {
  return identifier(value);
}

function memberAccess(receiver: string, name: string): string {
  return isIdentifier(name) ? `${receiver}.${name}` : `${receiver}[${stringLiteral(name)}]`;
}

function propertyKey(name: string): string {
  return isIdentifier(name) && !reservedIdentifiers.has(name) ? name : stringLiteral(name);
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

function lastFqnPart(fqn: string): string {
  return fqn.slice(fqn.lastIndexOf(".") + 1);
}

function packageFromFqn(fqn: string | undefined): string | undefined {
  if (fqn === undefined) return undefined;
  const index = fqn.lastIndexOf(".");
  return index < 0 ? undefined : fqn.slice(0, index);
}

function baseNameWithoutExtension(path: string): string {
  const last = path.split(/[\\/]/u).at(-1) ?? path;
  const dot = last.lastIndexOf(".");
  return dot < 0 ? last : last.slice(0, dot);
}

function readerArg(): string {
  return "_r";
}

function required<T>(value: T | undefined, message: string): T {
  if (value === undefined) {
    throw new CodegenError(message);
  }
  return value;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function compareString(left: string, right: string): number {
  return left < right ? -1 : left > right ? 1 : 0;
}

class CodegenError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "CodegenError";
  }
}
