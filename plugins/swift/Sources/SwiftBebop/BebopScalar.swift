/// A scalar type safe for bulk memcpy in Bebop wire format.
///
/// Conforming types must satisfy `MemoryLayout<Self>.stride == MemoryLayout<Self>.size`
/// (no padding bytes). This is required because Bebop packs scalars contiguously
/// on the wire, so `stride`-based memcpy is only correct when stride equals size.
///
/// All Bebop primitive scalar types (fixed-width integers, IEEE floats, BFloat16)
/// conform to this protocol. User-defined types should not conform unless they
/// genuinely have no padding.
public protocol BebopScalar: BitwiseCopyable {}

extension Bool: BebopScalar {}
extension UInt8: BebopScalar {}
extension Int8: BebopScalar {}
extension Int16: BebopScalar {}
extension UInt16: BebopScalar {}
extension Int32: BebopScalar {}
extension UInt32: BebopScalar {}
extension Int64: BebopScalar {}
extension UInt64: BebopScalar {}
extension Int128: BebopScalar {}
extension UInt128: BebopScalar {}
extension Float16: BebopScalar {}
extension Float: BebopScalar {}
extension Double: BebopScalar {}
extension BFloat16: BebopScalar {}
extension BebopUUID: BebopScalar {}
