/// Zero-field record used as a placeholder request or response type.
///
/// Wire format: zero bytes. Decode always succeeds, encode writes nothing.
public struct BebopEmpty: BebopRecord, BebopReflectable {
    public init() {}

    public static func decode(from _: inout BebopReader) throws -> BebopEmpty {
        BebopEmpty()
    }

    public func encode(to _: inout BebopWriter) {}

    public var encodedSize: Int { 0 }

    public struct View: Sendable {
        public init(_ bytes: [UInt8]) throws {
            guard bytes.isEmpty else { throw BebopDecodingError.trailingData }
        }

        public init(_ encoded: BebopView) throws {
            guard encoded.isEmpty else { throw BebopDecodingError.trailingData }
        }

        public func decoded() -> BebopEmpty { BebopEmpty() }
    }

    public static func readView(from _: inout BebopViewReader) throws -> View {
        try View([])
    }

    public static let bebopReflection = BebopTypeReflection(
        name: "BebopEmpty",
        fqn: "bebop.Empty",
        kind: .struct,
        detail: .struct(StructReflection(fields: []))
    )
}
