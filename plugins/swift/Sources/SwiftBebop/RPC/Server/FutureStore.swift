import Synchronization

public protocol FutureStorage: Sendable {
    func register(
        ctx: RpcContext, idempotencyKey: BebopUUID?, owner: String,
        discardResult: Bool,
        execute: @escaping @Sendable (BebopUUID) async -> FutureResult
    ) async throws -> BebopUUID

    /// Persist result. Return the owner for downstream notification, nil if ID unknown.
    func complete(id: BebopUUID, result: FutureResult) async -> String?

    /// Push a completed result to matching subscribers.
    func notify(id: BebopUUID, result: FutureResult, owner: String) async

    @discardableResult
    func cancel(id: BebopUUID, owner: String) async -> Bool

    func subscribe(
        futureIds: [BebopUUID]?, owner: String
    ) async -> (immediate: [FutureResult], stream: AsyncStream<FutureResult>)

    func contains(_ id: BebopUUID) async -> Bool
}

public final class FutureStore: FutureStorage, Sendable {
    private struct FutureEntry: Sendable {
        var state: FutureState
        let owner: String
    }

    enum FutureState: Sendable {
        case pending(Task<Void, Never>?, RpcContext)
        case completed(FutureResult)
    }

    private struct Subscriber: Sendable {
        let id: UInt64
        let owner: String
        let futureIds: Set<BebopUUID>?
        let continuation: AsyncStream<FutureResult>.Continuation

        func accepts(_ resultId: BebopUUID, owner resultOwner: String) -> Bool {
            guard owner == resultOwner else { return false }
            guard let futureIds else { return true }
            return futureIds.contains(resultId)
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
        var pendingCount: UInt = 0
    }

    private let _state: Mutex<State> = .init(.init())
    let maxPendingFutures: UInt
    let maxCompletedFutures: UInt

    public init(maxPendingFutures: UInt = .max, maxCompletedFutures: UInt = 10000) {
        self.maxPendingFutures = maxPendingFutures
        self.maxCompletedFutures = maxCompletedFutures
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
                    // Notify subscribers, then remove the entry without persisting
                    await notify(id: id, result: result, owner: owner)
                    removePending(id: id)
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
            if case .pending = entry.state {
                state.pendingCount -= 1
            }
            let owner = entry.owner
            entry.state = .completed(result)
            state.futures[id] = entry
            state.completedOrder.append(id)
            evict(&state)
            return owner
        }
    }

    // MARK: - Notification

    public func notify(id: BebopUUID, result: FutureResult, owner: String) async {
        let matching = _state.withLock { state in
            Array(state.subscribers.values.filter { $0.accepts(id, owner: owner) })
        }
        for sub in matching {
            sub.continuation.yield(result)
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
    ) async -> (immediate: [FutureResult], stream: AsyncStream<FutureResult>) {
        let (stream, continuation) = AsyncStream.makeStream(of: FutureResult.self)

        let (immediate, subId) = _state.withLock { state -> ([FutureResult], UInt64) in
            var immediate: [FutureResult] = []

            if let ids {
                for id in ids {
                    if let entry = state.futures[id],
                       entry.owner == owner,
                       case let .completed(result) = entry.state
                    {
                        immediate.append(result)
                    }
                }
            } else {
                for (_, entry) in state.futures where entry.owner == owner {
                    if case let .completed(result) = entry.state {
                        immediate.append(result)
                    }
                }
            }

            let subId = state.nextSubscriberId
            state.nextSubscriberId += 1
            let idSet = ids.map { Set($0) }
            state.subscribers[subId] =
                Subscriber(id: subId, owner: owner, futureIds: idSet, continuation: continuation)
            return (immediate, subId)
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
