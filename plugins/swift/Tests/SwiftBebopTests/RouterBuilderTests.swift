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

    @Test func acceptsConfigurationAtInitialization() {
        let config = BebopRouterConfig(
            discoveryEnabled: false,
            decodeLimits: BebopDecodeLimits(maxCollectionElements: 12, maxDepth: 3)
        )

        let router = BebopRouterBuilder(config: config).build()

        #expect(!router.config.discoveryEnabled)
        #expect(router.config.decodeLimits == config.decodeLimits)
    }

    @Test func registeredMethodsAreAccessible() {
        let router = buildRouter()
        #expect(router.methodType(for: getWidgetId) == .unary)
        #expect(router.methodType(for: listWidgetsId) == .serverStream)
        #expect(router.methodType(for: uploadWidgetsId) == .clientStream)
        #expect(router.methodType(for: syncWidgetsId) == .duplexStream)
    }

    @Test func routerBindsDecodeLimitsToTheCallContext() async throws {
        let builder = BebopRouterBuilder()
        builder.register(widgetService: WidgetHandler())
        builder.config.decodeLimits = BebopDecodeLimits(
            maxCollectionElements: 7,
            maxDepth: 4
        )
        let router = builder.build()
        let context = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)

        _ = try await router.unary(
            methodId: getWidgetId,
            payload: EchoRequest(value: "limits").encode(),
            ctx: context
        )

        #expect(context.decodeLimits == builder.config.decodeLimits)
    }

    @Test func routerRejectsMismatchedContextMethodId() async {
        let router = buildRouter()
        let context = RpcContext(methodId: listWidgetsId, metadata: [:], deadline: nil)

        await #expect(throws: BebopRpcError.self) {
            _ = try await router.unary(
                methodId: getWidgetId,
                payload: EchoRequest(value: "wrong context").encode(),
                ctx: context
            )
        }
    }
}
