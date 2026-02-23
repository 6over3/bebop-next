import Testing

@testable import SwiftBebop

private let alice = "alice"
private let bob = "bob"

@Suite struct FutureStoreTests {

  @Test func registerAndComplete() async throws {
    let store = FutureStore()
    let id = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1, 2, 3], metadata: [:])))
    }
    #expect(store.contains(id))

    try? await Task.sleep(for: .milliseconds(50))

    let (immediate, _) = store.subscribe(futureIds: [id], owner: alice)
    #expect(immediate.count == 1)
    #expect(immediate[0].id == id)
    guard case .success(let s) = immediate[0].outcome else {
      Issue.record("expected success")
      return
    }
    #expect(s.payload == [1, 2, 3])
  }

  @Test func cancelPendingFuture() async throws {
    let gate = AsyncStream.makeStream(of: Void.self)

    let store = FutureStore()
    let id = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      for await _ in gate.stream { break }
      return FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [], metadata: [:])))
    }

    let cancelled = store.cancel(id: id, owner: alice)
    #expect(cancelled)

    gate.continuation.finish()
  }

  @Test func cancelByWrongOwnerFails() async throws {
    let gate = AsyncStream.makeStream(of: Void.self)

    let store = FutureStore()
    let id = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      for await _ in gate.stream { break }
      return FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [], metadata: [:])))
    }

    #expect(!store.cancel(id: id, owner: bob))

    store.cancel(id: id, owner: alice)
    gate.continuation.finish()
  }

  @Test func cancelCompletedFutureReturnsFalse() async throws {
    let store = FutureStore()
    let id = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [], metadata: [:])))
    }

    try await Task.sleep(for: .milliseconds(50))

    #expect(!store.cancel(id: id, owner: alice))
  }

  @Test func cancelUnknownIdReturnsFalse() {
    let store = FutureStore()
    #expect(!store.cancel(id: BebopUUID.random(), owner: alice))
  }

  @Test func cancelClearsIdempotencyKey() async throws {
    let store = FutureStore()
    let gate = AsyncStream.makeStream(of: Void.self)
    let key = BebopUUID.random()

    let id1 = try store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) { futureId in
      for await _ in gate.stream { break }
      return FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }

    store.cancel(id: id1, owner: alice)
    gate.continuation.finish()

    let id2 = try store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
    }
    #expect(id1 != id2)
  }

  @Test func idempotencyDedup() async throws {
    let store = FutureStore()
    let key = BebopUUID.random()

    let id1 = try store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }
    let id2 = try store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
    }
    #expect(id1 == id2)
  }

  @Test func idempotencyKeyFromDifferentOwnerRejects() async throws {
    let store = FutureStore()
    let key = BebopUUID.random()

    _ = try store.register(ctx: RpcContext(), idempotencyKey: key, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }

    #expect(throws: BebopRpcError.self) {
      _ = try store.register(ctx: RpcContext(), idempotencyKey: key, owner: bob) { futureId in
        FutureResult(
          id: futureId,
          outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
      }
    }
  }

  @Test func nilIdempotencyKeyDoesNotDedup() async throws {
    let store = FutureStore()
    let id1 = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }
    let id2 = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
    }
    #expect(id1 != id2)
  }

  @Test func subscribeReceivesPendingResults() async throws {
    let gate = AsyncStream.makeStream(of: Void.self)

    let store = FutureStore()
    let id = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      for await _ in gate.stream { break }
      return FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [99], metadata: [:])))
    }

    let (immediate, stream) = store.subscribe(futureIds: [id], owner: alice)
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
    let id = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [], metadata: [:])))
    }

    try await Task.sleep(for: .milliseconds(50))

    let (aliceResults, _) = store.subscribe(futureIds: [id], owner: alice)
    #expect(aliceResults.count == 1)

    let (bobResults, _) = store.subscribe(futureIds: [id], owner: bob)
    #expect(bobResults.isEmpty)
  }

  @Test func subscribeWildcardFiltersbyOwner() async throws {
    let store = FutureStore()
    _ = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }
    _ = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: bob) { futureId in
      FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
    }

    try await Task.sleep(for: .milliseconds(50))

    let (aliceResults, _) = store.subscribe(futureIds: nil, owner: alice)
    #expect(aliceResults.count == 1)

    let (bobResults, _) = store.subscribe(futureIds: nil, owner: bob)
    #expect(bobResults.count == 1)
  }

  @Test func dispatchLimitRejectsWhenExceeded() async throws {
    let store = FutureStore(maxPendingFutures: 2)
    let gate = AsyncStream.makeStream(of: Void.self)

    _ = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { _ in
      for await _ in gate.stream { break }
      return FutureResult(
        id: BebopUUID.random(), outcome: .success(FutureSuccess(payload: [], metadata: [:])))
    }
    _ = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { _ in
      for await _ in gate.stream { break }
      return FutureResult(
        id: BebopUUID.random(), outcome: .success(FutureSuccess(payload: [], metadata: [:])))
    }

    #expect(throws: BebopRpcError.self) {
      _ = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { _ in
        FutureResult(
          id: BebopUUID.random(), outcome: .success(FutureSuccess(payload: [], metadata: [:])))
      }
    }

    gate.continuation.finish()
  }

  @Test func evictsOldestCompletedFutures() async throws {
    let store = FutureStore(maxCompletedFutures: 2)

    let id1 = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(id: futureId, outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }

    try await Task.sleep(for: .milliseconds(50))

    let id2 = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(id: futureId, outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
    }

    try await Task.sleep(for: .milliseconds(50))

    _ = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      FutureResult(id: futureId, outcome: .success(FutureSuccess(payload: [3], metadata: [:])))
    }

    try await Task.sleep(for: .milliseconds(50))

    #expect(!store.contains(id1))
    #expect(store.contains(id2))
  }

  @Test func subscriberFilterOnlyDeliversMatchingResults() async throws {
    let gate = AsyncStream.makeStream(of: Void.self)
    let store = FutureStore()

    let id1 = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      for await _ in gate.stream { break }
      return FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [1], metadata: [:])))
    }

    let id2 = try store.register(ctx: RpcContext(), idempotencyKey: nil, owner: alice) { futureId in
      for await _ in gate.stream { break }
      return FutureResult(
        id: futureId,
        outcome: .success(FutureSuccess(payload: [2], metadata: [:])))
    }

    let (_, stream) = store.subscribe(futureIds: [id1], owner: alice)
    gate.continuation.finish()

    var received: [BebopUUID] = []
    for await result in stream {
      received.append(result.id)
      break
    }

    #expect(received == [id1])
    _ = id2
  }
}
