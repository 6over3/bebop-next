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
    ) async throws -> ClientStream<[UInt8], [UInt8], Metadata>

    func duplexStream(
        method: UInt32,
        context: RpcContext
    ) async throws -> DuplexStream<[UInt8], [UInt8], Metadata>
}
