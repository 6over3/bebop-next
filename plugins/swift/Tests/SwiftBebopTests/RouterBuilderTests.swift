import Testing

@testable import SwiftBebop

@Suite struct RouterBuilderTests {
    @Test func buildEmptyRouter() {
        let builder = BebopRouterBuilder()
        let router = builder.build()
        #expect(router.config.discoveryEnabled)
        #expect(router.methodType(for: getWidgetId) == nil)
    }

    @Test func disableDiscovery() {
        let builder = BebopRouterBuilder()
        builder.config.discoveryEnabled = false
        let router = builder.build()
        #expect(!router.config.discoveryEnabled)
    }

    @Test func registeredMethodsAreAccessible() {
        let router = buildRouter()
        #expect(router.methodType(for: getWidgetId) == .unary)
        #expect(router.methodType(for: listWidgetsId) == .serverStream)
        #expect(router.methodType(for: uploadWidgetsId) == .clientStream)
        #expect(router.methodType(for: syncWidgetsId) == .duplexStream)
    }
}
