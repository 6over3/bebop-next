import CBFloat16

extension BFloat16: ExpressibleByIntegerLiteral {
    @inlinable @inline(__always)
    public init(integerLiteral value: Float.IntegerLiteralType) {
        self = BFloat16(Float(integerLiteral: value))
    }
}

extension BFloat16: AdditiveArithmetic {
    @inlinable @inline(__always)
    public static func + (lhs: BFloat16, rhs: BFloat16) -> BFloat16 {
        BFloat16(bitPattern: cbfloat16_add(lhs._value, rhs._value))
    }

    @inlinable @inline(__always)
    public static func - (lhs: BFloat16, rhs: BFloat16) -> BFloat16 {
        BFloat16(bitPattern: cbfloat16_sub(lhs._value, rhs._value))
    }
}

extension BFloat16: Numeric {
    @inlinable
    public init?<T: BinaryInteger>(exactly source: T) {
        guard let float = Float(exactly: source),
              let result = BFloat16(exactly: float) else { return nil }
        self = result
    }

    @inlinable @inline(__always)
    public var magnitude: BFloat16 {
        BFloat16(bitPattern: cbfloat16_abs(_value))
    }

    @inlinable @inline(__always)
    public static func * (lhs: BFloat16, rhs: BFloat16) -> BFloat16 {
        BFloat16(bitPattern: cbfloat16_mul(lhs._value, rhs._value))
    }

    @inlinable @inline(__always)
    public static func *= (lhs: inout BFloat16, rhs: BFloat16) {
        lhs = lhs * rhs
    }
}

extension BFloat16: SignedNumeric {
    @inlinable @inline(__always)
    public prefix static func - (operand: BFloat16) -> BFloat16 {
        BFloat16(bitPattern: cbfloat16_neg(operand._value))
    }

    @inlinable @inline(__always)
    public mutating func negate() { self = -self }
}
