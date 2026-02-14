import Testing

@testable import SwiftBebop

@Suite struct FrameWriterTests {
  @Test func dataFrame() throws {
    let bytes = FrameWriter.data([0xAA, 0xBB], streamId: 3)
    let frame = try Frame.decode(from: bytes)
    #expect(frame.payload == [0xAA, 0xBB])
    #expect(frame.header.streamId == 3)
    #expect(!frame.isEndStream)
    #expect(!frame.isError)
  }

  @Test func endStreamFrame() throws {
    let bytes = FrameWriter.endStream([0xCC], streamId: 1)
    let frame = try Frame.decode(from: bytes)
    #expect(frame.payload == [0xCC])
    #expect(frame.isEndStream)
    #expect(!frame.isError)
  }

  @Test func errorFrame() throws {
    let err = BebopRpcError(code: .notFound, detail: "gone")
    let bytes = FrameWriter.error(err, streamId: 2)
    let frame = try Frame.decode(from: bytes)
    #expect(frame.isEndStream)
    #expect(frame.isError)

    let wire = try RpcError.decode(from: frame.payload)
    #expect(wire.code == .notFound)
    #expect(wire.detail == "gone")
  }

  @Test func trailerFrame() throws {
    let bytes = FrameWriter.trailer(["x-id": "123"], streamId: 4)
    let frame = try Frame.decode(from: bytes)
    #expect(frame.isEndStream)
    #expect(frame.isTrailer)

    let meta = try TrailingMetadata.decode(from: frame.payload)
    #expect(meta.metadata?["x-id"] == "123")
  }
}
