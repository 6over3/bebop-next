import Foundation
import SwiftBebop
import Testing

@testable import BebopPlugin

// MARK: - Round-Trip Helper

private func roundTrip<T: BebopRecord & Equatable>(_ value: T) throws -> T {
    let bytes = value.serializedData()
    #expect(bytes.count == value.encodedSize)
    return try T.decode(from: bytes)
}

// MARK: - Enum Round-Trips

@Test func typeKindRoundTrip() throws {
    let kinds: [TypeKind] = [.bool, .byte, .int32, .string, .uuid, .array, .map, .defined]
    for kind in kinds {
        let decoded = try roundTrip(kind)
        #expect(decoded == kind)
    }
}

@Test func definitionKindRoundTrip() throws {
    let kinds: [DefinitionKind] = [.enum, .struct, .message, .union, .service, .const, .decorator]
    for kind in kinds {
        #expect(try roundTrip(kind) == kind)
    }
}

@Test func visibilityRoundTrip() throws {
    for vis: Visibility in [.default, .export, .local] {
        #expect(try roundTrip(vis) == vis)
    }
}

@Test func methodTypeRoundTrip() throws {
    for mt: BebopPlugin.MethodType in [.unary, .serverStream, .clientStream, .duplexStream] {
        #expect(try roundTrip(mt) == mt)
    }
}

@Test func diagnosticSeverityRoundTrip() throws {
    for sev: DiagnosticSeverity in [.error, .warning, .info, .hint] {
        #expect(try roundTrip(sev) == sev)
    }
}

// MARK: - Struct Round-Trips

@Test func versionRoundTrip() throws {
    let v = Version(major: 1, minor: 2, patch: 3, suffix: "beta.1")
    let decoded = try roundTrip(v)
    #expect(decoded.major == 1)
    #expect(decoded.minor == 2)
    #expect(decoded.patch == 3)
    #expect(decoded.suffix == "beta.1")
}

@Test func versionEmptySuffix() throws {
    let v = Version(major: 0, minor: 0, patch: 0, suffix: "")
    let decoded = try roundTrip(v)
    #expect(decoded.suffix == "")
}

@Test func decoratorArgRoundTrip() throws {
    let arg = DecoratorArg(
        name: "message",
        value: LiteralValue(kind: .string, stringValue: "deprecated")
    )
    let decoded = try roundTrip(arg)
    #expect(decoded.name == "message")
    #expect(decoded.value.kind == .string)
    #expect(decoded.value.stringValue == "deprecated")
}

// MARK: - Message Round-Trips

@Test func typeDescriptorScalar() throws {
    let td = TypeDescriptor(kind: .int32)
    let decoded = try roundTrip(td)
    #expect(decoded.kind == .int32)
    #expect(decoded.arrayElement == nil)
    #expect(decoded.mapKey == nil)
}

@Test func typeDescriptorArray() throws {
    let td = TypeDescriptor(kind: .array, arrayElement: TypeDescriptor(kind: .string))
    let decoded = try roundTrip(td)
    #expect(decoded.kind == .array)
    #expect(decoded.arrayElement?.kind == .string)
}

@Test func typeDescriptorFixedArray() throws {
    let td = TypeDescriptor(
        kind: .fixedArray,
        fixedArrayElement: TypeDescriptor(kind: .float32),
        fixedArraySize: 16
    )
    let decoded = try roundTrip(td)
    #expect(decoded.kind == .fixedArray)
    #expect(decoded.fixedArrayElement?.kind == .float32)
    #expect(decoded.fixedArraySize == 16)
}

@Test func typeDescriptorMap() throws {
    let td = TypeDescriptor(
        kind: .map,
        mapKey: TypeDescriptor(kind: .string),
        mapValue: TypeDescriptor(kind: .uint64)
    )
    let decoded = try roundTrip(td)
    #expect(decoded.kind == .map)
    #expect(decoded.mapKey?.kind == .string)
    #expect(decoded.mapValue?.kind == .uint64)
}

@Test func typeDescriptorDefined() throws {
    let td = TypeDescriptor(kind: .defined, definedFqn: "my.pkg.Foo")
    let decoded = try roundTrip(td)
    #expect(decoded.kind == .defined)
    #expect(decoded.definedFqn == "my.pkg.Foo")
}

@Test func typeDescriptorEmpty() throws {
    let td = TypeDescriptor()
    let decoded = try roundTrip(td)
    #expect(decoded.kind == nil)
}

