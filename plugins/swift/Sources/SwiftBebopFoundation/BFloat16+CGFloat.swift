import Foundation
import SwiftBebop

extension BFloat16 {
  public init?(exactly other: CGFloat) {
    self = BFloat16(other)
    guard CGFloat(self) == other else { return nil }
  }
}

extension CGFloat {
  public init(_ other: BFloat16) {
    self.init(NativeType(other))
  }

  public init?(exactly other: BFloat16) {
    self.init(NativeType(other))
    guard BFloat16(self) == other else { return nil }
  }
}
