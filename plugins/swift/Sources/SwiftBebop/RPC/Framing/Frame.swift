public struct Frame: Sendable {
  public static let headerSize = 9

  public let header: FrameHeader
  public let payload: [UInt8]
  public let cursor: UInt64?

  public init(header: FrameHeader, payload: [UInt8], cursor: UInt64? = nil) {
    assert(header.flags.contains(.cursor) == (cursor != nil))
    self.header = header
    self.payload = payload
    self.cursor = cursor
  }

  public init(payload: [UInt8], flags: FrameFlags, streamId: UInt32 = 0, cursor: UInt64? = nil) {
    var resolvedFlags = flags
    if cursor != nil { resolvedFlags.insert(.cursor) }
    self.header = FrameHeader(
      length: UInt32(payload.count),
      flags: resolvedFlags,
      streamId: streamId
    )
    self.payload = payload
    self.cursor = cursor
  }

  public var isEndStream: Bool { header.flags.contains(.endStream) }
  public var isError: Bool { header.flags.contains(.error) }
  public var isTrailer: Bool { header.flags.contains(.trailer) }
  public var isCursor: Bool { header.flags.contains(.cursor) }

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
    let totalNeeded = header.flags.contains(.cursor) ? payloadEnd + 8 : payloadEnd
    guard bytes.count >= totalNeeded else {
      throw BebopRpcError(
        code: .invalidArgument,
        detail: "frame truncated: need \(totalNeeded) bytes, got \(bytes.count)"
      )
    }
    let payload = Array(bytes[payloadStart..<payloadEnd])
    var cursor: UInt64?
    if header.flags.contains(.cursor) {
      cursor = bytes.withUnsafeBufferPointer {
        UInt64(
          littleEndian: UnsafeRawBufferPointer($0).loadUnaligned(
            fromByteOffset: payloadEnd, as: UInt64.self))
      }
    }
    return Frame(header: header, payload: payload, cursor: cursor)
  }

  public func encode() -> [UInt8] {
    let cursorSize = cursor != nil ? 8 : 0
    var writer = BebopWriter(capacity: Self.headerSize + payload.count + cursorSize)
    header.encode(to: &writer)
    writer.writeBytes(payload)
    if let cursor {
      writer.writeUInt64(cursor)
    }
    return writer.toBytes()
  }
}
