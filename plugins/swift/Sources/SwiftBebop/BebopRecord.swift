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

extension BebopRecord {
    /// Decode from a raw byte array.
    public static func decode(from bytes: [UInt8]) throws -> Self {
        try bytes.withUnsafeBufferPointer { buf in
            var reader = BebopReader(data: UnsafeRawBufferPointer(buf))
            return try Self.decode(from: &reader)
        }
    }

    /// Encode to a new byte array.
    public func serializedData() -> [UInt8] {
        var writer = BebopWriter()
        encode(to: &writer)
        return writer.toBytes()
    }
}
