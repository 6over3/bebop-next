import Testing

@testable import SwiftBebop

@Suite struct WidgetServiceClientTests {
    @Test func unaryGetWidget() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let response = try await client.getWidget(EchoRequest(value: "hello"))
        #expect(response.value.value == "hello")
    }

    @Test func unaryGetWidgetDeconstructed() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let response = try await client.getWidget(value: "decon")
        #expect(response.value.value == "decon")
    }

    @Test func serverStreamListWidgets() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(CountRequest(n: 3))
        var results: [UInt32] = []
        for try await item in stream {
            results.append(item.i)
        }
        #expect(results == [0, 1, 2])
    }

    @Test func serverStreamListWidgetsDeconstructed() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(n: 4)
        var results: [UInt32] = []
        for try await item in stream {
            results.append(item.i)
        }
        #expect(results == [0, 1, 2, 3])
    }

    @Test func clientStreamUploadWidgets() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let response = try await client.uploadWidgets { send in
            try await send(EchoRequest(value: "a"))
            try await send(EchoRequest(value: "b"))
        }
        #expect(response.value.value == "a,b")
    }

    @Test func duplexStreamSyncWidgets() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        try await client.syncWidgets { send, finish, responses in
            try await send(EchoRequest(value: "x"))
            try await send(EchoRequest(value: "y"))
            try await finish()
            var results: [String] = []
            for try await item in responses {
                results.append(item.value)
            }
            #expect(results == ["x", "y"])
        }
    }
}
