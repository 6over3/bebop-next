import Testing

@testable import SwiftBebop

struct TestCallContext: CallContext, @unchecked Sendable {
  let methodId: UInt32
  let requestMetadata: [String: String]
  let deadline: BebopTimestamp?
  var isCancelled: Bool = false

  init(
    methodId: UInt32 = 0,
    metadata: [String: String] = [:],
    deadline: BebopTimestamp? = nil
  ) {
    self.methodId = methodId
    self.requestMetadata = metadata
    self.deadline = deadline
  }

  func setResponseMetadata(_ key: String, _ value: String) {}
}

struct WidgetHandler: WidgetServiceHandler {
  func getWidget(
    _ request: EchoRequest, context: some CallContext
  ) async throws -> EchoResponse {
    EchoResponse(value: request.value)
  }

  func listWidgets(
    _ request: CountRequest, context: some CallContext
  ) async throws -> AsyncThrowingStream<CountResponse, Error> {
    AsyncThrowingStream { c in
      for i in 0..<request.n {
        c.yield(CountResponse(i: i))
      }
      c.finish()
    }
  }

  func uploadWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>,
    context: some CallContext
  ) async throws -> EchoResponse {
    var parts: [String] = []
    for try await req in requests {
      parts.append(req.value)
    }
    return EchoResponse(value: parts.joined(separator: ","))
  }

  func syncWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>,
    context: some CallContext
  ) async throws -> AsyncThrowingStream<EchoResponse, Error> {
    let (stream, continuation) = AsyncThrowingStream.makeStream(of: EchoResponse.self)
    let task = Task {
      for try await req in requests {
        continuation.yield(EchoResponse(value: req.value))
      }
      continuation.finish()
    }
    continuation.onTermination = { _ in task.cancel() }
    return stream
  }
}

struct LoopbackChannel: BebopChannel {
  let router: BebopRouter<TestCallContext>

  func unary(method: UInt32, request: [UInt8], options: CallOptions) async throws -> [UInt8] {
    let ctx = TestCallContext(
      methodId: method, metadata: options.metadata, deadline: options.deadline)
    return try await router.unary(methodId: method, payload: request, ctx: ctx)
  }

  func serverStream(method: UInt32, request: [UInt8], options: CallOptions) async throws
    -> AsyncThrowingStream<[UInt8], Error>
  {
    let ctx = TestCallContext(
      methodId: method, metadata: options.metadata, deadline: options.deadline)
    return try await router.serverStream(methodId: method, payload: request, ctx: ctx)
  }

  func clientStream(method: UInt32, options: CallOptions) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> [UInt8]
  ) {
    let ctx = TestCallContext(
      methodId: method, metadata: options.metadata, deadline: options.deadline)
    return try await router.clientStream(methodId: method, ctx: ctx)
  }

  func duplexStream(method: UInt32, options: CallOptions) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Void,
    responses: AsyncThrowingStream<[UInt8], Error>
  ) {
    let ctx = TestCallContext(
      methodId: method, metadata: options.metadata, deadline: options.deadline)
    return try await router.duplexStream(methodId: method, ctx: ctx)
  }
}

let getWidgetId = WidgetService.Method.getWidget.rawValue
let listWidgetsId = WidgetService.Method.listWidgets.rawValue
let uploadWidgetsId = WidgetService.Method.uploadWidgets.rawValue
let syncWidgetsId = WidgetService.Method.syncWidgets.rawValue

func buildRouter(
  handler: some WidgetServiceHandler = WidgetHandler(),
  interceptors: [any BebopInterceptor] = [],
  discoveryEnabled: Bool = true
) -> BebopRouter<TestCallContext> {
  let builder = BebopRouterBuilder<TestCallContext>()
  builder.discoveryEnabled = discoveryEnabled
  for i in interceptors { builder.addInterceptor(i) }
  builder.register(widgetService: handler)
  return builder.build()
}

func buildChannel(
  handler: some WidgetServiceHandler = WidgetHandler(),
  interceptors: [any BebopInterceptor] = [],
  discoveryEnabled: Bool = true
) -> LoopbackChannel {
  LoopbackChannel(
    router: buildRouter(
      handler: handler, interceptors: interceptors, discoveryEnabled: discoveryEnabled))
}

actor Counter {
  var value = 0
  func increment() { value += 1 }
}
