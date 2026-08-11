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

    public struct View: BebopRecordView {
        public let encoded: BebopView

        public init(
            _ bytes: [UInt8],
            limits: BebopDecodeLimits = .default
        ) throws {
            try self.init(BebopView(bytes), limits: limits)
        }

        public init(
            _ encoded: BebopView,
            limits _: BebopDecodeLimits = .default
        ) throws {
            guard encoded.isEmpty else { throw BebopDecodingError.trailingData }
            self.encoded = encoded
        }

        public func decoded() -> BebopEmpty { BebopEmpty() }
    }

    public static func readView(from reader: inout BebopViewReader) throws -> View {
        try View(reader.view(from: reader.position))
    }

    public static let bebopReflection = BebopTypeReflection(
        name: "BebopEmpty",
        fqn: "bebop.Empty",
        kind: .struct,
        detail: .struct(StructReflection(fields: []))
    )
}
