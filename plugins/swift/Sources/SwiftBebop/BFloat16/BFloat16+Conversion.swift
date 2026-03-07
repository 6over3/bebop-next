import CBFloat16

public extension Float {
    @inlinable @inline(__always)
    init(_ value: BFloat16) {
        self = Float(bitPattern: UInt32(value._value) << 16)
    }

    init?(exactly other: BFloat16) {
        guard !other.isNaN else { return nil }
        self = Float(other)
    }
}

public extension Double {
    @inlinable @inline(__always)
    init(_ other: BFloat16) {
        self.init(Float(other))
    }

    init?(exactly other: BFloat16) {
        self = Double(other)
        guard BFloat16(self) == other else { return nil }
    }
}

public extension BFloat16 {
    init?(exactly other: Float) {
        self = BFloat16(other)
        guard Float(self) == other else { return nil }
    }

    init?(exactly other: Double) {
        self = BFloat16(other)
        guard Double(self) == other else { return nil }
    }
}

public extension BinaryInteger {
    init(_ source: BFloat16) {
        self = Self(Float(source))
    }

    init?(exactly source: BFloat16) {
        guard let value = Self(exactly: Float(source)) else { return nil }
        self = value
    }
}