@Test func fieldDescriptorRoundTrip() throws {
    let f = FieldDescriptor(
        name: "user_id",
        documentation: "Primary key.",
        type: TypeDescriptor(kind: .uint64),
        index: 3
    )
    let decoded = try roundTrip(f)
    #expect(decoded.name == "user_id")
    #expect(decoded.documentation == "Primary key.")
    #expect(decoded.type?.kind == .uint64)
    #expect(decoded.index == 3)
}

@Test func fieldDescriptorWithDecorators() throws {
    let f = FieldDescriptor(
        name: "old",
        type: TypeDescriptor(kind: .string),
        index: 1,
        decorators: [DecoratorUsage(fqn: "deprecated")]
    )
    let decoded = try roundTrip(f)
    #expect(decoded.decorators?.count == 1)
    #expect(decoded.decorators?.first?.fqn == "deprecated")
}

@Test func enumMemberDescriptorRoundTrip() throws {
    let m = EnumMemberDescriptor(name: "Red", value: 42)
    let decoded = try roundTrip(m)
    #expect(decoded.name == "Red")
    #expect(decoded.value == 42)
}

@Test func literalValueBool() throws {
    let v = LiteralValue(kind: .bool, boolValue: true)
    let decoded = try roundTrip(v)
    #expect(decoded.kind == .bool)
    #expect(decoded.boolValue == true)
}

@Test func literalValueInt() throws {
    let v = LiteralValue(kind: .int, intValue: -42)
    let decoded = try roundTrip(v)
    #expect(decoded.kind == .int)
    #expect(decoded.intValue == -42)
}

@Test func literalValueFloat() throws {
    let v = LiteralValue(kind: .float, floatValue: 3.14)
    let decoded = try roundTrip(v)
    #expect(decoded.kind == .float)
    #expect(decoded.floatValue == 3.14)
}

@Test func literalValueString() throws {
    let v = LiteralValue(kind: .string, stringValue: "hello world")
    let decoded = try roundTrip(v)
    #expect(decoded.kind == .string)
    #expect(decoded.stringValue == "hello world")
}

@Test func literalValueUUID() throws {
    let id = BebopUUID(uuidString: "12345678-1234-5678-1234-567812345678")!
    let v = LiteralValue(kind: .uuid, uuidValue: id)
    let decoded = try roundTrip(v)
    #expect(decoded.kind == .uuid)
    #expect(decoded.uuidValue == id)
}

@Test func unionBranchDescriptorTypeRef() throws {
    let b = UnionBranchDescriptor(discriminator: 1, typeRefFqn: "pkg.Foo", name: "Foo")
    let decoded = try roundTrip(b)
    #expect(decoded.discriminator == 1)
    #expect(decoded.typeRefFqn == "pkg.Foo")
    #expect(decoded.name == "Foo")
    #expect(decoded.inlineFqn == nil)
}

@Test func unionBranchDescriptorInline() throws {
    let b = UnionBranchDescriptor(discriminator: 2, inlineFqn: "pkg.MyUnion.Inner")
    let decoded = try roundTrip(b)
    #expect(decoded.discriminator == 2)
    #expect(decoded.inlineFqn == "pkg.MyUnion.Inner")
    #expect(decoded.typeRefFqn == nil)
}

// MARK: - Definition Descriptor Round-Trips

@Test func definitionDescriptorEnum() throws {
    let def = DefinitionDescriptor(
        kind: .enum, name: "Color", fqn: "test.Color",
        enumDef: EnumDef(
            baseType: .uint32,
            members: [
                EnumMemberDescriptor(name: "Red", value: 0),
                EnumMemberDescriptor(name: "Blue", value: 1),
            ], isFlags: false
        )
    )
    let decoded = try roundTrip(def)
    #expect(decoded.kind == .enum)
    #expect(decoded.name == "Color")
    #expect(decoded.fqn == "test.Color")
    #expect(decoded.enumDef?.baseType == .uint32)
    #expect(decoded.enumDef?.isFlags == false)
    #expect(decoded.enumDef?.members?.count == 2)
    #expect(decoded.enumDef?.members?[0].name == "Red")
    #expect(decoded.enumDef?.members?[1].value == 1)
}

@Test func definitionDescriptorStruct() throws {
    let def = DefinitionDescriptor(
        kind: .struct, name: "Point", fqn: "test.Point",
        structDef: StructDef(
            fields: [
                FieldDescriptor(name: "x", type: TypeDescriptor(kind: .float64)),
                FieldDescriptor(name: "y", type: TypeDescriptor(kind: .float64)),
            ], isMutable: true
        )
    )
    let decoded = try roundTrip(def)
    #expect(decoded.kind == .struct)
    #expect(decoded.name == "Point")
    #expect(decoded.structDef?.isMutable == true)
    #expect(decoded.structDef?.fields?.count == 2)
}

