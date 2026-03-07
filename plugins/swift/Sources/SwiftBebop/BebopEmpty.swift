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

    public static let bebopReflection = BebopTypeReflection(
        name: "BebopEmpty",
        fqn: "bebop.Empty",
        kind: .struct,
        detail: .struct(StructReflection(fields: []))
    )
}
