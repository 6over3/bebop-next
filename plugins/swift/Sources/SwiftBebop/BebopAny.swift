/// A type-erased Bebop record that carries its type URL alongside raw bytes.
///
/// Use `pack(_:)` to wrap a concrete record and `unpack(as:)` to recover it.
/// The type URL format is `"type.bebop.sh/<fqn>"` by default.
public struct BebopAny: BebopRecord, BebopReflectable {
    /// Default prefix prepended to fully-qualified names in type URLs.
    public static let typeURLPrefix = "type.bebop.sh/"

    /// The fully-qualified type URL identifying the packed record type.
    public let typeURL: String

    /// The raw Bebop-encoded bytes of the packed record.
    public let value: [UInt8]

    public init(typeURL: String, value: [UInt8]) {
        self.typeURL = typeURL
        self.value = value
    }

    public static func decode(from reader: inout BebopReader) throws -> BebopAny {
        let typeURL = try reader.readString()
        let byteCount = try Int(reader.readUInt32())
        let value = try reader.readBytes(byteCount)
        return BebopAny(typeURL: typeURL, value: value)
    }

    public func encode(to writer: inout BebopWriter) {
        writer.writeString(typeURL)
        writer.writeUInt32(UInt32(value.count))
        writer.writeBytes(value)
    }

    public var encodedSize: Int {
        (4 + typeURL.utf8.count + 1) + (4 + value.count)
    }

    // MARK: - Pack / Unpack / Is

    /// Wrap a concrete record into a `BebopAny`.
    ///
    /// - Parameters:
    ///   - record: The record to serialize and wrap.
    ///   - prefix: URL prefix prepended to the record's FQN.
    public static func pack<T: BebopRecord & BebopReflectable>(
        _ record: T, prefix: String = typeURLPrefix
    ) -> BebopAny {
        BebopAny(
            typeURL: prefix + T.bebopReflection.fqn,
            value: record.serializedData()
        )
    }

    /// Decode the packed bytes as a concrete record type.
    ///
    /// - Parameter type: The expected record type.
    /// - Throws: `BebopDecodingError.typeMismatch` if the type URL doesn't match.
    public func unpack<T: BebopRecord & BebopReflectable>(
        as type: T.Type
    ) throws -> T {
        guard self.is(type) else {
            throw BebopDecodingError.typeMismatch(
                expected: T.bebopReflection.fqn,
                actual: typeName ?? typeURL
            )
        }
        return try T.decode(from: value)
    }

    /// Check whether this `BebopAny` wraps the given record type.
    public func `is`<T: BebopReflectable>(_: T.Type) -> Bool {
        typeName == T.bebopReflection.fqn
    }

    /// The bare type name extracted from the type URL (after the last `/`).
    public var typeName: String? {
        guard let idx = typeURL.lastIndex(of: "/") else { return nil }
        return String(typeURL[typeURL.index(after: idx)...])
    }

    // MARK: - Reflection

    public static let bebopReflection = BebopTypeReflection(
        name: "BebopAny",
        fqn: "bebop.Any",
        kind: .struct,
        detail: .struct(
            StructReflection(fields: [
                BebopFieldReflection(name: "type_url", index: 0, typeName: "string"),
                BebopFieldReflection(name: "value", index: 1, typeName: "byte[]"),
            ]))
    )
}
