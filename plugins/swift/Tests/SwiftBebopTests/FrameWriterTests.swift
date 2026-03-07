import Testing

@testable import SwiftBebop

private actor FrameLog {
    var frames: [Frame] = []

    func append(_ raw: [UInt8]) throws {
        try frames.append(Frame.decode(from: raw))
    }

    func all() -> [Frame] { frames }
}

private func collectingWriter() -> (FrameWriter, FrameLog) {
    let log = FrameLog()
    let writer = FrameWriter { bytes, _ in try await log.append(bytes) }
    return (writer, log)
}

@Suite struct FrameWriterTests {
    @Test func dataFrame() async throws {
        let (writer, log) = collectingWriter()
        try await writer.data([0xAA, 0xBB], streamId: 3)

        let frames = await log.all()
        #expect(frames.count == 1)
        #expect(frames[0].payload == [0xAA, 0xBB])
        #expect(frames[0].header.streamId == 3)
        #expect(!frames[0].isEndStream)
        #expect(!frames[0].isError)
    }

    @Test func endStreamFrame() async throws {
        let (writer, log) = collectingWriter()
        try await writer.endStream([0xCC], streamId: 1)

        let frames = await log.all()
        #expect(frames.count == 1)
        #expect(frames[0].payload == [0xCC])
        #expect(frames[0].isEndStream)
        #expect(!frames[0].isError)
    }

    @Test func errorFrame() async throws {
        let (writer, log) = collectingWriter()
        let err = BebopRpcError(code: .notFound, detail: "gone")
        try await writer.error(err, streamId: 2)

        let frames = await log.all()
        #expect(frames.count == 1)
        #expect(frames[0].isEndStream)
        #expect(frames[0].isError)

        let wire = try RpcError.decode(from: frames[0].payload)
        #expect(wire.code == .notFound)
        #expect(wire.detail == "gone")
    }

    @Test func trailerFrame() async throws {
        let (writer, log) = collectingWriter()
        try await writer.trailer(["x-id": "123"], streamId: 4)

        let frames = await log.all()
        #expect(frames.count == 1)
        #expect(frames[0].isEndStream)
        #expect(frames[0].isTrailer)

        let meta = try TrailingMetadata.decode(from: frames[0].payload)
        #expect(meta.metadata?["x-id"] == "123")
    }

    // MARK: - writeUnary

    @Test func unaryWithoutMetadata() async throws {
        let (writer, log) = collectingWriter()
        try await writer.writeUnary([1, 2, 3], streamId: 5)

        let frames = await log.all()
        #expect(frames.count == 1)
        #expect(frames[0].payload == [1, 2, 3])
        #expect(frames[0].isEndStream)
        #expect(!frames[0].isTrailer)
    }

    @Test func unaryWithMetadata() async throws {
        let (writer, log) = collectingWriter()
        try await writer.writeUnary([1], metadata: ["k": "v"], streamId: 6)

        let frames = await log.all()
        #expect(frames.count == 2)
        #expect(frames[0].payload == [1])
        #expect(!frames[0].isEndStream)
        #expect(frames[1].isEndStream)
        #expect(frames[1].isTrailer)

        let meta = try TrailingMetadata.decode(from: frames[1].payload)
        #expect(meta.metadata?["k"] == "v")
    }

    // MARK: - drainServerStream

    @Test func drainServerStream() async throws {
        let (writer, log) = collectingWriter()
        let stream = AsyncThrowingStream<StreamElement, Error> { c in
            c.yield(StreamElement(bytes: [10]))
            c.yield(StreamElement(bytes: [20]))
            c.finish()
        }
        try await writer.drainServerStream(stream, streamId: 7)

        let frames = await log.all()
        #expect(frames.count == 3)
        #expect(frames[0].payload == [10])
        #expect(!frames[0].isEndStream)
        #expect(frames[1].payload == [20])
        #expect(!frames[1].isEndStream)
        #expect(frames[2].isEndStream)
        #expect(frames[2].payload.isEmpty)
    }

    @Test func drainServerStreamWithMetadata() async throws {
        let (writer, log) = collectingWriter()
        let stream = AsyncThrowingStream<StreamElement, Error> { c in
            c.yield(StreamElement(bytes: [10]))
            c.finish()
        }
        try await writer.drainServerStream(stream, metadata: { ["trail": "yes"] }, streamId: 8)

        let frames = await log.all()
        #expect(frames.count == 2)
        #expect(frames[0].payload == [10])
        #expect(frames[1].isEndStream)
        #expect(frames[1].isTrailer)

        let meta = try TrailingMetadata.decode(from: frames[1].payload)
        #expect(meta.metadata?["trail"] == "yes")
    }

