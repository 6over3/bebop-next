import BebopPlugin

enum GenerateStruct {
    static func generate(_ def: DefinitionDescriptor, nested: [String] = [], options: GeneratorOptions) throws -> [String] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("struct missing name")
        }
        guard let defFqn = def.fqn else {
            throw CodegenError.malformedDefinition("struct '\(defName)' missing fqn")
        }
        guard let structDef = def.structDef else {
            throw CodegenError.malformedDefinition("struct '\(defName)' missing body")
        }
        let fields = structDef.fields ?? []
        let name = NamingPolicy.typeName(defName)
        let vis = effectiveVisibility(for: def, options: options)
        let prefix = declPrefix(doc: def.documentation, decorators: def.decorators)
        let binding = structDef.isMutable == true ? "var" : "let"

        let fieldDecls = try fields.map { field -> (name: String, swiftName: String, type: TypeDescriptor, swiftType: String, doc: String) in
            guard let fName = field.name else {
                throw CodegenError.malformedDefinition("struct '\(defName)' field missing name")
            }
            guard let fType = field.type else {
                throw CodegenError.malformedDefinition("struct '\(defName)' field '\(fName)' missing type")
            }
            return (
                name: fName,
                swiftName: NamingPolicy.fieldName(fName),
                type: fType,
                swiftType: try TypeMapper.swiftType(for: fType),
                doc: docComment(field.documentation)
            )
        }

        var body: [String] = []

        for f in fieldDecls {
            body.append("\(f.doc)\(vis)\(binding) \(f.swiftName): \(f.swiftType)")
        }

        if !fieldDecls.isEmpty {
            let ckFields = fieldDecls.map { (swiftName: $0.swiftName, originalName: $0.name) }
            body.append(codingKeysDecl(ckFields))
        }

        // decode
        var decodeBody: [String] = []
        for f in fieldDecls {
            let readExpr = try TypeMapper.readExpression(for: f.type)
            decodeBody.append("let \(f.swiftName) = \(readExpr)")
        }
        let args = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(separator: ", ")
        decodeBody.append("return \(name)(\(args))")
        let decodeBodyStr = decodeBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)static func decode(from reader: inout BebopReader) throws -> \(name) {\n\(decodeBodyStr)\n}")

        // encode
        var encodeBody: [String] = []
        for f in fieldDecls {
            let writeExpr = try TypeMapper.writeExpression(for: f.type, value: f.swiftName)
            encodeBody.append(writeExpr)
        }
        let encodeBodyStr = encodeBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)func encode(to writer: inout BebopWriter) {\n\(encodeBodyStr)\n}")

        // encodedSize
        let fixedSizes = fieldDecls.compactMap { TypeMapper.fixedSize(for: $0.type) }
        if fixedSizes.count == fieldDecls.count {
            body.append("\(vis)var encodedSize: Int { \(String(fixedSizes.reduce(0, +))) }")
        } else {
            var sizeBody: [String] = ["var size = 0"]
            for f in fieldDecls {
                let expr = try TypeMapper.sizeExpression(for: f.type, value: f.swiftName)
                sizeBody.append("size += \(expr)")
            }
            sizeBody.append("return size")
            let sizeBodyStr = sizeBody.map { indent($0) }.joined(separator: "\n")
            body.append("\(vis)var encodedSize: Int {\n\(sizeBodyStr)\n}")
        }

        body.append(
            "\(vis)static let bebopReflection = BebopTypeReflection(name: \(quoted(defName)), fqn: \(quoted(defFqn)), kind: .struct, detail: .struct(StructReflection(fields: [\(reflectionFields(fieldDecls))])))"
        )

        for decl in nested {
            body.append(decl)
        }

        let bodyStr = body.map { indent($0) }.joined(separator: "\n")
        return ["\(prefix)\(vis)struct \(name): BebopRecord, BebopReflectable {\n\(bodyStr)\n}"]
    }

    private static func reflectionFields(_ fields: [(name: String, swiftName: String, type: TypeDescriptor, swiftType: String, doc: String)]) -> String {
        fields.map { f in
            "BebopFieldReflection(name: \(quoted(f.name)), index: 0, typeName: \(quoted(f.swiftType)))"
        }.joined(separator: ", ")
    }
}
