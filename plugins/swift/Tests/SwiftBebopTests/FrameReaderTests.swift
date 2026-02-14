import Testing

@testable import SwiftBebop

private actor ByteStream {
  private let data: [UInt8]
  private var offset = 0

  init(_ data: [UInt8]) { self.data = data }

  func read(_ count: Int) -> [UInt8] {
    guard offset < data.count else { return [] }
    let end = min(offset + count, data.count)
    let slice = Array(data[offset..<end])
    offset = end
    return slice
  }
}

private actor ReadCounter {
  var value = 0
  func next() -> Int {
    value += 1
    return value
  }
}

@Suite struct FrameReaderTests {
  @Test func readsSingleFrame() async throws {
    let stream = ByteStream(FrameWriter.endStream([1, 2, 3]))
    let reader = FrameReader { count in await stream.read(count) }

    let frame = try await reader.nextFrame()
    #expect(frame != nil)
    #expect(frame!.payload == [1, 2, 3])
    #expect(frame!.isEndStream)
  }

  @Test func returnsNilOnEOF() async throws {
    let reader = FrameReader { _ in [] }
    let frame = try await reader.nextFrame()
    #expect(frame == nil)
  }

  @Test func throwsOnIncompleteHeader() async throws {
    let reader = FrameReader { count in
      count == Frame.headerSize ? [0, 1, 2] : []
    }
    await #expect(throws: BebopRpcError.self) {
      _ = try await reader.nextFrame()
    }
  }

  @Test func throwsOnIncompletePayload() async throws {
    let frameBytes = FrameWriter.data([1, 2, 3, 4])
    let counter = ReadCounter()
    let reader = FrameReader { _ in
      let n = await counter.next()
      if n == 1 { return Array(frameBytes[0..<Frame.headerSize]) }
      return [1, 2]
    }
    await #expect(throws: BebopRpcError.self) {
      _ = try await reader.nextFrame()
    }
  }

  @Test func readsMultipleFrames() async throws {
    let frame1 = FrameWriter.data([10, 20])
    let frame2 = FrameWriter.endStream([30, 40])
    let stream = ByteStream(frame1 + frame2)
    let reader = FrameReader { count in await stream.read(count) }

    let f1 = try await reader.nextFrame()
    #expect(f1 != nil)
    #expect(f1!.payload == [10, 20])
    #expect(!f1!.isEndStream)

    let f2 = try await reader.nextFrame()
    #expect(f2 != nil)
    #expect(f2!.payload == [30, 40])
    #expect(f2!.isEndStream)
  }

  @Test func zeroLengthPayload() async throws {
    let stream = ByteStream(FrameWriter.endStream([]))
    let reader = FrameReader { count in await stream.read(count) }
    let frame = try await reader.nextFrame()
    #expect(frame != nil)
    #expect(frame!.payload.isEmpty)
  }
}
