import BebopPlugin

enum CodegenError: Error, CustomStringConvertible {
  case malformedType(String)
  case malformedDefinition(String)

  var description: String {
    switch self {
    case .malformedType(let msg): return "malformed type: \(msg)"
    case .malformedDefinition(let msg): return "malformed definition: \(msg)"
    }
  }
}

enum TypeMapper {
  static func swiftType(for type: TypeDescriptor) throws -> String {
    guard let kind = type.kind else {
      throw CodegenError.malformedType("missing type kind")
    }
    switch kind {
    case .bool: return "Bool"
    case .byte: return "UInt8"
    case .int8: return "Int8"
    case .int16: return "Int16"
    case .uint16: return "UInt16"
    case .int32: return "Int32"
    case .uint32: return "UInt32"
    case .int64: return "Int64"
    case .uint64: return "UInt64"
    case .int128: return "Int128"
    case .uint128: return "UInt128"
    case .float16: return "Float16"
    case .float32: return "Float"
    case .float64: return "Double"
    case .bfloat16: return "BFloat16"
    case .string: return "String"
    case .uuid: return "BebopUUID"
    case .timestamp: return "BebopTimestamp"
    case .duration: return "Duration"
    case .array:
      guard let elem = type.arrayElement else {
        throw CodegenError.malformedType("array missing element type")
      }
      return "[\(try swiftType(for: elem))]"
    case .fixedArray:
      guard let elem = type.fixedArrayElement else {
        throw CodegenError.malformedType("fixedArray missing element type")
      }
      guard let size = type.fixedArraySize else {
        throw CodegenError.malformedType("fixedArray missing size")
      }
      return "InlineArray<\(size), \(try swiftType(for: elem))>"
    case .map:
      guard let key = type.mapKey else {
        throw CodegenError.malformedType("map missing key type")
      }
      guard let val = type.mapValue else {
        throw CodegenError.malformedType("map missing value type")
      }
      return "[\(try swiftType(for: key)): \(try swiftType(for: val))]"
    case .defined:
      guard let fqn = type.definedFqn else {
        throw CodegenError.malformedType("defined type missing fqn")
      }
      return NamingPolicy.fqnToTypeName(fqn)
    default:
      throw CodegenError.malformedType("unknown type kind \(kind.rawValue)")
    }
  }

  static func readExpression(for type: TypeDescriptor, reader r: String = "reader") throws -> String
  {
    guard let kind = type.kind else {
      throw CodegenError.malformedType("missing type kind")
    }
    switch kind {
    case .bool: return "try \(r).readBool()"
    case .byte: return "try \(r).readByte()"
    case .int8: return "try \(r).readInt8()"
    case .int16: return "try \(r).readInt16()"
    case .uint16: return "try \(r).readUInt16()"
    case .int32: return "try \(r).readInt32()"
    case .uint32: return "try \(r).readUInt32()"
    case .int64: return "try \(r).readInt64()"
    case .uint64: return "try \(r).readUInt64()"
    case .int128: return "try \(r).readInt128()"
    case .uint128: return "try \(r).readUInt128()"
    case .float16: return "try \(r).readFloat16()"
    case .float32: return "try \(r).readFloat32()"
    case .float64: return "try \(r).readFloat64()"
    case .bfloat16: return "try \(r).readBFloat16()"
    case .string: return "try \(r).readString()"
    case .uuid: return "try \(r).readUUID()"
    case .timestamp: return "try \(r).readTimestamp()"
    case .duration: return "try \(r).readDuration()"
    case .array:
      guard let elem = type.arrayElement else {
        throw CodegenError.malformedType("array missing element type")
      }
      guard let elemKind = elem.kind else {
        throw CodegenError.malformedType("array element missing type kind")
      }
      let elemType = try swiftType(for: elem)
      if isBulkScalar(elemKind) {
        return "try \(r).readLengthPrefixedArray(of: \(elemType).self)"
      }
      let inner = try readExpression(for: elem, reader: "_r")
      return "try \(r).readDynamicArray { _r in \(inner) }"
    case .fixedArray:
      guard let elem = type.fixedArrayElement else {
        throw CodegenError.malformedType("fixedArray missing element type")
      }
      guard let elemKind = elem.kind else {
        throw CodegenError.malformedType("fixedArray element missing type kind")
      }
      guard let size = type.fixedArraySize else {
        throw CodegenError.malformedType("fixedArray missing size")
      }
      let elemType = try swiftType(for: elem)
      let fullType = "InlineArray<\(size), \(elemType)>"
      if isBulkScalar(elemKind) {
        return "(try \(r).readInlineArray(of: \(elemType).self) as \(fullType))"
      }
      let inner = try readExpression(for: elem, reader: "_r")
      return "(try \(r).readFixedInlineArray { _r in \(inner) } as \(fullType))"
    case .map:
      guard let mapKey = type.mapKey else {
        throw CodegenError.malformedType("map missing key type")
      }
      guard let mapValue = type.mapValue else {
        throw CodegenError.malformedType("map missing value type")
      }
      let keyRead = try readExpression(for: mapKey, reader: "_r")
      let valRead = try readExpression(for: mapValue, reader: "_r")
      return "try \(r).readDynamicMap { _r in (\(keyRead), \(valRead)) }"
    case .defined:
      guard let fqn = type.definedFqn else {
        throw CodegenError.malformedType("defined type missing fqn")
      }
      let typeName = NamingPolicy.fqnToTypeName(fqn)
      return "try \(typeName).decode(from: &\(r))"
    default:
      throw CodegenError.malformedType("unknown type kind \(kind.rawValue)")
    }
  }

