/// An immutable, zero-copy slice of Bebop-encoded bytes.
///
/// A view retains the original array's copy-on-write storage. Creating nested
/// views does not copy their payload bytes.
public struct BebopView: Sendable, RandomAccessCollection {
    public typealias Element = UInt8
    public typealias Index = Int

    @usableFromInline
    let storage: [UInt8]

    @usableFromInline
    let bounds: Range<Int>

    /// Creates a view over an encoded byte array without copying its storage.
    @inlinable
    public init(_ bytes: [UInt8]) {
        storage = bytes
        bounds = bytes.indices
    }

    @usableFromInline
    init(storage: [UInt8], bounds: Range<Int>) {
        self.storage = storage
        self.bounds = bounds
    }

    @inlinable
    public var startIndex: Int { 0 }

    @inlinable
    public var endIndex: Int { bounds.count }

    @inlinable
    public subscript(position: Int) -> UInt8 {
        precondition(indices.contains(position), "BebopView index out of bounds")
        return storage[bounds.lowerBound + position]
    }

    @inlinable
    public func index(after index: Int) -> Int { index + 1 }

    @inlinable
    public func index(before index: Int) -> Int { index - 1 }

    /// Provides contiguous access to this slice without allocating.
    @inlinable
    public func withUnsafeBytes<Result>(
        _ body: (UnsafeRawBufferPointer) throws -> Result
    ) rethrows -> Result {
        try storage.withUnsafeBytes { bytes in
            let base = bytes.baseAddress.map { $0 + bounds.lowerBound }
            return try body(UnsafeRawBufferPointer(start: base, count: bounds.count))
        }
    }

    /// Materializes this slice as an independent byte array.
    @inlinable
    public var bytes: [UInt8] { Array(self) }

    @usableFromInline
    func slice(_ range: Range<Int>) -> BebopView {
        precondition(range.lowerBound >= 0 && range.upperBound <= count)
        return BebopView(
            storage: storage,
            bounds: (bounds.lowerBound + range.lowerBound)..<(bounds.lowerBound + range.upperBound)
        )
    }
}

/// A validated, immutable UTF-8 string that remains backed by encoded bytes.
public struct BebopStringView: Sendable, Hashable, CustomStringConvertible {
    public let rawBytes: BebopView

    @usableFromInline
    init(validated rawBytes: BebopView) {
        self.rawBytes = rawBytes
    }

    /// Materializes the string value.
    @inlinable
    public var value: String {
        rawBytes.withUnsafeBytes { String(decoding: $0, as: UTF8.self) }
    }

    @inlinable
    public var description: String { value }

    @inlinable
    public static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.rawBytes.elementsEqual(rhs.rawBytes)
    }

    @inlinable
    public func hash(into hasher: inout Hasher) {
        for byte in rawBytes { hasher.combine(byte) }
    }
}

/// A cursor used by generated views to validate and slice encoded values.
public struct BebopViewReader: Sendable {
    public let encoded: BebopView

    @usableFromInline
    var offset: Int

    @inlinable
    public init(_ encoded: BebopView) {
        self.encoded = encoded
        offset = 0
    }

    @inlinable
    public var position: Int { offset }

    @inlinable
    public var remaining: Int { encoded.count - offset }

    @inlinable
    public mutating func finish() throws {
        guard offset == encoded.count else { throw BebopDecodingError.trailingData }
    }

    @inlinable
    public func view(from start: Int) -> BebopView {
        encoded.slice(start..<offset)
    }

    @usableFromInline
    @inline(__always)
    func ensureBytes(_ count: Int) throws {
        guard _fastPath(count >= 0 && offset >= 0 && count <= encoded.count - offset) else {
            throw BebopDecodingError.unexpectedEndOfData
        }
    }

    @usableFromInline
    @inline(__always)
    mutating func readInteger<Value: FixedWidthInteger>(_: Value.Type) throws -> Value {
        let size = MemoryLayout<Value>.size
        try ensureBytes(size)
        let value = encoded.withUnsafeBytes {
            $0.loadUnaligned(fromByteOffset: offset, as: Value.self)
        }
        offset += size
        return Value(littleEndian: value)
    }

    @inlinable
    public mutating func readBool() throws -> Bool { try readByte() != 0 }

