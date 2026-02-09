import CBFloat16
#if canImport(Foundation)
import Foundation
#endif

extension Float {
    @inlinable @inline(__always)
    public init(_ value: BFloat16) {
        self = cbfloat16_to_float(value._value)
    }

    public init?(exactly other: BFloat16) {
        guard !other.isNaN else { return nil }
        self = Float(other)
    }
}

extension Double {
    @inlinable @inline(__always)
    public init(_ other: BFloat16) {
        self.init(Float(other))
    }

    public init?(exactly other: BFloat16) {
        self = Double(other)
        guard BFloat16(self) == other else { return nil }
    }
}

extension BFloat16 {
    public init?(exactly other: Float) {
        self = BFloat16(other)
        guard Float(self) == other else { return nil }
    }

    public init?(exactly other: Double) {
        self = BFloat16(other)
        guard Double(self) == other else { return nil }
    }
}

extension BinaryInteger {
    public init(_ source: BFloat16) {
        self = Self(Float(source))
    }

    public init?(exactly source: BFloat16) {
        guard let value = Self(exactly: Float(source)) else { return nil }
        self = value
    }
}

#if canImport(Foundation)
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
#endif
