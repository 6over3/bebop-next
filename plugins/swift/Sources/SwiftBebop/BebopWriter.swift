import Foundation

/// Write Bebop wire-format data into a growable byte buffer.
///
/// All multi-byte values are written little-endian. The writer owns its
/// backing allocation and frees it on `deinit` (non-copyable).
///
///     var writer = BebopWriter()
///     writer.writeUInt32(42)
///     writer.writeString("hello")
///     let bytes = writer.toBytes()
public struct BebopWriter: ~Copyable, @unchecked Sendable {
    @usableFromInline var storage: UnsafeMutableRawPointer
    @usableFromInline var _count: Int
    @usableFromInline var capacity: Int

    /// Create a writer with the given initial buffer capacity in bytes.
    public init(capacity: Int = 256) {
        let cap = max(capacity, 64)
        guard let ptr = malloc(cap) else {
            preconditionFailure("BebopWriter: failed to allocate \(cap) bytes")
        }
        self.storage = ptr
        self._count = 0
        self.capacity = cap
    }

    deinit {
        free(storage)
    }

    /// The number of bytes written so far.
    public var count: Int { _count }

    @usableFromInline
    mutating func grow(to minCapacity: Int) {
        var newCapacity = capacity
        while newCapacity < minCapacity {
            newCapacity &*= 2
        }
        guard let newStorage = realloc(storage, newCapacity) else {
            preconditionFailure("BebopWriter: failed to reallocate \(newCapacity) bytes")
        }
        storage = newStorage
        capacity = newCapacity
    }

    @inlinable @inline(__always)
    mutating func ensureCapacity(for additional: Int) {
        if _slowPath(_count &+ additional > capacity) {
            grow(to: _count &+ additional)
        }
    }

    /// Copy the written data into a new `[UInt8]`.
    public func toBytes() -> [UInt8] {
        let len = _count
        let ptr = storage
        return [UInt8](unsafeUninitializedCapacity: len) { buffer, initializedCount in
            if len > 0 {
                UnsafeMutableRawPointer(buffer.baseAddress!)
                    .copyMemory(from: ptr, byteCount: len)
            }
            initializedCount = len
        }
    }

    /// Access the written bytes as a raw buffer pointer for zero-copy reads.
    public func withUnsafeBytes<T>(_ body: (UnsafeRawBufferPointer) throws -> T) rethrows -> T {
        try body(UnsafeRawBufferPointer(start: storage, count: _count))
    }

    // MARK: - Primitives

    @inlinable @inline(__always)
    public mutating func writeBool(_ value: Bool) {
        ensureCapacity(for: 1)
        storage.storeBytes(of: value ? 1 : 0 as UInt8, toByteOffset: _count, as: UInt8.self)
        _count &+= 1
    }

    @inlinable @inline(__always)
    public mutating func writeByte(_ value: UInt8) {
        ensureCapacity(for: 1)
        storage.storeBytes(of: value, toByteOffset: _count, as: UInt8.self)
        _count &+= 1
    }

    @inlinable @inline(__always)
    public mutating func writeUInt8(_ value: UInt8) {
        writeByte(value)
    }

    @inlinable @inline(__always)
    public mutating func writeInt8(_ value: Int8) {
        ensureCapacity(for: 1)
        storage.storeBytes(of: value, toByteOffset: _count, as: Int8.self)
        _count &+= 1
    }

    @inlinable @inline(__always)
    public mutating func writeUInt16(_ value: UInt16) {
        ensureCapacity(for: 2)
        storage.storeBytes(of: value.littleEndian, toByteOffset: _count, as: UInt16.self)
        _count &+= 2
    }

    @inlinable @inline(__always)
    public mutating func writeInt16(_ value: Int16) {
        ensureCapacity(for: 2)
        storage.storeBytes(of: value.littleEndian, toByteOffset: _count, as: Int16.self)
        _count &+= 2
    }

    @inlinable @inline(__always)
    public mutating func writeUInt32(_ value: UInt32) {
        ensureCapacity(for: 4)
        storage.storeBytes(of: value.littleEndian, toByteOffset: _count, as: UInt32.self)
        _count &+= 4
    }

    @inlinable @inline(__always)
    public mutating func writeInt32(_ value: Int32) {
        ensureCapacity(for: 4)
        storage.storeBytes(of: value.littleEndian, toByteOffset: _count, as: Int32.self)
        _count &+= 4
    }

