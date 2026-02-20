import Testing

@testable import SwiftBebop

@Suite struct EndToEndTests {
  @Test func fullUnaryFlow() async throws {
    let client = WidgetServiceClient(channel: buildChannel())
    let response = try await client.getWidget(value: "e2e")
    #expect(response.value.value == "e2e")
  }

  @Test func fullServerStreamFlow() async throws {
    let client = WidgetServiceClient(channel: buildChannel())
    let stream = try await client.listWidgets(n: 5)
    var items: [UInt32] = []
    for try await item in stream {
      items.append(item.i)
    }
    #expect(items == [0, 1, 2, 3, 4])
  }

  @Test func fullClientStreamFlow() async throws {
    let client = WidgetServiceClient(channel: buildChannel())
    let response = try await client.uploadWidgets { send in
      try await send(EchoRequest(value: "hello"))
      try await send(EchoRequest(value: "world"))
    }
    #expect(response.value.value == "hello,world")
  }

  @Test func fullDuplexStreamFlow() async throws {
    let client = WidgetServiceClient(channel: buildChannel())
    try await client.syncWidgets { send, finish, responses in
      try await send(EchoRequest(value: "m1"))
      try await send(EchoRequest(value: "m2"))
      try await finish()
      var results: [String] = []
      for try await item in responses {
        results.append(item.value)
      }
      #expect(results == ["m1", "m2"])
    }
  }

  @Test func discoveryThroughChannel() async throws {
    let discovery = DiscoveryClient(channel: buildChannel())
    let response = try await discovery.listServices()
    #expect(response.services.count == 1)

    let svc = response.services[0]
    #expect(svc.name == "WidgetService")
    let methodNames = svc.methods.map(\.name)
    #expect(methodNames.contains("GetWidget"))
    #expect(methodNames.contains("ListWidgets"))
    #expect(methodNames.contains("UploadWidgets"))
    #expect(methodNames.contains("SyncWidgets"))
  }

  @Test func batchThroughChannel() async throws {
    let batch = buildChannel().makeBatch()
    let echoRef = batch.widgetService.getWidget(value: "batch-e2e")
    let streamRef = batch.widgetService.listWidgets(n: 2)
    let results = try await batch.execute()
    #expect(try results[echoRef].value == "batch-e2e")
    #expect(try results[streamRef].map(\.i) == [0, 1])
  }

  @Test func interceptorInEndToEnd() async throws {
    struct TagInterceptor: BebopInterceptor {
      func intercept(
        methodId: UInt32, ctx: RpcContext,
        proceed: @Sendable () async throws -> Void
      ) async throws {
        try await proceed()
      }
    }

    let client = WidgetServiceClient(channel: buildChannel(interceptors: [TagInterceptor()]))
    let response = try await client.getWidget(value: "intercepted")
    #expect(response.value.value == "intercepted")
  }

  @Test func errorFromHandlerPropagates() async {
    struct FailHandler: WidgetServiceHandler {
      func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
        throw BebopRpcError(code: .internal, detail: "boom")
      }
      func listWidgets(_ request: CountRequest, context: RpcContext) async throws
        -> AsyncThrowingStream<CountResponse, Error>
      { fatalError() }
      func uploadWidgets(
        _ requests: AsyncThrowingStream<EchoRequest, Error>,
        context: RpcContext
      ) async throws -> EchoResponse { fatalError() }
      func syncWidgets(
        _ requests: AsyncThrowingStream<EchoRequest, Error>,
        context: RpcContext
      ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
    }

    let client = WidgetServiceClient(channel: buildChannel(handler: FailHandler()))
    do {
      _ = try await client.getWidget(value: "fail").value
      Issue.record("should have thrown")
    } catch let err as BebopRpcError {
      #expect(err.code == .internal)
      #expect(err.detail == "boom")
    } catch {
      Issue.record("unexpected error: \(error)")
    }
  }
}
