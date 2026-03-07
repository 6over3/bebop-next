import Testing

@testable import SwiftBebop

@Suite struct RouterBatchTests {
    @Test func singleUnaryCall() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "hi").serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        #expect(response.results.count == 1)
        #expect(response.results[0].callId == 0)

        guard case let .success(s) = response.results[0].outcome else {
            Issue.record("expected success")
            return
        }
        let echo = try EchoResponse.decode(from: s.payloads[0])
        #expect(echo.value == "hi")
    }

    @Test func multipleIndependentCalls() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "a").serializedData(), inputFrom: -1
                ),
                BatchCall(
                    callId: 1, methodId: getWidgetId,
                    payload: EchoRequest(value: "b").serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        #expect(response.results.count == 2)

        let outcomes = Dictionary(
            response.results.map { ($0.callId, $0.outcome) },
            uniquingKeysWith: { a, _ in a }
        )

        guard case let .success(s1) = outcomes[0] else {
            Issue.record("expected success for call 0")
            return
        }
        #expect(try EchoResponse.decode(from: s1.payloads[0]).value == "a")

        guard case let .success(s2) = outcomes[1] else {
            Issue.record("expected success for call 1")
            return
        }
        #expect(try EchoResponse.decode(from: s2.payloads[0]).value == "b")
    }

    @Test func dependentCallForwardsPayload() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "hello").serializedData(), inputFrom: -1
                ),
                BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        #expect(response.results.count == 2)
    }

    @Test func serverStreamInBatch() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: listWidgetsId,
                    payload: CountRequest(n: 3).serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)

        guard case let .success(s) = response.results[0].outcome else {
            Issue.record("expected success")
            return
        }
        #expect(s.payloads.count == 3)
        for (i, payload) in s.payloads.enumerated() {
            let resp = try CountResponse.decode(from: payload)
            #expect(resp.i == UInt32(i))
        }
    }

    @Test func unknownMethodInBatchReturnsError() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(callId: 0, methodId: 0xDEAD, payload: [], inputFrom: -1),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        guard case let .error(e) = response.results[0].outcome else {
            Issue.record("expected error")
            return
        }
        #expect(e.code == .notFound)
    }

    @Test func emptyBatchReturnsEmptyResponse() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(calls: [], metadata: [:])
        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        #expect(response.results.isEmpty)
    }

    @Test func duplicateCallIdThrows() async {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "a").serializedData(), inputFrom: -1
                ),
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "b").serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        await #expect(throws: BebopRpcError.self) {
            _ = try await router.unary(
                methodId: 1, payload: req.serializedData(), ctx: ctx
            )
        }
    }

    @Test func negativeCallIdThrows() async {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(callId: -1, methodId: getWidgetId, payload: [], inputFrom: -1),
            ],
            metadata: [:]
        )

        await #expect(throws: BebopRpcError.self) {
            _ = try await router.unary(
                methodId: 1, payload: req.serializedData(), ctx: ctx
            )
        }
    }

    @Test func invalidDependencyThrows() async {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(callId: 0, methodId: getWidgetId, payload: [], inputFrom: 99),
            ],
            metadata: [:]
        )

        await #expect(throws: BebopRpcError.self) {
            _ = try await router.unary(
                methodId: 1, payload: req.serializedData(), ctx: ctx
            )
        }
    }

    @Test func handlerErrorPreservesCodeAndDetail() async throws {
        struct FailingHandler: WidgetServiceHandler {
            func getWidget(_: EchoRequest, context _: RpcContext) async throws -> EchoResponse {
                throw BebopRpcError(code: .internal, detail: "boom")
            }

            func listWidgets(_: CountRequest, context _: RpcContext) async throws
                -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> EchoResponse { fatalError() }
            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
        }

        let router = buildRouter(handler: FailingHandler())
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "fail").serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        guard case let .error(e) = response.results[0].outcome else {
            Issue.record("expected error outcome")
            return
        }
        #expect(e.code == .internal)
        #expect(e.detail == "boom")
    }

    @Test func handlerErrorCascadesToDependents() async throws {
        struct FailingHandler: WidgetServiceHandler {
            func getWidget(_: EchoRequest, context _: RpcContext) async throws -> EchoResponse {
                throw BebopRpcError(code: .permissionDenied, detail: "nope")
            }

            func listWidgets(_: CountRequest, context _: RpcContext) async throws
                -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> EchoResponse { fatalError() }
            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
        }

        let router = buildRouter(handler: FailingHandler())
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "x").serializedData(), inputFrom: -1
                ),
                BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)

        let outcomes = Dictionary(
            response.results.map { ($0.callId, $0.outcome) },
            uniquingKeysWith: { a, _ in a }
        )

        guard case let .error(e0) = outcomes[0] else {
            Issue.record("call 0 should error")
            return
        }
        #expect(e0.code == .permissionDenied)
        #expect(e0.detail == "nope")

        guard case .error = outcomes[1] else {
            Issue.record("call 1 should cascade")
            return
        }
    }

    @Test func failedDependencyCascades() async throws {
        let router = buildRouter()
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(callId: 0, methodId: 0xDEAD, payload: [], inputFrom: -1),
                BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)

        let outcomes = Dictionary(
            response.results.map { ($0.callId, $0.outcome) },
            uniquingKeysWith: { a, _ in a }
        )

        guard case .error = outcomes[0] else {
            Issue.record("call 0 should error")
            return
        }
        guard case .error = outcomes[1] else {
            Issue.record("call 1 should cascade error")
            return
        }
    }

    // MARK: - Response metadata

    @Test func singleCallMetadata() async throws {
        struct MetadataHandler: WidgetServiceHandler {
            func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
                context.setResponseMetadata("trace-id", "abc-123")
                return EchoResponse(value: request.value)
            }

            func listWidgets(_: CountRequest, context _: RpcContext) async throws
                -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> EchoResponse { fatalError() }
            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
        }

        let router = buildRouter(handler: MetadataHandler())
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "hi").serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        let results = BatchResults(response)
        let ref = CallRef<EchoResponse>(callId: 0)

        let meta = try results.metadata(for: ref)
        #expect(meta["trace-id"] == "abc-123")
    }

    @Test func concurrentCallMetadataIsolation() async throws {
        struct TaggingHandler: WidgetServiceHandler {
            func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
                context.setResponseMetadata("source", request.value)
                return EchoResponse(value: request.value)
            }

            func listWidgets(_: CountRequest, context _: RpcContext) async throws
                -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> EchoResponse { fatalError() }
            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
        }

        let router = buildRouter(handler: TaggingHandler())
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "alpha").serializedData(), inputFrom: -1
                ),
                BatchCall(
                    callId: 1, methodId: getWidgetId,
                    payload: EchoRequest(value: "beta").serializedData(), inputFrom: -1
                ),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        let results = BatchResults(response)

        let ref0 = CallRef<EchoResponse>(callId: 0)
        let ref1 = CallRef<EchoResponse>(callId: 1)

        let meta0 = try results.metadata(for: ref0)
        let meta1 = try results.metadata(for: ref1)
        #expect(meta0["source"] == "alpha")
        #expect(meta1["source"] == "beta")
    }

    @Test func dependentCallMetadataIsolation() async throws {
        struct TaggingHandler: WidgetServiceHandler {
            func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
                context.setResponseMetadata("step", request.value)
                return EchoResponse(value: request.value)
            }

            func listWidgets(_: CountRequest, context _: RpcContext) async throws
                -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> EchoResponse { fatalError() }
            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
        }

        let router = buildRouter(handler: TaggingHandler())
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "first").serializedData(), inputFrom: -1
                ),
                BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        let results = BatchResults(response)

        let ref0 = CallRef<EchoResponse>(callId: 0)
        let ref1 = CallRef<EchoResponse>(callId: 1)

        let meta0 = try results.metadata(for: ref0)
        let meta1 = try results.metadata(for: ref1)
        #expect(meta0["step"] == "first")
        #expect(meta1["step"] == "first")
        #expect(meta0.count == 1)
        #expect(meta1.count == 1)
    }

    @Test func inputFromPropagatesUpstreamResponseMetadata() async throws {
        struct MetaPropHandler: WidgetServiceHandler {
            func getWidget(_ request: EchoRequest, context: RpcContext) async throws -> EchoResponse {
                if request.value == "upstream" {
                    context.setResponseMetadata("widget-id", "42")
                }
                // Downstream call can read propagated metadata
                if let widgetId = context.metadata["widget-id"] {
                    context.setResponseMetadata("saw-widget-id", widgetId)
                }
                return EchoResponse(value: request.value)
            }

            func listWidgets(_: CountRequest, context _: RpcContext) async throws
                -> AsyncThrowingStream<CountResponse, Error> { fatalError() }
            func uploadWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> EchoResponse { fatalError() }
            func syncWidgets(
                _: AsyncThrowingStream<EchoRequest, Error>, context _: RpcContext
            ) async throws -> AsyncThrowingStream<EchoResponse, Error> { fatalError() }
        }

        let router = buildRouter(handler: MetaPropHandler())
        let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

        let req = BatchRequest(
            calls: [
                BatchCall(
                    callId: 0, methodId: getWidgetId,
                    payload: EchoRequest(value: "upstream").serializedData(), inputFrom: -1
                ),
                BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
            ],
            metadata: [:]
        )

        let responseBytes = try await router.unary(
            methodId: 1, payload: req.serializedData(), ctx: ctx
        )
        let response = try BatchResponse.decode(from: responseBytes)
        let results = BatchResults(response)

        let ref1 = CallRef<EchoResponse>(callId: 1)
        let meta1 = try results.metadata(for: ref1)
        #expect(meta1["saw-widget-id"] == "42")
    }
}
