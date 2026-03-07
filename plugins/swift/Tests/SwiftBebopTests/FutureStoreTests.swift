import Testing

@testable import SwiftBebop

private let alice = "alice"
private let bob = "bob"

@Suite struct FutureStoreTests {
    @Test func registerAndComplete() async throws {
        let store = FutureStore()
        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1, 2, 3], metadata: [:]))
            )
        }
        let registered = await store.contains(id)
        #expect(registered)

        try? await Task.sleep(for: .milliseconds(50))

        let (immediate, _) = await store.subscribe(futureIds: [id], owner: alice)
        #expect(immediate.count == 1)
        #expect(immediate[0].id == id)
        guard case let .success(s) = immediate[0].outcome else {
            Issue.record("expected success")
            return
        }
        #expect(s.payload == [1, 2, 3])
    }

    @Test func cancelPendingFuture() async throws {
        let gate = AsyncStream.makeStream(of: Void.self)

        let store = FutureStore()
        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [], metadata: [:]))
            )
        }

        let cancelled = await store.cancel(id: id, owner: alice)
        #expect(cancelled)

        gate.continuation.finish()
    }

    @Test func cancelByWrongOwnerFails() async throws {
        let gate = AsyncStream.makeStream(of: Void.self)

        let store = FutureStore()
        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [], metadata: [:]))
            )
        }

        let bobCancelled = await store.cancel(id: id, owner: bob)
        #expect(!bobCancelled)

        await store.cancel(id: id, owner: alice)
        gate.continuation.finish()
    }

    @Test func cancelCompletedFutureReturnsFalse() async throws {
        let store = FutureStore()
        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [], metadata: [:]))
            )
        }

        try await Task.sleep(for: .milliseconds(50))

        let completedCancelled = await store.cancel(id: id, owner: alice)
        #expect(!completedCancelled)
    }

    @Test func cancelUnknownIdReturnsFalse() async {
        let store = FutureStore()
        let unknownCancelled = await store.cancel(id: BebopUUID.random(), owner: alice)
        #expect(!unknownCancelled)
    }

    @Test func cancelClearsIdempotencyKey() async throws {
        let store = FutureStore()
        let gate = AsyncStream.makeStream(of: Void.self)
        let key = BebopUUID.random()

        let id1 = try await store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) {
            futureId in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1], metadata: [:]))
            )
        }

        await store.cancel(id: id1, owner: alice)
        gate.continuation.finish()

        let id2 = try await store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [2], metadata: [:]))
            )
        }
        #expect(id1 != id2)
    }

    @Test func idempotencyDedup() async throws {
        let store = FutureStore()
        let key = BebopUUID.random()

        let id1 = try await store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1], metadata: [:]))
            )
        }
        let id2 = try await store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [2], metadata: [:]))
            )
        }
        #expect(id1 == id2)
    }

    @Test func idempotencyKeyFromDifferentOwnerRejects() async throws {
        let store = FutureStore()
        let key = BebopUUID.random()

        _ = try await store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1], metadata: [:]))
            )
        }

        await #expect(throws: BebopRpcError.self) {
            _ = try await store.register(ctx: RpcContext(), idempotencyKey: key, owner: bob) {
                futureId in
                FutureResult(
                    id: futureId,
                    outcome: .success(FutureSuccess(payload: [2], metadata: [:]))
                )
            }
        }
    }

    @Test func nilIdempotencyKeyDoesNotDedup() async throws {
        let store = FutureStore()
        let id1 = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1], metadata: [:]))
            )
        }
        let id2 = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [2], metadata: [:]))
            )
        }
        #expect(id1 != id2)
    }

    @Test func subscribeReceivesPendingResults() async throws {
        let gate = AsyncStream.makeStream(of: Void.self)

        let store = FutureStore()
        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [99], metadata: [:]))
            )
        }

        let (immediate, stream) = await store.subscribe(futureIds: [id], owner: alice)
        #expect(immediate.isEmpty)

        gate.continuation.finish()

        var received: [FutureResult] = []
        for await result in stream {
            received.append(result)
            break
        }
        #expect(received.count == 1)
        #expect(received[0].id == id)
    }

    @Test func subscribeFiltersbyOwner() async throws {
        let store = FutureStore()
        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [], metadata: [:]))
            )
        }

        try await Task.sleep(for: .milliseconds(50))

        let (aliceResults, _) = await store.subscribe(futureIds: [id], owner: alice)
        #expect(aliceResults.count == 1)

        let (bobResults, _) = await store.subscribe(futureIds: [id], owner: bob)
        #expect(bobResults.isEmpty)
    }

    @Test func subscribeWildcardFiltersbyOwner() async throws {
        let store = FutureStore()
        _ = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1], metadata: [:]))
            )
        }
        _ = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: bob) { futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [2], metadata: [:]))
            )
        }

        try await Task.sleep(for: .milliseconds(50))

        let (aliceResults, _) = await store.subscribe(futureIds: nil, owner: alice)
        #expect(aliceResults.count == 1)

        let (bobResults, _) = await store.subscribe(futureIds: nil, owner: bob)
        #expect(bobResults.count == 1)
    }

    @Test func dispatchLimitRejectsWhenExceeded() async throws {
        let store = FutureStore(maxPendingFutures: 2)
        let gate = AsyncStream.makeStream(of: Void.self)

        _ = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { _ in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: BebopUUID.random(), outcome: .success(FutureSuccess(payload: [], metadata: [:]))
            )
        }
        _ = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { _ in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: BebopUUID.random(), outcome: .success(FutureSuccess(payload: [], metadata: [:]))
            )
        }

        await #expect(throws: BebopRpcError.self) {
            _ = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { _ in
                FutureResult(
                    id: BebopUUID.random(), outcome: .success(FutureSuccess(payload: [], metadata: [:]))
                )
            }
        }

        gate.continuation.finish()
    }

    @Test func evictsOldestCompletedFutures() async throws {
        let store = FutureStore(maxCompletedFutures: 2)

        let id1 = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(id: futureId, outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
        }

        try await Task.sleep(for: .milliseconds(50))

        let id2 = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(id: futureId, outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
        }

        try await Task.sleep(for: .milliseconds(50))

        _ = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(id: futureId, outcome: .success(FutureSuccess(payload: [3], metadata: [:])))
        }

        try await Task.sleep(for: .milliseconds(50))

        let containsId1 = await store.contains(id1)
        #expect(!containsId1)
        let containsId2 = await store.contains(id2)
        #expect(containsId2)
    }

    @Test func subscriberFilterOnlyDeliversMatchingResults() async throws {
        let gate = AsyncStream.makeStream(of: Void.self)
        let store = FutureStore()

        let id1 = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [1], metadata: [:]))
            )
        }

        let id2 = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            for await _ in gate.stream {
                break
            }
            return FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [2], metadata: [:]))
            )
        }

        let (_, stream) = await store.subscribe(futureIds: [id1], owner: alice)
        gate.continuation.finish()

        var received: [BebopUUID] = []
        for await result in stream {
            received.append(result.id)
            break
        }

        #expect(received == [id1])
        _ = id2
    }

    @Test func serverSideFireAndForgetDiscardsCompletedResults() async throws {
        let store = FutureStore(maxCompletedFutures: 0)

        let (_, subscriberStream) = await store.subscribe(futureIds: nil, owner: alice)

        let id = try await store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) {
            futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [42], metadata: [:]))
            )
        }

        var pushed: [FutureResult] = []
        for await result in subscriberStream {
            pushed.append(result)
            break
        }
        #expect(pushed.count == 1)
        #expect(pushed[0].id == id)

        let retained = await store.contains(id)
        #expect(!retained)
        let (immediate, _) = await store.subscribe(futureIds: [id], owner: alice)
        #expect(immediate.isEmpty)
    }

    @Test func clientSideFireAndForgetDiscardsResult() async throws {
        let store = FutureStore()

        let (_, subscriberStream) = await store.subscribe(futureIds: nil, owner: alice)

        let id = try await store.register(
            ctx: RpcContext(), idempotencyKey: nil, owner: alice, discardResult: true
        ) { futureId in
            FutureResult(
                id: futureId,
                outcome: .success(FutureSuccess(payload: [7], metadata: [:]))
            )
        }

        var pushed: [FutureResult] = []
        for await result in subscriberStream {
            pushed.append(result)
            break
        }
        #expect(pushed.count == 1)
        #expect(pushed[0].id == id)

        // Default retention store, but fire-and-forget discards this future
        let retained = await store.contains(id)
        #expect(!retained)
    }
}