    @inlinable @inline(__always)
    public mutating func writeUInt64(_ value: UInt64) {
        ensureCapacity(for: 8)
        storage.storeBytes(of: value.littleEndian, toByteOffset: _count, as: UInt64.self)
        _count &+= 8
    }

    @inlinable @inline(__always)
    public mutating func writeInt64(_ value: Int64) {
        ensureCapacity(for: 8)
        storage.storeBytes(of: value.littleEndian, toByteOffset: _count, as: Int64.self)
        _count &+= 8
    }

    @inlinable
    public mutating func writeUInt128(_ value: UInt128) {
        ensureCapacity(for: 16)
        let low = UInt64(truncatingIfNeeded: value)
        let high = UInt64(truncatingIfNeeded: value &>> 64)
        storage.storeBytes(of: low.littleEndian, toByteOffset: _count, as: UInt64.self)
        storage.storeBytes(of: high.littleEndian, toByteOffset: _count &+ 8, as: UInt64.self)
        _count &+= 16
    }

    @inlinable
    public mutating func writeInt128(_ value: Int128) {
        ensureCapacity(for: 16)
        let uval = UInt128(bitPattern: value)
        let low = UInt64(truncatingIfNeeded: uval)
        let high = UInt64(truncatingIfNeeded: uval &>> 64)
        storage.storeBytes(of: low.littleEndian, toByteOffset: _count, as: UInt64.self)
        storage.storeBytes(of: high.littleEndian, toByteOffset: _count &+ 8, as: UInt64.self)
        _count &+= 16
    }

    @inlinable @inline(__always)
    public mutating func writeFloat16(_ value: Float16) {
        writeUInt16(value.bitPattern)
    }

    @inlinable @inline(__always)
    public mutating func writeFloat32(_ value: Float) {
        writeUInt32(value.bitPattern)
    }

    @inlinable @inline(__always)
    public mutating func writeFloat64(_ value: Double) {
        writeUInt64(value.bitPattern)
    }

    @inlinable @inline(__always)
    public mutating func writeBFloat16(_ value: BFloat16) {
        writeUInt16(value.bitPattern)
    }

    // MARK: - String

    @inlinable
    public mutating func writeString(_ value: String) {
        var value = value
        value.withUTF8 { utf8 in
            let length = utf8.count
            ensureCapacity(for: 4 + length + 1)
            storage.storeBytes(
                of: UInt32(length).littleEndian, toByteOffset: _count, as: UInt32.self
            )
            _count &+= 4
            if length > 0 {
                (storage + _count).copyMemory(
                    from: UnsafeRawPointer(utf8.baseAddress!), byteCount: length
                )
            }
            _count &+= length
            storage.storeBytes(of: 0 as UInt8, toByteOffset: _count, as: UInt8.self)
            _count &+= 1
        }
    }

    // MARK: - UUID

    @inlinable
    public mutating func writeUUID(_ value: UUID) {
        ensureCapacity(for: 16)
        Swift.withUnsafeBytes(of: value.uuid) { src in
            (storage + _count).copyMemory(from: src.baseAddress!, byteCount: 16)
        }
        _count &+= 16
    }

    // MARK: - Timestamp & Duration

    @inlinable
    public mutating func writeTimestamp(_ value: BebopTimestamp) {
        ensureCapacity(for: 12)
        storage.storeBytes(
            of: value.seconds.littleEndian, toByteOffset: _count, as: Int64.self
        )
        storage.storeBytes(
            of: value.nanoseconds.littleEndian, toByteOffset: _count &+ 8, as: Int32.self
        )
        _count &+= 12
    }

    @inlinable
    public mutating func writeDuration(_ value: Duration) {
        let (seconds, attoseconds) = value.components
        ensureCapacity(for: 12)
        storage.storeBytes(
            of: seconds.littleEndian, toByteOffset: _count, as: Int64.self
        )
        storage.storeBytes(
            of: Int32(attoseconds / 1_000_000_000).littleEndian,
            toByteOffset: _count &+ 8,
            as: Int32.self
        )
        _count &+= 12
    }

    // MARK: - InlineArray

    /// Bulk-write a fixed-size `InlineArray` of `BitwiseCopyable` elements via memcpy.
    @inlinable
    public mutating func writeInlineArray<let N: Int, T: BitwiseCopyable>(
        _ array: InlineArray<N, T>
    ) {
        let byteCount = N &* MemoryLayout<T>.stride
        guard byteCount > 0 else { return }
        ensureCapacity(for: byteCount)
        withUnsafePointer(to: array) { ptr in
            (storage + _count).copyMemory(
                from: UnsafeRawPointer(ptr), byteCount: byteCount
            )
        }
        _count &+= byteCount
    }

