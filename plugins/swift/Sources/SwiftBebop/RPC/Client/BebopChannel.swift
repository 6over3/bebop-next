/// Transport-layer abstraction for RPC clients.
public protocol BebopChannel: Sendable {
  func unary(
    method: UInt32,
    request: [UInt8],
    options: CallOptions
  ) async throws -> [UInt8]

  func serverStream(
    method: UInt32,
    request: [UInt8],
    options: CallOptions
  ) async throws -> AsyncThrowingStream<[UInt8], Error>

  func clientStream(
    method: UInt32,
    options: CallOptions
  ) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> [UInt8]
  )

  func duplexStream(
    method: UInt32,
    options: CallOptions
  ) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Void,
    responses: AsyncThrowingStream<[UInt8], Error>
  )
}
