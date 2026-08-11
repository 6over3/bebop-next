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
        let upload = try await client.uploadWidgets()
        try await upload.send(EchoRequest(value: "a"))
        try await upload.send(EchoRequest(value: "b"))
        let response = try await upload.finish()
        #expect(response.value.value == "a,b")
    }

    @Test func duplexStreamSyncWidgets() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        try await sync.send(EchoRequest(value: "x"))
        try await sync.send(EchoRequest(value: "y"))
        try await sync.finish()
        var results: [String] = []
        for try await item in sync {
            results.append(item.value)
        }
        #expect(results == ["x", "y"])
    }

    @Test func duplexHandlerMayAwaitARequestBeforeProducingResponses() async throws {
        struct AwaitingHandler: WidgetServiceHandler {
            func getWidget(
                _ request: EchoRequest.View, context _: RpcContext
            ) async throws -> EchoResponse {
                EchoResponse(value: request.value.string)
            }

            func listWidgets(
                _: CountRequest.View, context _: RpcContext
            ) async throws -> AsyncThrowingStream<CountResponse, Error> {
                AsyncThrowingStream { $0.finish() }
            }

            func uploadWidgets(
                _ requests: AsyncThrowingStream<EchoRequest.View, Error>,
                context _: RpcContext
            ) async throws -> EchoResponse {
                var iterator = requests.makeAsyncIterator()
                guard let first = try await iterator.next() else {
                    throw BebopRpcError(code: .invalidArgument)
                }
                return EchoResponse(value: first.value.string)
            }

            func syncWidgets(
                _ requests: AsyncThrowingStream<EchoRequest.View, Error>,
                context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> {
                var iterator = requests.makeAsyncIterator()
                let first = try await iterator.next()
                return AsyncThrowingStream { continuation in
                    if let first {
                        continuation.yield(EchoResponse(value: first.value.string))
                    }
                    continuation.finish()
                }
            }
        }

        let client = WidgetServiceClient(channel: buildChannel(handler: AwaitingHandler()))
        let sync = try await client.syncWidgets()
        try await sync.send(EchoRequest(value: "ready"))
        try await sync.finish()

        var iterator = sync.responses.makeAsyncIterator()
        #expect(try await iterator.next()?.value == "ready")
        #expect(try await iterator.next() == nil)
    }

    @Test func duplexHandlerSetupErrorsReachTheResponseIterator() async throws {
        struct FailingHandler: WidgetServiceHandler {
            func getWidget(
                _: EchoRequest.View, context _: RpcContext
            ) async throws -> EchoResponse { throw BebopRpcError(code: .unimplemented) }

            func listWidgets(
                _: CountRequest.View, context _: RpcContext
            ) async throws -> AsyncThrowingStream<CountResponse, Error> {
                throw BebopRpcError(code: .unimplemented)
            }

            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest.View, Error>,
                context _: RpcContext
            ) async throws -> EchoResponse { throw BebopRpcError(code: .unimplemented) }

            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest.View, Error>,
                context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> {
                throw BebopRpcError(code: .permissionDenied)
            }
        }

        let client = WidgetServiceClient(channel: buildChannel(handler: FailingHandler()))
        let sync = try await client.syncWidgets()
        var iterator = sync.makeAsyncIterator()

        await #expect(throws: BebopRpcError.self) {
            _ = try await iterator.next()
        }
    }
}
