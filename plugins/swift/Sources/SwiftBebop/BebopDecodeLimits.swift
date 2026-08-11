/// Resource limits applied while decoding untrusted Bebop payloads.
public struct BebopDecodeLimits: Sendable, Hashable {
    /// Maximum number of elements accepted in a single array or map.
    public let maxCollectionElements: UInt32

    /// Maximum nesting depth accepted for generated records.
    public let maxDepth: UInt16

    public init(
        maxCollectionElements: UInt32 = 1_000_000,
        maxDepth: UInt16 = 128
    ) {
        self.maxCollectionElements = maxCollectionElements
        self.maxDepth = maxDepth
    }

    /// Production-safe default decoding limits.
    public static let `default` = BebopDecodeLimits()
}
