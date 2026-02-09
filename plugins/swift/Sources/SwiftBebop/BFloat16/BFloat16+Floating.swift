import CBFloat16

extension BFloat16: FloatingPoint {
    public typealias Exponent = Int

    @inlinable
    public init(sign: FloatingPointSign, exponent: Int, significand: BFloat16) {
        var result = significand
        if sign == .minus { result = -result }
        if significand.isFinite && !significand.isZero {
            var clamped = exponent
            let leastNormalExponent = 1 - Int(BFloat16._exponentBias)
            let greatestFiniteExponent = Int(BFloat16._exponentBias)
            if clamped < leastNormalExponent {
                clamped = max(clamped, 3 * leastNormalExponent)
                while clamped < leastNormalExponent {
                    result *= BFloat16.leastNormalMagnitude
                    clamped -= leastNormalExponent
                }
            } else if clamped > greatestFiniteExponent {
                clamped = min(clamped, 3 * greatestFiniteExponent)
                let step = BFloat16(
                    sign: .plus,
                    exponentBitPattern: BFloat16._infinityExponent - 1,
                    significandBitPattern: 0)
                while clamped > greatestFiniteExponent {
                    result *= step
                    clamped -= greatestFiniteExponent
                }
            }
            let scale = BFloat16(
                sign: .plus,
                exponentBitPattern: UInt16(Int(BFloat16._exponentBias) + clamped),
                significandBitPattern: 0)
            result = result * scale
        }
        self = result
    }

    @inlinable
    public static var nan: BFloat16 { BFloat16(bitPattern: 0x7FC0) }

    @inlinable
    public static var signalingNaN: BFloat16 { BFloat16(bitPattern: 0xFF81) }

    @inlinable
    public static var infinity: BFloat16 { BFloat16(bitPattern: 0x7F80) }

    @inlinable
    public static var greatestFiniteMagnitude: BFloat16 { BFloat16(bitPattern: 0x7F7F) }

    @inlinable
    public static var pi: BFloat16 { BFloat16(bitPattern: 0x4049) }

    @inlinable
    public var ulp: BFloat16 {
        guard _fastPath(isFinite) else { return .nan }
        if _fastPath(isNormal) {
            let bitPattern_ = bitPattern & BFloat16.infinity.bitPattern
            return BFloat16(bitPattern: bitPattern_) * BFloat16.ulpOfOne
        }
        return .leastNormalMagnitude * BFloat16.ulpOfOne
    }

    @inlinable
    public static var ulpOfOne: BFloat16 { 0x1.0p-7 }

    @inlinable
    public static var leastNormalMagnitude: BFloat16 { 0x1.0p-126 }

    @inlinable
    public static var leastNonzeroMagnitude: BFloat16 {
        leastNormalMagnitude * ulpOfOne
    }

    @inlinable
    public var sign: FloatingPointSign {
        FloatingPointSign(
            rawValue: Int(bitPattern &>> (BFloat16.significandBitCount + BFloat16.exponentBitCount)))!
    }

    @inlinable
    public var exponent: Int {
        if !isFinite { return .max }
        if isZero { return .min }
        let provisional = Int(exponentBitPattern) - Int(BFloat16._exponentBias)
        if isNormal { return provisional }
        let shift = BFloat16.significandBitCount - significandBitPattern._binaryLogarithm()
        return provisional + 1 - shift
    }

    @inlinable
    public var significand: BFloat16 {
        if isNaN { return self }
        if isNormal {
            return BFloat16(
                sign: .plus,
                exponentBitPattern: BFloat16._exponentBias,
                significandBitPattern: significandBitPattern)
        }
        if _slowPath(isSubnormal) {
            let shift = BFloat16.significandBitCount - significandBitPattern._binaryLogarithm()
            return BFloat16(
                sign: .plus,
                exponentBitPattern: BFloat16._exponentBias,
                significandBitPattern: significandBitPattern &<< shift)
        }
        return BFloat16(
            sign: .plus,
            exponentBitPattern: exponentBitPattern,
            significandBitPattern: 0)
    }

    @inlinable @inline(__always)
    public static func / (lhs: BFloat16, rhs: BFloat16) -> BFloat16 {
        BFloat16(bitPattern: cbfloat16_div(lhs._value, rhs._value))
    }

    @inlinable @inline(__always)
    public static func /= (lhs: inout BFloat16, rhs: BFloat16) {
        lhs = lhs / rhs
    }

    @inlinable @inline(__always)
    public mutating func formRemainder(dividingBy other: BFloat16) {
        var f = Float(self)
        f.formRemainder(dividingBy: Float(other))
        self = BFloat16(f)
    }

    @inlinable @inline(__always)
    public mutating func formTruncatingRemainder(dividingBy other: BFloat16) {
        var f = Float(self)
        f.formTruncatingRemainder(dividingBy: Float(other))
        self = BFloat16(f)
    }

    @inlinable @inline(__always)
    public mutating func formSquareRoot() {
        _value = cbfloat16_sqrt(_value)
    }