@Test func definitionDescriptorMessage() throws {
    let def = DefinitionDescriptor(
        kind: .message, name: "Request", fqn: "test.Request",
        messageDef: MessageDef(fields: [
            FieldDescriptor(name: "url", type: TypeDescriptor(kind: .string), index: 1),
            FieldDescriptor(name: "timeout", type: TypeDescriptor(kind: .uint32), index: 2),
        ])
    )
    let decoded = try roundTrip(def)
    #expect(decoded.kind == .message)
    #expect(decoded.messageDef?.fields?.count == 2)
    #expect(decoded.messageDef?.fields?[0].index == 1)
    #expect(decoded.messageDef?.fields?[1].index == 2)
}

@Test func definitionDescriptorUnion() throws {
    let def = DefinitionDescriptor(
        kind: .union, name: "Result", fqn: "test.Result",
        unionDef: UnionDef(branches: [
            UnionBranchDescriptor(discriminator: 1, typeRefFqn: "test.Ok", name: "Ok"),
            UnionBranchDescriptor(discriminator: 2, typeRefFqn: "test.Err", name: "Err"),
        ])
    )
    let decoded = try roundTrip(def)
    #expect(decoded.kind == .union)
    #expect(decoded.unionDef?.branches?.count == 2)
    #expect(decoded.unionDef?.branches?[0].discriminator == 1)
    #expect(decoded.unionDef?.branches?[1].discriminator == 2)
}

@Test func definitionDescriptorConst() throws {
    let def = DefinitionDescriptor(
        kind: .const, name: "PI", fqn: "test.PI",
        constDef: ConstDef(
            type: TypeDescriptor(kind: .float64),
            value: LiteralValue(kind: .float, floatValue: 3.14159)
        )
    )
    let decoded = try roundTrip(def)
    #expect(decoded.kind == .const)
    #expect(decoded.constDef?.value?.floatValue == 3.14159)
}

@Test func definitionDescriptorWithDocAndDecorators() throws {
    let def = DefinitionDescriptor(
        kind: .message, name: "Old", fqn: "test.Old",
        documentation: "Deprecated message.",
        decorators: [
            DecoratorUsage(
                fqn: "deprecated",
                args: [
                    DecoratorArg(name: "", value: LiteralValue(kind: .string, stringValue: "use New")),
                ]
            ),
        ],
        messageDef: MessageDef(fields: [])
    )
    let decoded = try roundTrip(def)
    #expect(decoded.documentation == "Deprecated message.")
    #expect(decoded.decorators?.count == 1)
    #expect(decoded.decorators?[0].fqn == "deprecated")
    #expect(decoded.decorators?[0].args?.count == 1)
    #expect(decoded.decorators?[0].args?[0].value.stringValue == "use New")
}

@Test func definitionDescriptorNested() throws {
    let inner = DefinitionDescriptor(
        kind: .struct, name: "Inner", fqn: "test.Outer.Inner",
        visibility: .local,
        structDef: StructDef(fields: [], isMutable: false)
    )
    let outer = DefinitionDescriptor(
        kind: .struct, name: "Outer", fqn: "test.Outer",
        nested: [inner],
        structDef: StructDef(fields: [], isMutable: false)
    )
    let decoded = try roundTrip(outer)
    #expect(decoded.nested?.count == 1)
    #expect(decoded.nested?[0].name == "Inner")
    #expect(decoded.nested?[0].visibility == .local)
}

@Test func definitionDescriptorEmpty() throws {
    let def = DefinitionDescriptor()
    let decoded = try roundTrip(def)
    #expect(decoded.kind == nil)
    #expect(decoded.name == nil)
}

// MARK: - Schema Round-Trip

@Test func schemaDescriptorRoundTrip() throws {
    let schema = SchemaDescriptor(
        path: "test.bop",
        package: "mypackage",
        definitions: [
            DefinitionDescriptor(
                kind: .struct, name: "Foo", fqn: "mypackage.Foo",
                structDef: StructDef(
                    fields: [
                        FieldDescriptor(name: "id", type: TypeDescriptor(kind: .uint32)),
                    ], isMutable: false
                )
            ),
        ]
    )
    let decoded = try roundTrip(schema)
    #expect(decoded.path == "test.bop")
    #expect(decoded.package == "mypackage")
    #expect(decoded.definitions?.count == 1)
    #expect(decoded.definitions?[0].name == "Foo")
}

