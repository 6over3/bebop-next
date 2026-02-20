enum MethodRegistration: Sendable {
  case unary(@Sendable ([UInt8], RpcContext) async throws -> [UInt8])
  case serverStream(
    @Sendable ([UInt8], RpcContext) async throws -> AsyncThrowingStream<[UInt8], Error>)
  case clientStream(
    @Sendable (RpcContext) async throws -> (
      send: @Sendable ([UInt8]) async throws -> Void,
      finish: @Sendable () async throws -> [UInt8]
    ))
  case duplexStream(
    @Sendable (RpcContext) async throws -> (
      send: @Sendable ([UInt8]) async throws -> Void,
      finish: @Sendable () async throws -> Void,
      responses: AsyncThrowingStream<[UInt8], Error>
    ))

  var methodType: MethodType {
    switch self {
    case .unary: .unary
    case .serverStream: .serverStream
    case .clientStream: .clientStream
    case .duplexStream: .duplexStream
    }
  }
}
