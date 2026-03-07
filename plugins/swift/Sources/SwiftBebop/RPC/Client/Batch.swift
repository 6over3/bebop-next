import Synchronization

public final class Batch<Channel: BebopChannel>: Sendable {
    @usableFromInline let channel: Channel
    private let metadata: [String: String]

    private struct State: Sendable {
        var calls: [BatchCall] = []
        var nextId: Int32 = 0
        var executed = false
    }

    private let _state = Mutex(State())

    init(channel: Channel, metadata: [String: String] = [:]) {
        self.channel = channel
        self.metadata = metadata
    }

    // MARK: - Building

    @discardableResult
    public func addUnary<Response: BebopRecord>(
        methodId: UInt32,
        request: some BebopRecord
    ) -> CallRef<Response> {
        _state.withLock { state in
            precondition(!state.executed, "Batch already executed")
            let id = state.nextId
            state.nextId += 1
            state.calls.append(
                BatchCall(
                    callId: id,
                    methodId: methodId,
                    payload: request.serializedData(),
                    inputFrom: -1
                ))
            return CallRef(callId: id)
        }
    }

    @discardableResult
    public func addUnary<Response: BebopRecord>(
        methodId: UInt32,
        forwardingFrom callId: Int32
    ) -> CallRef<Response> {
        _state.withLock { state in
            precondition(!state.executed, "Batch already executed")
            let id = state.nextId
            state.nextId += 1
            state.calls.append(
                BatchCall(
                    callId: id,
                    methodId: methodId,
                    payload: [],
                    inputFrom: callId
                ))
            return CallRef(callId: id)
        }
    }

    @discardableResult
    public func addServerStream<Response: BebopRecord>(
        methodId: UInt32,
        request: some BebopRecord
    ) -> StreamRef<Response> {
        _state.withLock { state in
            precondition(!state.executed, "Batch already executed")
            let id = state.nextId
            state.nextId += 1
            state.calls.append(
                BatchCall(
                    callId: id,
                    methodId: methodId,
                    payload: request.serializedData(),
                    inputFrom: -1
                ))
            return StreamRef(callId: id)
        }
    }

    @discardableResult
    public func addServerStream<Response: BebopRecord>(
        methodId: UInt32,
        forwardingFrom callId: Int32
    ) -> StreamRef<Response> {
        _state.withLock { state in
            precondition(!state.executed, "Batch already executed")
            let id = state.nextId
            state.nextId += 1
            state.calls.append(
                BatchCall(
                    callId: id,
                    methodId: methodId,
                    payload: [],
                    inputFrom: callId
                ))
            return StreamRef(callId: id)
        }
    }

    // MARK: - Execution

    public func execute(context: RpcContext = RpcContext()) async throws -> BatchResults {
        let request = _state.withLock { state -> BatchRequest in
            precondition(!state.executed, "Batch already executed")
            state.executed = true
            return BatchRequest(calls: state.calls, metadata: metadata)
        }
        let result = try await channel.unary(
            method: BebopReservedMethod.batch,
            request: request.serializedData(),
            context: context
        )
        let response = try BatchResponse.decode(from: result.value)
        return BatchResults(response)
    }

    /// Dispatch the entire batch as a future. The server runs the batch
    /// in the background; await the returned future for results.
    public func dispatch(
        using dispatcher: FutureDispatcher<Channel>,
        options: DispatchOptions = .init(),
        context: RpcContext = RpcContext()
    ) async throws -> BebopFuture<BatchResults> {
        let request = _state.withLock { state -> BatchRequest in
            precondition(!state.executed, "Batch already executed")
            state.executed = true
            return BatchRequest(calls: state.calls, metadata: metadata)
        }

        let dispatchReq = FutureDispatchRequest(
            methodId: BebopReservedMethod.batch,
            payload: request.serializedData(),
            idempotencyKey: options.idempotencyKey,
            metadata: context.metadata,
            deadline: context.deadline,
            discardResult: options.discardResult
        )

        let dispatchCtx = RpcContext(metadata: context.metadata)

        let response = try await dispatcher.channel.unary(
            method: BebopReservedMethod.dispatch,
            request: dispatchReq.serializedData(),
            context: dispatchCtx
        )

        let handle = try FutureHandle.decode(from: response.value)

        return BebopFuture(
            id: handle.id,
            resolver: dispatcher.resolver,
            decode: { bytes in
                let batchResponse = try BatchResponse.decode(from: bytes)
                return BatchResults(batchResponse)
            }
        )
    }
}

public extension BebopChannel {
    func makeBatch(metadata: [String: String] = [:]) -> Batch<Self> {
        Batch(channel: self, metadata: metadata)
    }
}
