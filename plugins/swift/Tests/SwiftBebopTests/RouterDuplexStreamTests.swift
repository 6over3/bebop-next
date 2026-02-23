import Testing

@testable import SwiftBebop

@Suite struct RouterDuplexStreamTests {
  @Test func echoesBidirectionally() async throws {
    let router = buildRouter()
    let ctx = RpcContext(methodId: syncWidgetsId, metadata: [:], deadline: nil)
    let (send, finish, responses) = try await router.duplexStream(methodId: syncWidgetsId, ctx: ctx)

    try await send(EchoRequest(value: "x").serializedData())
    try await send(EchoRequest(value: "y").serializedData())
    try await finish()

    var results: [String] = []
    for try await element in responses {
      let res = try EchoResponse.decode(from: element.bytes)
      results.append(res.value)
    }
    #expect(results == ["x", "y"])
  }

  @Test func wrongMethodTypeThrows() async {
    let router = buildRouter()
    let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.duplexStream(methodId: getWidgetId, ctx: ctx)
    }
  }
}
