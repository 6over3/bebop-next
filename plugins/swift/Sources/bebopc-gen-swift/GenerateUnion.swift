import SwiftSyntax
import SwiftSyntaxBuilder
import BebopPlugin

enum GenerateUnion {
    static func generate(_ def: DefinitionDescriptor, nested: [DeclSyntax] = [], options: GeneratorOptions) throws -> [DeclSyntax] {
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

        let enumDecl = try EnumDeclSyntax(
            "\(raw: prefix)\(raw: vis)enum \(raw: name): BebopRecord, BebopReflectable"
        ) {
            DeclSyntax("case unknown(discriminator: UInt8, data: [UInt8])")
            for branch in branches {
                let caseName = try branchCaseName(branch, unionName: defName)
                let typeName = try branchTypeName(branch, unionName: defName)
                let bp = declPrefix(doc: branch.documentation, decorators: branch.decorators)
                DeclSyntax("\(raw: bp)case \(raw: caseName)(\(raw: typeName))")
            }

            try FunctionDeclSyntax(
                "\(raw: vis)static func decode(from reader: inout BebopReader) throws -> \(raw: name)"
            ) {
                DeclSyntax("let length = try reader.readMessageLength()")
                DeclSyntax("let end = reader.position + Int(length)")
                DeclSyntax("let disc = try reader.readByte()")
                StmtSyntax(
                    """
                    switch disc {
                    \(raw: try decodeCases(branches, unionName: defName, swiftName: name))
                    default:
                        let remaining = end - reader.position
                        let data = try reader.readBytes(remaining)
                        return .unknown(discriminator: disc, data: data)
                    }
                    """
                )
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to writer: inout BebopWriter)") {
                ExprSyntax("let pos = writer.reserveMessageLength()")
                StmtSyntax(
                    """
                    switch self {
                    \(raw: try encodeCases(branches, unionName: defName))
                    }
                    """
                )
                ExprSyntax("writer.fillMessageLength(at: pos)")
            }

            try VariableDeclSyntax("\(raw: vis)var encodedSize: Int") {
                StmtSyntax(
                    """
                    switch self {
                    \(raw: try encodedSizeCases(branches, unionName: defName))
                    }
                    """
                )
            }

            DeclSyntax("enum CodingKeys: String, CodingKey { case discriminator, value }")

            try InitializerDeclSyntax("\(raw: vis)init(from decoder: Decoder) throws") {
                DeclSyntax("let container = try decoder.container(keyedBy: CodingKeys.self)")
                DeclSyntax("let disc = try container.decode(UInt8.self, forKey: .discriminator)")
                StmtSyntax(
                    """
                    switch disc {
                    \(raw: try codableDecodeCases(branches, unionName: defName))
                    default:
                        let data = try container.decode([UInt8].self, forKey: .value)
                        self = .unknown(discriminator: disc, data: data)
                    }
                    """
                )
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to encoder: Encoder) throws") {
                DeclSyntax("var container = encoder.container(keyedBy: CodingKeys.self)")
                StmtSyntax(
                    """
                    switch self {
                    \(raw: try codableEncodeCases(branches, unionName: defName))
                    case .unknown(let disc, let data):
                        try container.encode(disc, forKey: .discriminator)
                        try container.encode(data, forKey: .value)
                    }
                    """
                )
            }

            DeclSyntax(
                "\(raw: vis)static let bebopReflection = BebopTypeReflection(name: \(literal: defName), fqn: \(literal: defFqn), kind: .union, detail: .union(UnionReflection(branches: [\(raw: try reflectionBranches(branches, unionName: defName))])))"
            )

            for decl in nested {
                decl
            }
        }

        return [DeclSyntax(enumDecl)]
    }

    private static func branchCaseName(_ branch: UnionBranchDescriptor, unionName: String) throws -> String {
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

    private static func branchTypeName(_ branch: UnionBranchDescriptor, unionName: String) throws -> String {
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

    private static func decodeCases(_ branches: [UnionBranchDescriptor], unionName: String, swiftName: String) throws -> String {
        var result = ""
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: unionName)
            let typeName = try branchTypeName(branch, unionName: unionName)
            result += "case \(disc):\n"
            result += "    return .\(caseName)(try \(typeName).decode(from: &reader))\n"
        }
        return result
    }

    private static func encodeCases(_ branches: [UnionBranchDescriptor], unionName: String) throws -> String {
        var result = ""
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: unionName)
            result += "case .\(caseName)(let _v):\n"
            result += "    writer.writeByte(\(disc))\n"
            result += "    _v.encode(to: &writer)\n"
        }
        result += "case .unknown(let disc, let data):\n"
        result += "    writer.writeByte(disc)\n"
        result += "    writer.writeBytes(data)\n"
        return result
    }

    private static func encodedSizeCases(_ branches: [UnionBranchDescriptor], unionName: String) throws -> String {
        var result = ""
        for branch in branches {
            let caseName = try branchCaseName(branch, unionName: unionName)
            result += "case .\(caseName)(let _v):\n"
            result += "    return 4 + 1 + _v.encodedSize\n"
        }
        result += "case .unknown(_, let data):\n"
        result += "    return 4 + 1 + data.count\n"
        return result
    }

    private static func codableDecodeCases(_ branches: [UnionBranchDescriptor], unionName: String) throws -> String {
        var result = ""
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: unionName)
            let typeName = try branchTypeName(branch, unionName: unionName)
            result += "case \(disc):\n"
            result += "    self = .\(caseName)(try container.decode(\(typeName).self, forKey: .value))\n"
        }
        return result
    }

    private static func codableEncodeCases(_ branches: [UnionBranchDescriptor], unionName: String) throws -> String {
        var result = ""
        for branch in branches {
            guard let disc = branch.discriminator else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(branch, unionName: unionName)
            result += "case .\(caseName)(let _v):\n"
            result += "    try container.encode(UInt8(\(disc)), forKey: .discriminator)\n"
            result += "    try container.encode(_v, forKey: .value)\n"
        }
        return result
    }

    private static func reflectionBranches(_ branches: [UnionBranchDescriptor], unionName: String) throws -> String {
        try branches.map { b in
            guard let disc = b.discriminator else {
                throw CodegenError.malformedDefinition("union '\(unionName)' branch missing discriminator")
            }
            let caseName = try branchCaseName(b, unionName: unionName)
            let typeName = try branchTypeName(b, unionName: unionName)
            return "(discriminator: \(disc), name: \(quoted(caseName)), typeName: \(quoted(typeName)))"
        }.joined(separator: ", ")
    }
}
