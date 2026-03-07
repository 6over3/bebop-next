/// Transport-layer abstraction for RPC clients.
public protocol BebopChannel: Sendable {
    associatedtype Metadata: Sendable

    func unary(
        method: UInt32,
        request: [UInt8],
        context: RpcContext
    ) async throws -> Response<[UInt8], Metadata>

    func serverStream(
        method: UInt32,
        request: [UInt8],
        context: RpcContext
    ) async throws -> StreamResponse<[UInt8], Metadata>

    func clientStream(
        method: UInt32,
        context: RpcContext
    ) async throws -> (
        send: @Sendable ([UInt8]) async throws -> Void,
        finish: @Sendable () async throws -> Response<[UInt8], Metadata>
    )

    func duplexStream(
        method: UInt32,
        context: RpcContext
    ) async throws -> (
        send: @Sendable ([UInt8]) async throws -> Void,
        finish: @Sendable () async throws -> Void,
        responses: StreamResponse<[UInt8], Metadata>
    )
}
