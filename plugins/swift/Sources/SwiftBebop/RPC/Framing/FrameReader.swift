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
      throw BebopRpcError(
        code: .resourceExhausted,
        detail: "frame payload \(payloadLength) exceeds limit \(maxPayloadSize)")
    }
    if payloadLength == 0 {
      return Frame(header: header, payload: [])
    }
    let payload = try await read(payloadLength)
    guard payload.count == payloadLength else {
      throw BebopRpcError(code: .invalidArgument, detail: "incomplete frame payload")
    }
    return Frame(header: header, payload: payload)
  }
}
