import BebopPlugin

enum GenerateMessage {
    static func generate(
        _ def: DefinitionDescriptor, nested: [String] = [], options: GeneratorOptions
    ) throws -> [String] {
        guard let defName = def.name else {
            throw CodegenError.malformedDefinition("message missing name")
        }
        guard let defFqn = def.fqn else {
            throw CodegenError.malformedDefinition("message '\(defName)' missing fqn")
        }
        guard let messageDef = def.messageDef else {
            throw CodegenError.malformedDefinition("message '\(defName)' missing body")
        }
        let fields = messageDef.fields ?? []
        let name = NamingPolicy.typeName(defName)
        let vis = effectiveVisibility(for: def, options: options)
        let prefix = declPrefix(doc: def.documentation, decorators: def.decorators)

        let fieldDecls = try fields.map {
            field -> (
                name: String, swiftName: String, type: TypeDescriptor, swiftType: String, index: UInt32,
                prefix: String
            ) in
            guard let fName = field.name else {
                throw CodegenError.malformedDefinition("message '\(defName)' field missing name")
            }
            guard let fType = field.type else {
                throw CodegenError.malformedDefinition("message '\(defName)' field '\(fName)' missing type")
            }
            guard let fIndex = field.index else {
                throw CodegenError.malformedDefinition(
                    "message '\(defName)' field '\(fName)' missing index")
            }
            return try (
                name: fName,
                swiftName: NamingPolicy.fieldName(fName),
                type: fType,
                swiftType: TypeMapper.swiftType(for: fType),
                index: fIndex,
                prefix: declPrefix(doc: field.documentation, decorators: field.decorators)
            )
        }.sorted { $0.index < $1.index }

        var body: [String] = []

        // fields
        for f in fieldDecls {
            body.append("\(f.prefix)\(vis)var \(f.swiftName): \(f.swiftType)?")
        }

        // init
        let initParams = fieldDecls.map { "\($0.swiftName): \($0.swiftType)? = nil" }.joined(
            separator: ", ")
        var initBody: [String] = []
        for f in fieldDecls {
            initBody.append("self.\(f.swiftName) = \(f.swiftName)")
        }
        let initBodyStr = initBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)init(\(initParams)) {\n\(initBodyStr)\n}")

        // ==
        let eqExpr = fieldDecls.map { "lhs.\($0.swiftName) == rhs.\($0.swiftName)" }.joined(
            separator: " && ")
        let eqBody = ["return \(eqExpr.isEmpty ? "true" : eqExpr)"]
        let eqBodyStr = eqBody.map { indent($0) }.joined(separator: "\n")
        let boolType = TypeMapper.unshadow("Bool")
        body.append("\(vis)static func == (lhs: \(name), rhs: \(name)) -> \(boolType) {\n\(eqBodyStr)\n}")

