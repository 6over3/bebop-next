import SwiftSyntax
import SwiftSyntaxBuilder
import BebopPlugin

enum GenerateStruct {
    static func generate(_ def: DefinitionDescriptor, nested: [DeclSyntax] = [], options: GeneratorOptions) throws -> [DeclSyntax] {
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

        let structDecl = try StructDeclSyntax(
            "\(raw: prefix)\(raw: vis)struct \(raw: name): BebopRecord, BebopReflectable"
        ) {
            for f in fieldDecls {
                DeclSyntax("\(raw: f.doc)\(raw: vis)\(raw: binding) \(raw: f.swiftName): \(raw: f.swiftType)")
            }

            if !fieldDecls.isEmpty {
                let ckFields = fieldDecls.map { (swiftName: $0.swiftName, originalName: $0.name) }
                DeclSyntax("\(raw: codingKeysDecl(ckFields))")
            }

            try FunctionDeclSyntax(
                "\(raw: vis)static func decode(from reader: inout BebopReader) throws -> \(raw: name)"
            ) {
                for f in fieldDecls {
                    let readExpr = try TypeMapper.readExpression(for: f.type)
                    DeclSyntax("let \(raw: f.swiftName) = \(raw: readExpr)")
                }
                let args = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(separator: ", ")
                StmtSyntax("return \(raw: name)(\(raw: args))")
            }

            try FunctionDeclSyntax("\(raw: vis)func encode(to writer: inout BebopWriter)") {
                for f in fieldDecls {
                    let writeExpr = try TypeMapper.writeExpression(for: f.type, value: f.swiftName)
                    ExprSyntax("\(raw: writeExpr)")
                }
            }

            let fixedSizes = fieldDecls.compactMap { TypeMapper.fixedSize(for: $0.type) }
            if fixedSizes.count == fieldDecls.count {
                DeclSyntax("\(raw: vis)var encodedSize: Int { \(raw: String(fixedSizes.reduce(0, +))) }")
            } else {
                try VariableDeclSyntax("\(raw: vis)var encodedSize: Int") {
                    DeclSyntax("var size = 0")
                    for f in fieldDecls {
                        let expr = try TypeMapper.sizeExpression(for: f.type, value: f.swiftName)
                        ExprSyntax("size += \(raw: expr)")
                    }
                    StmtSyntax("return size")
                }
            }

            DeclSyntax(
                "\(raw: vis)static let bebopReflection = BebopTypeReflection(name: \(literal: defName), fqn: \(literal: defFqn), kind: .struct, detail: .struct(StructReflection(fields: [\(raw: reflectionFields(fieldDecls))])))"
            )

            for decl in nested {
                decl
            }
        }

        return [DeclSyntax(structDecl)]
    }

    private static func reflectionFields(_ fields: [(name: String, swiftName: String, type: TypeDescriptor, swiftType: String, doc: String)]) -> String {
        fields.map { f in
            "BebopFieldReflection(name: \(quoted(f.name)), index: 0, typeName: \(quoted(f.swiftType)))"
        }.joined(separator: ", ")
    }
}
