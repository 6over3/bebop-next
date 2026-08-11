#if canImport(Darwin)
    import Darwin
#elseif canImport(ucrt)
    import ucrt
#elseif canImport(Bionic)
    import Bionic
#elseif os(WASI)
    import WASILibc
#elseif canImport(Musl)
    import Musl
#elseif canImport(Glibc)
    import Glibc
#elseif canImport(Android)
    import Android
#endif

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
        storage = ptr
        _count = 0
        self.capacity = cap
    }

    deinit {
        free(storage)
    }

    /// The number of bytes written so far.
    public var count: Int { _count }

    @usableFromInline
    mutating func grow(to minCapacity: Int) {
        precondition(minCapacity >= 0, "BebopWriter capacity overflow")
        var newCapacity = capacity
        while newCapacity < minCapacity {
            if newCapacity > Int.max / 2 {
                newCapacity = minCapacity
                break
            }
            newCapacity *= 2
        }
        guard let newStorage = realloc(storage, newCapacity) else {
            preconditionFailure("BebopWriter: failed to reallocate \(newCapacity) bytes")
        }
        storage = newStorage
        capacity = newCapacity
    }

    @inlinable @inline(__always)
    mutating func ensureCapacity(for additional: Int) {
        precondition(additional >= 0 && _count <= Int.max - additional, "BebopWriter size overflow")
        let required = _count + additional
        if _slowPath(required > capacity) {
            grow(to: required)
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

    #if !os(macOS) || arch(arm64)
    @inlinable @inline(__always)
    public mutating func writeFloat16(_ value: Float16) {
        writeUInt16(value.bitPattern)
    }
    #endif

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
            precondition(length <= Int(UInt32.max), "string exceeds uint32 length")
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
    public mutating func writeUUID(_ value: BebopUUID) {
        ensureCapacity(for: 16)
        Swift.withUnsafeBytes(of: value) { src in
            (storage + _count).copyMemory(from: src.baseAddress!, byteCount: 16)
        }
        _count &+= 16
    }

    // MARK: - Timestamp & Duration

    @inlinable
    public mutating func writeTimestamp(_ value: BebopTimestamp) {
        ensureCapacity(for: 16)
        storage.storeBytes(
            of: value.seconds.littleEndian, toByteOffset: _count, as: Int64.self
        )
        storage.storeBytes(
            of: value.nanoseconds.littleEndian, toByteOffset: _count &+ 8, as: Int32.self
        )
        storage.storeBytes(
            of: value.offsetMs.littleEndian, toByteOffset: _count &+ 12, as: Int32.self
        )
        _count &+= 16
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

    /// Bulk-write a fixed-size `InlineArray` of `BebopScalar` elements via memcpy.
    @inlinable
    public mutating func writeInlineArray< let N: Int, T: BebopScalar > (
        _ array: InlineArray<N, T>
    ) {
        let (byteCount, overflow) = N.multipliedReportingOverflow(by: MemoryLayout<T>.stride)
        precondition(!overflow, "fixed array byte count overflow")
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
    public mutating func writeFixedInlineArray< let N: Int, T > (
        _ array: InlineArray<N, T>,
        _ body: (inout BebopWriter, T) -> Void
    ) {
        for i in 0 ..< N {
            body(&self, array[i])
        }
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
    public mutating func writeArray<T: BebopScalar>(_ values: [T]) {
        let (byteCount, overflow) = values.count.multipliedReportingOverflow(
            by: MemoryLayout<T>.stride
        )
        precondition(!overflow, "array byte count overflow")
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
        precondition(values.count <= Int(UInt32.max), "array exceeds uint32 element count")
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
        precondition(map.count <= Int(UInt32.max), "map exceeds uint32 entry count")
        writeUInt32(UInt32(map.count))
        for (k, v) in map {
            body(&self, k, v)
        }
    }

    @inlinable
    public mutating func writeLengthPrefixedArray(_ values: [some BebopScalar]) {
        precondition(values.count <= Int(UInt32.max), "array exceeds uint32 element count")
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

    /// The current number of encoded bytes.
    @inlinable @inline(__always)
    public var position: Int { _count }

    /// Begins an indexed message and returns its payload start.
    @inlinable
    public mutating func beginMessage() -> Int {
        writeUInt32(0)
        return _count
    }

    /// Finishes an empty indexed message.
    @inlinable
    public mutating func endMessage(payloadStart: Int) {
        precondition(_count == payloadStart, "empty message contains payload bytes")
        writeByte(0)
        endLengthPrefixedValue(at: payloadStart - 4)
    }

    /// Appends the indexed-message boundaries and directory, then fills its length prefix.
    public mutating func endMessage<let N: Int>(
        payloadStart: Int,
        tags: borrowing InlineArray<N, UInt8>,
        offsets: borrowing InlineArray<N, UInt32>,
        count: Int
    ) {
        precondition(count >= 0 && count <= N)
        precondition(payloadStart >= 4 && payloadStart <= _count)
        let payloadSize = _count - payloadStart
        precondition(payloadSize <= Int(UInt32.max))
        let tagValues = tags.span
        let offsetValues = offsets.span
        if count == 0 {
            precondition(payloadSize == 0, "empty message contains payload bytes")
        } else {
            precondition(offsetValues[0] == 0)
        }
        for index in 0..<count {
            precondition(Int(offsetValues[index]) <= payloadSize)
            if index > 0 {
                precondition(tagValues[index] > tagValues[index - 1])
                precondition(offsetValues[index] >= offsetValues[index - 1])
            }
        }

        let width = payloadSize <= 255 ? 1 : payloadSize <= 65_535 ? 2 : 4
        let directory = BebopMessageLayout.directoryLayout(tags: tagValues, count: count)
        ensureCapacity(for: max(0, count - 1) * width + directory.size + 1)
        if count > 1 {
            for index in 1..<count { writeOffset(offsetValues[index], width: width) }
        }

        switch directory.kind {
        case 0:
            break
        case 1...3:
            for index in 0..<count { writeByte(tagValues[index]) }
        case 4...6:
            var mask: UInt32 = 0
            for index in 0..<count { mask |= 1 << UInt32(tagValues[index] - 1) }
            writeOffset(mask, width: directory.size)
        case 7:
            var next = 0
            var rank: UInt8 = 0
            for block in UInt8(0)..<8 where directory.blockMask & (1 << block) != 0 {
                var mask: UInt32 = 0
                let firstTag = block * 32 + 1
                let limit = UInt16(firstTag) + 32
                while next < count, UInt16(tagValues[next]) < limit {
                    mask |= 1 << UInt32(tagValues[next] - firstTag)
                    next += 1
                }
                writeByte(rank)
                writeUInt32(mask)
                rank += UInt8(mask.nonzeroBitCount)
            }
            writeByte(directory.blockMask)
        default:
            preconditionFailure("invalid indexed-message directory")
        }
        let widthCode: UInt8 = width == 1 ? 0 : width == 2 ? 1 : 2
        writeByte(directory.kind << 2 | widthCode)
        endLengthPrefixedValue(at: payloadStart - 4)
    }

    @inline(__always)
    private mutating func writeOffset(_ value: UInt32, width: Int) {
        switch width {
        case 1: writeByte(UInt8(truncatingIfNeeded: value))
        case 2: writeUInt16(UInt16(truncatingIfNeeded: value))
        default: writeUInt32(value)
        }
    }

    /// Begins a non-indexed, length-prefixed value and returns its reservation.
    @inlinable
    public mutating func beginLengthPrefixedValue() -> Int {
        ensureCapacity(for: 4)
        let pos = _count
        _count &+= 4
        return pos
    }

    /// Finishes the value begun at `position` and writes its body length.
    @inlinable
    public mutating func endLengthPrefixedValue(at position: Int) {
        precondition(_count - position - 4 <= Int(UInt32.max), "message exceeds uint32 length")
        let length = UInt32(_count - position - 4)
        storage.storeBytes(
            of: length.littleEndian, toByteOffset: position, as: UInt32.self
        )
    }

}
