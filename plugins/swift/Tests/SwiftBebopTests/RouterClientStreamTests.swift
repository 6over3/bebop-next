import Testing

@testable import SwiftBebop

@Suite struct RouterClientStreamTests {
  @Test func collectsValues() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: uploadWidgetsId)
    let (send, finish) = try await router.clientStream(methodId: uploadWidgetsId, ctx: ctx)
    try await send(EchoRequest(value: "a").serializedData())
    try await send(EchoRequest(value: "b").serializedData())
    try await send(EchoRequest(value: "c").serializedData())
    let resBytes = try await finish()
    let res = try EchoResponse.decode(from: resBytes)
    #expect(res.value == "a,b,c")
  }

  @Test func emptyCollect() async throws {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: uploadWidgetsId)
    let (_, finish) = try await router.clientStream(methodId: uploadWidgetsId, ctx: ctx)
    let resBytes = try await finish()
    let res = try EchoResponse.decode(from: resBytes)
    #expect(res.value == "")
  }

  @Test func wrongMethodTypeThrows() async {
    let router = buildRouter()
    let ctx = TestCallContext(methodId: getWidgetId)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.clientStream(methodId: getWidgetId, ctx: ctx)
    }
  }
}
