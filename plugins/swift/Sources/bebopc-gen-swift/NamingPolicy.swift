import BebopPlugin

enum NamingPolicy {
  static func typeName(_ name: String) -> String {
    escapeKeyword(name)
  }

  static func fieldName(_ name: String) -> String {
    escapeKeyword(toCamelCase(name))
  }

  static func enumCaseName(_ name: String) -> String {
    escapeKeyword(toCamelCase(name))
  }

  static func unionCaseName(_ name: String) -> String {
    escapeKeyword(toCamelCase(name))
  }

  static func registerSchemas(_ schemas: [SchemaDescriptor]) throws {
    fqnMap.removeAll()
    fqnMap["bebop.Any"] = "BebopAny"
    fqnMap["bebop.Empty"] = "BebopEmpty"
    for schema in schemas {
      let prefix: String
      if let pkg = schema.package {
        prefix = pkg + "."
      } else {
        prefix = ""
      }
      if let definitions = schema.definitions {
        for def in definitions {
          try registerDefinition(def, prefix: prefix)
        }
      }
    }
  }

  private static func registerDefinition(_ def: DefinitionDescriptor, prefix: String) throws {
    guard let fqn = def.fqn else {
      throw CodegenError.malformedDefinition("definition missing fqn during registration")
    }
    let stripped: String
    if !prefix.isEmpty && fqn.hasPrefix(prefix) {
      stripped = String(fqn.dropFirst(prefix.count))
    } else {
      stripped = fqn
    }
    let swiftName = stripped.split(separator: ".").map { typeName(String($0)) }.joined(
      separator: ".")
    fqnMap[fqn] = swiftName

    if let nested = def.nested {
      for child in nested {
        try registerDefinition(child, prefix: prefix)
      }
    }
  }

  nonisolated(unsafe) private static var fqnMap: [String: String] = [:]

  static func fqnToTypeName(_ fqn: String) -> String {
    if let cached = fqnMap[fqn] {
      return cached
    }
    let parts = fqn.split(separator: ".")
    if parts.count <= 1 {
      return typeName(fqn)
    }
    let typeParts = parts.dropFirst()
    return typeParts.map { typeName(String($0)) }.joined(separator: ".")
  }

  private static func toCamelCase(_ name: String) -> String {
    guard !name.isEmpty else { return name }

    if name.contains("_") {
      let parts = name.split(separator: "_", omittingEmptySubsequences: true)
      guard let first = parts.first else { return name }
      var result = first.lowercased()
      for part in parts.dropFirst() {
        let lower = part.lowercased()
        result.append(lower.prefix(1).uppercased())
        result.append(contentsOf: lower.dropFirst())
      }
      return result
    }

    if name.first?.isLowercase == true { return name }

    var result = ""
    var i = name.startIndex
    while i < name.endIndex && name[i].isUppercase {
      let next = name.index(after: i)
      if next < name.endIndex && name[next].isUppercase {
        result.append(contentsOf: name[i].lowercased().prefix(1))
      } else if next < name.endIndex && name[next].isLowercase && result.count > 1 {
        break
      } else {
        result.append(contentsOf: name[i].lowercased().prefix(1))
      }
      i = next
    }
    result.append(contentsOf: name[i...])
    return result
  }

  private static let hardKeywords: Set<String> = [
    "Any", "as", "associatedtype", "break", "case", "catch", "class", "continue",
    "default", "defer", "deinit", "do", "else", "enum", "extension", "fallthrough",
    "false", "fileprivate", "for", "func", "guard", "if", "import", "in", "init",
    "inout", "internal", "is", "let", "nil", "operator", "precedencegroup",
    "private", "protocol", "public", "repeat", "rethrows", "return", "self",
    "Self", "static", "struct", "subscript", "super", "switch", "throw", "throws",
    "true", "try", "typealias", "var", "where", "while",
  ]

  private static func escapeKeyword(_ name: String) -> String {
    hardKeywords.contains(name) ? "`\(name)`" : name
  }
}