    @inlinable
    public mutating func readByte() throws -> UInt8 {
        try ensureBytes(1)
        let value = encoded[offset]
        offset += 1
        return value
    }
    @inlinable public mutating func readUInt8() throws -> UInt8 { try readByte() }
    @inlinable public mutating func readInt8() throws -> Int8 { try readInteger(Int8.self) }
    @inlinable public mutating func readUInt16() throws -> UInt16 { try readInteger(UInt16.self) }
    @inlinable public mutating func readInt16() throws -> Int16 { try readInteger(Int16.self) }
    @inlinable public mutating func readUInt32() throws -> UInt32 { try readInteger(UInt32.self) }
    @inlinable public mutating func readInt32() throws -> Int32 { try readInteger(Int32.self) }
    @inlinable public mutating func readUInt64() throws -> UInt64 { try readInteger(UInt64.self) }
    @inlinable public mutating func readInt64() throws -> Int64 { try readInteger(Int64.self) }
    @inlinable public mutating func readUInt128() throws -> UInt128 { try readInteger(UInt128.self) }
    @inlinable public mutating func readInt128() throws -> Int128 { try readInteger(Int128.self) }
    #if !os(macOS) || arch(arm64)
    @inlinable public mutating func readFloat16() throws -> Float16 {
        try Float16(bitPattern: readUInt16())
    }
    #endif
    @inlinable public mutating func readFloat32() throws -> Float {
        try Float(bitPattern: readUInt32())
    }
    @inlinable public mutating func readFloat64() throws -> Double {
        try Double(bitPattern: readUInt64())
    }
    @inlinable public mutating func readBFloat16() throws -> BFloat16 {
        try BFloat16(bitPattern: readUInt16())
    }
    @inlinable public mutating func readUUID() throws -> BebopUUID {
        let size = MemoryLayout<BebopUUID>.size
        try ensureBytes(size)
        let value = encoded.withUnsafeBytes {
            $0.loadUnaligned(fromByteOffset: offset, as: BebopUUID.self)
        }
        offset += size
        return value
    }
    @inlinable public mutating func readTimestamp() throws -> BebopTimestamp {
        BebopTimestamp(
            seconds: try readInt64(),
            nanoseconds: try readInt32(),
            offsetMs: try readInt32())
    }
    @inlinable public mutating func readDuration() throws -> Duration {
        let seconds = try readInt64()
        let nanoseconds = try readInt32()
        return Duration(
            secondsComponent: seconds,
            attosecondsComponent: Int64(nanoseconds) * 1_000_000_000)
    }

    public mutating func readStringView() throws -> BebopStringView {
        let length = Int(try readUInt32())
        guard length >= 0, length < remaining else { throw BebopDecodingError.unexpectedEndOfData }
        let content = encoded.slice(offset..<(offset + length))
        guard encoded[offset + length] == 0 else {
            throw BebopDecodingError.invalidStringTerminator
        }
        let valid = content.withUnsafeBytes { bytes in
            let codeUnits = bytes.bindMemory(to: UInt8.self)
            return (try? UTF8Span(validating: codeUnits.span)) != nil
        }
        guard valid else { throw BebopDecodingError.invalidUTF8 }
        offset += length + 1
        return BebopStringView(validated: content)
    }

    public mutating func readBytesView(count: Int) throws -> BebopView {
        guard count >= 0, count <= remaining else { throw BebopDecodingError.unexpectedEndOfData }
        let result = encoded.slice(offset..<(offset + count))
        offset += count
        return result
    }

    public mutating func readMessageView() throws -> BebopMessageView {
        let start = offset
        let bodyLength = Int(try readUInt32())
        guard bodyLength > 0, bodyLength <= remaining else {
            throw BebopDecodingError.unexpectedEndOfData
        }
        offset += bodyLength
        return try BebopMessageView(encoded.slice(start..<offset))
    }

    /// Reads a non-indexed, length-prefixed aggregate such as a union.
    public mutating func readLengthPrefixedView() throws -> BebopView {
        let start = offset
        let bodyLength = Int(try readUInt32())
        guard bodyLength > 0, bodyLength <= remaining else {
            throw BebopDecodingError.unexpectedEndOfData
        }
        offset += bodyLength
        return encoded.slice(start..<offset)
    }

    public mutating func readArrayView<Element: Sendable>(
        _ decode: @escaping @Sendable (inout BebopViewReader) throws -> Element
    ) throws -> BebopArrayView<Element> {
        let start = offset
        let count = Int(try readUInt32())
        for _ in 0..<count { _ = try decode(&self) }
        return BebopArrayView(
            validated: encoded.slice(start..<offset), count: count, decode: decode)
    }

    public mutating func readFixedArrayView<Element: Sendable>(
        count: Int,
        _ decode: @escaping @Sendable (inout BebopViewReader) throws -> Element
    ) throws -> BebopArrayView<Element> {
        let start = offset
        for _ in 0..<count { _ = try decode(&self) }
        return BebopArrayView(
            validated: encoded.slice(start..<offset), count: count, prefixSize: 0, decode: decode)
    }

