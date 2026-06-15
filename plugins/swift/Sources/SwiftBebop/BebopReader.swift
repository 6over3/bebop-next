/// Read Bebop wire-format data from a raw byte buffer.
///
/// All multi-byte values are read little-endian. The reader tracks a current
/// offset and advances it after each read. Out-of-bounds reads throw
/// `BebopDecodingError.unexpectedEndOfData`.
///
/// - Important: The buffer must remain valid for the lifetime of the reader.
public struct BebopReader: @unchecked Sendable {
    @usableFromInline let base: UnsafeRawPointer
    @usableFromInline let end: Int
    @usableFromInline var limit: Int
    @usableFromInline var limitStack: [Int]
    @usableFromInline var offset: Int

    /// Create a reader over the given buffer.
    public init(data: UnsafeRawBufferPointer) {
        base = data.baseAddress ?? UnsafeRawPointer(bitPattern: 1)!
        end = data.count
        limit = data.count
        limitStack = []
        offset = 0
    }

    /// The current byte offset in the buffer.
    @inlinable @inline(__always)
    public var position: Int { offset }

    /// Move the read cursor to an absolute byte offset.
    @inlinable @inline(__always)
    public mutating func seek(to position: Int) {
        offset = position
    }

    @inlinable @inline(__always)
    public var remaining: Int { max(0, limit - offset) }

    @usableFromInline
    mutating func popExpiredLimitIfNeeded() {
        while offset == limit, let restored = limitStack.popLast() {
            limit = restored
        }
    }

    @inlinable @inline(__always)
    func ensureBytes(_ count: Int) throws {
        guard _fastPath(count >= 0 && offset >= 0 && offset <= limit && count <= limit - offset) else {
            throw BebopDecodingError.unexpectedEndOfData
        }
    }

    @usableFromInline
    mutating func advance(by count: Int) {
        offset += count
        popExpiredLimitIfNeeded()
    }

    // MARK: - Primitives

    @inlinable @inline(__always)
    public mutating func readBool() throws -> Bool {
        try ensureBytes(1)
        let value = base.load(fromByteOffset: offset, as: UInt8.self)
        advance(by: 1)
        return value != 0
    }

    @inlinable @inline(__always)
    public mutating func readByte() throws -> UInt8 {
        try ensureBytes(1)
        let value = base.load(fromByteOffset: offset, as: UInt8.self)
        advance(by: 1)
        return value
    }

    @inlinable @inline(__always)
    public mutating func readUInt8() throws -> UInt8 {
        try readByte()
    }

    @inlinable @inline(__always)
    public mutating func readInt8() throws -> Int8 {
        try ensureBytes(1)
        let value = base.load(fromByteOffset: offset, as: Int8.self)
        advance(by: 1)
        return value
    }

    @inlinable @inline(__always)
    public mutating func readUInt16() throws -> UInt16 {
        try ensureBytes(2)
        let value = base.loadUnaligned(fromByteOffset: offset, as: UInt16.self)
        advance(by: 2)
        return UInt16(littleEndian: value)
    }

    @inlinable @inline(__always)
    public mutating func readInt16() throws -> Int16 {
        try ensureBytes(2)
        let value = base.loadUnaligned(fromByteOffset: offset, as: Int16.self)
        advance(by: 2)
        return Int16(littleEndian: value)
    }

    @inlinable @inline(__always)
    public mutating func readUInt32() throws -> UInt32 {
        try ensureBytes(4)
        let value = base.loadUnaligned(fromByteOffset: offset, as: UInt32.self)
        advance(by: 4)
        return UInt32(littleEndian: value)
    }

    @inlinable @inline(__always)
    public mutating func readInt32() throws -> Int32 {
        try ensureBytes(4)
        let value = base.loadUnaligned(fromByteOffset: offset, as: Int32.self)
        advance(by: 4)
        return Int32(littleEndian: value)
    }

    @inlinable @inline(__always)
    public mutating func readUInt64() throws -> UInt64 {
        try ensureBytes(8)
        let value = base.loadUnaligned(fromByteOffset: offset, as: UInt64.self)
        advance(by: 8)
        return UInt64(littleEndian: value)
    }

    @inlinable @inline(__always)
    public mutating func readInt64() throws -> Int64 {
        try ensureBytes(8)
        let value = base.loadUnaligned(fromByteOffset: offset, as: Int64.self)
        advance(by: 8)
        return Int64(littleEndian: value)
    }

