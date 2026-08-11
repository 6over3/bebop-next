import Synchronization

public protocol FutureStorage: Sendable {
    func register(
        ctx: RpcContext, idempotencyKey: BebopUUID?, owner: String,
        discardResult: Bool,
        execute: @escaping @Sendable (BebopUUID) async -> FutureResult
    ) async throws -> BebopUUID

    /// Persist result. Return the owner for downstream notification, nil if ID unknown.
    func complete(id: BebopUUID, result: FutureResult) async throws -> String?

    /// Push a completed result to matching subscribers.
    func notify(id: BebopUUID, result: FutureResult, owner: String) async throws

    @discardableResult
    func cancel(id: BebopUUID, owner: String) async throws -> Bool

    func subscribe(
        futureIds: [BebopUUID]?, owner: String
    ) async throws -> (
        immediate: [FutureResult],
        stream: AsyncThrowingStream<FutureResult, Error>
    )

    func contains(_ id: BebopUUID) async throws -> Bool
}

public final class FutureStore: FutureStorage, Sendable {
    private struct FutureEntry: Sendable {
        var state: FutureState
        let owner: String
    }

    enum FutureState: Sendable {
        case pending(Task<Void, Never>?, RpcContext)
        case completed(FutureResult, sequence: UInt64)
    }

    private struct Subscriber: Sendable {
        let id: UInt64
        let owner: String
        var futureIds: Set<BebopUUID>?
        let minimumSequence: UInt64?
        let continuation: AsyncThrowingStream<FutureResult, Error>.Continuation

        func accepts(
            _ resultId: BebopUUID,
            owner resultOwner: String,
            sequence: UInt64?
        ) -> Bool {
            guard owner == resultOwner else { return false }
            if let futureIds { return futureIds.contains(resultId) }
            guard let minimumSequence, let sequence else { return true }
            return sequence >= minimumSequence
        }
    }

    private struct State: Sendable {
        var futures: [BebopUUID: FutureEntry] = [:]
        var subscribers: [UInt64: Subscriber] = [:]
        var nextSubscriberId: UInt64 = 0
        var idempotencyIndex: [BebopUUID: BebopUUID] = [:]
        var reverseIdempotency: [BebopUUID: BebopUUID] = [:]
        var completedOrder: [BebopUUID] = []
        var completedEvictionOffset: Int = 0
        var nextCompletionSequence: UInt64 = 0
        var pendingCount: UInt = 0
    }

    private let _state: Mutex<State> = .init(.init())
    let maxPendingFutures: UInt
    let maxCompletedFutures: UInt
    let subscriberBufferCapacity: Int

    public init(
        maxPendingFutures: UInt = 10_000,
        maxCompletedFutures: UInt = 10_000,
        subscriberBufferCapacity: Int = 256
    ) {
        precondition(subscriberBufferCapacity > 0, "subscriberBufferCapacity must be positive")
        self.maxPendingFutures = maxPendingFutures
        self.maxCompletedFutures = maxCompletedFutures
        self.subscriberBufferCapacity = subscriberBufferCapacity
    }

    // MARK: - Registration

    public func register(
        ctx: RpcContext,
        idempotencyKey: BebopUUID?,
        owner: String,
        discardResult: Bool = false,
        execute: @escaping @Sendable (BebopUUID) async -> FutureResult
    ) async throws -> BebopUUID {
        try _state.withLock { state throws(BebopRpcError) in
            if let idempotencyKey, let existing = state.idempotencyIndex[idempotencyKey] {
                if let entry = state.futures[existing], entry.owner == owner {
                    return existing
                }
                throw BebopRpcError(code: .permissionDenied)
            }

            if maxPendingFutures < .max, state.pendingCount >= maxPendingFutures {
                throw BebopRpcError(
                    code: .resourceExhausted,
                    detail: "too many pending futures"
                )
            }

            let id = BebopUUID.random()

            if let idempotencyKey {
                state.idempotencyIndex[idempotencyKey] = id
                state.reverseIdempotency[id] = idempotencyKey
            }

            state.futures[id] = FutureEntry(state: .pending(nil, ctx), owner: owner)
            state.pendingCount += 1

            let task = Task<Void, Never> { [weak self] in
                let result = await execute(id)
                guard let self else { return }
                if discardResult {
                    removePending(id: id)
                    await notify(id: id, result: result, owner: owner)
                } else if let owner = await complete(id: id, result: result) {
                    await notify(id: id, result: result, owner: owner)
                }
            }

            state.futures[id] = FutureEntry(state: .pending(task, ctx), owner: owner)
            return id
        }
    }

    // MARK: - Completion

    public func complete(id: BebopUUID, result: FutureResult) async -> String? {
        _state.withLock { state -> String? in
            guard var entry = state.futures[id] else { return nil }
            guard case .pending = entry.state else { return nil }
            state.pendingCount -= 1
            let owner = entry.owner
            let sequence = state.nextCompletionSequence
            state.nextCompletionSequence &+= 1
            entry.state = .completed(result, sequence: sequence)
            state.futures[id] = entry
            state.completedOrder.append(id)
            evict(&state)
            return owner
        }
    }

    // MARK: - Notification

