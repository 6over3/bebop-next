import Foundation

/// A Bebop timestamp stored as seconds and nanoseconds since the Unix epoch.
///
/// Wire format: 8 bytes (Int64 seconds) + 4 bytes (Int32 nanoseconds) = 12 bytes,
/// little-endian.
public struct BebopTimestamp: Sendable, Hashable, Equatable, Codable, BitwiseCopyable {
    public var seconds: Int64
    public var nanoseconds: Int32

    public init(seconds: Int64, nanoseconds: Int32) {
        self.seconds = seconds
        self.nanoseconds = nanoseconds
    }

    /// Convert from Foundation `Date`.
    public init(date: Date) {
        let ti = date.timeIntervalSince1970
        seconds = Int64(ti)
        nanoseconds = Int32((ti - Double(seconds)) * 1e9)
    }

    /// Convert to Foundation `Date`.
    public var date: Date {
        Date(timeIntervalSince1970: Double(seconds) + Double(nanoseconds) / 1e9)
    }
}
