import Testing

@testable import SwiftBebop

@Suite struct RouterServerStreamTests {
  @Test func countsUp() async throws {
    let router = buildRouter()
    let ctx = RpcContext(methodId: listWidgetsId, metadata: [:], deadline: nil)
    let req = CountRequest(n: 3)
    let stream = try await router.serverStream(
      methodId: listWidgetsId, payload: req.serializedData(), ctx: ctx)
    var results: [UInt32] = []
    for try await element in stream {
      let resp = try CountResponse.decode(from: element.bytes)
      results.append(resp.i)
    }
    #expect(results == [0, 1, 2])
  }

  @Test func emptyStream() async throws {
    let router = buildRouter()
    let ctx = RpcContext(methodId: listWidgetsId, metadata: [:], deadline: nil)
    let req = CountRequest(n: 0)
    let stream = try await router.serverStream(
      methodId: listWidgetsId, payload: req.serializedData(), ctx: ctx)
    var count = 0
    for try await _ in stream { count += 1 }
    #expect(count == 0)
  }

  @Test func unknownMethodThrows() async {
    let router = buildRouter()
    let ctx = RpcContext(methodId: 0xDEAD, metadata: [:], deadline: nil)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.serverStream(methodId: 0xDEAD, payload: [], ctx: ctx)
    }
  }

  @Test func wrongMethodTypeThrows() async {
    let router = buildRouter()
    let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.serverStream(methodId: getWidgetId, payload: [], ctx: ctx)
    }
  }
}
