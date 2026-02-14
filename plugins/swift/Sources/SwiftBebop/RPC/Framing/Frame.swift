public struct Frame: Sendable {
  public static let headerSize = 9

  public let header: FrameHeader
  public let payload: [UInt8]

  public init(header: FrameHeader, payload: [UInt8]) {
    self.header = header
    self.payload = payload
  }

  public init(payload: [UInt8], flags: FrameFlags, streamId: UInt32 = 0) {
    self.header = FrameHeader(
      length: UInt32(payload.count),
      flags: flags,
      streamId: streamId
    )
    self.payload = payload
  }

  public var isEndStream: Bool { header.flags.contains(.endStream) }
  public var isError: Bool { header.flags.contains(.error) }
  public var isTrailer: Bool { header.flags.contains(.trailer) }

  public static func decode(from bytes: [UInt8]) throws -> Frame {
    guard bytes.count >= headerSize else {
      throw BebopRpcError(code: .invalidArgument, detail: "frame too short: \(bytes.count) bytes")
    }
    let header = try bytes.withUnsafeBufferPointer { buf in
      var reader = BebopReader(data: UnsafeRawBufferPointer(buf))
      return try FrameHeader.decode(from: &reader)
    }
    let payloadStart = headerSize
    let payloadEnd = payloadStart + Int(header.length)
    guard bytes.count >= payloadEnd else {
      throw BebopRpcError(
        code: .invalidArgument,
        detail: "frame truncated: need \(payloadEnd) bytes, got \(bytes.count)"
      )
    }
    let payload = Array(bytes[payloadStart..<payloadEnd])
    return Frame(header: header, payload: payload)
  }

  public func encode() -> [UInt8] {
    var writer = BebopWriter(capacity: Self.headerSize + payload.count)
    header.encode(to: &writer)
    writer.writeBytes(payload)
    return writer.toBytes()
  }
}
