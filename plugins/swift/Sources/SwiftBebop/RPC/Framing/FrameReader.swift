public struct FrameReader: Sendable {
    public typealias ReadBytes = @Sendable (Int) async throws -> [UInt8]

    public static let defaultMaxPayloadSize = UInt.max

    private let read: ReadBytes
    private let maxPayloadSize: UInt

    public init(read: @escaping ReadBytes, maxPayloadSize: UInt = Self.defaultMaxPayloadSize) {
        self.read = read
        self.maxPayloadSize = maxPayloadSize
    }

    /// Return nil on clean EOF.
    public func nextFrame() async throws -> Frame? {
        let headerBytes = try await read(Frame.headerSize)
        guard !headerBytes.isEmpty else { return nil }
        guard headerBytes.count == Frame.headerSize else {
            throw BebopRpcError(code: .invalidArgument, detail: "incomplete frame header")
        }
        let header = try headerBytes.withUnsafeBufferPointer { buf in
            var reader = BebopReader(data: UnsafeRawBufferPointer(buf))
            return try FrameHeader.decode(from: &reader)
        }
        let payloadLength = Int(header.length)
        guard UInt(payloadLength) <= maxPayloadSize else {
            throw BebopRpcError(code: .resourceExhausted, detail: "frame payload too large")
        }
        let payload: [UInt8]
        if payloadLength == 0 {
            payload = []
        } else {
            payload = try await read(payloadLength)
            guard payload.count == payloadLength else {
                throw BebopRpcError(code: .invalidArgument, detail: "incomplete frame payload")
            }
        }
        var cursor: UInt64?
        if header.flags.contains(.cursor) {
            let cursorBytes = try await read(8)
            guard cursorBytes.count == 8 else {
                throw BebopRpcError(code: .invalidArgument, detail: "incomplete frame cursor")
            }
            cursor = cursorBytes.withUnsafeBufferPointer {
                UInt64(
                    littleEndian: UnsafeRawBufferPointer($0).loadUnaligned(fromByteOffset: 0, as: UInt64.self)
                )
            }
        }
        return Frame(header: header, payload: payload, cursor: cursor)
    }
}
