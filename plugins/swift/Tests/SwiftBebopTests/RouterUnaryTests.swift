import Testing

@testable import SwiftBebop

@Suite struct RouterUnaryTests {
  @Test func echoRoundTrip() async throws {
    let router = buildRouter()
    let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
    let req = EchoRequest(value: "hello")
    let resBytes = try await router.unary(
      methodId: getWidgetId, payload: req.serializedData(), ctx: ctx)
    let res = try EchoResponse.decode(from: resBytes)
    #expect(res.value == "hello")
  }

  @Test func unknownMethodThrowsNotFound() async {
    let router = buildRouter()
    let ctx = RpcContext(methodId: 0xDEAD, metadata: [:], deadline: nil)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.unary(methodId: 0xDEAD, payload: [], ctx: ctx)
    }
  }

  @Test func wrongMethodTypeThrowsUnimplemented() async {
    let router = buildRouter()
    let ctx = RpcContext(methodId: listWidgetsId, metadata: [:], deadline: nil)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.unary(methodId: listWidgetsId, payload: [], ctx: ctx)
    }
  }

  @Test func methodTypeLookup() {
    let router = buildRouter()
    #expect(router.methodType(for: getWidgetId) == .unary)
    #expect(router.methodType(for: listWidgetsId) == .serverStream)
    #expect(router.methodType(for: uploadWidgetsId) == .clientStream)
    #expect(router.methodType(for: syncWidgetsId) == .duplexStream)
    #expect(router.methodType(for: 0xFFFF) == nil)
  }
}
