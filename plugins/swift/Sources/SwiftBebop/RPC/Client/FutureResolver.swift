import Synchronization

public final class FutureResolver: Sendable {
    private struct PendingEntry: Sendable {
        let entryId: UInt64
        let continuation: CheckedContinuation<FutureOutcome, any Error>
    }

    private struct ResolverState: Sendable {
        var pending: [BebopUUID: [PendingEntry]] = [:]
        var completed: [BebopUUID: FutureOutcome] = [:]
        var completedOrder: [BebopUUID] = []
        var completedEvictionOffset: Int = 0
        var streamTask: Task<Void, Never>?
        var nextEntryId: UInt64 = 0
    }

    private let _sendCancel: @Sendable ([UInt8]) async throws -> Void
    private let _consumeResolveStream: @Sendable (
        [UInt8],
        @escaping @Sendable ([UInt8]) throws -> Void
    ) async throws -> Void
    private let maxCompletedResults: Int

    private let _state: Mutex<ResolverState> = .init(.init())

    init(
        maxCompletedResults: Int = 10000,
        sendCancel: @escaping @Sendable ([UInt8]) async throws -> Void,
        consumeResolveStream: @escaping @Sendable (
            [UInt8],
            @escaping @Sendable ([UInt8]) throws -> Void
        ) async throws -> Void
    ) {
        precondition(maxCompletedResults >= 0, "maxCompletedResults must be nonnegative")
        self.maxCompletedResults = maxCompletedResults
        _sendCancel = sendCancel
        _consumeResolveStream = consumeResolveStream
    }

    deinit {
        let (orphaned, task) = _state.withLock { state -> ([PendingEntry], Task<Void, Never>?) in
            let all = state.pending.values.flatMap(\.self)
            state.pending.removeAll()
            let task = state.streamTask
            state.streamTask = nil
            return (all, task)
        }
        task?.cancel()
        for entry in orphaned {
            entry.continuation.resume(throwing: CancellationError())
        }
    }

    func `await`(id: BebopUUID) async throws -> FutureOutcome {
        if let cached = _state.withLock({ $0.completed[id] }) {
            return cached
        }

        let entryId = _state.withLock { state -> UInt64 in
            let eid = state.nextEntryId
            state.nextEntryId += 1
            return eid
        }

        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                var registered = false
                _state.withLock { state in
                    if let cached = state.completed[id] {
                        continuation.resume(returning: cached)
                        return
                    }
                    if Task.isCancelled {
                        continuation.resume(throwing: CancellationError())
                        return
                    }
                    state.pending[id, default: []].append(
                        PendingEntry(entryId: entryId, continuation: continuation))
                    registered = true
                }
                if registered { ensureResolveStream() }
            }
        } onCancel: {
            let entry = _state.withLock { state -> PendingEntry? in
                guard var entries = state.pending[id] else { return nil }
                guard let idx = entries.firstIndex(where: { $0.entryId == entryId }) else { return nil }
                let entry = entries.remove(at: idx)
                if entries.isEmpty {
                    state.pending.removeValue(forKey: id)
                } else {
                    state.pending[id] = entries
                }
                return entry
            }
            entry?.continuation.resume(throwing: CancellationError())
        }
    }

    func resolve(result: FutureResult) {
        let entries = _state.withLock { state -> [PendingEntry] in
            if state.completed.updateValue(result.outcome, forKey: result.id) == nil {
                state.completedOrder.append(result.id)
            }
            evict(&state)
            return state.pending.removeValue(forKey: result.id) ?? []
        }
        for entry in entries {
            entry.continuation.resume(returning: result.outcome)
        }
    }

    func cancel(id: BebopUUID) async throws {
        let req = FutureCancelRequest(id: id)
        try await _sendCancel(req.serializedData())
    }

    private func ensureResolveStream() {
        _state.withLock { state in
            guard state.streamTask == nil else { return }
            state.streamTask = Task { [weak self] in
                guard let self else { return }
                var streamError: (any Error)?
                do {
                    let req = FutureResolveRequest()
                    try await _consumeResolveStream(req.serializedData()) { bytes in
                        let result = try FutureResult.decode(from: bytes)
                        self.resolve(result: result)
                    }
                } catch is CancellationError {
                    streamError = CancellationError()
                } catch {
                    streamError = error
                }
                let orphaned = _state.withLock { state -> [PendingEntry] in
                    state.streamTask = nil
                    let all = state.pending.values.flatMap(\.self)
                    state.pending.removeAll()
                    return all
                }
                let failure: any Error =
                    streamError
                        ?? BebopRpcError(code: .unavailable, detail: "resolve stream closed")
                for entry in orphaned {
                    entry.continuation.resume(throwing: failure)
                }
            }
        }
    }

    private func evict(_ state: inout ResolverState) {
        let activeCount = state.completedOrder.count - state.completedEvictionOffset
        var evicted = 0
        while (activeCount - evicted) > maxCompletedResults {
            let evictId = state.completedOrder[state.completedEvictionOffset + evicted]
            state.completed.removeValue(forKey: evictId)
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
