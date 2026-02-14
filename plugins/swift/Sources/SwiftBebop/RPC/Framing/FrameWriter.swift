public enum FrameWriter {
  public static func data(_ payload: [UInt8], streamId: UInt32 = 0) -> [UInt8] {
    Frame(payload: payload, flags: [], streamId: streamId).encode()
  }

  public static func endStream(_ payload: [UInt8], streamId: UInt32 = 0) -> [UInt8] {
    Frame(payload: payload, flags: .endStream, streamId: streamId).encode()
  }

  public static func error(_ error: BebopRpcError, streamId: UInt32 = 0) -> [UInt8] {
    let payload = error.toWire().serializedData()
    return Frame(payload: payload, flags: [.endStream, .error], streamId: streamId).encode()
  }

  public static func trailer(_ metadata: [String: String], streamId: UInt32 = 0) -> [UInt8] {
    let payload = TrailingMetadata(metadata: metadata).serializedData()
    return Frame(payload: payload, flags: [.endStream, .trailer], streamId: streamId).encode()
  }
}
