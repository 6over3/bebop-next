/// A Bebop-serializable record type (struct, message, or union).
///
/// Generated types conform to this protocol. It provides wire-format
/// encoding/decoding plus convenience methods for `[UInt8]` round-trips.
public protocol BebopRecord: Sendable, Hashable, Equatable, Codable {
    /// Decode an instance by reading fields from `reader`.
    static func decode(from reader: inout BebopReader) throws -> Self

    /// Encode this instance's fields into `writer`.
    func encode(to writer: inout BebopWriter)

    /// The exact number of bytes this instance will occupy on the wire.
    var encodedSize: Int { get }
}

public extension BebopRecord {
    /// Decode from a raw byte array.
    static func decode(
        from bytes: [UInt8],
        limits: BebopDecodeLimits = .default
    ) throws -> Self {
        try bytes.withUnsafeBufferPointer { buf in
            var reader = BebopReader(data: UnsafeRawBufferPointer(buf), limits: limits)
            let value = try Self.decode(from: &reader)
            guard reader.position == bytes.count else {
                throw BebopDecodingError.trailingData
            }
            return value
        }
    }

    /// Encode to a new byte array.
    func encode() -> [UInt8] {
        var writer = BebopWriter()
        encode(to: &writer)
        return writer.toBytes()
    }

}