    public mutating func readMapView<Key: Sendable, Value: Sendable>(
        key: @escaping @Sendable (inout BebopViewReader) throws -> Key,
        value: @escaping @Sendable (inout BebopViewReader) throws -> Value
    ) throws -> BebopMapView<Key, Value> {
        let start = offset
        let count = Int(try readUInt32())
        for _ in 0..<count {
            _ = try key(&self)
            _ = try value(&self)
        }
        return BebopMapView(
            validated: encoded.slice(start..<offset), count: count, key: key, value: value)
    }
}

/// An immutable, lazily materialized view over an encoded array.
public struct BebopArrayView<Element: Sendable>: Sendable, Sequence {
    public let count: Int
    private let encoded: BebopView
    private let prefixSize: Int
    private let decode: @Sendable (inout BebopViewReader) throws -> Element

    @usableFromInline
    init(
        validated encoded: BebopView,
        count: Int,
        prefixSize: Int = 4,
        decode: @escaping @Sendable (inout BebopViewReader) throws -> Element
    ) {
        self.encoded = encoded
        self.count = count
        self.prefixSize = prefixSize
        self.decode = decode
    }

    public var isEmpty: Bool { count == 0 }

    public var first: Element? {
        var iterator = makeIterator()
        return iterator.next()
    }

    public func makeIterator() -> Iterator {
        Iterator(
            reader: BebopViewReader(encoded.slice(prefixSize..<encoded.count)),
            remaining: count,
            decode: decode)
    }

    public struct Iterator: IteratorProtocol {
        private var reader: BebopViewReader
        private var remaining: Int
        private let decode: @Sendable (inout BebopViewReader) throws -> Element

        fileprivate init(
            reader: BebopViewReader,
            remaining: Int,
            decode: @escaping @Sendable (inout BebopViewReader) throws -> Element
        ) {
            self.reader = reader
            self.remaining = remaining
            self.decode = decode
        }

        public mutating func next() -> Element? {
            guard remaining > 0 else { return nil }
            remaining -= 1
            do {
                return try decode(&reader)
            } catch {
                preconditionFailure("validated Bebop array became invalid: \(error)")
            }
        }
    }
}

/// An immutable, lazily materialized view over an encoded map.
public struct BebopMapView<Key: Sendable, Value: Sendable>: Sendable, Sequence {
    public typealias Element = (key: Key, value: Value)

    public let count: Int
    private let encoded: BebopView
    private let readKey: @Sendable (inout BebopViewReader) throws -> Key
    private let readValue: @Sendable (inout BebopViewReader) throws -> Value

    @usableFromInline
    init(
        validated encoded: BebopView,
        count: Int,
        key: @escaping @Sendable (inout BebopViewReader) throws -> Key,
        value: @escaping @Sendable (inout BebopViewReader) throws -> Value
    ) {
        self.encoded = encoded
        self.count = count
        self.readKey = key
        self.readValue = value
    }

    public var isEmpty: Bool { count == 0 }

    public var first: Element? {
        var iterator = makeIterator()
        return iterator.next()
    }

    public func makeIterator() -> Iterator {
        Iterator(
            reader: BebopViewReader(encoded.slice(4..<encoded.count)),
            remaining: count,
            readKey: readKey,
            readValue: readValue)
    }

    public struct Iterator: IteratorProtocol {
        private var reader: BebopViewReader
        private var remaining: Int
        private let readKey: @Sendable (inout BebopViewReader) throws -> Key
        private let readValue: @Sendable (inout BebopViewReader) throws -> Value

        fileprivate init(
            reader: BebopViewReader,
            remaining: Int,
            readKey: @escaping @Sendable (inout BebopViewReader) throws -> Key,
            readValue: @escaping @Sendable (inout BebopViewReader) throws -> Value
        ) {
            self.reader = reader
            self.remaining = remaining
            self.readKey = readKey
            self.readValue = readValue
        }

        public mutating func next() -> Element? {
            guard remaining > 0 else { return nil }
            remaining -= 1
            do {
                return try (readKey(&reader), readValue(&reader))
            } catch {
                preconditionFailure("validated Bebop map became invalid: \(error)")
            }
        }
    }
}

public extension BebopMapView where Key: Equatable {
    subscript(key: Key) -> Value? {
        first { $0.key == key }?.value
    }
}

public extension BebopMapView where Key == BebopStringView {
    subscript(key: String) -> Value? {
        first { $0.key.rawBytes.elementsEqual(key.utf8) }?.value
    }
}
