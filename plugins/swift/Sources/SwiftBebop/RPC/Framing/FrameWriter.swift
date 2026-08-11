public struct FrameWriter: Sendable {
    public typealias WriteBytes = @Sendable ([UInt8], FrameFlags) async throws -> Void

    private let write: WriteBytes
    private let maxPayloadSize: Int

    public init(maxPayloadSize: Int = Int(FrameReader.defaultMaxPayloadSize), write: @escaping WriteBytes) {
        precondition(maxPayloadSize >= 0, "maxPayloadSize must be nonnegative")
        self.maxPayloadSize = maxPayloadSize
        self.write = write
    }

    // MARK: - Frame-level writes

    public func data(_ payload: [UInt8], streamId: UInt32 = 0, cursor: UInt64? = nil) async throws {
        try validatePayload(payload)
        let frame = Frame(payload: payload, flags: [], streamId: streamId, cursor: cursor)
        try await write(frame.encode(), frame.header.flags)
    }

    public func endStream(_ payload: [UInt8], streamId: UInt32 = 0) async throws {
        try validatePayload(payload)
        let flags: FrameFlags = .endStream
        try await write(Frame(payload: payload, flags: flags, streamId: streamId).encode(), flags)
    }

    public func error(_ error: BebopRpcError, streamId: UInt32 = 0) async throws {
        let flags: FrameFlags = [.endStream, .error]
        let payload = error.toWire().serializedData()
        try validatePayload(payload)
        try await write(Frame(payload: payload, flags: flags, streamId: streamId).encode(), flags)
    }

    public func trailer(_ metadata: [String: String], streamId: UInt32 = 0) async throws {
        let flags: FrameFlags = [.endStream, .trailer]
        let payload = TrailingMetadata(metadata: metadata).serializedData()
        try validatePayload(payload)
        try await write(Frame(payload: payload, flags: flags, streamId: streamId).encode(), flags)
    }

    private func validatePayload(_ payload: [UInt8]) throws {
        guard payload.count <= maxPayloadSize else {
            throw BebopRpcError(code: .resourceExhausted, detail: "frame payload too large")
        }
    }
}

// MARK: - Response-level helpers

public extension FrameWriter {
    func writeUnary(
        _ payload: [UInt8], metadata: [String: String] = [:], streamId: UInt32 = 0
    ) async throws {
        if metadata.isEmpty {
            try await endStream(payload, streamId: streamId)
        } else {
            try await data(payload, streamId: streamId)
            try await trailer(metadata, streamId: streamId)
        }
    }

    /// Send ERROR frame if the stream throws; otherwise send TRAILER or END_STREAM.
    func drainServerStream(
        _ stream: AsyncThrowingStream<StreamElement, Error>,
        metadata: @Sendable () -> [String: String] = { [:] },
        streamId: UInt32 = 0
    ) async throws {
        do {
            for try await element in stream {
                try await data(element.bytes, streamId: streamId, cursor: element.cursor)
            }
        } catch {
            try await self.error(
                (error as? BebopRpcError) ?? BebopRpcError(code: .internal, detail: "\(error)"),
                streamId: streamId
            )
            return
        }
        let trailing = metadata()
        if trailing.isEmpty {
            try await endStream([], streamId: streamId)
        } else {
            try await trailer(trailing, streamId: streamId)
        }
    }
}
