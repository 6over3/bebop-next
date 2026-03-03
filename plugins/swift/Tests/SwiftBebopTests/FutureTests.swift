import Testing

@testable import SwiftBebop

private struct SlowHandler: WidgetServiceHandler {
  let delay: Duration

  init(delay: Duration = .milliseconds(500)) {
    self.delay = delay
  }

  func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
    try await Task.sleep(for: delay)
    return EchoResponse(value: request.value)
  }
  func listWidgets(_ request: CountRequest, context: RpcContext) async throws
    -> AsyncThrowingStream<CountResponse, Error>
  { fatalError() }
  func uploadWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>, context: RpcContext
  ) async throws -> EchoResponse { fatalError() }
  func syncWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>, context: RpcContext
  ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
}

private struct MetadataHandler: WidgetServiceHandler {
  func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
    context.setResponseMetadata("x-trace", "abc-123")
    return EchoResponse(value: request.value)
  }
  func listWidgets(_ request: CountRequest, context: RpcContext) async throws
    -> AsyncThrowingStream<CountResponse, Error>
  { fatalError() }
  func uploadWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>, context: RpcContext
  ) async throws -> EchoResponse { fatalError() }
  func syncWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>, context: RpcContext
  ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
}

@Suite struct FutureTests {

  @Test func dispatchUnaryAndAwait() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "hello"))
    let result = try await future.value
    #expect(result.value == "hello")
  }

  @Test func dispatchViaGeneratedAccessor() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future = try await dispatcher.widgetService.getWidget(
      EchoRequest(value: "accessor"))
    let result = try await future.value
    #expect(result.value == "accessor")
  }

  @Test func dispatchViaDeconstructedAccessor() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future = try await dispatcher.widgetService.getWidget(value: "decon")
    let result = try await future.value
    #expect(result.value == "decon")
  }

  @Test func cancelPendingFuture() async throws {
    let channel = buildChannel(handler: SlowHandler(delay: .seconds(10)), futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "slow"))

    try await future.cancel()
  }

  @Test func idempotencyDedup() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()
    let key = BebopUUID.random()

    let f1: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "a"),
      options: .init(idempotencyKey: key))
    let f2: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "b"),
      options: .init(idempotencyKey: key))
    #expect(f1.id == f2.id)
  }

  @Test func batchDispatch() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let batch = channel.makeBatch()
    let ref = batch.widgetService.getWidget(EchoRequest(value: "batched"))
    let future = try await batch.dispatch(using: dispatcher)
    let results = try await future.value
    let echo = try results[ref]
    #expect(echo.value == "batched")
  }

  @Test func rehydrateFuture() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "saved"))

    _ = try await future.value

    let rehydrated = dispatcher.future(id: future.id, as: EchoResponse.self)
    let result = try await rehydrated.value
    #expect(result.value == "saved")
  }

  @Test func multipleConcurrentFutures() async throws {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let futures = try await withThrowingTaskGroup(
      of: BebopFuture<EchoResponse>.self,
      returning: [BebopFuture<EchoResponse>].self
    ) { group in
      for i in 0..<5 {
        group.addTask {
          try await dispatcher.dispatch(
            methodId: getWidgetId,
            request: EchoRequest(value: "req-\(i)"))
        }
      }
      return try await group.reduce(into: []) { $0.append($1) }
    }

    var values: Set<String> = []
    for f in futures {
      let result = try await f.value
      values.insert(result.value)
    }
    #expect(values.count == 5)
  }

  @Test func dispatchNonexistentMethod() async {
    let channel = buildChannel(futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    await #expect(throws: BebopRpcError.self) {
      let _: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
        methodId: 0xDEAD,
        request: EchoRequest(value: "nope"))
    }
  }

  @Test func futuresDisabledReturnsUnimplemented() async {
    let channel = buildChannel(futuresEnabled: false)
    let dispatcher = channel.makeFutureDispatcher()

    await #expect(throws: BebopRpcError.self) {
      let _: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
        methodId: getWidgetId,
        request: EchoRequest(value: "x"))
    }
  }

  @Test func dispatchClientStreamRejectsWithInvalidArgument() async throws {
    let channel = buildChannel(futuresEnabled: true)

    let req = FutureDispatchRequest(
      methodId: uploadWidgetsId,
      payload: EchoRequest(value: "x").serializedData())

    do {
      _ = try await channel.unary(
        method: BebopReservedMethod.dispatch,
        request: req.serializedData(),
        context: RpcContext())
      Issue.record("expected error")
    } catch let error as BebopRpcError {
      #expect(error.code == .invalidArgument)
    }
  }

  @Test func dispatchDuplexStreamRejectsWithInvalidArgument() async throws {
    let channel = buildChannel(futuresEnabled: true)

    let req = FutureDispatchRequest(
      methodId: syncWidgetsId,
      payload: EchoRequest(value: "x").serializedData())

    do {
      _ = try await channel.unary(
        method: BebopReservedMethod.dispatch,
        request: req.serializedData(),
        context: RpcContext())
      Issue.record("expected error")
    } catch let error as BebopRpcError {
      #expect(error.code == .invalidArgument)
    }
  }

  @Test func dispatchServerStreamRejectsWithInvalidArgument() async throws {
    let channel = buildChannel(futuresEnabled: true)

    let req = FutureDispatchRequest(
      methodId: listWidgetsId,
      payload: CountRequest(n: 1).serializedData())

    do {
      _ = try await channel.unary(
        method: BebopReservedMethod.dispatch,
        request: req.serializedData(),
        context: RpcContext())
      Issue.record("expected error")
    } catch let error as BebopRpcError {
      #expect(error.code == .invalidArgument)
    }
  }

  @Test func dispatchRecursiveRejectsWithInvalidArgument() async throws {
    let channel = buildChannel(futuresEnabled: true)

    let req = FutureDispatchRequest(
      methodId: BebopReservedMethod.dispatch,
      payload: [])

    do {
      _ = try await channel.unary(
        method: BebopReservedMethod.dispatch,
        request: req.serializedData(),
        context: RpcContext())
      Issue.record("expected error")
    } catch let error as BebopRpcError {
      #expect(error.code == .invalidArgument)
    }
  }

  @Test func awaitWithTimeout() async throws {
    let channel = buildChannel(handler: SlowHandler(), futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "slow"))

    await #expect(throws: BebopRpcError.self) {
      _ = try await future.value(timeout: .milliseconds(50))
    }

    let rehydrated = dispatcher.future(id: future.id, as: EchoResponse.self)
    let result = try await rehydrated.value
    #expect(result.value == "slow")
  }

  @Test func detachWithoutCancel() async throws {
    let channel = buildChannel(handler: SlowHandler(), futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "detach"))

    let task = Task { try await future.value }
    try await Task.sleep(for: .milliseconds(50))
    task.cancel()

    await #expect(throws: CancellationError.self) {
      _ = try await task.value
    }

    let rehydrated = dispatcher.future(id: future.id, as: EchoResponse.self)
    let result = try await rehydrated.value
    #expect(result.value == "detach")
  }

  @Test func concurrentAwaitsOnSameFuture() async throws {
    let channel = buildChannel(
      handler: SlowHandler(delay: .milliseconds(100)), futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "shared"))

    async let a = future.value
    async let b = future.value
    let (ra, rb) = try await (a, b)
    #expect(ra.value == "shared")
    #expect(rb.value == "shared")
  }

  @Test func metadataPreservedOnResponse() async throws {
    let channel = buildChannel(handler: MetadataHandler(), futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "meta"))

    let resp: Response<EchoResponse, [String: String]> = try await future.response
    #expect(resp.value.value == "meta")
    #expect(resp.metadata["x-trace"] == "abc-123")
  }

  @Test func deadlineEnforcedServerSide() async throws {
    let channel = buildChannel(handler: SlowHandler(delay: .seconds(10)), futuresEnabled: true)
    let dispatcher = channel.makeFutureDispatcher()

    let ctx = RpcContext(timeout: .milliseconds(100))

    let future: BebopFuture<EchoResponse> = try await dispatcher.dispatch(
      methodId: getWidgetId,
      request: EchoRequest(value: "timeout"),
      context: ctx)

    do {
      _ = try await future.value
      Issue.record("expected deadline error")
    } catch let error as BebopRpcError {
      #expect(error.code == .deadlineExceeded)
    }
  }
}
