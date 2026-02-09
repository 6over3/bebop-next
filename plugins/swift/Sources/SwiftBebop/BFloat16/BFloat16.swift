import CBFloat16

/// A half-precision brain floating-point value type (bfloat16).
///
/// `BFloat16` uses the same 8-bit exponent as `Float` but only 7 bits of
/// significand, giving the same dynamic range with reduced precision.
/// Conforms to the full `BinaryFloatingPoint` protocol chain including
/// `SIMDScalar` and `AtomicRepresentable`.
public struct BFloat16: Sendable, BitwiseCopyable {
    @usableFromInline
    internal var _value: UInt16

    /// Create a `BFloat16` with a value of zero.
    @inlinable @inline(__always)
    public init() {
        _value = 0
    }

    /// Create a `BFloat16` from its 16-bit IEEE 754 representation.
    @inlinable @inline(__always)
    public init(bitPattern: UInt16) {
        _value = bitPattern
    }

    @inlinable
    public static var zero: BFloat16 { BFloat16() }

    @inlinable
    public static var negativeZero: BFloat16 { BFloat16(bitPattern: 0x8000) }

    @inlinable
    public static var one: BFloat16 { BFloat16(bitPattern: 0x3F80) }

    @inlinable
    public static var negativeOne: BFloat16 { BFloat16(bitPattern: 0xBF80) }
}

extension BFloat16: Strideable {
    public typealias Stride = BFloat16

    @inlinable @inline(__always)
    public func distance(to other: BFloat16) -> BFloat16 { other - self }

    @inlinable @inline(__always)
    public func advanced(by n: BFloat16) -> BFloat16 { self + n }
}

extension BFloat16: ExpressibleByFloatLiteral {
    public typealias FloatLiteralType = Float

    @inlinable @inline(__always)
    public init(floatLiteral value: Float) {
        self = BFloat16(value)
    }
}

extension BFloat16: Equatable {}