  static func writeExpression(for type: TypeDescriptor, value: String, writer w: String = "writer")
    throws -> String
  {
    guard let kind = type.kind else {
      throw CodegenError.malformedType("missing type kind")
    }
    switch kind {
    case .bool: return "\(w).writeBool(\(value))"
    case .byte: return "\(w).writeByte(\(value))"
    case .int8: return "\(w).writeInt8(\(value))"
    case .int16: return "\(w).writeInt16(\(value))"
    case .uint16: return "\(w).writeUInt16(\(value))"
    case .int32: return "\(w).writeInt32(\(value))"
    case .uint32: return "\(w).writeUInt32(\(value))"
    case .int64: return "\(w).writeInt64(\(value))"
    case .uint64: return "\(w).writeUInt64(\(value))"
    case .int128: return "\(w).writeInt128(\(value))"
    case .uint128: return "\(w).writeUInt128(\(value))"
    case .float16: return "\(w).writeFloat16(\(value))"
    case .float32: return "\(w).writeFloat32(\(value))"
    case .float64: return "\(w).writeFloat64(\(value))"
    case .bfloat16: return "\(w).writeBFloat16(\(value))"
    case .string: return "\(w).writeString(\(value))"
    case .uuid: return "\(w).writeUUID(\(value))"
    case .timestamp: return "\(w).writeTimestamp(\(value))"
    case .duration: return "\(w).writeDuration(\(value))"
    case .array:
      guard let elem = type.arrayElement else {
        throw CodegenError.malformedType("array missing element type")
      }
      guard let elemKind = elem.kind else {
        throw CodegenError.malformedType("array element missing type kind")
      }
      if isBulkScalar(elemKind) {
        return "\(w).writeLengthPrefixedArray(\(value))"
      }
      let inner = try writeExpression(for: elem, value: "_el", writer: "_w")
      return "\(w).writeDynamicArray(\(value)) { _w, _el in \(inner) }"
    case .fixedArray:
      guard let elem = type.fixedArrayElement else {
        throw CodegenError.malformedType("fixedArray missing element type")
      }
      guard let elemKind = elem.kind else {
        throw CodegenError.malformedType("fixedArray element missing type kind")
      }
      if isBulkScalar(elemKind) {
        return "\(w).writeInlineArray(\(value))"
      }
      let inner = try writeExpression(for: elem, value: "_el", writer: "_w")
      return "\(w).writeFixedInlineArray(\(value)) { _w, _el in \(inner) }"
    case .map:
      guard let key = type.mapKey else {
        throw CodegenError.malformedType("map missing key type")
      }
      guard let val = type.mapValue else {
        throw CodegenError.malformedType("map missing value type")
      }
      let keyWrite = try writeExpression(for: key, value: "_k", writer: "_w")
      let valWrite = try writeExpression(for: val, value: "_v", writer: "_w")
      return "\(w).writeDynamicMap(\(value)) { _w, _k, _v in \(keyWrite)\n\(valWrite) }"
    case .defined:
      return "\(value).encode(to: &\(w))"
    default:
      throw CodegenError.malformedType("unknown type kind \(kind.rawValue)")
    }
  }

  // MARK: - Size expressions

