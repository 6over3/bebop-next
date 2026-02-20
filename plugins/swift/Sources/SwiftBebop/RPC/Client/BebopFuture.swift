public struct BebopFuture<Value: Sendable>: Sendable {
  public let id: BebopUUID
  let resolver: FutureResolver
  let decode: @Sendable ([UInt8]) throws -> Value

  public var value: Value {
    get async throws {
      try await response.value
    }
  }

  /// Includes response metadata set by the handler.
  public var response: Response<Value, [String: String]> {
    get async throws {
      let outcome = try await resolver.await(id: id)
      switch outcome {
      case .success(let success):
        return Response(
          value: try decode(success.payload),
          metadata: success.metadata)
      case .error(let rpcError):
        throw BebopRpcError(from: rpcError)
      case .unknown:
        throw BebopRpcError(code: .internal, detail: "unknown future outcome")
      }
    }
  }

  /// Await with a client-side timeout. On timeout the server-side future
  /// keeps running; rehydrate with `FutureDispatcher.future(id:as:)` later.
  public func value(timeout: Duration) async throws -> Value {
    try await withThrowingTaskGroup(of: Value.self) { group in
      group.addTask { try await self.value }
      group.addTask {
        try await Task.sleep(for: timeout)
        throw BebopRpcError(code: .deadlineExceeded, detail: "await timed out")
      }
      guard let result = try await group.next() else {
        throw BebopRpcError(code: .deadlineExceeded, detail: "await timed out")
      }
      group.cancelAll()
      return result
    }
  }

  public func cancel() async throws {
    try await resolver.cancel(id: id)
  }
}