@Test func schemaDescriptorEmpty() throws {
    let schema = SchemaDescriptor()
    let decoded = try roundTrip(schema)
    #expect(decoded.path == nil)
    #expect(decoded.definitions == nil)
}

// MARK: - Plugin Types Round-Trip

@Test func codeGeneratorRequestRoundTrip() throws {
    let req = CodeGeneratorRequest(
        filesToGenerate: ["a.bop", "b.bop"],
        parameter: "Visibility=internal",
        compilerVersion: Version(major: 1, minor: 0, patch: 0, suffix: ""),
        hostOptions: ["opt1": "val1"]
    )
    let decoded = try roundTrip(req)
    #expect(decoded.filesToGenerate == ["a.bop", "b.bop"])
    #expect(decoded.parameter == "Visibility=internal")
    #expect(decoded.compilerVersion?.major == 1)
    #expect(decoded.hostOptions?["opt1"] == "val1")
}

@Test func codeGeneratorRequestEmpty() throws {
    let req = CodeGeneratorRequest()
    let decoded = try roundTrip(req)
    #expect(decoded.filesToGenerate == nil)
    #expect(decoded.schemas == nil)
}

@Test func generatedFileRoundTrip() throws {
    let f = GeneratedFile(name: "output.swift", content: "// generated\n")
    let decoded = try roundTrip(f)
    #expect(decoded.name == "output.swift")
    #expect(decoded.content == "// generated\n")
    #expect(decoded.insertionPoint == nil)
}

@Test func codeGeneratorResponseRoundTrip() throws {
    let resp = CodeGeneratorResponse(
        files: [
            GeneratedFile(name: "a.swift", content: "code_a"),
            GeneratedFile(name: "b.swift", content: "code_b"),
        ]
    )
    let decoded = try roundTrip(resp)
    #expect(decoded.error == nil)
    #expect(decoded.files?.count == 2)
    #expect(decoded.files?[0].name == "a.swift")
    #expect(decoded.files?[1].content == "code_b")
}

@Test func codeGeneratorResponseError() throws {
    let resp = CodeGeneratorResponse(error: "something went wrong")
    let decoded = try roundTrip(resp)
    #expect(decoded.error == "something went wrong")
    #expect(decoded.files == nil)
}

@Test func diagnosticRoundTrip() throws {
    let d = Diagnostic(
        severity: .warning,
        text: "field is deprecated",
        hint: "use new_field instead",
        file: "test.bop",
        span: {
            let values: [Int32] = [1, 5, 1, 20]
            return InlineArray<4, Int32> { values[$0] }
        }()
    )
    let decoded = try roundTrip(d)
    #expect(decoded.severity == .warning)
    #expect(decoded.text == "field is deprecated")
    #expect(decoded.hint == "use new_field instead")
    #expect(decoded.file == "test.bop")
    let span = try #require(decoded.span)
    #expect(span[0] == 1)
    #expect(span[1] == 5)
    #expect(span[2] == 1)
    #expect(span[3] == 20)
}

// MARK: - DescriptorCompat: ResponseBuilder

@Test func responseBuilderAddFiles() throws {
    let builder = ResponseBuilder()
    builder.addFile(name: "a.swift", content: "code_a")
    builder.addFile(name: "b.swift", content: "code_b")
    let bytes = try builder.encode()
    let resp = try CodeGeneratorResponse.decode(from: bytes)
    #expect(resp.files?.count == 2)
    #expect(resp.files?[0].name == "a.swift")
    #expect(resp.files?[0].content == "code_a")
    #expect(resp.files?[1].name == "b.swift")
}

@Test func responseBuilderSetError() throws {
    let builder = ResponseBuilder()
    builder.setError("fatal error")
    let bytes = try builder.encode()
    let resp = try CodeGeneratorResponse.decode(from: bytes)
    #expect(resp.error == "fatal error")
    #expect(resp.files == nil)
}

@Test func responseBuilderEmpty() throws {
    let builder = ResponseBuilder()
    let bytes = try builder.encode()
    let resp = try CodeGeneratorResponse.decode(from: bytes)
    #expect(resp.error == nil)
    #expect(resp.files == nil)
}

// MARK: - DescriptorCompat: Extension Helpers

