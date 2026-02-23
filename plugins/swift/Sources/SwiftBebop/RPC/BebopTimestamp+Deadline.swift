#if canImport(Darwin)
  import Darwin
#elseif canImport(Bionic)
  import Bionic
#elseif os(WASI)
  import WASILibc
#elseif canImport(Musl)
  import Musl
#elseif canImport(Glibc)
  import Glibc
#elseif canImport(Android)
  import Android
#endif

extension BebopTimestamp {
  public static var now: BebopTimestamp {
    var ts = timespec()
    clock_gettime(CLOCK_REALTIME, &ts)
    return BebopTimestamp(seconds: Int64(ts.tv_sec), nanoseconds: Int32(ts.tv_nsec))
  }

  public init(fromNow duration: Duration) {
    let current = Self.now
    let (extraSeconds, attos) = duration.components
    let addNanos = attos / 1_000_000_000
    var totalNanos = Int64(current.nanoseconds) + addNanos
    var totalSeconds = current.seconds + extraSeconds
    if totalNanos >= 1_000_000_000 {
      totalSeconds += totalNanos / 1_000_000_000
      totalNanos %= 1_000_000_000
    } else if totalNanos < 0 {
      let borrow = (-totalNanos + 999_999_999) / 1_000_000_000
      totalSeconds -= borrow
      totalNanos += borrow * 1_000_000_000
    }
    self.init(seconds: totalSeconds, nanoseconds: Int32(totalNanos))
  }

  public var isPast: Bool {
    let current = Self.now
    return seconds < current.seconds
      || (seconds == current.seconds && nanoseconds <= current.nanoseconds)
  }

  /// Nil if already past.
  public var timeRemaining: Duration? {
    let current = Self.now
    var diffSec = seconds - current.seconds
    var diffNano = Int64(nanoseconds) - Int64(current.nanoseconds)
    if diffNano < 0 {
      diffSec -= 1
      diffNano += 1_000_000_000
    }
    guard diffSec >= 0 else { return nil }
    return Duration(
      secondsComponent: diffSec,
      attosecondsComponent: diffNano * 1_000_000_000
    )
  }

}
