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
      return (
        name: fName,
        swiftName: NamingPolicy.fieldName(fName),
        type: fType,
        swiftType: try TypeMapper.swiftType(for: fType),
        index: fIndex,
        prefix: declPrefix(doc: field.documentation, decorators: field.decorators)
      )
    }

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
    let eqBody: [String] = ["return \(eqExpr.isEmpty ? "true" : eqExpr)"]
    let eqBodyStr = eqBody.map { indent($0) }.joined(separator: "\n")
    body.append("\(vis)static func == (lhs: \(name), rhs: \(name)) -> Bool {\n\(eqBodyStr)\n}")

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
      "let length = try reader.readMessageLength()",
      "let end = reader.position + Int(length)",
    ]
    for f in fieldDecls {
      decodeBody.append("var \(f.swiftName): \(f.swiftType)? = nil")
    }
    var switchLines: [String] = [
      "while reader.position < end {",
      "    let tag = try reader.readTag()",
      "    if tag == 0 { break }",
      "    switch tag {",
    ]
    for f in fieldDecls {
      let readExpr = try TypeMapper.readExpression(for: f.type)
      switchLines.append("    case \(f.index):")
      switchLines.append("        \(f.swiftName) = \(readExpr)")
    }
    switchLines.append("    default:")
    switchLines.append("        try reader.skip(end - reader.position)")
    switchLines.append("    }")
    switchLines.append("}")
    decodeBody.append(switchLines.joined(separator: "\n"))
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
      "let pos = writer.reserveMessageLength()",
    ]
    for f in fieldDecls {
      let writeExpr = try TypeMapper.writeExpression(for: f.type, value: "_v")
      encodeBody.append(
        "if let _v = \(f.swiftName) {\n    writer.writeTag(\(String(f.index)))\n    \(writeExpr)\n}"
      )
    }
    encodeBody.append("writer.writeEndMarker()")
    encodeBody.append("writer.fillMessageLength(at: pos)")
    encodeBody.append("// @@bebop_insertion_point(encode_end:\(defName))")
    let encodeBodyStr = encodeBody.map { indent($0) }.joined(separator: "\n")
    body.append("\(vis)func encode(to writer: inout BebopWriter) {\n\(encodeBodyStr)\n}")

    // encodedSize
    if fieldDecls.isEmpty {
      body.append("\(vis)var encodedSize: Int { 5 }")
    } else {
      var sizeBody: [String] = ["var size = 5"]
      for f in fieldDecls {
        let sizeExpr = try TypeMapper.sizeExpression(for: f.type, value: "_v")
        let needsValue = sizeExpr.contains("_v")
        if needsValue {
          sizeBody.append("if let _v = \(f.swiftName) { size += 1 + \(sizeExpr) }")
        } else {
          sizeBody.append("if \(f.swiftName) != nil { size += 1 + \(sizeExpr) }")
        }
      }
      sizeBody.append("return size")
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
      var encCodableBody: [String] = ["var container = encoder.container(keyedBy: CodingKeys.self)"]
      for f in fieldDecls {
        if f.type.kind == .fixedArray {
          let size = f.type.fixedArraySize!
          encCodableBody.append("if let \(f.swiftName) = \(f.swiftName) {")
          encCodableBody.append(
            "    var \(f.swiftName)Container = container.nestedUnkeyedContainer(forKey: .\(f.swiftName))"
          )
          encCodableBody.append(
            "    for i in 0..<\(size) { try \(f.swiftName)Container.encode(\(f.swiftName)[i]) }"
          )
          encCodableBody.append("}")
        } else {
          encCodableBody.append(
            "try container.encodeIfPresent(\(f.swiftName), forKey: .\(f.swiftName))"
          )
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
          decCodableBody.append("if container.contains(.\(f.swiftName)) {")
          decCodableBody.append(
            "    var \(f.swiftName)Container = try container.nestedUnkeyedContainer(forKey: .\(f.swiftName))"
          )
          decCodableBody.append(
            "    \(f.swiftName) = try InlineArray<\(size), \(elemType)> { _ in try \(f.swiftName)Container.decode(\(elemType).self) }"
          )
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
      "\(prefix)\(vis)final class \(name): BebopRecord, BebopReflectable, @unchecked Sendable {\n\(bodyStr)\n}"
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
