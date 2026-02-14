import Testing

@testable import SwiftBebop

@Suite struct InterceptorTests {
  struct CountingInterceptor: BebopInterceptor, @unchecked Sendable {
    let counter: Counter

    func intercept(
      methodId: UInt32, ctx: some CallContext,
      proceed: @Sendable () async throws -> Void
    ) async throws {
      await counter.increment()
      try await proceed()
    }
  }

  struct RejectingInterceptor: BebopInterceptor {
    func intercept(
      methodId: UInt32, ctx: some CallContext,
      proceed: @Sendable () async throws -> Void
    ) async throws {
      throw BebopRpcError(code: .permissionDenied, detail: "rejected")
    }
  }

  struct MethodFilterInterceptor: BebopInterceptor {
    let allowedMethods: Set<UInt32>

    func intercept(
      methodId: UInt32, ctx: some CallContext,
      proceed: @Sendable () async throws -> Void
    ) async throws {
      guard allowedMethods.contains(methodId) else {
        throw BebopRpcError(code: .permissionDenied, detail: "method \(methodId) blocked")
      }
      try await proceed()
    }
  }

  @Test func interceptorRunsBeforeHandler() async throws {
    let counter = Counter()
    let router = buildRouter(interceptors: [CountingInterceptor(counter: counter)])
    let ctx = TestCallContext(methodId: getWidgetId)
    let req = EchoRequest(value: "test")
    _ = try await router.unary(methodId: getWidgetId, payload: req.serializedData(), ctx: ctx)
    let count = await counter.value
    #expect(count == 1)
  }

  @Test func interceptorCanReject() async throws {
    let router = buildRouter(interceptors: [RejectingInterceptor()])
    let ctx = TestCallContext(methodId: getWidgetId)
    let req = EchoRequest(value: "test")
    do {
      _ = try await router.unary(methodId: getWidgetId, payload: req.serializedData(), ctx: ctx)
      Issue.record("should have thrown")
    } catch let error as BebopRpcError {
      #expect(error.code == .permissionDenied)
      #expect(error.detail == "rejected")
    } catch {
      Issue.record("unexpected error: \(error)")
    }
  }

  @Test func multipleInterceptorsRunInOrder() async throws {
    let c1 = Counter()
    let c2 = Counter()
    let router = buildRouter(interceptors: [
      CountingInterceptor(counter: c1),
      CountingInterceptor(counter: c2),
    ])
    let ctx = TestCallContext(methodId: getWidgetId)
    let req = EchoRequest(value: "test")
    _ = try await router.unary(methodId: getWidgetId, payload: req.serializedData(), ctx: ctx)
    #expect(await c1.value == 1)
    #expect(await c2.value == 1)
  }

  @Test func rejectingInterceptorStopsChain() async throws {
    let counter = Counter()
    let router = buildRouter(interceptors: [
      RejectingInterceptor(),
      CountingInterceptor(counter: counter),
    ])
    let ctx = TestCallContext(methodId: getWidgetId)
    let req = EchoRequest(value: "test")
    do {
      _ = try await router.unary(methodId: getWidgetId, payload: req.serializedData(), ctx: ctx)
    } catch {}
    #expect(await counter.value == 0)
  }

  @Test func interceptorAffectsServerStream() async throws {
    let router = buildRouter(interceptors: [RejectingInterceptor()])
    let ctx = TestCallContext(methodId: listWidgetsId)
    let req = CountRequest(n: 3)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.serverStream(
        methodId: listWidgetsId, payload: req.serializedData(), ctx: ctx)
    }
  }

  @Test func interceptorAffectsClientStream() async {
    let router = buildRouter(interceptors: [RejectingInterceptor()])
    let ctx = TestCallContext(methodId: uploadWidgetsId)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.clientStream(methodId: uploadWidgetsId, ctx: ctx)
    }
  }

  @Test func interceptorAffectsDuplexStream() async {
    let router = buildRouter(interceptors: [RejectingInterceptor()])
    let ctx = TestCallContext(methodId: syncWidgetsId)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.duplexStream(methodId: syncWidgetsId, ctx: ctx)
    }
  }

  @Test func interceptorFiltersSpecificMethods() async throws {
    let filter = MethodFilterInterceptor(allowedMethods: [getWidgetId])
    let router = buildRouter(interceptors: [filter])

    let ctx1 = TestCallContext(methodId: getWidgetId)
    let req = EchoRequest(value: "allowed")
    let res = try await router.unary(
      methodId: getWidgetId, payload: req.serializedData(), ctx: ctx1)
    let echo = try EchoResponse.decode(from: res)
    #expect(echo.value == "allowed")

    let ctx2 = TestCallContext(methodId: listWidgetsId)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.serverStream(
        methodId: listWidgetsId, payload: CountRequest(n: 1).serializedData(), ctx: ctx2)
    }
  }
}
