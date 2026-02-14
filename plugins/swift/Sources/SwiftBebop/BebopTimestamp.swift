/// A Bebop timestamp stored as seconds and nanoseconds since the Unix epoch.
///
/// Wire format: 8 bytes (Int64 seconds) + 4 bytes (Int32 nanoseconds) = 12 bytes,
/// little-endian.
///
/// Foundation `Date` conversions are available via the `SwiftBebopFoundation` module.
public struct BebopTimestamp: Sendable, Hashable, Equatable, Codable, BitwiseCopyable {
  public var seconds: Int64
  public var nanoseconds: Int32

  public init(seconds: Int64, nanoseconds: Int32) {
    self.seconds = seconds
    self.nanoseconds = nanoseconds
  }
}
