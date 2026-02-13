import BebopPlugin

enum GenerateEnum {
    static func generate(_ def: DefinitionDescriptor, options: GeneratorOptions) throws -> [String] {
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
    ) throws -> [String] {
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

        var body: [String] = []
        body.append("\(vis)let rawValue: \(baseType)")
        body.append("\(vis)init(rawValue: \(baseType)) { self.rawValue = rawValue }")

        for m in memberDecls {
            body.append("\(m.prefix)\(vis)static let \(m.caseName) = \(name)(rawValue: \(m.literal))")
        }

        body.append("""
        \(vis)static func decode(from reader: inout BebopReader) throws -> \(name) {
            return \(name)(rawValue: try reader.\(readMethod)())
        }
        """)

        body.append("""
        \(vis)func encode(to writer: inout BebopWriter) {
            writer.\(writeMethod)(rawValue)
        }
        """)

        body.append("\(vis)var encodedSize: Int { \(String(encodedSize)) }")

        body.append(
            "\(vis)static let bebopReflection = BebopTypeReflection(name: \(quoted(defName)), fqn: \(quoted(defFqn)), kind: .enum, detail: .enum(EnumReflection(members: [\(try reflectionMembers(members, enumName: defName))], isFlags: false)))"
        )

        let bodyStr = body.map { indent($0) }.joined(separator: "\n")
        return ["\(prefix)\(vis)struct \(name): RawRepresentable, Sendable, Hashable, BebopRecord, BebopReflectable {\n\(bodyStr)\n}"]
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
    ) throws -> [String] {
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

        var body: [String] = []
        body.append("\(vis)let rawValue: \(baseType)")
        body.append("\(vis)init(rawValue: \(baseType)) { self.rawValue = rawValue }")

        for m in memberDecls where m.value != 0 {
            body.append("\(m.prefix)\(vis)static let \(m.caseName) = \(name)(rawValue: \(m.literal))")
        }

        body.append("""
        \(vis)static func decode(from reader: inout BebopReader) throws -> \(name) {
            return \(name)(rawValue: try reader.\(readMethod)())
        }
        """)

        body.append("""
        \(vis)func encode(to writer: inout BebopWriter) {
            writer.\(writeMethod)(rawValue)
        }
        """)

        body.append("\(vis)var encodedSize: Int { \(String(encodedSize)) }")

        body.append("""
        \(vis)init(from decoder: Decoder) throws {
            let container = try decoder.singleValueContainer()
            self.rawValue = try container.decode(\(baseType).self)
        }
        """)

        body.append("""
        \(vis)func encode(to encoder: Encoder) throws {
            var container = encoder.singleValueContainer()
            try container.encode(rawValue)
        }
        """)

        body.append(
            "\(vis)static let bebopReflection = BebopTypeReflection(name: \(quoted(defName)), fqn: \(quoted(defFqn)), kind: .enum, detail: .enum(EnumReflection(members: [\(try reflectionMembers(members, enumName: defName))], isFlags: true)))"
        )

        let bodyStr = body.map { indent($0) }.joined(separator: "\n")
        return ["\(prefix)\(vis)struct \(name): OptionSet, Sendable, BebopRecord, BebopReflectable {\n\(bodyStr)\n}"]
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
