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
    try validateUniqueGeneratedNames(
      fieldDecls.map(\.swiftName),
      in: "message '\(defName)'"
    )
    var fieldIndexes: Set<UInt32> = []
    for field in fieldDecls {
      guard field.index > 0, field.index <= UInt8.max else {
        throw CodegenError.malformedDefinition(
          "message '\(defName)' field '\(field.name)' index must be between 1 and 255"
        )
      }
      guard fieldIndexes.insert(field.index).inserted else {
        throw CodegenError.malformedDefinition(
          "message '\(defName)' has duplicate field index \(field.index)"
        )
      }
    }

    var body: [String] = []

    let initParams = fieldDecls.map { "\($0.swiftName): \($0.swiftType)? = nil" }.joined(
      separator: ", ")
    let copyArguments = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(
      separator: ", ")
    if fieldDecls.isEmpty {
      body.append("\(vis)init() {}")
    } else {
      let storageFields = fieldDecls.map { "var \($0.swiftName): \($0.swiftType)?" }
      let storageAssignments = fieldDecls.map { "self.\($0.swiftName) = \($0.swiftName)" }
      let storageLines =
        storageFields
        + ["init(\(initParams)) {"]
        + storageAssignments.map { "    " + $0 }
        + ["}", "func copy() -> _BebopStorage { _BebopStorage(\(copyArguments)) }"]
      let storageBody = storageLines.map { indent($0) }.joined(separator: "\n")
      body.append("private final class _BebopStorage: @unchecked Sendable {\n\(storageBody)\n}")
      body.append("private var _bebopStorage: _BebopStorage")

      for f in fieldDecls {
        body.append(
          """
          \(f.prefix)\(vis)var \(f.swiftName): \(f.swiftType)? {
              get { _bebopStorage.\(f.swiftName) }
              set {
                  if !isKnownUniquelyReferenced(&_bebopStorage) { _bebopStorage = _bebopStorage.copy() }
                  _bebopStorage.\(f.swiftName) = newValue
              }
          }
          """)
      }

      body.append("\(vis)init(\(initParams)) { _bebopStorage = _BebopStorage(\(copyArguments)) }")
    }

    // ==
    var eqBody: [String] = []
    for field in fieldDecls {
      if field.type.kind == .fixedArray {
        eqBody.append("switch (lhs.\(field.swiftName), rhs.\(field.swiftName)) {")
        eqBody.append("case (.none, .none): break")
        eqBody.append("case let (.some(left), .some(right)):")
        eqBody.append(
          contentsOf: try fixedArrayEqualityLines(
            type: field.type,
            lhs: "left",
            rhs: "right"
          ).map { "    " + $0 }
        )
        eqBody.append("default: return false")
        eqBody.append("}")
      } else {
        eqBody.append(
          "if lhs.\(field.swiftName) != rhs.\(field.swiftName) { return false }"
        )
      }
    }
    eqBody.append("return true")
    let eqBodyStr = eqBody.map { indent($0) }.joined(separator: "\n")
    let boolType = TypeMapper.unshadow("Bool")
    body.append(
      "\(vis)static func == (lhs: \(name), rhs: \(name)) -> \(boolType) {\n\(eqBodyStr)\n}")

    // hash
    var hashBody: [String] = []
    for f in fieldDecls {
      if f.type.kind == .fixedArray {
        hashBody.append("if let value = \(f.swiftName) {")
        hashBody.append("    hasher.combine(true)")
        hashBody.append(
          contentsOf: try fixedArrayHashLines(type: f.type, value: "value")
            .map { "    " + $0 }
        )
        hashBody.append("} else {")
        hashBody.append("    hasher.combine(false)")
        hashBody.append("}")
      } else {
        hashBody.append("hasher.combine(\(f.swiftName))")
      }
    }
    let hashBodyStr = hashBody.map { indent($0) }.joined(separator: "\n")
    body.append("\(vis)func hash(into hasher: inout Hasher) {\n\(hashBodyStr)\n}")

    // decode
    var decodeBody: [String] = [
      "// @@bebop_insertion_point(decode_start:\(defName))"
    ]
    for f in fieldDecls {
      decodeBody.append("var \(f.swiftName): \(f.swiftType)? = nil")
    }
    for f in fieldDecls {
      let readExpr = try TypeMapper.readExpression(for: f.type, reader: "fieldReader")
      decodeBody.append("try withField(\(f.index)) { fieldReader in")
      decodeBody.append(indent("\(f.swiftName) = \(readExpr)"))
      decodeBody.append("}")
    }
    decodeBody.append("// @@bebop_insertion_point(decode_end:\(defName))")
    let args = fieldDecls.map { "\($0.swiftName): \($0.swiftName)" }.joined(separator: ", ")
    decodeBody.append("return \(name)(\(args))")
    let messageDecodeBody = decodeBody.map { indent($0) }.joined(separator: "\n")
    let decodeBodyStr = indent(
      "return try reader.readMessage { withField in\n\(messageDecodeBody)\n}"
    )
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
        "if let _v = \(f.swiftName) {\n    tags[fieldCount] = \(String(f.index))\n    offsets[fieldCount] = UInt32(writer.position - payloadStart)\n    fieldCount += 1\n\(indent(writeExpr))\n}"
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

    if !fieldDecls.isEmpty {
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
        "self.init()",
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
      body.append("\(vis)init(from decoder: Decoder) throws {\n\(decCodableStr)\n}")
    }

    var viewBody: [String] = ["private let message: BebopMessageView"]
    for f in fieldDecls {
      let viewType = try TypeMapper.viewType(for: f.type)
      let readExpr = try TypeMapper.viewReadExpression(for: f.type, reader: "fieldReader")
      viewBody.append(
        """
        \(f.prefix)\(vis)var \(f.swiftName): \(viewType)? {
            get throws {
                try message.decodeField(\(f.index)) { fieldReader in
        \(indent(readExpr, 3))
                }
            }
        }
        """)
    }
    viewBody.append("\(vis)var encoded: BebopView { message.encoded }")
    viewBody.append("\(vis)func field(_ tag: UInt8) -> BebopView? { message.field(tag) }")
    viewBody.append(
      "\(vis)init(_ bytes: [UInt8], limits: BebopDecodeLimits = .default) throws { try self.init(BebopView(bytes), limits: limits) }"
    )
    viewBody.append(
      "\(vis)init(_ encoded: BebopView, limits: BebopDecodeLimits = .default) throws { try self.init(indexed: BebopMessageView(encoded, limits: limits)) }"
    )
    let indexedInitBody = ["self.message = message"]
    let indexedInitBodyString = indexedInitBody.map { indent($0) }.joined(separator: "\n")
    viewBody.append(
      "fileprivate init(indexed message: BebopMessageView) {\n\(indexedInitBodyString)\n}"
    )
    let decodedBody = [
      "try message.encoded.withUnsafeBytes { bytes in",
      "    var reader = BebopReader(data: bytes, limits: message.limits)",
      "    return try \(name).decode(from: &reader)",
      "}",
    ].map { indent($0) }.joined(separator: "\n")
    viewBody.append("\(vis)func decoded() throws -> \(name) {\n\(decodedBody)\n}")
    let viewBodyString = viewBody.map { indent($0) }.joined(separator: "\n\n")
    body.append("\(vis)struct View: BebopRecordView {\n\(viewBodyString)\n}")

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
      "\(prefix)\(vis)struct \(name): BebopRecord, BebopReflectable {\n\(bodyStr)\n}"
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
