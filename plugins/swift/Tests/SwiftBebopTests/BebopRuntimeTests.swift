import Testing
import Foundation
@testable import SwiftBebop

// MARK: - Helpers

/// Write then read back a single value within a valid pointer scope.
private func roundTrip<T>(
    _ write: (inout BebopWriter) -> Void,
    read: (inout BebopReader) throws -> T
) throws -> T {
    var writer = BebopWriter()
    write(&writer)
    return try writer.withUnsafeBytes { buf in
        var reader = BebopReader(data: buf)
        return try read(&reader)
    }
}

// MARK: - Bool

@Test func boolTrue() throws {
    let v = try roundTrip({ $0.writeBool(true) }, read: { try $0.readBool() })
    #expect(v == true)
}

@Test func boolFalse() throws {
    let v = try roundTrip({ $0.writeBool(false) }, read: { try $0.readBool() })
    #expect(v == false)
}

// MARK: - Integer Primitives

@Test func byte() throws {
    let v = try roundTrip({ $0.writeByte(0xFF) }, read: { try $0.readByte() })
    #expect(v == 0xFF)
}

@Test func uint8() throws {
    for v: UInt8 in [0, 1, 127, 255] {
        let r = try roundTrip({ $0.writeUInt8(v) }, read: { try $0.readUInt8() })
        #expect(r == v)
    }
}

@Test func int8() throws {
    for v: Int8 in [.min, -1, 0, 1, .max] {
        let r = try roundTrip({ $0.writeInt8(v) }, read: { try $0.readInt8() })
        #expect(r == v)
    }
}

@Test func uint16() throws {
    for v: UInt16 in [0, 1, 0x00FF, 0xFF00, .max] {
        let r = try roundTrip({ $0.writeUInt16(v) }, read: { try $0.readUInt16() })
        #expect(r == v)
    }
}

@Test func int16() throws {
    for v: Int16 in [.min, -1, 0, 1, .max] {
        let r = try roundTrip({ $0.writeInt16(v) }, read: { try $0.readInt16() })
        #expect(r == v)
    }
}

@Test func uint32() throws {
    for v: UInt32 in [0, 1, 0xDEAD_BEEF, .max] {
        let r = try roundTrip({ $0.writeUInt32(v) }, read: { try $0.readUInt32() })
        #expect(r == v)
    }
}

@Test func int32() throws {
    for v: Int32 in [.min, -1, 0, 1, .max] {
        let r = try roundTrip({ $0.writeInt32(v) }, read: { try $0.readInt32() })
        #expect(r == v)
    }
}

@Test func uint64() throws {
    for v: UInt64 in [0, 1, 0xDEAD_BEEF_CAFE_BABE, .max] {
        let r = try roundTrip({ $0.writeUInt64(v) }, read: { try $0.readUInt64() })
        #expect(r == v)
    }
}

@Test func int64() throws {
    for v: Int64 in [.min, -1, 0, 1, .max] {
        let r = try roundTrip({ $0.writeInt64(v) }, read: { try $0.readInt64() })
        #expect(r == v)
    }
}

@Test func uint128() throws {
    let values: [UInt128] = [0, 1, .max, UInt128(UInt64.max) << 64 | UInt128(UInt64.max)]
    for v in values {
        let r = try roundTrip({ $0.writeUInt128(v) }, read: { try $0.readUInt128() })
        #expect(r == v)
    }
}

@Test func int128() throws {
    let values: [Int128] = [.min, -1, 0, 1, .max]
    for v in values {
        let r = try roundTrip({ $0.writeInt128(v) }, read: { try $0.readInt128() })
        #expect(r == v)
    }
}

// MARK: - Floats

@Test func float16() throws {
    let values: [Float16] = [0, 1.5, -1.5, .infinity, .greatestFiniteMagnitude]
    for v in values {
        let r = try roundTrip({ $0.writeFloat16(v) }, read: { try $0.readFloat16() })
        #expect(r == v)
    }
}

@Test func float16NaN() throws {
    let r = try roundTrip({ $0.writeFloat16(.nan) }, read: { try $0.readFloat16() })
    #expect(r.isNaN)
}

@Test func float32() throws {
    let values: [Float] = [0, 1.5, -1.5, .pi, .infinity, .greatestFiniteMagnitude]
    for v in values {
        let r = try roundTrip({ $0.writeFloat32(v) }, read: { try $0.readFloat32() })
        #expect(r == v)
    }
}

@Test func float32NaN() throws {
    let r = try roundTrip({ $0.writeFloat32(.nan) }, read: { try $0.readFloat32() })
    #expect(r.isNaN)
}