@Test func fieldIsDeprecated() {
    let deprecated = FieldDescriptor(
        name: "old",
        type: TypeDescriptor(kind: .string),
        index: 1,
        decorators: [DecoratorUsage(fqn: "deprecated")]
    )
    let notDeprecated = FieldDescriptor(
        name: "new",
        type: TypeDescriptor(kind: .string),
        index: 2
    )
    let noDecorators = FieldDescriptor(name: "plain", type: TypeDescriptor(kind: .int32), index: 3)

    #expect(deprecated.isDeprecated == true)
    #expect(notDeprecated.isDeprecated == false)
    #expect(noDecorators.isDeprecated == false)
}

@Test func typeKindIsScalar() {
    let scalars: [TypeKind] = [
        .bool, .byte, .int8, .int16, .uint16, .int32, .uint32,
        .int64, .uint64, .int128, .uint128,
        .float16, .float32, .float64, .bfloat16,
        .string, .uuid, .timestamp, .duration,
    ]
    for kind in scalars {
        #expect(kind.isScalar == true, "expected \(kind.rawValue) to be scalar")
    }

    let nonScalars: [TypeKind] = [.unknown, .array, .fixedArray, .map, .defined]
    for kind in nonScalars {
        #expect(kind.isScalar == false, "expected \(kind.rawValue) to not be scalar")
    }
}

// MARK: - BebopAny

@Test func bebopAnyRoundTrip() throws {
    let any = BebopAny(typeURL: "type.bebop.sh/test.Foo", value: [1, 2, 3])
    let decoded = try roundTrip(any)
    #expect(decoded.typeURL == "type.bebop.sh/test.Foo")
    #expect(decoded.value == [1, 2, 3])
}

@Test func bebopAnyPackUnpack() throws {
    let version = Version(major: 2, minor: 5, patch: 0, suffix: "")
    let packed = BebopAny.pack(version)
    #expect(packed.typeURL == "type.bebop.sh/bebop.Version")
    #expect(packed.typeName == "bebop.Version")

    let unpacked = try packed.unpack(as: Version.self)
    #expect(unpacked == version)
}

@Test func bebopAnyIs() throws {
    let version = Version(major: 1, minor: 0, patch: 0, suffix: "")
    let packed = BebopAny.pack(version)
    #expect(packed.is(Version.self) == true)
    #expect(packed.is(CodeGeneratorResponse.self) == false)
}

@Test func bebopAnyUnpackTypeMismatch() throws {
    let version = Version(major: 1, minor: 0, patch: 0, suffix: "")
    let packed = BebopAny.pack(version)
    #expect(throws: BebopDecodingError.self) {
        try packed.unpack(as: CodeGeneratorResponse.self)
    }
}

@Test func bebopAnyTypeName() {
    let a = BebopAny(typeURL: "type.bebop.sh/my.pkg.Foo", value: [])
    #expect(a.typeName == "my.pkg.Foo")

    let b = BebopAny(typeURL: "no-slash", value: [])
    #expect(b.typeName == nil)

    let c = BebopAny(typeURL: "custom.prefix/Bar", value: [])
    #expect(c.typeName == "Bar")
}

@Test func bebopAnyCustomPrefix() throws {
    let version = Version(major: 1, minor: 0, patch: 0, suffix: "")
    let packed = BebopAny.pack(version, prefix: "example.com/types/")
    #expect(packed.typeURL == "example.com/types/bebop.Version")
    #expect(packed.is(Version.self) == true)
    let unpacked = try packed.unpack(as: Version.self)
    #expect(unpacked == version)
}

@Test func bebopAnyEmptyValue() throws {
    let any = BebopAny(typeURL: "type.bebop.sh/test.Empty", value: [])
    let decoded = try roundTrip(any)
    #expect(decoded.value.isEmpty)
}

@Test func bebopAnyReflection() {
    let r = BebopAny.bebopReflection
    #expect(r.name == "BebopAny")
    #expect(r.fqn == "bebop.Any")
}

@Test func bebopAnyEncodedSize() {
    let any = BebopAny(typeURL: "type.bebop.sh/test.X", value: [1, 2, 3])
    let typeURLSize = 4 + "type.bebop.sh/test.X".utf8.count + 1
    let valueSize = 4 + 3
    #expect(any.encodedSize == typeURLSize + valueSize)
}

// MARK: - Typealiases

@Test func typealiasesExist() {
    // PluginRequest is CodeGeneratorRequest
    let req = PluginRequest()
    #expect(req.filesToGenerate == nil)

    // CompilerVersion is Version
    let ver = CompilerVersion(major: 0, minor: 0, patch: 0, suffix: "")
    #expect(ver.major == 0)
}
