private enum MessageDirectoryKind: UInt8 {
    case empty = 0
    case tiny1 = 1
    case tiny2 = 2
    case tiny3 = 3
    case mask8 = 4
    case mask16 = 5
    case mask32 = 6
    case blocks = 7
}

@usableFromInline
struct ParsedMessageIndex: Sendable {
    @usableFromInline let encodedCount: Int
    @usableFromInline let payloadEnd: Int
    @usableFromInline let boundariesOffset: Int
    @usableFromInline let directoryOffset: Int
    @usableFromInline let fieldCount: Int
    @usableFromInline let offsetWidth: Int
    @usableFromInline let directoryKind: UInt8
}

@inline(__always)
private func loadLittleEndian(_ bytes: UnsafeRawBufferPointer, at offset: Int, width: Int) -> UInt32 {
    switch width {
    case 1:
        return UInt32(bytes.load(fromByteOffset: offset, as: UInt8.self))
    case 2:
        return UInt32(UInt16(littleEndian: bytes.loadUnaligned(fromByteOffset: offset, as: UInt16.self)))
    default:
        return UInt32(littleEndian: bytes.loadUnaligned(fromByteOffset: offset, as: UInt32.self))
    }
}

@usableFromInline
func parseMessageIndex(_ bytes: UnsafeRawBufferPointer) throws -> ParsedMessageIndex {
    guard bytes.count >= 5 else { throw BebopDecodingError.malformedMessage }
    let bodyLength = Int(loadLittleEndian(bytes, at: 0, width: 4))
    guard bodyLength == bytes.count - 4, bodyLength > 0 else {
        throw BebopDecodingError.malformedMessage
    }

    let control = bytes[bytes.count - 1]
    let widthCode = Int(control & 3)
    let kindValue = (control >> 2) & 7
    guard control & 0xe0 == 0, widthCode != 3,
          let kind = MessageDirectoryKind(rawValue: kindValue)
    else { throw BebopDecodingError.malformedMessage }
    let width = 1 << widthCode

    var directorySize: Int
    var fieldCount = 0
    switch kind {
    case .empty:
        directorySize = 0
    case .tiny1, .tiny2, .tiny3:
        directorySize = Int(kind.rawValue)
        fieldCount = directorySize
    case .mask8:
        directorySize = 1
    case .mask16:
        directorySize = 2
    case .mask32:
        directorySize = 4
    case .blocks:
        guard bodyLength >= 2 else { throw BebopDecodingError.malformedMessage }
        directorySize = 1 + 5 * bytes[bytes.count - 2].nonzeroBitCount
    }
    guard directorySize + 1 <= bodyLength else { throw BebopDecodingError.malformedMessage }

    let directoryOffset = bytes.count - 1 - directorySize
    switch kind {
    case .mask8, .mask16, .mask32:
        fieldCount = loadLittleEndian(bytes, at: directoryOffset, width: directorySize).nonzeroBitCount
    case .blocks:
        let blockMask = bytes[directoryOffset + directorySize - 1]
        guard blockMask != 0 else { throw BebopDecodingError.malformedMessage }
        var entry = directoryOffset
        var rank = 0
        for block in 0..<8 where blockMask & (1 << block) != 0 {
            guard Int(bytes[entry]) == rank else { throw BebopDecodingError.malformedMessage }
            let mask = loadLittleEndian(bytes, at: entry + 1, width: 4)
            guard mask != 0, block != 7 || mask & 0x8000_0000 == 0 else {
                throw BebopDecodingError.malformedMessage
            }
            rank += mask.nonzeroBitCount
            entry += 5
        }
        fieldCount = rank
    default:
        break
    }

    guard (fieldCount == 0) == (kind == .empty) else {
        throw BebopDecodingError.malformedMessage
    }
    if kind == .tiny1 || kind == .tiny2 || kind == .tiny3 {
        guard bytes[directoryOffset] != 0 else { throw BebopDecodingError.malformedMessage }
        for index in 1..<fieldCount where bytes[directoryOffset + index] <= bytes[directoryOffset + index - 1] {
            throw BebopDecodingError.malformedMessage
        }
    }

    let boundaryCount = max(0, fieldCount - 1)
    let boundarySize = boundaryCount * width
    guard boundarySize + directorySize + 1 <= bodyLength else {
        throw BebopDecodingError.malformedMessage
    }
    let boundariesOffset = directoryOffset - boundarySize
    let payloadEnd = boundariesOffset
    var previous: UInt32 = 0
    for rank in 0..<boundaryCount {
        let boundary = loadLittleEndian(bytes, at: boundariesOffset + rank * width, width: width)
        guard boundary >= previous, boundary <= payloadEnd - 4 else {
            throw BebopDecodingError.malformedMessage
        }
        previous = boundary
    }
    guard fieldCount != 0 || payloadEnd == 4 else {
        throw BebopDecodingError.malformedMessage
    }
    return ParsedMessageIndex(
        encodedCount: bytes.count,
        payloadEnd: payloadEnd,
        boundariesOffset: boundariesOffset,
        directoryOffset: directoryOffset,
        fieldCount: fieldCount,
        offsetWidth: width,
        directoryKind: kind.rawValue
    )
}