@Test func float64() throws {
    let values: [Double] = [0, 1.5, -1.5, .pi, .infinity, .greatestFiniteMagnitude]
    for v in values {
        let r = try roundTrip({ $0.writeFloat64(v) }, read: { try $0.readFloat64() })
        #expect(r == v)
    }
}

@Test func float64NaN() throws {
    let r = try roundTrip({ $0.writeFloat64(.nan) }, read: { try $0.readFloat64() })
    #expect(r.isNaN)
}

@Test func bfloat16() throws {
    let values: [BFloat16] = [BFloat16(1.0), BFloat16(-1.0), BFloat16(0.0)]
    for v in values {
        let r = try roundTrip({ $0.writeBFloat16(v) }, read: { try $0.readBFloat16() })
        #expect(r == v)
    }
}

@Test func bfloat16NaN() throws {
    let r = try roundTrip({ $0.writeBFloat16(.nan) }, read: { try $0.readBFloat16() })
    #expect(r.isNaN)
}

// MARK: - BFloat16 FloatingPoint

@Test func bfloat16Constants() {
    #expect(BFloat16.zero == BFloat16(0))
    #expect(BFloat16.zero == BFloat16.negativeZero)
    #expect(BFloat16.negativeZero.sign == .minus)
    #expect(BFloat16.one == BFloat16(1.0))
    #expect(BFloat16.negativeOne == BFloat16(-1.0))
    #expect(BFloat16.infinity == BFloat16(Float.infinity))
    #expect(-BFloat16.infinity == BFloat16(-Float.infinity))
    #expect(BFloat16(Float.greatestFiniteMagnitude) == BFloat16.infinity)
    #expect(BFloat16.infinity.nextDown == BFloat16.greatestFiniteMagnitude)
    #expect(BFloat16.leastNonzeroMagnitude.bitPattern == 1)
}

@Test func bfloat16Classification() {
    #expect(BFloat16.infinity.isInfinite)
    #expect(!BFloat16.infinity.isFinite)
    #expect(BFloat16.greatestFiniteMagnitude.isFinite)
    #expect(BFloat16.nan.isNaN)
    #expect(!BFloat16.nan.isSignalingNaN)
    #expect(BFloat16.signalingNaN.isNaN)
    #expect(BFloat16.signalingNaN.isSignalingNaN)
    #expect(BFloat16.zero.isZero)
    #expect(BFloat16.negativeZero.isZero)
    #expect(BFloat16.one.isNormal)
    #expect(BFloat16.leastNonzeroMagnitude.isSubnormal)
}

@Test func bfloat16Arithmetic() {
    let a = BFloat16(3.0)
    let b = BFloat16(2.0)
    #expect(a + b == BFloat16(5.0))
    #expect(a - b == BFloat16(1.0))
    #expect(a * b == BFloat16(6.0))
    #expect(a / b == BFloat16(1.5))
    #expect(-a == BFloat16(-3.0))
    #expect(a.magnitude == BFloat16(3.0))
    #expect((-a).magnitude == BFloat16(3.0))
}

@Test func bfloat16FMA() {
    var c = BFloat16(1.0)
    c.addProduct(BFloat16(3.0), BFloat16(2.0))
    #expect(c == BFloat16(7.0))
}

@Test func bfloat16Sqrt() {
    var v = BFloat16(4.0)
    v.formSquareRoot()
    #expect(v == BFloat16(2.0))
}

@Test func bfloat16Rounding() {
    #expect(BFloat16(250.49).bitPattern == BFloat16(250.0).bitPattern)
    #expect(BFloat16(250.50).bitPattern == BFloat16(250.0).bitPattern)
    #expect(BFloat16(250.51).bitPattern == BFloat16(251.0).bitPattern)
    #expect(BFloat16(251.50).bitPattern == BFloat16(252.0).bitPattern)
}

@Test func bfloat16Conversions() {
    let exact = BFloat16(7.0)
    #expect(Float(exact) == 7.0)
    #expect(Double(exact) == 7.0)
    #expect(Int(exact) == 7)
    #expect(Int(exactly: exact) == 7)
    #expect(Int(exactly: BFloat16(6.5)) == nil)
    #expect(BFloat16(exactly: 7.5 as Float) == BFloat16(7.5))
    #expect(BFloat16(exactly: 7.0001 as Float) == nil)
    #expect(Float(exactly: BFloat16.nan) == nil)
}

@Test func bfloat16NaNComparisons() {
    #expect(BFloat16.nan != BFloat16.nan)
    #expect(!(BFloat16.nan > BFloat16.nan))
    #expect(!(BFloat16.nan < BFloat16.nan))
}