        // hash
        var hashBody: [String] = []
        for f in fieldDecls {
            hashBody.append("hasher.combine(\(f.swiftName))")
        }
        let hashBodyStr = hashBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)func hash(into hasher: inout Hasher) {\n\(hashBodyStr)\n}")

        // decode
        var decodeBody: [String] = [
            "// @@bebop_insertion_point(decode_start:\(defName))",
            "let message = try reader.readMessage()",
        ]
        for f in fieldDecls {
            decodeBody.append("var \(f.swiftName): \(f.swiftType)? = nil")
        }
        for f in fieldDecls {
            let readExpr = try TypeMapper.readExpression(for: f.type, reader: "fieldReader")
            decodeBody.append("if let field = message.field(\(f.index)) {")
            decodeBody.append("    var fieldReader = BebopReader(data: field)")
            decodeBody.append("    \(f.swiftName) = \(readExpr)")
            decodeBody.append("    guard fieldReader.position == field.count else {")
            decodeBody.append("        throw BebopDecodingError.trailingData")
            decodeBody.append("    }")
            decodeBody.append("}")
        }
        decodeBody.append("// @@bebop_insertion_point(decode_end:\(defName))")
        let args = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(separator: ", ")
        decodeBody.append("return \(name)(\(args))")
        let decodeBodyStr = decodeBody.map { indent($0) }.joined(separator: "\n")
        body.append(
            "\(vis)static func decode(from reader: inout BebopReader) throws -> \(name) {\n\(decodeBodyStr)\n}"
        )

        // encode
        var encodeBody: [String] = [
            "// @@bebop_insertion_point(encode_start:\(defName))",
            "let payloadStart = writer.beginMessage()",
        ]
        if !fieldDecls.isEmpty {
            encodeBody.append("var tags = InlineArray<\(fieldDecls.count), UInt8> { _ in 0 }")
            encodeBody.append("var offsets = InlineArray<\(fieldDecls.count), UInt32> { _ in 0 }")
            encodeBody.append("var fieldCount = 0")
        }
        for f in fieldDecls {
            let writeExpr = try TypeMapper.writeExpression(for: f.type, value: "_v")
            encodeBody.append(
                "if let _v = \(f.swiftName) {\n    tags[fieldCount] = \(String(f.index))\n    offsets[fieldCount] = UInt32(writer.position - payloadStart)\n    fieldCount += 1\n    \(writeExpr)\n}"
            )
        }
        if fieldDecls.isEmpty {
            encodeBody.append("writer.endMessage(payloadStart: payloadStart)")
        } else {
            encodeBody.append(
                "writer.endMessage(payloadStart: payloadStart, tags: tags, offsets: offsets, count: fieldCount)"
            )
        }
        encodeBody.append("// @@bebop_insertion_point(encode_end:\(defName))")
        let encodeBodyStr = encodeBody.map { indent($0) }.joined(separator: "\n")
        body.append("\(vis)func encode(to writer: inout BebopWriter) {\n\(encodeBodyStr)\n}")

        // encodedSize
        if fieldDecls.isEmpty {
            body.append("\(vis)var encodedSize: Int { 5 }")
        } else {
            var sizeBody = [
                "var tags = InlineArray<\(fieldDecls.count), UInt8> { _ in 0 }",
                "var fieldCount = 0",
                "var payloadSize = 0",
            ]
            for f in fieldDecls {
                let sizeExpr = try TypeMapper.sizeExpression(for: f.type, value: "_v")
                let needsValue = sizeExpr.contains("_v")
                if needsValue {
                    sizeBody.append(
                        "if let _v = \(f.swiftName) { tags[fieldCount] = \(f.index); fieldCount += 1; payloadSize += \(sizeExpr) }"
                    )
                } else {
                    sizeBody.append(
                        "if \(f.swiftName) != nil { tags[fieldCount] = \(f.index); fieldCount += 1; payloadSize += \(sizeExpr) }"
                    )
                }
            }
            sizeBody.append(
                "return BebopMessageLayout.encodedSize(payloadSize: payloadSize, tags: tags, count: fieldCount)"
            )
            let sizeBodyStr = sizeBody.map { indent($0) }.joined(separator: "\n")
            body.append("\(vis)var encodedSize: Int {\n\(sizeBodyStr)\n}")
        }

        // coding keys
        if !fieldDecls.isEmpty {
            let ckFields = fieldDecls.map { (swiftName: $0.swiftName, originalName: $0.name) }
            body.append(codingKeysDecl(ckFields))
        }

        let hasFixedArray = fieldDecls.contains { $0.type.kind == .fixedArray }

        if hasFixedArray {
            var encCodableBody = ["var container = encoder.container(keyedBy: CodingKeys.self)"]
            for f in fieldDecls {
                if f.type.kind == .fixedArray {
                    encCodableBody.append("if let \(f.swiftName) = \(f.swiftName) {")
                    encCodableBody.append(
                        "    var \(f.swiftName)Container = container.nestedUnkeyedContainer(forKey: .\(f.swiftName))"
                    )
                    let encLines = try fixedArrayEncodeLines(
                        type: f.type, container: "\(f.swiftName)Container", value: f.swiftName
                    )
                    encCodableBody.append(contentsOf: encLines.map { "    " + $0 })
                    encCodableBody.append("}")
                } else {
                    encCodableBody.append(
                        "try container.encodeIfPresent(\(f.swiftName), forKey: .\(f.swiftName))"
                    )
                }
            }
            let encCodableStr = encCodableBody.map { indent($0) }.joined(separator: "\n")
            body.append("\(vis)func encode(to encoder: Encoder) throws {\n\(encCodableStr)\n}")

            var decCodableBody = [
                "let container = try decoder.container(keyedBy: CodingKeys.self)",
            ]
            for f in fieldDecls {
                if f.type.kind == .fixedArray {
                    decCodableBody.append("if container.contains(.\(f.swiftName)) {")
                    decCodableBody.append(
                        "    var \(f.swiftName)Container = try container.nestedUnkeyedContainer(forKey: .\(f.swiftName))"
                    )
                    let decodeExpr = try fixedArrayDecodeExpr(
                        type: f.type, container: "\(f.swiftName)Container"
                    )
                    decCodableBody.append("    \(f.swiftName) = try \(decodeExpr)")
                    decCodableBody.append("}")
                } else {
                    decCodableBody.append(
                        "\(f.swiftName) = try container.decodeIfPresent(\(f.swiftType).self, forKey: .\(f.swiftName))"
                    )
                }
            }
            let decCodableStr = decCodableBody.map { indent($0) }.joined(separator: "\n")
            body.append(
                "\(vis)required init(from decoder: Decoder) throws {\n\(decCodableStr)\n}"
            )
        }

        // immutable zero-copy view
        var viewBody: [String] = ["private let message: BebopMessageView"]
        for f in fieldDecls {
            let viewType = try TypeMapper.viewType(for: f.type)
            viewBody.append("\(f.prefix)\(vis)let \(f.swiftName): \(viewType)?")
        }
        viewBody.append(
            "\(vis)convenience init(_ bytes: [UInt8]) throws { try self.init(BebopView(bytes)) }"
        )
        viewBody.append(
            "\(vis)convenience init(_ encoded: BebopView) throws { try self.init(indexed: BebopMessageView(encoded)) }"
        )
        var indexedInitBody = ["self.message = message"]
        for f in fieldDecls {
            let readExpr = try TypeMapper.viewReadExpression(for: f.type, reader: "fieldReader")
            indexedInitBody.append("if let field = message.field(\(f.index)) {")
            indexedInitBody.append("    var fieldReader = BebopViewReader(field)")
            indexedInitBody.append("    self.\(f.swiftName) = \(readExpr)")
            indexedInitBody.append("    try fieldReader.finish()")
            indexedInitBody.append("} else {")
            indexedInitBody.append("    self.\(f.swiftName) = nil")
            indexedInitBody.append("}")
        }
        let indexedInitBodyString = indexedInitBody.map { indent($0) }.joined(separator: "\n")
        viewBody.append(
            "fileprivate init(indexed message: BebopMessageView) throws {\n\(indexedInitBodyString)\n}"
        )
        let decodedBody = [
            "try message.encoded.withUnsafeBytes { bytes in",
            "    var reader = BebopReader(data: bytes)",
            "    return try \(name).decode(from: &reader)",
            "}",
        ].map { indent($0) }.joined(separator: "\n")
        viewBody.append("\(vis)func decoded() throws -> \(name) {\n\(decodedBody)\n}")
        let viewBodyString = viewBody.map { indent($0) }.joined(separator: "\n\n")
        body.append("\(vis)final class View: Sendable {\n\(viewBodyString)\n}")

        body.append(
            """
            \(vis)static func readView(from reader: inout BebopViewReader) throws -> View {
                try View(indexed: reader.readMessageView())
            }
            """)

        body.append(
            """
            \(vis)static let bebopReflection = BebopTypeReflection(
                name: \(quoted(defName)),
                fqn: \(quoted(defFqn)),
                kind: .message,
                detail: .message(
                    MessageReflection(fields: [
            \(indent(reflectionFields(fieldDecls), 3))
                    ])
                )
            )
            """
        )

        for decl in nested {
            body.append(decl)
        }

        body.append("// @@bebop_insertion_point(message_scope:\(defName))")

        let bodyStr = body.map { indent($0) }.joined(separator: "\n\n")
        return [
            "\(prefix)\(vis)final class \(name): BebopRecord, BebopReflectable, @unchecked Sendable {\n\(bodyStr)\n}",
        ]
    }

    private static func reflectionFields(
        _ fields: [(
            name: String, swiftName: String, type: TypeDescriptor, swiftType: String, index: UInt32,
            prefix: String
        )]
    ) -> String {
        fields.map { f in
            """
            BebopFieldReflection(
                name: \(quoted(f.name)),
                index: \(f.index),
                typeName: \(quoted(f.swiftType))
            )
            """
        }.joined(separator: ",\n")
    }
}
