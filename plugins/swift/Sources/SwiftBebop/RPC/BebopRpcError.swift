/// RPC error bridging to/from the wire-format `RpcError` message.
public struct BebopRpcError: Error, Sendable, CustomStringConvertible {
    public let code: StatusCode
    public let detail: String?
    public let metadata: [String: String]?

    public init(
        code: StatusCode,
        detail: String? = nil,
        metadata: [String: String]? = nil
    ) {
        self.code = code
        self.detail = detail
        self.metadata = metadata
    }

    public init(from wire: RpcError) {
        code = wire.code ?? .unknown
        detail = wire.detail
        metadata = wire.metadata
    }

    public func toWire() -> RpcError {
        RpcError(code: code, detail: detail, metadata: metadata)
    }

    // MARK: - CustomStringConvertible

    public var description: String {
        let name = code.name ?? String(code.rawValue)
        if let detail {
            return "BebopRpcError(\(name)): \(detail)"
        }
        return "BebopRpcError(\(name))"
    }
}