    @inlinable
    public mutating func readUInt128() throws -> UInt128 {
        try ensureBytes(16)
        let low = UInt64(littleEndian: base.loadUnaligned(fromByteOffset: offset, as: UInt64.self))
        let high = UInt64(
            littleEndian: base.loadUnaligned(fromByteOffset: offset &+ 8, as: UInt64.self))
        advance(by: 16)
        return UInt128(high) << 64 | UInt128(low)
    }

    @inlinable
    public mutating func readInt128() throws -> Int128 {
        try Int128(bitPattern: readUInt128())
    }

    #if !os(macOS) || arch(arm64)
    @inlinable @inline(__always)
    public mutating func readFloat16() throws -> Float16 {
        try Float16(bitPattern: readUInt16())
    }
    #endif

    @inlinable @inline(__always)
    public mutating func readFloat32() throws -> Float {
        try Float(bitPattern: readUInt32())
    }

    @inlinable @inline(__always)
    public mutating func readFloat64() throws -> Double {
        try Double(bitPattern: readUInt64())
    }

    @inlinable @inline(__always)
    public mutating func readBFloat16() throws -> BFloat16 {
        try BFloat16(bitPattern: readUInt16())
    }

    // MARK: - String

    @inlinable
    public mutating func readString() throws -> String {
        let length = try Int(readUInt32())
        try ensureBytes(length + 1)
        guard base.load(fromByteOffset: offset + length, as: UInt8.self) == 0 else {
            throw BebopDecodingError.invalidStringTerminator
        }
        let bytes = UnsafeBufferPointer(
            start: length == 0 ? nil : (base + offset).assumingMemoryBound(to: UInt8.self),
            count: length
        )
        let str = String(decoding: bytes, as: UTF8.self)
        guard str.utf8.elementsEqual(bytes) else {
            throw BebopDecodingError.invalidUTF8
        }
        advance(by: length + 1)
        return str
    }

    // MARK: - UUID

    @inlinable
    public mutating func readUUID() throws -> BebopUUID {
        try ensureBytes(16)
        let uuid = base.loadUnaligned(fromByteOffset: offset, as: BebopUUID.self)
        advance(by: 16)
        return uuid
    }

    // MARK: - Timestamp & Duration

    @inlinable
    public mutating func readTimestamp() throws -> BebopTimestamp {
        try ensureBytes(16)
        let seconds = Int64(littleEndian: base.loadUnaligned(fromByteOffset: offset, as: Int64.self))
        let nanos = Int32(littleEndian: base.loadUnaligned(fromByteOffset: offset &+ 8, as: Int32.self))
        let offsetMs = Int32(
            littleEndian: base.loadUnaligned(fromByteOffset: offset &+ 12, as: Int32.self))
        advance(by: 16)
        return BebopTimestamp(seconds: seconds, nanoseconds: nanos, offsetMs: offsetMs)
    }

    @inlinable
    public mutating func readDuration() throws -> Duration {
        try ensureBytes(12)
        let seconds = Int64(littleEndian: base.loadUnaligned(fromByteOffset: offset, as: Int64.self))
        let nanos = Int32(littleEndian: base.loadUnaligned(fromByteOffset: offset &+ 8, as: Int32.self))
        advance(by: 12)
        return Duration(
            secondsComponent: seconds,
            attosecondsComponent: Int64(nanos) * 1_000_000_000
        )
    }

    // MARK: - Bulk

    @inlinable
    public mutating func readBytes(_ count: Int) throws -> [UInt8] {
        try ensureBytes(count)
        let result = [UInt8](unsafeUninitializedCapacity: count) { buf, initialized in
            if count > 0 {
                UnsafeMutableRawPointer(buf.baseAddress!)
                    .copyMemory(from: base + offset, byteCount: count)
            }
            initialized = count
        }
        advance(by: count)
        return result
    }

    /// Bulk-read `count` contiguous scalars via memcpy.
    ///
    /// Only valid for types whose in-memory layout matches the Bebop
    /// little-endian wire format (fixed-width integers and IEEE floats).
    @inlinable
    public mutating func readArray<T: BebopScalar>(_ count: Int, of _: T.Type) throws -> [T] {
        let (byteCount, overflow) = count.multipliedReportingOverflow(by: MemoryLayout<T>.stride)
        guard !overflow else { throw BebopDecodingError.invalidLength }
        try ensureBytes(byteCount)
        let src = base + offset
        let result = [T](unsafeUninitializedCapacity: count) { buf, initialized in
            if count > 0 {
                UnsafeMutableRawPointer(buf.baseAddress!)
                    .copyMemory(from: src, byteCount: byteCount)
            }
            initialized = count
        }
        advance(by: byteCount)
        return result
    }