    public func notify(id: BebopUUID, result: FutureResult, owner: String) async {
        let matching = _state.withLock { state in
            let sequence: UInt64? = if let entry = state.futures[id],
                case let .completed(_, sequence) = entry.state
            {
                sequence
            } else {
                nil
            }
            return Array(
                state.subscribers.values.filter {
                    $0.accepts(id, owner: owner, sequence: sequence)
                })
        }
        var deliveries: [(UInt64, AsyncThrowingStream<FutureResult, Error>.Continuation.YieldResult)] = []
        deliveries.reserveCapacity(matching.count)
        for sub in matching {
            deliveries.append((sub.id, sub.continuation.yield(result)))
        }

        var failures: [AsyncThrowingStream<FutureResult, Error>.Continuation] = []
        var completions: [AsyncThrowingStream<FutureResult, Error>.Continuation] = []
        _state.withLock { state in
            for (subscriberId, delivery) in deliveries {
                guard var subscriber = state.subscribers[subscriberId] else { continue }
                switch delivery {
                case .enqueued:
                    subscriber.futureIds?.remove(id)
                    if subscriber.futureIds?.isEmpty == true {
                        state.subscribers.removeValue(forKey: subscriberId)
                        completions.append(subscriber.continuation)
                    } else {
                        state.subscribers[subscriberId] = subscriber
                    }
                case .dropped:
                    state.subscribers.removeValue(forKey: subscriberId)
                    failures.append(subscriber.continuation)
                case .terminated:
                    state.subscribers.removeValue(forKey: subscriberId)
                @unknown default:
                    state.subscribers.removeValue(forKey: subscriberId)
                    failures.append(subscriber.continuation)
                }
            }
        }
        for continuation in completions {
            continuation.finish()
        }
        for continuation in failures {
            continuation.finish(
                throwing: BebopRpcError(
                    code: .resourceExhausted,
                    detail: "future subscriber could not keep up"
                )
            )
        }
    }

    // MARK: - Cancellation

    @discardableResult
    public func cancel(id: BebopUUID, owner: String) async -> Bool {
        _state.withLock { state in
            guard let entry = state.futures[id],
                  entry.owner == owner,
                  case let .pending(task, ctx) = entry.state
            else { return false }
            ctx.cancel()
            task?.cancel()
            state.pendingCount -= 1
            state.futures.removeValue(forKey: id)
            if let key = state.reverseIdempotency.removeValue(forKey: id) {
                state.idempotencyIndex.removeValue(forKey: key)
            }
            return true
        }
    }

    // MARK: - Subscription

    public func subscribe(
        futureIds ids: [BebopUUID]?,
        owner: String
    ) async -> (
        immediate: [FutureResult],
        stream: AsyncThrowingStream<FutureResult, Error>
    ) {
        let (stream, continuation) = AsyncThrowingStream.makeStream(
            of: FutureResult.self,
            throwing: Error.self,
            bufferingPolicy: .bufferingOldest(subscriberBufferCapacity)
        )

        let (immediate, subId) = _state.withLock { state -> ([FutureResult], UInt64?) in
            var immediate: [FutureResult] = []
            var remainingIds = ids.map(Set.init)

            if let ids {
                var seen: Set<BebopUUID> = []
                for id in ids {
                    guard seen.insert(id).inserted else { continue }
                    if let entry = state.futures[id],
                       entry.owner == owner,
                       case let .completed(result, _) = entry.state
                    {
                        immediate.append(result)
                        remainingIds?.remove(id)
                    }
                }
            } else {
                for (_, entry) in state.futures where entry.owner == owner {
                    if case let .completed(result, _) = entry.state {
                        immediate.append(result)
                    }
                }
            }

            if remainingIds?.isEmpty == true {
                return (immediate, nil)
            }

            let subId = state.nextSubscriberId
            state.nextSubscriberId += 1
            state.subscribers[subId] =
                Subscriber(
                    id: subId,
                    owner: owner,
                    futureIds: remainingIds,
                    minimumSequence: ids == nil ? state.nextCompletionSequence : nil,
                    continuation: continuation
                )
            return (immediate, subId)
        }

        guard let subId else {
            continuation.finish()
            return (immediate, stream)
        }

        continuation.onTermination = { [weak self] _ in
            _ = self?._state.withLock { state in
                state.subscribers.removeValue(forKey: subId)
            }
        }

        return (immediate, stream)
    }

    public func contains(_ id: BebopUUID) async -> Bool {
        _state.withLock { $0.futures[id] != nil }
    }

    // MARK: - Fire-and-forget cleanup

    /// Remove a pending entry after notification without persisting as completed.
    private func removePending(id: BebopUUID) {
        _state.withLock { state in
            guard let entry = state.futures[id],
                  case .pending = entry.state
            else { return }
            state.pendingCount -= 1
            state.futures.removeValue(forKey: id)
            if let key = state.reverseIdempotency.removeValue(forKey: id) {
                state.idempotencyIndex.removeValue(forKey: key)
            }
        }
    }

    // MARK: - Eviction

    private func evict(_ state: inout State) {
        guard maxCompletedFutures < .max else { return }
        let activeCount = state.completedOrder.count - state.completedEvictionOffset
        var evicted = 0
        while (activeCount - evicted) > Int(maxCompletedFutures) {
            let evictId = state.completedOrder[state.completedEvictionOffset + evicted]
            state.futures.removeValue(forKey: evictId)
            if let key = state.reverseIdempotency.removeValue(forKey: evictId) {
                state.idempotencyIndex.removeValue(forKey: key)
            }
            evicted += 1
        }
        state.completedEvictionOffset += evicted

        if state.completedEvictionOffset > 0,
           state.completedEvictionOffset >= state.completedOrder.count / 2
        {
            state.completedOrder.removeFirst(state.completedEvictionOffset)
            state.completedEvictionOffset = 0
        }
    }
}