  static func sizeExpression(for type: TypeDescriptor, value: String) throws -> String {
    guard let kind = type.kind else {
      throw CodegenError.malformedType("missing type kind")
    }
    switch kind {
    case .bool: return "1"
    case .byte: return "1"
    case .int8: return "1"
    case .int16: return "2"
    case .uint16: return "2"
    case .int32: return "4"
    case .uint32: return "4"
    case .int64: return "8"
    case .uint64: return "8"
    case .int128: return "16"
    case .uint128: return "16"
    case .float16: return "2"
    case .float32: return "4"
    case .float64: return "8"
    case .bfloat16: return "2"
    case .string: return "(4 + \(value).utf8.count + 1)"
    case .uuid: return "16"
    case .timestamp: return "12"
    case .duration: return "12"
    case .array:
      guard let elem = type.arrayElement else {
        throw CodegenError.malformedType("array missing element type")
      }
      guard let elemKind = elem.kind else {
        throw CodegenError.malformedType("array element missing type kind")
      }
      if let fixedSize = scalarSize(elemKind) {
        return "(4 + \(value).count &* \(fixedSize))"
      }
      let inner = try sizeExpression(for: elem, value: "_el")
      return "(4 + \(value).reduce(0) { _acc, _el in _acc + \(inner) })"
    case .fixedArray:
      guard let elem = type.fixedArrayElement else {
        throw CodegenError.malformedType("fixedArray missing element type")
      }
      guard let elemKind = elem.kind else {
        throw CodegenError.malformedType("fixedArray element missing type kind")
      }
      guard let size = type.fixedArraySize else {
        throw CodegenError.malformedType("fixedArray missing size")
      }
      if let fixedSize = scalarSize(elemKind) {
        return "\(size &* fixedSize)"
      }
      let inner = try sizeExpression(for: elem, value: "\(value)[_i]")
      if inner.contains("_i") {
        return "(0..<\(size)).reduce(0) { _acc, _i in _acc + \(inner) }"
      } else {
        return "(\(size) * \(inner))"
      }
    case .map:
      guard let key = type.mapKey else {
        throw CodegenError.malformedType("map missing key type")
      }
      guard let val = type.mapValue else {
        throw CodegenError.malformedType("map missing value type")
      }
      guard let keyKind = key.kind else {
        throw CodegenError.malformedType("map key missing type kind")
      }
      guard let valKind = val.kind else {
        throw CodegenError.malformedType("map value missing type kind")
      }
      if let kSize = scalarSize(keyKind), let vSize = scalarSize(valKind) {
        return "(4 + \(value).count &* \(kSize + vSize))"
      }
      let keyExpr = try sizeExpression(for: key, value: "_k")
      let valExpr = try sizeExpression(for: val, value: "_v")
      let kBind = keyExpr.contains("_k") ? "_k" : "_"
      let vBind = valExpr.contains("_v") ? "_v" : "_"
      return
        "(4 + \(value).reduce(0) { _acc, _kv in let (\(kBind), \(vBind)) = _kv; return _acc + \(keyExpr) + \(valExpr) })"
    case .defined:
      return "\(value).encodedSize"
    default:
      throw CodegenError.malformedType("unknown type kind \(kind.rawValue)")
    }
  }

  static func fixedSize(for type: TypeDescriptor) -> Int? {
    guard let kind = type.kind else { return nil }
    if let s = scalarSize(kind) { return Int(s) }
    if kind == .fixedArray {
      guard let elem = type.fixedArrayElement,
        let elemKind = elem.kind,
        let elemSize = scalarSize(elemKind),
        let count = type.fixedArraySize
      else { return nil }
      return Int(count) * Int(elemSize)
    }
    return nil
  }

  static func scalarSize(_ kind: TypeKind) -> UInt32? {
    switch kind {
    case .bool, .byte, .int8: return 1
    case .int16, .uint16, .float16, .bfloat16: return 2
    case .int32, .uint32, .float32: return 4
    case .int64, .uint64, .float64: return 8
    case .int128, .uint128: return 16
    case .uuid: return 16
    case .timestamp, .duration: return 12
    default: return nil
    }
  }

  static func enumScalarSize(_ kind: TypeKind) throws -> Int {
    switch kind {
    case .byte, .int8: return 1
    case .int16, .uint16: return 2
    case .int32, .uint32: return 4
    case .int64, .uint64: return 8
    default:
      throw CodegenError.malformedDefinition("unsupported enum base type \(kind.rawValue)")
    }
  }

  static func isBulkScalar(_ kind: TypeKind) -> Bool {
    switch kind {
    case .bool, .byte, .int8, .int16, .uint16, .int32, .uint32,
      .int64, .uint64, .int128, .uint128,
      .float16, .float32, .float64, .bfloat16, .uuid:
      return true
    default:
      return false
    }
  }

  static func enumBaseType(_ kind: TypeKind) throws -> String {
    switch kind {
    case .byte: return "UInt8"
    case .int8: return "Int8"
    case .int16: return "Int16"
    case .uint16: return "UInt16"
    case .int32: return "Int32"
    case .uint32: return "UInt32"
    case .int64: return "Int64"
    case .uint64: return "UInt64"
    default:
      throw CodegenError.malformedDefinition("unsupported enum base type \(kind.rawValue)")
    }
  }

  static func enumReadMethod(_ kind: TypeKind) throws -> String {
    switch kind {
    case .byte: return "readByte"
    case .int8: return "readInt8"
    case .int16: return "readInt16"
    case .uint16: return "readUInt16"
    case .int32: return "readInt32"
    case .uint32: return "readUInt32"
    case .int64: return "readInt64"
    case .uint64: return "readUInt64"
    default:
      throw CodegenError.malformedDefinition("unsupported enum base type \(kind.rawValue)")
    }
  }

  static func enumWriteMethod(_ kind: TypeKind) throws -> String {
    switch kind {
    case .byte: return "writeByte"
    case .int8: return "writeInt8"
    case .int16: return "writeInt16"
    case .uint16: return "writeUInt16"
    case .int32: return "writeInt32"
    case .uint32: return "writeUInt32"
    case .int64: return "writeInt64"
    case .uint64: return "writeUInt64"
    default:
      throw CodegenError.malformedDefinition("unsupported enum base type \(kind.rawValue)")
    }
  }
}