    // MARK: - InlineArray

    /// Bulk-read a fixed-size `InlineArray` of `BebopScalar` elements via memcpy.
    @inlinable
    public mutating func readInlineArray< let N: Int, T: BebopScalar > (
        of type: T.Type
    ) throws -> InlineArray<N, T> {
        let (byteCount, overflow) = N.multipliedReportingOverflow(by: MemoryLayout<T>.stride)
        guard !overflow else { throw BebopDecodingError.invalidLength }
        try ensureBytes(byteCount)
        let result = (base + offset).loadUnaligned(as: InlineArray<N, T>.self)
        advance(by: byteCount)
        return result
    }

    /// Read a fixed-size `InlineArray` of non-trivial elements using a per-element closure.
    @inlinable
    public mutating func readFixedInlineArray< let N: Int, T > (
        _ body: (inout BebopReader) throws -> T
    ) throws -> InlineArray<N, T> {
        var reader = self
        let result = try InlineArray<N, T> { _ in try body(&reader) }
        self = reader
        return result
    }

    // MARK: - Collection helpers (closure-based, for nested containers)

    /// Read a fixed-count array using a per-element closure.
    @inlinable
    public mutating func readFixedArray<T>(
        _ count: Int, _ body: (inout BebopReader) throws -> T
    ) throws -> [T] {
        try ensureBytes(count)
        var result = [T]()
        result.reserveCapacity(min(count, remaining))
        for _ in 0 ..< count {
            try result.append(body(&self))
        }
        return result
    }

    /// Read a length-prefixed array using a per-element closure.
    @inlinable
    public mutating func readDynamicArray<T>(
        _ body: (inout BebopReader) throws -> T
    ) throws -> [T] {
        let count = try Int(readUInt32())
        var result = [T]()
        result.reserveCapacity(min(count, remaining))
        for _ in 0 ..< count {
            try result.append(body(&self))
        }
        return result
    }

    /// Read a length-prefixed map using a per-entry closure that returns a key-value pair.
    @inlinable
    public mutating func readDynamicMap<K: Hashable, V>(
        _ body: (inout BebopReader) throws -> (K, V)
    ) throws -> [K: V] {
        let count = try Int(readUInt32())
        var result = [K: V](minimumCapacity: min(count, remaining))
        for _ in 0 ..< count {
            let (k, v) = try body(&self)
            result[k] = v
        }
        return result
    }

    // MARK: - Collection helpers (length prefix)

    @inlinable
    public mutating func readLengthPrefixedArray<T: BebopScalar>(of type: T.Type) throws -> [T] {
        let count = try Int(readUInt32())
        return try readArray(count, of: type)
    }

    @inlinable @inline(__always)
    public mutating func readArrayLength() throws -> UInt32 {
        try readUInt32()
    }

    @inlinable @inline(__always)
    public mutating func readMapLength() throws -> UInt32 {
        try readUInt32()
    }

    // MARK: - Message helpers

    /// Read the 4-byte message body length prefix.
    @inlinable @inline(__always)
    public mutating func readMessageLength() throws -> UInt32 {
        let length = try readUInt32()
        let byteCount = Int(length)
        guard byteCount > 0 else {
            throw BebopDecodingError.unexpectedEndOfData
        }
        try ensureBytes(byteCount)
        limitStack.append(limit)
        limit = offset + byteCount
        return length
    }

    /// Read a 1-byte message field tag. Returns 0 for the end-of-message marker.
    @inlinable @inline(__always)
    public mutating func readTag() throws -> UInt8 {
        try ensureBytes(1)
        let tag = base.load(fromByteOffset: offset, as: UInt8.self)
        offset += 1
        if tag == 0, offset != limit {
            throw BebopDecodingError.trailingData
        }
        popExpiredLimitIfNeeded()
        return tag
    }

    /// Advance the read cursor by `count` bytes, skipping over unknown data.
    @inlinable @inline(__always)
    public mutating func skip(_ count: Int) throws {
        try ensureBytes(count)
        advance(by: count)
    }
}
