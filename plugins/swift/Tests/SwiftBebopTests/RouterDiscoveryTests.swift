import Testing

@testable import SwiftBebop

@Suite struct RouterDiscoveryTests {
  @Test func returnsServiceInfo() async throws {
    let router = buildRouter()
    let ctx = TestCallContext()
    let bytes = try await router.unary(methodId: 0, payload: [], ctx: ctx)
    let response = try DiscoveryResponse.decode(from: bytes)
    #expect(response.services.count == 1)
    #expect(response.services[0].name == "WidgetService")
    #expect(response.services[0].methods.count == 4)
  }

  @Test func disabledDiscoveryThrows() async {
    let router = buildRouter(discoveryEnabled: false)
    let ctx = TestCallContext()
    await #expect(throws: BebopRpcError.self) {
      _ = try await router.unary(methodId: 0, payload: [], ctx: ctx)
    }
  }
}
