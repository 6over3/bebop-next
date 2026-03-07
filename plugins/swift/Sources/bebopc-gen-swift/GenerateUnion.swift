import BebopPlugin

enum GenerateUnion {
    static func generate(
        _ def: DefinitionDescriptor, nested: [String] = [], options: GeneratorOptions
    ) throws -> [String] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("union missing name")
        }
        guard let defFqn = def.fqn else {
            throw CodegenError.malformedDefinition("union '\(defName)' missing fqn")
        }
        guard let unionDef = def.unionDef else {
            throw CodegenError.malformedDefinition("union '\(defName)' missing body")
        }
        guard let branches = unionDef.branches else {
            throw CodegenError.malformedDefinition("union '\(defName)' missing branches")
        }
        let name = NamingPolicy.typeName(defName)
        let vis = effectiveVisibility(for: def, options: options)
        let prefix = declPrefix(doc: def.documentation, decorators: def.decorators)

        var body: [String] = []

        // cases
        body.append("case unknown(discriminator: UInt8, data: [UInt8])")
        for branch in branches {
            let caseName = try branchCaseName(branch, unionName: defName)
            let typeName = try branchTypeName(branch, unionName: defName)
            let bp = declPrefix(doc: branch.documentation, decorators: branch.decorators)
            body.append("\(bp)case \(caseName)(\(typeName))")
        }

        // decode
        var decodeBody: [String] = [
            "// @@bebop_insertion_point(decode_start:\(defName))",
            "let length = try reader.readMessageLength()",
            "let end = reader.position + Int(length)",
            "let disc = try reader.readByte()",
        ]
        var decodeSwitchLines = ["switch disc {"]
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(defName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: defName)
            let typeName = try branchTypeName(branch, unionName: defName)
            decodeSwitchLines.append("case \(disc):")
            decodeSwitchLines.append("    return .\(caseName)(try \(typeName).decode(from: &reader))")
        }
        decodeSwitchLines.append("// @@bebop_insertion_point(decode_switch:\(defName))")
        decodeSwitchLines.append("default:")
        decodeSwitchLines.append("    let remaining = end - reader.position")
        decodeSwitchLines.append("    let data = try reader.readBytes(remaining)")
        decodeSwitchLines.append("    return .unknown(discriminator: disc, data: data)")
        decodeSwitchLines.append("}")
        decodeBody.append(decodeSwitchLines.joined(separator: "\n"))
        decodeBody.append("// @@bebop_insertion_point(decode_end:\(defName))")
        let decodeBodyStr = decodeBody.map { indent($0) }.joined(separator: "\n")
        body.append(
            "\(vis)static func decode(from reader: inout BebopReader) throws -> \(name) {\n\(decodeBodyStr)\n}"
        )

        // encode
        var encodeBody: [String] = [
            "// @@bebop_insertion_point(encode_start:\(defName))",
            "let pos = writer.reserveMessageLength()",
        ]
        var encodeSwitchLines = ["switch self {"]
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(defName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: defName)
            encodeSwitchLines.append("case .\(caseName)(let _v):")
            encodeSwitchLines.append("    writer.writeByte(\(disc))")
            encodeSwitchLines.append("    _v.encode(to: &writer)")
        }
        encodeSwitchLines.append("// @@bebop_insertion_point(encode_switch:\(defName))")
        encodeSwitchLines.append("case .unknown(let disc, let data):")
        encodeSwitchLines.append("    writer.writeByte(disc)")
        encodeSwitchLines.append("    writer.writeBytes(data)")
        encodeSwitchLines.append("}")
        encodeBody.append(encodeSwitchLines.joined(separator: "\n"))
        encodeBody.append("writer.fillMessageLength(at: pos)")
        encodeBody.append("// @@bebop_insertion_point(encode_end:\(defName))")
        let encodeBodyStr = encodeBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)func encode(to writer: inout BebopWriter) {\n\(encodeBodyStr)\n}")

        // encodedSize
        var sizeSwitchLines = ["switch self {"]
        for branch in branches {
            let caseName = try branchCaseName(branch, unionName: defName)
            sizeSwitchLines.append("case .\(caseName)(let _v):")
            sizeSwitchLines.append("    return 4 + 1 + _v.encodedSize")
        }
        sizeSwitchLines.append("case .unknown(_, let data):")
        sizeSwitchLines.append("    return 4 + 1 + data.count")
        sizeSwitchLines.append("}")
        let sizeBodyStr = indent(sizeSwitchLines.joined(separator: "\n"))
        body.append("\(vis)var encodedSize: Int {\n\(sizeBodyStr)\n}")

        // CodingKeys
        let ckRawType = TypeMapper.unshadow("String")
        body.append("enum CodingKeys: \(ckRawType), CodingKey { case discriminator, value }")

        // init(from decoder:)
        var codableDecodeBody: [String] = [
            "let container = try decoder.container(keyedBy: CodingKeys.self)",
            "let disc = try container.decode(UInt8.self, forKey: .discriminator)",
        ]
        var codableDecodeSwitchLines = ["switch disc {"]
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(defName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: defName)
            let typeName = try branchTypeName(branch, unionName: defName)
            codableDecodeSwitchLines.append("case \(disc):")
            codableDecodeSwitchLines.append(
                "    self = .\(caseName)(try container.decode(\(typeName).self, forKey: .value))")
        }
        codableDecodeSwitchLines.append("default:")
        codableDecodeSwitchLines.append(
            "    let data = try container.decode([UInt8].self, forKey: .value)")
        codableDecodeSwitchLines.append("    self = .unknown(discriminator: disc, data: data)")
        codableDecodeSwitchLines.append("}")
        codableDecodeBody.append(codableDecodeSwitchLines.joined(separator: "\n"))
        let codableDecodeBodyStr = codableDecodeBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)init(from decoder: Decoder) throws {\n\(codableDecodeBodyStr)\n}")

        // encode(to encoder:)
        var codableEncodeBody = [
            "var container = encoder.container(keyedBy: CodingKeys.self)",
        ]
        var codableEncodeSwitchLines = ["switch self {"]
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(defName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: defName)
            codableEncodeSwitchLines.append("case .\(caseName)(let _v):")
            codableEncodeSwitchLines.append(
                "    try container.encode(UInt8(\(disc)), forKey: .discriminator)")
            codableEncodeSwitchLines.append("    try container.encode(_v, forKey: .value)")
        }
        codableEncodeSwitchLines.append("case .unknown(let disc, let data):")
        codableEncodeSwitchLines.append("    try container.encode(disc, forKey: .discriminator)")
        codableEncodeSwitchLines.append("    try container.encode(data, forKey: .value)")
        codableEncodeSwitchLines.append("}")
        codableEncodeBody.append(codableEncodeSwitchLines.joined(separator: "\n"))
        let codableEncodeBodyStr = codableEncodeBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)func encode(to encoder: Encoder) throws {\n\(codableEncodeBodyStr)\n}")

        try body.append(
            """
            \(vis)static let bebopReflection = BebopTypeReflection(
                name: \(quoted(defName)),
                fqn: \(quoted(defFqn)),
                kind: .union,
                detail: .union(
                    UnionReflection(branches: [
            \(indent(reflectionBranches(branches, unionName: defName), 3))
                    ])
                )
            )
            """
        )

        for decl in nested {
            body.append(decl)
        }

        body.append("// @@bebop_insertion_point(union_scope:\(defName))")

        let bodyStr = body.map { indent($0) }.joined(separator: "\n\n")
        return ["\(prefix)\(vis)enum \(name): BebopRecord, BebopReflectable, Codable {\n\(bodyStr)\n}"]
    }

    private static func branchCaseName(_ branch: UnionBranchDescriptor, unionName: String) throws
        -> String
    {
        if let name = branch.name {
            return NamingPolicy.unionCaseName(name)
        }
        if let fqn = branch.inlineFqn {
            guard let last = fqn.split(separator: ".").last else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch has empty inlineFqn")
            }
            return NamingPolicy.unionCaseName(String(last))
        }
        throw CodegenError.malformedDefinition("union '\(unionName)' branch missing name")
    }

    private static func branchTypeName(_ branch: UnionBranchDescriptor, unionName: String) throws
        -> String
    {
        if let fqn = branch.typeRefFqn {
            return NamingPolicy.fqnToTypeName(fqn)
        }
        if let fqn = branch.inlineFqn {
            guard let last = fqn.split(separator: ".").last else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch has empty inlineFqn")
            }
            return NamingPolicy.typeName(String(last))
        }
        throw CodegenError.malformedDefinition("union '\(unionName)' branch missing type reference")
    }

    private static func reflectionBranches(_ branches: [UnionBranchDescriptor], unionName: String)
        throws -> String
    {
        try branches.map { b in
            guard let disc = b.discriminator else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(b, unionName: unionName)
            let typeName = try branchTypeName(b, unionName: unionName)
            return "(discriminator: \(disc), name: \(quoted(caseName)), typeName: \(quoted(typeName)))"
        }.joined(separator: ",\n")
    }
}