    @inlinable @inline(__always)
    public mutating func addProduct(_ lhs: BFloat16, _ rhs: BFloat16) {
        _value = cbfloat16_fma(lhs._value, rhs._value, _value)
    }

    @inlinable @inline(__always)
    public mutating func round(_ rule: FloatingPointRoundingRule) {
        var f = Float(self)
        f.round(rule)
        self = BFloat16(f)
    }

    @inlinable
    public var nextUp: BFloat16 {
        let x = self + 0
        if _fastPath(x < .infinity) {
            let increment = Int16(bitPattern: x.bitPattern) &>> 15 | 1
            let bitPattern_ = x.bitPattern &+ UInt16(bitPattern: increment)
            return BFloat16(bitPattern: bitPattern_)
        }
        return x
    }

    @inlinable @inline(__always)
    public func isEqual(to other: BFloat16) -> Bool {
        cbfloat16_equal(_value, other._value)
    }

    @inlinable @inline(__always)
    public func isLess(than other: BFloat16) -> Bool {
        cbfloat16_lt(_value, other._value)
    }

    @inlinable @inline(__always)
    public func isLessThanOrEqualTo(_ other: BFloat16) -> Bool {
        cbfloat16_lte(_value, other._value)
    }

    @inlinable
    public var isNormal: Bool { exponentBitPattern > 0 && isFinite }

    @inlinable
    public var isFinite: Bool { exponentBitPattern < BFloat16._infinityExponent }

    @inlinable
    public var isZero: Bool { exponentBitPattern == 0 && significandBitPattern == 0 }

    @inlinable
    public var isSubnormal: Bool { exponentBitPattern == 0 && significandBitPattern != 0 }

    @inlinable
    public var isInfinite: Bool { bitPattern & 0x7FFF == 0x7F80 }

    @inlinable
    public var isNaN: Bool { bitPattern & 0x7FFF > 0x7F80 }

    @inlinable
    public var isSignalingNaN: Bool {
        isNaN && (significandBitPattern & BFloat16._quietNaNMask) == 0
    }

    @inlinable
    public var isCanonical: Bool {
        if BFloat16.leastNonzeroMagnitude == BFloat16.leastNormalMagnitude {
            if exponentBitPattern == 0 && significandBitPattern != 0 {
                return false
            }
        }
        return true
    }
}

extension BFloat16: BinaryFloatingPoint {
    public typealias RawSignificand = UInt16
    public typealias RawExponent = UInt16

    @inlinable @inline(__always)
    public init(
        sign: FloatingPointSign,
        exponentBitPattern: UInt16,
        significandBitPattern: UInt16
    ) {
        let signShift = BFloat16.significandBitCount + BFloat16.exponentBitCount
        let sign = UInt16(sign == .minus ? 1 : 0)
        let exponent = exponentBitPattern & BFloat16._infinityExponent
        let significand = significandBitPattern & BFloat16._significandMask
        self.init(
            bitPattern: sign &<< signShift
                | exponent &<< UInt16(BFloat16.significandBitCount)
                | significand)
    }

    @inlinable @inline(__always)
    public init(_ value: Float) {
        _value = cbfloat16_from_float(value)
    }

    @inlinable @inline(__always)
    public init(_ value: Double) {
        _value = cbfloat16_from_double(value)
    }

    @inlinable
    public static var exponentBitCount: Int { 8 }

    @inlinable
    public static var significandBitCount: Int { 7 }

    @inlinable
    internal static var _infinityExponent: UInt16 { 1 &<< UInt16(exponentBitCount) - 1 }

    @inlinable
    internal static var _exponentBias: UInt16 { _infinityExponent &>> 1 }

    @inlinable
    internal static var _significandMask: UInt16 { 1 &<< UInt16(significandBitCount) - 1 }

    @inlinable
    internal static var _quietNaNMask: UInt16 { 1 &<< UInt16(significandBitCount - 1) }

    @inlinable
    public var bitPattern: UInt16 { _value }

    @inlinable
    public var exponentBitPattern: UInt16 {
        (bitPattern &>> UInt16(BFloat16.significandBitCount)) & BFloat16._infinityExponent
    }

    @inlinable
    public var significandBitPattern: UInt16 {
        bitPattern & BFloat16._significandMask
    }

    @inlinable
    public var binade: BFloat16 {
        guard _fastPath(isFinite) else { return .nan }
        if _slowPath(isSubnormal) {
            let bitPattern_ = (self * 0x1p10).bitPattern & (-BFloat16.infinity).bitPattern
            return BFloat16(bitPattern: bitPattern_) * 0x1p-10
        }
        return BFloat16(bitPattern: bitPattern & (-BFloat16.infinity).bitPattern)
    }

    @inlinable
    public var significandWidth: Int {
        let trailingZeroBits = significandBitPattern.trailingZeroBitCount
        if isNormal {
            guard significandBitPattern != 0 else { return 0 }
            return BFloat16.significandBitCount &- trailingZeroBits
        }
        if isSubnormal {
            let leadingZeroBits = significandBitPattern.leadingZeroBitCount
            return UInt16.bitWidth &- (trailingZeroBits &+ leadingZeroBits &+ 1)
        }
        return -1
    }
}
