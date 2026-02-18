/// Transport-layer abstraction for RPC clients.
public protocol BebopChannel: Sendable {
  associatedtype Metadata: Sendable

  func unary(
    method: UInt32,
    request: [UInt8],
    options: CallOptions
  ) async throws -> Reply<[UInt8], Metadata>

  func serverStream(
    method: UInt32,
    request: [UInt8],
    options: CallOptions
  ) async throws -> StreamReply<[UInt8], Metadata>

  func clientStream(
    method: UInt32,
    options: CallOptions
  ) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Reply<[UInt8], Metadata>
  )

  func duplexStream(
    method: UInt32,
    options: CallOptions
  ) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Void,
    responses: StreamReply<[UInt8], Metadata>
  )
}