    /// Write a fixed-size `InlineArray` of non-trivial elements using a per-element closure.
    @inlinable
    public mutating func writeFixedInlineArray<let N: Int, T>(
        _ array: InlineArray<N, T>,
        _ body: (inout BebopWriter, T) -> Void
    ) {
        for i in 0..<N {
            body(&self, array[i])
        }
    }

    // MARK: - Data

    @inlinable
    public mutating func writeData(_ data: Data) {
        guard !data.isEmpty else { return }
        ensureCapacity(for: data.count)
        data.withUnsafeBytes { buf in
            (storage + _count).copyMemory(from: buf.baseAddress!, byteCount: buf.count)
        }
        _count &+= data.count
    }

    // MARK: - Bulk

    @inlinable
    public mutating func writeBytes(_ bytes: [UInt8]) {
        let count = bytes.count
        guard count > 0 else { return }
        ensureCapacity(for: count)
        bytes.withUnsafeBufferPointer { buf in
            (storage + _count).copyMemory(
                from: UnsafeRawPointer(buf.baseAddress!), byteCount: count
            )
        }
        _count &+= count
    }

    /// Bulk-write contiguous scalars via memcpy.
    ///
    /// Only valid for types whose in-memory layout matches the Bebop
    /// little-endian wire format (fixed-width integers and IEEE floats).
    @inlinable
    public mutating func writeArray<T: BitwiseCopyable>(_ values: [T]) {
        let byteCount = values.count &* MemoryLayout<T>.stride
        guard byteCount > 0 else { return }
        ensureCapacity(for: byteCount)
        values.withUnsafeBufferPointer { buf in
            (storage + _count).copyMemory(
                from: UnsafeRawPointer(buf.baseAddress!), byteCount: byteCount
            )
        }
        _count &+= byteCount
    }

    // MARK: - Collection helpers (closure-based, for nested containers)

    /// Write each element of a collection using a per-element closure (no length prefix).
    @inlinable
    public mutating func writeFixedArray<C: Collection>(
        _ values: C, _ body: (inout BebopWriter, C.Element) -> Void
    ) {
        for value in values {
            body(&self, value)
        }
    }

    /// Write a length-prefixed array using a per-element closure.
    @inlinable
    public mutating func writeDynamicArray<C: Collection>(
        _ values: C, _ body: (inout BebopWriter, C.Element) -> Void
    ) {
        writeUInt32(UInt32(values.count))
        for value in values {
            body(&self, value)
        }
    }

    /// Write a length-prefixed map using a per-entry closure.
    @inlinable
    public mutating func writeDynamicMap<K, V>(
        _ map: [K: V], _ body: (inout BebopWriter, K, V) -> Void
    ) {
        writeUInt32(UInt32(map.count))
        for (k, v) in map {
            body(&self, k, v)
        }
    }

    @inlinable
    public mutating func writeLengthPrefixedArray<T: BitwiseCopyable>(_ values: [T]) {
        writeUInt32(UInt32(values.count))
        writeArray(values)
    }

    // MARK: - Collection helpers (length prefix)

    @inlinable @inline(__always)
    public mutating func writeArrayLength(_ count: UInt32) {
        writeUInt32(count)
    }

    @inlinable @inline(__always)
    public mutating func writeMapLength(_ count: UInt32) {
        writeUInt32(count)
    }

    // MARK: - Message helpers

    /// Reserve 4 bytes for a message length prefix. Return the offset to
    /// pass to `fillMessageLength(at:)` after the message body is written.
    @inlinable
    public mutating func reserveMessageLength() -> Int {
        ensureCapacity(for: 4)
        let pos = _count
        _count &+= 4
        return pos
    }

    /// Backfill the message length at the offset returned by `reserveMessageLength()`.
    @inlinable
    public mutating func fillMessageLength(at position: Int) {
        let length = UInt32(_count - position - 4)
        storage.storeBytes(
            of: length.littleEndian, toByteOffset: position, as: UInt32.self
        )
    }

    @inlinable @inline(__always)
    public mutating func writeTag(_ tag: UInt8) {
        writeByte(tag)
    }

    @inlinable @inline(__always)
    public mutating func writeEndMarker() {
        writeByte(0)
    }
}
