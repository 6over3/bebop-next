import SwiftSyntax
import SwiftSyntaxBuilder
import BebopPlugin

enum GenerateMessage {
    static func generate(_ def: DefinitionDescriptor, nested: [DeclSyntax] = [], options: GeneratorOptions) throws -> [DeclSyntax] {
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

        let fieldDecls = try fields.map { field -> (name: String, swiftName: String, type: TypeDescriptor, swiftType: String, index: UInt32, prefix: String) in
            guard let fName = field.name else {
                throw CodegenError.malformedDefinition("message '\(defName)' field missing name")
            }
            guard let fType = field.type else {
                throw CodegenError.malformedDefinition("message '\(defName)' field '\(fName)' missing type")
            }
            guard let fIndex = field.index else {
                throw CodegenError.malformedDefinition("message '\(defName)' field '\(fName)' missing index")
            }
            return (
                name: fName,
                swiftName: NamingPolicy.fieldName(fName),
                type: fType,
                swiftType: try TypeMapper.swiftType(for: fType),
                index: fIndex,
                prefix: declPrefix(doc: field.documentation, decorators: field.decorators)
            )
        }

        let classDecl = try ClassDeclSyntax(
            "\(raw: prefix)\(raw: vis)final class \(raw: name): BebopRecord, BebopReflectable, @unchecked Sendable"
        ) {
            for f in fieldDecls {
                DeclSyntax("\(raw: f.prefix)\(raw: vis)var \(raw: f.swiftName): \(raw: f.swiftType)?")
            }

            let initParams = fieldDecls.map { "\($0.swiftName): \($0.swiftType)? = nil" }.joined(separator: ", ")
            try InitializerDeclSyntax("\(raw: vis)init(\(raw: initParams))") {
                for f in fieldDecls {
                    ExprSyntax("self.\(raw: f.swiftName) = \(raw: f.swiftName)")
                }
            }

            let eqExpr = fieldDecls.map { "lhs.\($0.swiftName) == rhs.\($0.swiftName)" }.joined(separator: " && ")
            try FunctionDeclSyntax(
                "\(raw: vis)static func == (lhs: \(raw: name), rhs: \(raw: name)) -> Bool"
            ) {
                StmtSyntax("return \(raw: eqExpr.isEmpty ? "true" : eqExpr)")
            }

            try FunctionDeclSyntax("\(raw: vis)func hash(into hasher: inout Hasher)") {
                for f in fieldDecls {
                    ExprSyntax("hasher.combine(\(raw: f.swiftName))")
                }
            }

            try FunctionDeclSyntax(
                "\(raw: vis)static func decode(from reader: inout BebopReader) throws -> \(raw: name)"
            ) {
                DeclSyntax("let length = try reader.readMessageLength()")
                DeclSyntax("let end = reader.position + Int(length)")

                for f in fieldDecls {
                    DeclSyntax("var \(raw: f.swiftName): \(raw: f.swiftType)? = nil")
                }

                StmtSyntax(
                    """
                    while reader.position < end {
                        let tag = try reader.readTag()
                        if tag == 0 { break }
                        switch tag {
                        \(raw: try switchCases(fieldDecls))
                        default:
                            try reader.skip(end - reader.position)
                        }
                    }
                    """
                )

                let args = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(separator: ", ")
                StmtSyntax("return \(raw: name)(\(raw: args))")
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to writer: inout BebopWriter)") {
                DeclSyntax("let pos = writer.reserveMessageLength()")

                for f in fieldDecls {
                    let writeExpr = try TypeMapper.writeExpression(for: f.type, value: "_v")
                    CodeBlockItemSyntax(
                        """
                        if let _v = \(raw: f.swiftName) {
                            writer.writeTag(\(raw: String(f.index)))
                            \(raw: writeExpr)
                        }
                        """
                    )
                }

                ExprSyntax("writer.writeEndMarker()")
                ExprSyntax("writer.fillMessageLength(at: pos)")
            }

            if fieldDecls.isEmpty {
                DeclSyntax("\(raw: vis)var encodedSize: Int { 5 }")
            } else {
                try VariableDeclSyntax("\(raw: vis)var encodedSize: Int") {
                    DeclSyntax("var size = 5")
                    for f in fieldDecls {
                        let sizeExpr = try TypeMapper.sizeExpression(for: f.type, value: "_v")
                        let needsValue = sizeExpr.contains("_v")
                        if needsValue {
                            CodeBlockItemSyntax(
                                """
                                if let _v = \(raw: f.swiftName) { size += 1 + \(raw: sizeExpr) }
                                """
                            )
                        } else {
                            CodeBlockItemSyntax(
                                """
                                if \(raw: f.swiftName) != nil { size += 1 + \(raw: sizeExpr) }
                                """
                            )
                        }
                    }
                    StmtSyntax("return size")
                }
            }

            if !fieldDecls.isEmpty {
                let ckFields = fieldDecls.map { (swiftName: $0.swiftName, originalName: $0.name) }
                DeclSyntax("\(raw: codingKeysDecl(ckFields))")
            }

            DeclSyntax(
                "\(raw: vis)static let bebopReflection = BebopTypeReflection(name: \(literal: defName), fqn: \(literal: defFqn), kind: .message, detail: .message(MessageReflection(fields: [\(raw: reflectionFields(fieldDecls))])))"
            )

            for decl in nested {
                decl
            }
        }

        return [DeclSyntax(classDecl)]
    }

    private static func switchCases(_ fields: [(name: String, swiftName: String, type: TypeDescriptor, swiftType: String, index: UInt32, prefix: String)]) throws -> String {
        var result = ""
        for f in fields {
            let readExpr = try TypeMapper.readExpression(for: f.type)
            result += "case \(f.index):\n"
            result += "    \(f.swiftName) = \(readExpr)\n"
        }
        return result
    }

    private static func reflectionFields(_ fields: [(name: String, swiftName: String, type: TypeDescriptor, swiftType: String, index: UInt32, prefix: String)]) -> String {
        fields.map { f in
            "BebopFieldReflection(name: \(quoted(f.name)), index: \(f.index), typeName: \(quoted(f.swiftType)))"
        }.joined(separator: ", ")
    }
}
