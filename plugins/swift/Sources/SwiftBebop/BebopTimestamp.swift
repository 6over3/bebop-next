/// A Bebop timestamp stored as seconds and nanoseconds since the Unix epoch.
///
/// Wire format: 8 bytes (Int64 seconds) + 4 bytes (Int32 nanoseconds) + 4 bytes (Int32 offset_ms) = 16 bytes,
/// little-endian, 8-byte aligned.
///
/// The `offsetMs` field represents the ISO 8601-2 timezone offset in milliseconds.
///
/// Foundation `Date` conversions are available via the `SwiftBebopFoundation` module.
public struct BebopTimestamp: Sendable, Hashable, Equatable, Codable, BitwiseCopyable {
    public var seconds: Int64
    public var nanoseconds: Int32
    public var offsetMs: Int32

    public init(seconds: Int64, nanoseconds: Int32, offsetMs: Int32 = 0) {
        self.seconds = seconds
        self.nanoseconds = nanoseconds
        self.offsetMs = offsetMs
    }
}
