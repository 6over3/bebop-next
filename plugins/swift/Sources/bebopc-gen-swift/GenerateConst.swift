import BebopPlugin

enum GenerateConst {
    static func generate(_ def: DefinitionDescriptor, options: GeneratorOptions) throws -> [String] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("const missing name")
        }
        guard let constDef = def.constDef else {
            throw CodegenError.malformedDefinition("const '\(defName)' missing body")
        }
        guard let constType = constDef.type else {
            throw CodegenError.malformedDefinition("const '\(defName)' missing type")
        }
        guard let constValue = constDef.value else {
            throw CodegenError.malformedDefinition("const '\(defName)' missing value")
        }
        let name = NamingPolicy.fieldName(defName)
        let swiftType = try TypeMapper.swiftType(for: constType)
        let literal = try literalExpression(constValue, constName: defName)
        let vis = effectiveVisibility(for: def, options: options)
        let prefix = docComment(def.documentation)

        return ["\(prefix)\(vis)let \(name): \(swiftType) = \(literal)"]
    }

    private static func literalExpression(_ value: LiteralValue, constName: String) throws -> String {
        guard let kind = value.kind else {
            throw CodegenError.malformedDefinition("const '\(constName)' literal missing kind")
        }
        switch kind {
        case .bool:
            guard let v = value.boolValue else {
                throw CodegenError.malformedDefinition("const '\(constName)' bool literal missing value")
            }
            return v ? "true" : "false"
        case .int:
            guard let v = value.intValue else {
                throw CodegenError.malformedDefinition("const '\(constName)' int literal missing value")
            }
            return "\(v)"
        case .float:
            guard let v = value.floatValue else {
                throw CodegenError.malformedDefinition("const '\(constName)' float literal missing value")
            }
            if v.isNaN { return "Double.nan" }
            if v.isInfinite { return v > 0 ? "Double.infinity" : "-Double.infinity" }
            return "\(v)"
        case .string:
            guard let s = value.stringValue else {
                throw CodegenError.malformedDefinition("const '\(constName)' string literal missing value")
            }
            let escaped = s
                .replacingOccurrences(of: "\\", with: "\\\\")
                .replacingOccurrences(of: "\"", with: "\\\"")
                .replacingOccurrences(of: "\n", with: "\\n")
            return "\"\(escaped)\""
        case .uuid:
            guard let v = value.uuidValue else {
                throw CodegenError.malformedDefinition("const '\(constName)' uuid literal missing value")
            }
            return "BebopUUID(uuidString: \"\(v.uuidString)\")!"
        default:
            throw CodegenError.malformedDefinition("const '\(constName)' unsupported literal kind \(kind.rawValue)")
        }
    }
}
