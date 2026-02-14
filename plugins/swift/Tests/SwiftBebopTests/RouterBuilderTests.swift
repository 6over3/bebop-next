import Testing

@testable import SwiftBebop

@Suite struct RouterBuilderTests {
  @Test func buildEmptyRouter() {
    let builder = BebopRouterBuilder<TestCallContext>()
    let router = builder.build()
    #expect(router.discoveryEnabled)
    #expect(router.methodType(for: getWidgetId) == nil)
  }

  @Test func disableDiscovery() {
    let builder = BebopRouterBuilder<TestCallContext>()
    builder.discoveryEnabled = false
    let router = builder.build()
    #expect(!router.discoveryEnabled)
  }

  @Test func registeredMethodsAreAccessible() {
    let router = buildRouter()
    #expect(router.methodType(for: getWidgetId) == .unary)
    #expect(router.methodType(for: listWidgetsId) == .serverStream)
    #expect(router.methodType(for: uploadWidgetsId) == .clientStream)
    #expect(router.methodType(for: syncWidgetsId) == .duplexStream)
  }
}
