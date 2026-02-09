import SwiftSyntax
import SwiftSyntaxBuilder
import BebopPlugin

enum GenerateEnum {
    static func generate(_ def: DefinitionDescriptor, options: GeneratorOptions) throws -> [DeclSyntax] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("enum missing name")
        }
        guard let enumDef = def.enumDef else {
            throw CodegenError.malformedDefinition("enum '\(defName)' missing body")
        }
        let members = enumDef.members ?? []
        guard let baseKind = enumDef.baseType else {
            throw CodegenError.malformedDefinition("enum '\(defName)' missing base type")
        }
        guard let isFlags = enumDef.isFlags else {
            throw CodegenError.malformedDefinition("enum '\(defName)' missing isFlags")
        }
        let name = NamingPolicy.typeName(defName)
        let baseType = try TypeMapper.enumBaseType(baseKind)
        let readMethod = try TypeMapper.enumReadMethod(baseKind)
        let writeMethod = try TypeMapper.enumWriteMethod(baseKind)
        let vis = effectiveVisibility(for: def, options: options)

        if isFlags {
            return try generateFlags(def, members: members, name: name, vis: vis, baseKind: baseKind, baseType: baseType, readMethod: readMethod, writeMethod: writeMethod)
        } else {
            return try generateRegularEnum(def, members: members, name: name, vis: vis, baseKind: baseKind, baseType: baseType, readMethod: readMethod, writeMethod: writeMethod)
        }
    }

    private static func generateRegularEnum(
        _ def: DefinitionDescriptor,
        members: [EnumMemberDescriptor],
        name: String,
        vis: String,
        baseKind: TypeKind,
        baseType: String,
        readMethod: String,
        writeMethod: String
    ) throws -> [DeclSyntax] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("enum missing name")
        }
        guard let defFqn = def.fqn else {
            throw CodegenError.malformedDefinition("enum '\(defName)' missing fqn")
        }
        let prefix = declPrefix(doc: def.documentation, decorators: def.decorators)
        let encodedSize = try TypeMapper.enumScalarSize(baseKind)

        let memberDecls = try members.map { member -> (caseName: String, literal: String, prefix: String) in
            guard let mName = member.name else {
                throw CodegenError.malformedDefinition("enum '\(defName)' member missing name")
            }
            guard let mValue = member.value else {
                throw CodegenError.malformedDefinition("enum '\(defName)' member '\(mName)' missing value")
            }
            return (
                caseName: NamingPolicy.enumCaseName(mName),
                literal: enumMemberLiteral(mValue, baseKind: baseKind),
                prefix: declPrefix(doc: member.documentation, decorators: member.decorators)
            )
        }

        let structDecl = try StructDeclSyntax(
            "\(raw: prefix)\(raw: vis)struct \(raw: name): RawRepresentable, Sendable, Hashable, BebopRecord, BebopReflectable"
        ) {
            DeclSyntax("\(raw: vis)let rawValue: \(raw: baseType)")
            DeclSyntax("\(raw: vis)init(rawValue: \(raw: baseType)) { self.rawValue = rawValue }")

            for m in memberDecls {
                DeclSyntax(
                    "\(raw: m.prefix)\(raw: vis)static let \(raw: m.caseName) = \(raw: name)(rawValue: \(raw: m.literal))"
                )
            }

            try FunctionDeclSyntax(
                "\(raw: vis)static func decode(from reader: inout BebopReader) throws -> \(raw: name)"
            ) {
                StmtSyntax(
                    "return \(raw: name)(rawValue: try reader.\(raw: readMethod)())"
                )
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to writer: inout BebopWriter)") {
                ExprSyntax("writer.\(raw: writeMethod)(rawValue)")
            }

            DeclSyntax("\(raw: vis)var encodedSize: Int { \(raw: String(encodedSize)) }")

            DeclSyntax(
                "\(raw: vis)static let bebopReflection = BebopTypeReflection(name: \(literal: defName), fqn: \(literal: defFqn), kind: .enum, detail: .enum(EnumReflection(members: [\(raw: try reflectionMembers(members, enumName: defName))], isFlags: false)))"
            )
        }

        return [DeclSyntax(structDecl)]
    }

    private static func generateFlags(
        _ def: DefinitionDescriptor,
        members: [EnumMemberDescriptor],
        name: String,
        vis: String,
        baseKind: TypeKind,
        baseType: String,
        readMethod: String,
        writeMethod: String
    ) throws -> [DeclSyntax] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("enum missing name")
        }
        guard let defFqn = def.fqn else {
            throw CodegenError.malformedDefinition("enum '\(defName)' missing fqn")
        }
        let prefix = declPrefix(doc: def.documentation, decorators: def.decorators)
        let encodedSize = try TypeMapper.enumScalarSize(baseKind)

        let memberDecls = try members.map { member -> (caseName: String, value: UInt64, literal: String, prefix: String) in
            guard let mName = member.name else {
                throw CodegenError.malformedDefinition("flags '\(defName)' member missing name")
            }
            guard let mValue = member.value else {
                throw CodegenError.malformedDefinition("flags '\(defName)' member '\(mName)' missing value")
            }
            return (
                caseName: NamingPolicy.enumCaseName(mName),
                value: mValue,
                literal: enumMemberLiteral(mValue, baseKind: baseKind),
                prefix: declPrefix(doc: member.documentation, decorators: member.decorators)
            )
        }

        let structDecl = try StructDeclSyntax(
            "\(raw: prefix)\(raw: vis)struct \(raw: name): OptionSet, Sendable, BebopRecord, BebopReflectable"
        ) {
            DeclSyntax("\(raw: vis)let rawValue: \(raw: baseType)")
            DeclSyntax("\(raw: vis)init(rawValue: \(raw: baseType)) { self.rawValue = rawValue }")

            for m in memberDecls where m.value != 0 {
                DeclSyntax(
                    "\(raw: m.prefix)\(raw: vis)static let \(raw: m.caseName) = \(raw: name)(rawValue: \(raw: m.literal))"
                )
            }

            try FunctionDeclSyntax(
                "\(raw: vis)static func decode(from reader: inout BebopReader) throws -> \(raw: name)"
            ) {
                StmtSyntax(
                    "return \(raw: name)(rawValue: try reader.\(raw: readMethod)())"
                )
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to writer: inout BebopWriter)") {
                ExprSyntax("writer.\(raw: writeMethod)(rawValue)")
            }

            DeclSyntax("\(raw: vis)var encodedSize: Int { \(raw: String(encodedSize)) }")

            try InitializerDeclSyntax("\(raw: vis)init(from decoder: Decoder) throws") {
                DeclSyntax("let container = try decoder.singleValueContainer()")
                ExprSyntax("self.rawValue = try container.decode(\(raw: baseType).self)")
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to encoder: Encoder) throws") {
                DeclSyntax("var container = encoder.singleValueContainer()")
                ExprSyntax("try container.encode(rawValue)")
            }

            DeclSyntax(
                "\(raw: vis)static let bebopReflection = BebopTypeReflection(name: \(literal: defName), fqn: \(literal: defFqn), kind: .enum, detail: .enum(EnumReflection(members: [\(raw: try reflectionMembers(members, enumName: defName))], isFlags: true)))"
            )
        }

        return [DeclSyntax(structDecl)]
    }

    private static func enumMemberLiteral(_ value: UInt64, baseKind: TypeKind) -> String {
        switch baseKind {
        case .int8:  return String(Int8(bitPattern: UInt8(truncatingIfNeeded: value)))
        case .int16: return String(Int16(bitPattern: UInt16(truncatingIfNeeded: value)))
        case .int32: return String(Int32(bitPattern: UInt32(truncatingIfNeeded: value)))
        case .int64: return String(Int64(bitPattern: value))
        default:     return String(value)
        }
    }

    private static func reflectionMembers(_ members: [EnumMemberDescriptor], enumName: String) throws -> String {
        try members.map { m in
            guard let mName = m.name else {
                throw CodegenError.malformedDefinition("enum '\(enumName)' member missing name")
            }
            guard let mValue = m.value else {
                throw CodegenError.malformedDefinition("enum '\(enumName)' member '\(mName)' missing value")
            }
            return "(name: \(quoted(mName)), value: \(mValue))"
        }.joined(separator: ", ")
    }
}
