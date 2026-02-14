import Foundation
import SwiftBebop

extension BebopTimestamp {
  /// Convert from Foundation `Date`.
  public init(date: Date) {
    let ti = date.timeIntervalSince1970
    let secs = Int64(ti)
    self.init(seconds: secs, nanoseconds: Int32((ti - Double(secs)) * 1e9))
  }

  /// Convert to Foundation `Date`.
  public var date: Date {
    Date(timeIntervalSince1970: Double(seconds) + Double(nanoseconds) / 1e9)
  }
}
