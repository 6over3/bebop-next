public final class Batch<Channel: BebopChannel> {
  @usableFromInline let channel: Channel
  private var calls: [BatchCall] = []
  private var nextId: Int32 = 0
  private let metadata: [String: String]

  init(channel: Channel, metadata: [String: String] = [:]) {
    self.channel = channel
    self.metadata = metadata
  }

  // MARK: - Building

  @discardableResult
  public func addUnary<Request: BebopRecord, Response: BebopRecord>(
    methodId: UInt32,
    request: Request
  ) -> CallRef<Response> {
    let id = nextId
    nextId += 1
    calls.append(
      BatchCall(
        callId: id,
        methodId: methodId,
        payload: request.serializedData(),
        inputFrom: -1
      ))
    return CallRef(callId: id)
  }

  @discardableResult
  public func addUnary<Response: BebopRecord>(
    methodId: UInt32,
    forwardingFrom callId: Int32
  ) -> CallRef<Response> {
    let id = nextId
    nextId += 1
    calls.append(
      BatchCall(
        callId: id,
        methodId: methodId,
        payload: [],
        inputFrom: callId
      ))
    return CallRef(callId: id)
  }

  @discardableResult
  public func addServerStream<Request: BebopRecord, Response: BebopRecord>(
    methodId: UInt32,
    request: Request
  ) -> StreamRef<Response> {
    let id = nextId
    nextId += 1
    calls.append(
      BatchCall(
        callId: id,
        methodId: methodId,
        payload: request.serializedData(),
        inputFrom: -1
      ))
    return StreamRef(callId: id)
  }

  @discardableResult
  public func addServerStream<Response: BebopRecord>(
    methodId: UInt32,
    forwardingFrom callId: Int32
  ) -> StreamRef<Response> {
    let id = nextId
    nextId += 1
    calls.append(
      BatchCall(
        callId: id,
        methodId: methodId,
        payload: [],
        inputFrom: callId
      ))
    return StreamRef(callId: id)
  }

  // MARK: - Execution

  @usableFromInline static var batchMethodId: UInt32 { 1 }
  public func execute(options: CallOptions = .default) async throws -> BatchResults {
    let request = BatchRequest(calls: calls, metadata: metadata)
    let requestBytes = request.serializedData()
    let responseBytes = try await channel.unary(
      method: Self.batchMethodId,
      request: requestBytes,
      options: options
    )
    let response = try BatchResponse.decode(from: responseBytes)
    return BatchResults(response)
  }
}

extension BebopChannel {
  public func makeBatch(metadata: [String: String] = [:]) -> Batch<Self> {
    Batch(channel: self, metadata: metadata)
  }
}