@Test func bfloat16Hashing() {
    #expect(BFloat16.zero.hashValue == BFloat16.negativeZero.hashValue)
}

@Test func bfloat16StringRoundTrip() {
    let v = BFloat16(3.5)
    let s = v.description
    #expect(BFloat16(s) == v)
    #expect(BFloat16("not a number") == nil)
}

@Test func bfloat16Codable() throws {
    let v = BFloat16(42.0)
    let data = try JSONEncoder().encode(v)
    let decoded = try JSONDecoder().decode(BFloat16.self, from: data)
    #expect(decoded == v)
}

@Test func bfloat16SIMD() {
    var v = SIMD4<BFloat16>(1.0, 2.0, 3.0, 4.0)
    for _ in 0..<11 {
        v += v
    }
    let expected = SIMD4<BFloat16>(2048.0, 4096.0, 6144.0, 8192.0)
    #expect(v == expected)
}

// MARK: - String

@Test func emptyString() throws {
    let r = try roundTrip({ $0.writeString("") }, read: { try $0.readString() })
    #expect(r == "")
}

@Test func asciiString() throws {
    let r = try roundTrip({ $0.writeString("hello") }, read: { try $0.readString() })
    #expect(r == "hello")
}

@Test func unicodeString() throws {
    let s = "Caf\u{00E9} \u{1F680}"
    let r = try roundTrip({ $0.writeString(s) }, read: { try $0.readString() })
    #expect(r == s)
}

@Test func stringWireFormat() throws {
    var writer = BebopWriter()
    writer.writeString("hi")
    let bytes = writer.toBytes()
    #expect(bytes.count == 7)
    #expect(bytes[0] == 2)
    #expect(bytes[1] == 0)
    #expect(bytes[2] == 0)
    #expect(bytes[3] == 0)
    #expect(bytes[4] == 0x68) // 'h'
    #expect(bytes[5] == 0x69) // 'i'
    #expect(bytes[6] == 0)    // NUL
}

// MARK: - UUID

@Test func uuid() throws {
    let id = UUID()
    let r = try roundTrip({ $0.writeUUID(id) }, read: { try $0.readUUID() })
    #expect(r == id)
}

@Test func uuidKnownBytes() throws {
    let id = UUID(uuidString: "12345678-1234-1234-1234-123456789ABC")!
    let r = try roundTrip({ $0.writeUUID(id) }, read: { try $0.readUUID() })
    #expect(r == id)
}

// MARK: - Timestamp

@Test func timestamp() throws {
    let ts = BebopTimestamp(seconds: 1_700_000_000, nanoseconds: 123_456_789)
    let r = try roundTrip({ $0.writeTimestamp(ts) }, read: { try $0.readTimestamp() })
    #expect(r.seconds == ts.seconds)
    #expect(r.nanoseconds == ts.nanoseconds)
}

@Test func timestampNegative() throws {
    let ts = BebopTimestamp(seconds: -1, nanoseconds: -500_000_000)
    let r = try roundTrip({ $0.writeTimestamp(ts) }, read: { try $0.readTimestamp() })
    #expect(r.seconds == -1)
    #expect(r.nanoseconds == -500_000_000)
}

// MARK: - Duration

@Test func duration() throws {
    let d = Duration(secondsComponent: 42, attosecondsComponent: 5_000_000_000)
    let r = try roundTrip({ $0.writeDuration(d) }, read: { try $0.readDuration() })
    #expect(r == d)
}

// MARK: - Wire Format: Little-Endian

@Test func uint32LittleEndian() throws {
    var writer = BebopWriter()
    writer.writeUInt32(0x04030201)
    #expect(writer.toBytes() == [0x01, 0x02, 0x03, 0x04])
}

@Test func uint16LittleEndian() throws {
    var writer = BebopWriter()
    writer.writeUInt16(0x0201)
    #expect(writer.toBytes() == [0x01, 0x02])
}

@Test func uint64LittleEndian() throws {
    var writer = BebopWriter()
    writer.writeUInt64(0x0807060504030201)
    #expect(writer.toBytes() == [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08])
}

// MARK: - Message Helpers

@Test func messageLength() throws {
    var writer = BebopWriter()
    let pos = writer.reserveMessageLength()
    writer.writeTag(1)
    writer.writeBool(true)
    writer.writeEndMarker()
    writer.fillMessageLength(at: pos)

    let result = try writer.withUnsafeBytes { buf -> (UInt32, UInt8, Bool, UInt8) in
        var reader = BebopReader(data: buf)
        return (
            try reader.readMessageLength(),
            try reader.readTag(),
            try reader.readBool(),
            try reader.readTag()
        )
    }
    #expect(result.0 == 3)
    #expect(result.1 == 1)
    #expect(result.2 == true)
    #expect(result.3 == 0)
}

