import Foundation
import SwiftBebop

public extension BFloat16 {
    init?(exactly other: CGFloat) {
        self = BFloat16(other)
        guard CGFloat(self) == other else { return nil }
    }
}

public extension CGFloat {
    init(_ other: BFloat16) {
        self.init(NativeType(other))
    }

    init?(exactly other: BFloat16) {
        self.init(NativeType(other))
        guard BFloat16(self) == other else { return nil }
    }
}