    @Test func drainServerStreamError() async throws {
        let (writer, log) = collectingWriter()
        let stream = AsyncThrowingStream<StreamElement, Error> { c in
            c.yield(StreamElement(bytes: [10]))
            c.finish(throwing: BebopRpcError(code: .cancelled, detail: "abort"))
        }
        try await writer.drainServerStream(stream, streamId: 9)

        let frames = await log.all()
        #expect(frames.count == 2)
        #expect(frames[0].payload == [10])
        #expect(frames[1].isError)
        #expect(frames[1].isEndStream)

        let wire = try RpcError.decode(from: frames[1].payload)
        #expect(wire.code == .cancelled)
    }

    @Test func drainEmptyStream() async throws {
        let (writer, log) = collectingWriter()
        let stream = AsyncThrowingStream<StreamElement, Error> { $0.finish() }
        try await writer.drainServerStream(stream, streamId: 10)

        let frames = await log.all()
        #expect(frames.count == 1)
        #expect(frames[0].isEndStream)
        #expect(frames[0].payload.isEmpty)
    }

    @Test func drainServerStreamWithCursor() async throws {
        let (writer, log) = collectingWriter()
        let stream = AsyncThrowingStream<StreamElement, Error> { c in
            c.yield(StreamElement(bytes: [10], cursor: 42))
            c.yield(StreamElement(bytes: [20], cursor: 99))
            c.finish()
        }
        try await writer.drainServerStream(stream, streamId: 11)

        let frames = await log.all()
        #expect(frames.count == 3)
        #expect(frames[0].payload == [10])
        #expect(frames[0].cursor == 42)
        #expect(frames[0].isCursor)
        #expect(frames[1].payload == [20])
        #expect(frames[1].cursor == 99)
        #expect(frames[1].isCursor)
        #expect(frames[2].isEndStream)
    }

    @Test func drainServerStreamMixedCursors() async throws {
        let (writer, log) = collectingWriter()
        let stream = AsyncThrowingStream<StreamElement, Error> { c in
            c.yield(StreamElement(bytes: [10]))
            c.yield(StreamElement(bytes: [20], cursor: 7))
            c.finish()
        }
        try await writer.drainServerStream(stream, streamId: 12)

        let frames = await log.all()
        #expect(frames.count == 3)
        #expect(frames[0].cursor == nil)
        #expect(!frames[0].isCursor)
        #expect(frames[1].cursor == 7)
        #expect(frames[1].isCursor)
    }

    @Test func backpressure() async throws {
        let events = EventLog()
        let gate = Gate()

        let writer = FrameWriter { _, _ in
            await events.record("write-start")
            await gate.wait()
            await events.record("write-end")
        }

        let stream = AsyncThrowingStream<StreamElement, Error> { c in
            c.yield(StreamElement(bytes: [1]))
            c.yield(StreamElement(bytes: [2]))
            c.finish()
        }

        let task = Task {
            try await writer.drainServerStream(stream)
        }

        // Wait for first write to block
        await events.waitFor("write-start", count: 1)
        // Second element hasn't been pulled yet because write is suspended
        let snapshot = await events.all()
        #expect(snapshot == ["write-start"])

        // Unblock — first write completes, second begins
        await gate.open()
        try await task.value

        let final = await events.all()
        // write-start, write-end pairs for 2 data frames + 1 endStream
        #expect(final.count == 6)
    }
}

// MARK: - Test helpers

private actor EventLog {
    private var entries: [String] = []
    private var waiters: [(String, Int, CheckedContinuation<Void, Never>)] = []

    func record(_ event: String) {
        entries.append(event)
        waiters.removeAll { target, count, continuation in
            if entries.filter({ $0 == target }).count >= count {
                continuation.resume()
                return true
            }
            return false
        }
    }

    func all() -> [String] { entries }

    func waitFor(_ event: String, count: Int) async {
        if entries.filter({ $0 == event }).count >= count { return }
        await withCheckedContinuation { c in
            waiters.append((event, count, c))
        }
    }
}

private actor Gate {
    private var opened = false
    private var waiters: [CheckedContinuation<Void, Never>] = []

    func open() {
        opened = true
        for w in waiters {
            w.resume()
        }
        waiters.removeAll()
    }

    func wait() async {
        if opened { return }
        await withCheckedContinuation { c in
            waiters.append(c)
        }
    }
}
