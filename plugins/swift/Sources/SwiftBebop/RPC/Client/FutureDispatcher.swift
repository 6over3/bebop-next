public struct DispatchOptions: Sendable {
    public var idempotencyKey: BebopUUID?
    public var discardResult: Bool?

    public init(
        idempotencyKey: BebopUUID? = nil,
        discardResult: Bool? = nil
    ) {
        self.idempotencyKey = idempotencyKey
        self.discardResult = discardResult
    }
}

/// Entry point for dispatching futures. One resolver per channel —
/// use `makeFutureResolver()` to share across dispatchers.
public struct FutureDispatcher<Channel: BebopChannel>: Sendable {
    public let channel: Channel
    public let resolver: FutureResolver

    /// Create with an existing resolver. Use when sharing a resolver
    /// across dispatchers or surviving reconnects.
    public init(channel: Channel, resolver: FutureResolver) {
        self.channel = channel
        self.resolver = resolver
    }

    public init(channel: Channel, maxCompletedResults: Int = 10000) {
        self.init(
            channel: channel,
            resolver: channel.makeFutureResolver(maxCompletedResults: maxCompletedResults)
        )
    }

    public func dispatch<Res: BebopRecord>(
        methodId: UInt32,
        request: some BebopRecord,
        options: DispatchOptions = .init(),
        context: RpcContext = RpcContext()
    ) async throws -> BebopFuture<Res> {
        let dispatchReq = FutureDispatchRequest(
            methodId: methodId,
            payload: request.serializedData(),
            idempotencyKey: options.idempotencyKey,
            metadata: context.metadata,
            deadline: context.deadline,
            discardResult: options.discardResult
        )

        // The deadline applies to the future's execution, not the dispatch call.
        let dispatchCtx = RpcContext(metadata: context.metadata)

        let response = try await channel.unary(
            method: BebopReservedMethod.dispatch,
            request: dispatchReq.serializedData(),
            context: dispatchCtx
        )

        let handle = try FutureHandle.decode(from: response.value)

        return BebopFuture(
            id: handle.id,
            resolver: resolver,
            decode: { try Res.decode(from: $0) }
        )
    }

    /// Rehydrate a future from a saved ID after reconnection or app restart.
    public func future<T: BebopRecord>(id: BebopUUID, as _: T.Type) -> BebopFuture<T> {
        BebopFuture(
            id: id,
            resolver: resolver,
            decode: { try T.decode(from: $0) }
        )
    }
}

public extension BebopChannel {
    /// Create a resolver bound to this channel. Share across dispatchers
    /// to use a single resolve stream.
    func makeFutureResolver(maxCompletedResults: Int = 10000) -> FutureResolver {
        FutureResolver(
            maxCompletedResults: maxCompletedResults,
            sendCancel: { [self] payload in
                _ = try await unary(
                    method: BebopReservedMethod.cancel,
                    request: payload,
                    context: RpcContext()
                )
            },
            openResolveStream: { [self] payload in
                let response = try await serverStream(
                    method: BebopReservedMethod.resolve,
                    request: payload,
                    context: RpcContext()
                )
                return AsyncThrowingStream { continuation in
                    let task = Task {
                        do {
                            for try await element in response {
                                continuation.yield(element)
                            }
                            continuation.finish()
                        } catch {
                            continuation.finish(throwing: error)
                        }
                    }
                    continuation.onTermination = { _ in task.cancel() }
                }
            }
        )
    }

    /// Convenience that creates a new resolver. Call `makeFutureResolver()`
    /// and pass it to `FutureDispatcher.init(channel:resolver:)` if you need
    /// to share a resolver across multiple dispatchers.
    func makeFutureDispatcher(maxCompletedResults: Int = 10000) -> FutureDispatcher<Self> {
        FutureDispatcher(channel: self, maxCompletedResults: maxCompletedResults)
    }
}
