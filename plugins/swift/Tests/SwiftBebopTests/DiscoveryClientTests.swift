import Testing

@testable import SwiftBebop

@Suite struct DiscoveryClientTests {
    @Test func listsServices() async throws {
        let client = DiscoveryClient(channel: buildChannel())
        let response = try await client.listServices()
        #expect(response.services.count == 1)
        #expect(response.services[0].name == "WidgetService")
        #expect(response.services[0].methods.count == 4)
    }

    @Test func disabledDiscoveryThrows() async {
        let client = DiscoveryClient(channel: buildChannel(discoveryEnabled: false))
        await #expect(throws: BebopRpcError.self) {
            _ = try await client.listServices()
        }
    }
}
