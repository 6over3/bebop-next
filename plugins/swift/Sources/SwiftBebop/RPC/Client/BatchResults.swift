/// Decoded results from a batch execution. Access via `CallRef` or `StreamRef` subscripts.
public struct BatchResults: Sendable {
    private let outcomes: [Int32: BatchOutcome]

    init(_ response: BatchResponse) {
        var map = [Int32: BatchOutcome](minimumCapacity: response.results.count)
        for result in response.results {
            map[result.callId] = result.outcome
        }
        outcomes = map
    }

    public subscript<R: BebopRecord>(ref: CallRef<R>) -> R {
        get throws {
            switch try outcome(for: ref.callId) {
            case let .success(success):
                guard let payload = success.payloads.first else {
                    throw BebopRpcError(code: .internal, detail: "batch call \(ref.callId): empty payloads")
                }
                return try R.decode(from: payload)
            case let .error(rpcError):
                throw BebopRpcError(from: rpcError)
            case .unknown:
                throw BebopRpcError(code: .internal, detail: "batch call \(ref.callId): unknown outcome")
            }
        }
    }

    public subscript<R: BebopRecord>(ref: StreamRef<R>) -> [R] {
        get throws {
            switch try outcome(for: ref.callId) {
            case let .success(success):
                return try success.payloads.map { try R.decode(from: $0) }
            case let .error(rpcError):
                throw BebopRpcError(from: rpcError)
            case .unknown:
                throw BebopRpcError(code: .internal, detail: "batch call \(ref.callId): unknown outcome")
            }
        }
    }

    public func metadata(for ref: CallRef<some BebopRecord>) throws -> [String: String] {
        switch try outcome(for: ref.callId) {
        case let .success(success):
            return success.metadata
        case let .error(rpcError):
            throw BebopRpcError(from: rpcError)
        case .unknown:
            throw BebopRpcError(code: .internal, detail: "batch call \(ref.callId): unknown outcome")
        }
    }

    public func metadata(for ref: StreamRef<some BebopRecord>) throws -> [String: String] {
        switch try outcome(for: ref.callId) {
        case let .success(success):
            return success.metadata
        case let .error(rpcError):
            throw BebopRpcError(from: rpcError)
        case .unknown:
            throw BebopRpcError(code: .internal, detail: "batch call \(ref.callId): unknown outcome")
        }
    }

    private func outcome(for callId: Int32) throws -> BatchOutcome {
        guard let outcome = outcomes[callId] else {
            throw BebopRpcError(code: .internal, detail: "batch call \(callId): not found in response")
        }
        return outcome
    }
}
