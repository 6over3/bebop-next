import Synchronization
import Testing

@testable import SwiftBebop

struct WidgetHandler: WidgetServiceHandler {
  func getWidget(
    _ request: EchoRequest, context: RpcContext
  ) async throws -> EchoResponse {
    EchoResponse(value: request.value)
  }

  func listWidgets(
    _ request: CountRequest, context: RpcContext
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
    context: RpcContext
  ) async throws -> EchoResponse {
    var parts: [String] = []
    for try await req in requests {
      parts.append(req.value)
    }
    return EchoResponse(value: parts.joined(separator: ","))
  }

  func syncWidgets(
    _ requests: AsyncThrowingStream<EchoRequest, Error>,
    context: RpcContext
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
  typealias Metadata = Void

  let router: BebopRouter

  func unary(method: UInt32, request: [UInt8], context: RpcContext) async throws -> Response<[UInt8], Void> {
    let serverCtx = context.binding(to: method)
    let data = try await router.unary(methodId: method, payload: request, ctx: serverCtx)
    return Response(value: data, metadata: ())
  }

  func serverStream(method: UInt32, request: [UInt8], context: RpcContext) async throws
    -> StreamResponse<[UInt8], Void>
  {
    let serverCtx = context.binding(to: method)
    let stream = try await router.serverStream(methodId: method, payload: request, ctx: serverCtx)
    return StreamResponse(stream: stream, trailing: { () })
  }

  func clientStream(method: UInt32, context: RpcContext) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Response<[UInt8], Void>
  ) {
    let serverCtx = context.binding(to: method)
    let (send, rawFinish) = try await router.clientStream(methodId: method, ctx: serverCtx)
    return (send: send, finish: { Response(value: try await rawFinish(), metadata: ()) })
  }

  func duplexStream(method: UInt32, context: RpcContext) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Void,
    responses: StreamResponse<[UInt8], Void>
  ) {
    let serverCtx = context.binding(to: method)
    let (send, finish, responses) = try await router.duplexStream(methodId: method, ctx: serverCtx)
    return (send: send, finish: finish, responses: StreamResponse(stream: responses, trailing: { () }))
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
) -> BebopRouter {
  let builder = BebopRouterBuilder()
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
