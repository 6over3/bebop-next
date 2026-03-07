import Testing

@testable import SwiftBebop

@Suite struct FrameTests {
    @Test func encodeDecodeRoundTrip() throws {
        let payload: [UInt8] = [0xDE, 0xAD, 0xBE, 0xEF]
        let frame = Frame(payload: payload, flags: .endStream, streamId: 5)
        let bytes = frame.encode()
        #expect(bytes.count == Frame.headerSize + payload.count)

        let decoded = try Frame.decode(from: bytes)
        #expect(decoded.payload == payload)
        #expect(decoded.header.length == 4)
        #expect(decoded.header.streamId == 5)
        #expect(decoded.isEndStream)
        #expect(!decoded.isError)
        #expect(!decoded.isTrailer)
    }

    @Test func emptyPayload() throws {
        let frame = Frame(payload: [], flags: [], streamId: 0)
        let bytes = frame.encode()
        #expect(bytes.count == Frame.headerSize)

        let decoded = try Frame.decode(from: bytes)
        #expect(decoded.payload.isEmpty)
        #expect(decoded.header.length == 0)
    }

    @Test func errorFlag() throws {
        let frame = Frame(payload: [1], flags: [.endStream, .error], streamId: 0)
        let decoded = try Frame.decode(from: frame.encode())
        #expect(decoded.isEndStream)
        #expect(decoded.isError)
    }

    @Test func trailerFlag() throws {
        let frame = Frame(payload: [1], flags: [.endStream, .trailer], streamId: 0)
        let decoded = try Frame.decode(from: frame.encode())
        #expect(decoded.isTrailer)
        #expect(decoded.isEndStream)
    }

    @Test func decodeTooShortThrows() throws {
        #expect(throws: BebopRpcError.self) {
            _ = try Frame.decode(from: [0, 1, 2])
        }
    }

    @Test func decodeTruncatedPayloadThrows() throws {
        let frame = Frame(payload: [1, 2, 3, 4], flags: [], streamId: 0)
        let bytes = Array(frame.encode().dropLast(2))
        #expect(throws: BebopRpcError.self) {
            _ = try Frame.decode(from: bytes)
        }
    }
}