// MARK: - Collection Helpers

@Test func arrayLength() throws {
    let r = try roundTrip({ $0.writeArrayLength(42) }, read: { try $0.readArrayLength() })
    #expect(r == 42)
}

@Test func mapLength() throws {
    let r = try roundTrip({ $0.writeMapLength(7) }, read: { try $0.readMapLength() })
    #expect(r == 7)
}

// MARK: - Skip

@Test func skip() throws {
    var writer = BebopWriter()
    writer.writeUInt32(0xAAAAAAAA)
    writer.writeUInt32(0xBBBBBBBB)
    let v = try writer.withUnsafeBytes { buf -> UInt32 in
        var reader = BebopReader(data: buf)
        try reader.skip(4)
        return try reader.readUInt32()
    }
    #expect(v == 0xBBBBBBBB)
}

// MARK: - Bounds Checking

@Test func readPastEnd() throws {
    let bytes: [UInt8] = [0x01]
    bytes.withUnsafeBytes { buf in
        var reader = BebopReader(data: buf)
        #expect(throws: BebopDecodingError.unexpectedEndOfData) {
            try reader.readUInt32()
        }
    }
}

@Test func readEmptyBuffer() throws {
    let bytes: [UInt8] = []
    bytes.withUnsafeBytes { buf in
        var reader = BebopReader(data: buf)
        #expect(throws: BebopDecodingError.unexpectedEndOfData) {
            try reader.readByte()
        }
    }
}

@Test func skipPastEnd() throws {
    let bytes: [UInt8] = [0x01, 0x02]
    bytes.withUnsafeBytes { buf in
        var reader = BebopReader(data: buf)
        #expect(throws: BebopDecodingError.unexpectedEndOfData) {
            try reader.skip(3)
        }
    }
}

// MARK: - Writer Growth

@Test func writerGrows() throws {
    var writer = BebopWriter(capacity: 64)
    for i: UInt32 in 0..<1000 {
        writer.writeUInt32(i)
    }
    #expect(writer.count == 4000)

    try writer.withUnsafeBytes { buf in
        var reader = BebopReader(data: buf)
        for i: UInt32 in 0..<1000 {
            let v = try reader.readUInt32()
            guard v == i else {
                #expect(v == i)
                return
            }
        }
    }
}

// MARK: - Sequential Multi-Type

@Test func mixedTypes() throws {
    let id = UUID(uuidString: "00000000-0000-0000-0000-000000000001")!
    var writer = BebopWriter()
    writer.writeBool(true)
    writer.writeInt8(-42)
    writer.writeUInt16(1000)
    writer.writeInt32(-100_000)
    writer.writeUInt64(0xDEADBEEF)
    writer.writeFloat32(3.14)
    writer.writeFloat64(2.718281828)
    writer.writeString("bebop")
    writer.writeUUID(id)

    try writer.withUnsafeBytes { buf in
        var r = BebopReader(data: buf)
        let vBool = try r.readBool()
        let vI8 = try r.readInt8()
        let vU16 = try r.readUInt16()
        let vI32 = try r.readInt32()
        let vU64 = try r.readUInt64()
        let vF32 = try r.readFloat32()
        let vF64 = try r.readFloat64()
        let vStr = try r.readString()
        let vUUID = try r.readUUID()

        #expect(vBool == true)
        #expect(vI8 == -42)
        #expect(vU16 == 1000)
        #expect(vI32 == -100_000)
        #expect(vU64 == 0xDEADBEEF)
        #expect(vF32 == 3.14 as Float)
        #expect(vF64 == 2.718281828)
        #expect(vStr == "bebop")
        #expect(vUUID == id)
    }
}

// MARK: - Unaligned Access

@Test func unalignedReads() throws {
    var writer = BebopWriter()
    writer.writeByte(0xFF)
    writer.writeUInt16(0x1234)
    writer.writeUInt32(0xDEADBEEF)
    writer.writeUInt64(0xCAFEBABE_12345678)

    try writer.withUnsafeBytes { buf in
        var r = BebopReader(data: buf)
        let vByte = try r.readByte()
        let vU16 = try r.readUInt16()
        let vU32 = try r.readUInt32()
        let vU64 = try r.readUInt64()

        #expect(vByte == 0xFF)
        #expect(vU16 == 0x1234)
        #expect(vU32 == 0xDEADBEEF)
        #expect(vU64 == 0xCAFEBABE_12345678)
    }
}
