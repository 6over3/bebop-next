import Testing

@testable import SwiftBebop

@Suite struct RouterBatchTests {
  @Test func singleUnaryCall() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "hi").serializedData(), inputFrom: -1)
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)
    #expect(response.results.count == 1)
    #expect(response.results[0].callId == 0)

    guard case .success(let s) = response.results[0].outcome else {
      Issue.record("expected success")
      return
    }
    let echo = try EchoResponse.decode(from: s.payloads[0])
    #expect(echo.value == "hi")
  }

  @Test func multipleIndependentCalls() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "a").serializedData(), inputFrom: -1),
        BatchCall(
          callId: 1, methodId: getWidgetId,
          payload: EchoRequest(value: "b").serializedData(), inputFrom: -1),
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)
    #expect(response.results.count == 2)

    let outcomes = Dictionary(
      response.results.map { ($0.callId, $0.outcome) },
      uniquingKeysWith: { a, _ in a })

    guard case .success(let s1) = outcomes[0] else {
      Issue.record("expected success for call 0")
      return
    }
    #expect(try EchoResponse.decode(from: s1.payloads[0]).value == "a")

    guard case .success(let s2) = outcomes[1] else {
      Issue.record("expected success for call 1")
      return
    }
    #expect(try EchoResponse.decode(from: s2.payloads[0]).value == "b")
  }

  @Test func dependentCallForwardsPayload() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "hello").serializedData(), inputFrom: -1),
        BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)
    #expect(response.results.count == 2)
  }

  @Test func serverStreamInBatch() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: listWidgetsId,
          payload: CountRequest(n: 3).serializedData(), inputFrom: -1)
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)

    guard case .success(let s) = response.results[0].outcome else {
      Issue.record("expected success")
      return
    }
    #expect(s.payloads.count == 3)
    for (i, payload) in s.payloads.enumerated() {
      let resp = try CountResponse.decode(from: payload)
      #expect(resp.i == UInt32(i))
    }
  }

  @Test func unknownMethodInBatchReturnsError() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(callId: 0, methodId: 0xDEAD, payload: [], inputFrom: -1)
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)
    guard case .error(let e) = response.results[0].outcome else {
      Issue.record("expected error")
      return
    }
    #expect(e.code == .notFound)
  }

  @Test func emptyBatchReturnsEmptyResponse() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(calls: [], metadata: [:])
    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)
    #expect(response.results.isEmpty)
  }

  @Test func duplicateCallIdThrows() async {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "a").serializedData(), inputFrom: -1),
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "b").serializedData(), inputFrom: -1),
      ],
      metadata: [:])

    await #expect(throws: BebopRpcError.self) {
      _ = try await router.unary(
        methodId: 1, payload: req.serializedData(), ctx: ctx)
    }
  }

  @Test func negativeCallIdThrows() async {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(callId: -1, methodId: getWidgetId, payload: [], inputFrom: -1)
      ],
      metadata: [:])

    await #expect(throws: BebopRpcError.self) {
      _ = try await router.unary(
        methodId: 1, payload: req.serializedData(), ctx: ctx)
    }
  }

  @Test func invalidDependencyThrows() async {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(callId: 0, methodId: getWidgetId, payload: [], inputFrom: 99)
      ],
      metadata: [:])

    await #expect(throws: BebopRpcError.self) {
      _ = try await router.unary(
        methodId: 1, payload: req.serializedData(), ctx: ctx)
    }
  }

  @Test func handlerErrorPreservesCodeAndDetail() async throws {
    struct FailingHandler: WidgetServiceHandler {
      func getWidget(_ request: EchoRequest, context: some CallContext) async throws -> EchoResponse {
        throw BebopRpcError(code: .internal, detail: "boom")
      }
      func listWidgets(_ request: CountRequest, context: some CallContext) async throws
        -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
      func uploadWidgets(
        _ requests: AsyncThrowingStream<EchoRequest, Error>, context: some CallContext
      ) async throws -> EchoResponse { fatalError() }
      func syncWidgets(
        _ requests: AsyncThrowingStream<EchoRequest, Error>, context: some CallContext
      ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
    }

    let router = buildRouter(handler: FailingHandler())
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "fail").serializedData(), inputFrom: -1)
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)
    guard case .error(let e) = response.results[0].outcome else {
      Issue.record("expected error outcome")
      return
    }
    #expect(e.code == .internal)
    #expect(e.detail == "boom")
  }

  @Test func handlerErrorCascadesToDependents() async throws {
    struct FailingHandler: WidgetServiceHandler {
      func getWidget(_ request: EchoRequest, context: some CallContext) async throws -> EchoResponse {
        throw BebopRpcError(code: .permissionDenied, detail: "nope")
      }
      func listWidgets(_ request: CountRequest, context: some CallContext) async throws
        -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
      func uploadWidgets(
        _ requests: AsyncThrowingStream<EchoRequest, Error>, context: some CallContext
      ) async throws -> EchoResponse { fatalError() }
      func syncWidgets(
        _ requests: AsyncThrowingStream<EchoRequest, Error>, context: some CallContext
      ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
    }

    let router = buildRouter(handler: FailingHandler())
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "x").serializedData(), inputFrom: -1),
        BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)

    let outcomes = Dictionary(
      response.results.map { ($0.callId, $0.outcome) },
      uniquingKeysWith: { a, _ in a })

    guard case .error(let e0) = outcomes[0] else {
      Issue.record("call 0 should error")
      return
    }
    #expect(e0.code == .permissionDenied)
    #expect(e0.detail == "nope")

    guard case .error = outcomes[1] else {
      Issue.record("call 1 should cascade")
      return
    }
  }

  @Test func failedDependencyCascades() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: 1)

    let req = BatchRequest(
      calls: [
        BatchCall(callId: 0, methodId: 0xDEAD, payload: [], inputFrom: -1),
        BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: req.serializedData(), ctx: ctx)
    let response = try BatchResponse.decode(from: responseBytes)

    let outcomes = Dictionary(
      response.results.map { ($0.callId, $0.outcome) },
      uniquingKeysWith: { a, _ in a })

    guard case .error = outcomes[0] else {
      Issue.record("call 0 should error")
      return
    }
    guard case .error = outcomes[1] else {
      Issue.record("call 1 should cascade error")
      return
    }
  }
}
