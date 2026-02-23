import BebopPlugin

enum GenerateStruct {
  static func generate(
    _ def: DefinitionDescriptor, nested: [String] = [], options: GeneratorOptions
  ) throws -> [String] {
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

    let fieldDecls = try fields.map {
      field -> (
        name: String, swiftName: String, type: TypeDescriptor, swiftType: String, doc: String
      ) in
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

    // public init
    if fieldDecls.isEmpty {
      body.append("\(vis)init() {}")
    } else {
      let initParams = fieldDecls.map {
        $0.type.kind == .map
          ? "\($0.swiftName): \($0.swiftType) = [:]"
          : "\($0.swiftName): \($0.swiftType)"
      }.joined(separator: ", ")
      let initAssigns = fieldDecls.map { "self.\($0.swiftName) = \($0.swiftName)" }
        .map { indent($0) }.joined(separator: "\n")
      body.append("\(vis)init(\(initParams)) {\n\(initAssigns)\n}")
    }

    let hasFixedArray = fieldDecls.contains { $0.type.kind == .fixedArray }

    if hasFixedArray {
      let eqExpr = fieldDecls.map { "lhs.\($0.swiftName) == rhs.\($0.swiftName)" }
        .joined(separator: " && ")
      body.append(
        "\(vis)static func == (lhs: \(name), rhs: \(name)) -> Bool {\n\(indent("return \(eqExpr)"))\n}"
      )

      var hashBody: [String] = []
      for f in fieldDecls {
        hashBody.append("hasher.combine(\(f.swiftName))")
      }
      let hashBodyStr = hashBody.map { indent($0) }.joined(separator: "\n")
      body.append("\(vis)func hash(into hasher: inout Hasher) {\n\(hashBodyStr)\n}")

      var encCodableBody: [String] = ["var container = encoder.container(keyedBy: CodingKeys.self)"]
      for f in fieldDecls {
        if f.type.kind == .fixedArray {
          let size = f.type.fixedArraySize!
          encCodableBody.append(
            "var \(f.swiftName)Container = container.nestedUnkeyedContainer(forKey: .\(f.swiftName))"
          )
          encCodableBody.append(
            "for i in 0..<\(size) { try \(f.swiftName)Container.encode(\(f.swiftName)[i]) }"
          )
        } else {
          encCodableBody.append("try container.encode(\(f.swiftName), forKey: .\(f.swiftName))")
        }
      }
      let encCodableStr = encCodableBody.map { indent($0) }.joined(separator: "\n")
      body.append("\(vis)func encode(to encoder: Encoder) throws {\n\(encCodableStr)\n}")

      var decCodableBody: [String] = [
        "let container = try decoder.container(keyedBy: CodingKeys.self)"
      ]
      for f in fieldDecls {
        if f.type.kind == .fixedArray {
          let size = f.type.fixedArraySize!
          let elemType = try TypeMapper.swiftType(for: f.type.fixedArrayElement!)
          decCodableBody.append(
            "var \(f.swiftName)Container = try container.nestedUnkeyedContainer(forKey: .\(f.swiftName))"
          )
          decCodableBody.append(
            "let \(f.swiftName) = try InlineArray<\(size), \(elemType)> { _ in try \(f.swiftName)Container.decode(\(elemType).self) }"
          )
        } else {
          decCodableBody.append(
            "let \(f.swiftName) = try container.decode(\(f.swiftType).self, forKey: .\(f.swiftName))"
          )
        }
      }
      let initArgs = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }
        .joined(separator: ", ")
      decCodableBody.append("self.init(\(initArgs))")
      let decCodableStr = decCodableBody.map { indent($0) }.joined(separator: "\n")
      body.append("\(vis)init(from decoder: Decoder) throws {\n\(decCodableStr)\n}")
    }

    // decode
    var decodeBody: [String] = ["// @@bebop_insertion_point(decode_start:\(defName))"]
    for f in fieldDecls {
      let readExpr = try TypeMapper.readExpression(for: f.type)
      decodeBody.append("let \(f.swiftName) = \(readExpr)")
    }
    decodeBody.append("// @@bebop_insertion_point(decode_end:\(defName))")
    let args = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(separator: ", ")
    decodeBody.append("return \(name)(\(args))")
    let decodeBodyStr = decodeBody.map { indent($0) }.joined(separator: "\n")
    body.append(
      "\(vis)static func decode(from reader: inout BebopReader) throws -> \(name) {\n\(decodeBodyStr)\n}"
    )

    // encode
    var encodeBody: [String] = ["// @@bebop_insertion_point(encode_start:\(defName))"]
    for f in fieldDecls {
      let writeExpr = try TypeMapper.writeExpression(for: f.type, value: f.swiftName)
      encodeBody.append(writeExpr)
    }
    encodeBody.append("// @@bebop_insertion_point(encode_end:\(defName))")
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
      """
      \(vis)static let bebopReflection = BebopTypeReflection(
          name: \(quoted(defName)),
          fqn: \(quoted(defFqn)),
          kind: .struct,
          detail: .struct(
              StructReflection(fields: [
      \(indent(reflectionFields(fieldDecls), 3))
              ])
          )
      )
      """
    )

    for decl in nested {
      body.append(decl)
    }

    body.append("// @@bebop_insertion_point(struct_scope:\(defName))")

    let bodyStr = body.map { indent($0) }.joined(separator: "\n\n")
    return ["\(prefix)\(vis)struct \(name): BebopRecord, BebopReflectable {\n\(bodyStr)\n}"]
  }

  private static func reflectionFields(
    _ fields: [(
      name: String, swiftName: String, type: TypeDescriptor, swiftType: String, doc: String
    )]
  ) -> String {
    fields.map { f in
      """
      BebopFieldReflection(
          name: \(quoted(f.name)),
          index: 0,
          typeName: \(quoted(f.swiftType))
      )
      """
    }.joined(separator: ",\n")
  }
}