@inline(__always)
func messageFieldRank(
    tag: UInt8, index: ParsedMessageIndex, bytes: UnsafeRawBufferPointer
) -> Int? {
    guard tag != 0, index.fieldCount != 0,
          let kind = MessageDirectoryKind(rawValue: index.directoryKind)
    else { return nil }

    switch kind {
    case .tiny1, .tiny2, .tiny3:
        for rank in 0..<index.fieldCount {
            let candidate = bytes[index.directoryOffset + rank]
            if candidate == tag { return rank }
            if candidate > tag { return nil }
        }
        return nil
    case .mask8, .mask16, .mask32:
        let maskWidth = 1 << (Int(kind.rawValue) - Int(MessageDirectoryKind.mask8.rawValue))
        guard Int(tag) <= maskWidth * 8 else { return nil }
        let mask = loadLittleEndian(bytes, at: index.directoryOffset, width: maskWidth)
        let bit = UInt32(1) << UInt32(tag - 1)
        guard mask & bit != 0 else { return nil }
        return (mask & (bit - 1)).nonzeroBitCount
    case .blocks:
        let block = Int((tag - 1) >> 5)
        let blockBit = UInt8(1) << UInt8(block)
        let topMask = bytes[index.encodedCount - 2]
        guard topMask & blockBit != 0 else { return nil }
        let preceding = (topMask & (blockBit - 1)).nonzeroBitCount
        let entry = index.directoryOffset + 5 * preceding
        let mask = loadLittleEndian(bytes, at: entry + 1, width: 4)
        let bit = UInt32(1) << UInt32((tag - 1) & 31)
        guard mask & bit != 0 else { return nil }
        return Int(bytes[entry]) + (mask & (bit - 1)).nonzeroBitCount
    case .empty:
        return nil
    }
}

@inline(__always)
func messageFieldRange(
    rank: Int, index: ParsedMessageIndex, bytes: UnsafeRawBufferPointer
) -> Range<Int> {
    let start = rank == 0
        ? 0
        : Int(loadLittleEndian(
            bytes, at: index.boundariesOffset + (rank - 1) * index.offsetWidth,
            width: index.offsetWidth))
    let end = rank + 1 == index.fieldCount
        ? index.payloadEnd - 4
        : Int(loadLittleEndian(
            bytes, at: index.boundariesOffset + rank * index.offsetWidth,
            width: index.offsetWidth))
    return (4 + start)..<(4 + end)
}

/// A validated, immutable, random-access view over an indexed Bebop message.
public struct BebopMessageView: Sendable {
    public let encoded: BebopView
    private let index: ParsedMessageIndex
    private let decodeLimits: BebopDecodeLimits
    private let depth: UInt16

    public init(
        _ encoded: BebopView,
        limits: BebopDecodeLimits = .default
    ) throws {
        try self.init(encoded, limits: limits, depth: 0)
    }

    @usableFromInline
    init(_ encoded: BebopView, limits: BebopDecodeLimits, depth: UInt16) throws {
        self.index = try encoded.withUnsafeBytes(parseMessageIndex)
        self.encoded = encoded
        decodeLimits = limits
        self.depth = depth
    }

    public init(
        _ bytes: [UInt8],
        limits: BebopDecodeLimits = .default
    ) throws {
        try self.init(BebopView(bytes), limits: limits)
    }

    public var limits: BebopDecodeLimits { decodeLimits }

    /// Returns the encoded value for `tag`, or `nil` when that field is absent.
    public func field(_ tag: UInt8) -> BebopView? {
        encoded.withUnsafeBytes { bytes in
            guard let rank = messageFieldRank(tag: tag, index: index, bytes: bytes) else { return nil }
            return encoded.slice(messageFieldRange(rank: rank, index: index, bytes: bytes))
        }
    }

    /// Decodes one known field while preserving the view's limits and ownership.
    public func decodeField<Value>(
        _ tag: UInt8,
        _ decode: (inout BebopViewReader) throws -> Value
    ) throws -> Value? {
        guard let field = field(tag) else { return nil }
        var reader = BebopViewReader(field, limits: decodeLimits, depth: depth)
        let value = try decode(&reader)
        try reader.finish()
        return value
    }
}

/// Indexed-message sizing shared by generated size calculations and the writer.
public enum BebopMessageLayout {
    public static func encodedSize<let N: Int>(
        payloadSize: Int, tags: borrowing InlineArray<N, UInt8>, count: Int
    ) -> Int {
        precondition(payloadSize >= 0 && payloadSize <= Int(UInt32.max))
        precondition(count >= 0 && count <= N)
        let width = payloadSize <= 255 ? 1 : payloadSize <= 65_535 ? 2 : 4
        let directory = directoryLayout(tags: tags.span, count: count)
        return 4 + payloadSize + max(0, count - 1) * width + directory.size + 1
    }

    @usableFromInline
    static func directoryLayout(
        tags: borrowing Span<UInt8>, count: Int
    ) -> (kind: UInt8, size: Int, blockMask: UInt8) {
        if count == 0 { return (MessageDirectoryKind.empty.rawValue, 0, 0) }
        precondition(tags[0] != 0)
        var blockMask: UInt8 = 0
        for index in 0..<count {
            if index > 0 { precondition(tags[index] > tags[index - 1]) }
            blockMask |= 1 << ((tags[index] - 1) >> 5)
        }
        var result = (
            kind: MessageDirectoryKind.blocks.rawValue,
            size: 1 + 5 * blockMask.nonzeroBitCount,
            blockMask: blockMask
        )
        let maxTag = tags[count - 1]
        if maxTag <= 8, result.size >= 1 {
            result.kind = MessageDirectoryKind.mask8.rawValue
            result.size = 1
        } else if maxTag <= 16, result.size >= 2 {
            result.kind = MessageDirectoryKind.mask16.rawValue
            result.size = 2
        } else if maxTag <= 32, result.size >= 4 {
            result.kind = MessageDirectoryKind.mask32.rawValue
            result.size = 4
        }
        if count <= 3, count < result.size {
            result.kind = UInt8(count)
            result.size = count
        }
        return result
    }
}
