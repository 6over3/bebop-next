import Testing

@testable import SwiftBebop

@Suite struct InterceptorTests {
    struct CountingInterceptor: BebopInterceptor, @unchecked Sendable {
        let counter: Counter

        func intercept<Value: Sendable>(
            methodId _: UInt32, ctx _: RpcContext,
            proceed: @Sendable () async throws -> Value
        ) async throws -> Value {
            await counter.increment()
            return try await proceed()
        }
    }

    struct RejectingInterceptor: BebopInterceptor {
        func intercept<Value: Sendable>(
            methodId _: UInt32, ctx _: RpcContext,
            proceed _: @Sendable () async throws -> Value
        ) async throws -> Value {
            throw BebopRpcError(code: .permissionDenied, detail: "rejected")
        }
    }

    struct MethodFilterInterceptor: BebopInterceptor {
        let allowedMethods: Set<UInt32>

        func intercept<Value: Sendable>(
            methodId: UInt32, ctx _: RpcContext,
            proceed: @Sendable () async throws -> Value
        ) async throws -> Value {
            guard allowedMethods.contains(methodId) else {
                throw BebopRpcError(code: .permissionDenied, detail: "method \(methodId) blocked")
            }
            return try await proceed()
        }
    }

    @Test func interceptorRunsBeforeHandler() async throws {
        let counter = Counter()
        let router = buildRouter(interceptors: [CountingInterceptor(counter: counter)])
        let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
        let req = EchoRequest(value: "test")
        _ = try await router.unary(methodId: getWidgetId, payload: req.encode(), ctx: ctx)
        let count = await counter.value
        #expect(count == 1)
    }

    @Test func interceptorCanReject() async throws {
        let router = buildRouter(interceptors: [RejectingInterceptor()])
        let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
        let req = EchoRequest(value: "test")
        do {
            _ = try await router.unary(methodId: getWidgetId, payload: req.encode(), ctx: ctx)
            Issue.record("should have thrown")
        } catch let error as BebopRpcError {
            #expect(error.code == .permissionDenied)
            #expect(error.detail == "rejected")
        } catch {
            Issue.record("unexpected error: \(error)")
        }
    }

    @Test func multipleInterceptorsRunInOrder() async throws {
        let c1 = Counter()
        let c2 = Counter()
        let router = buildRouter(interceptors: [
            CountingInterceptor(counter: c1),
            CountingInterceptor(counter: c2),
        ])
        let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
        let req = EchoRequest(value: "test")
        _ = try await router.unary(methodId: getWidgetId, payload: req.encode(), ctx: ctx)
        #expect(await c1.value == 1)
        #expect(await c2.value == 1)
    }

    @Test func rejectingInterceptorStopsChain() async throws {
        let counter = Counter()
        let router = buildRouter(interceptors: [
            RejectingInterceptor(),
            CountingInterceptor(counter: counter),
        ])
        let ctx = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
        let req = EchoRequest(value: "test")
        do {
            _ = try await router.unary(methodId: getWidgetId, payload: req.encode(), ctx: ctx)
        } catch {}
        #expect(await counter.value == 0)
    }

    @Test func interceptorAffectsServerStream() async throws {
        let router = buildRouter(interceptors: [RejectingInterceptor()])
        let ctx = RpcContext(methodId: listWidgetsId, metadata: [:], deadline: nil)
        let req = CountRequest(n: 3)
        await #expect(throws: BebopRpcError.self) {
            _ = try await router.serverStream(
                methodId: listWidgetsId, payload: req.encode(), ctx: ctx
            )
        }
    }

    @Test func interceptorAffectsClientStream() async {
        let router = buildRouter(interceptors: [RejectingInterceptor()])
        let ctx = RpcContext(methodId: uploadWidgetsId, metadata: [:], deadline: nil)
        await #expect(throws: BebopRpcError.self) {
            _ = try await router.clientStream(methodId: uploadWidgetsId, ctx: ctx)
        }
    }

    @Test func interceptorAffectsDuplexStream() async {
        let router = buildRouter(interceptors: [RejectingInterceptor()])
        let ctx = RpcContext(methodId: syncWidgetsId, metadata: [:], deadline: nil)
        await #expect(throws: BebopRpcError.self) {
            _ = try await router.duplexStream(methodId: syncWidgetsId, ctx: ctx)
        }
    }

    @Test func interceptorFiltersSpecificMethods() async throws {
        let filter = MethodFilterInterceptor(allowedMethods: [getWidgetId])
        let router = buildRouter(interceptors: [filter])

        let ctx1 = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
        let req = EchoRequest(value: "allowed")
        let res = try await router.unary(
            methodId: getWidgetId, payload: req.encode(), ctx: ctx1
        )
        let echo = try EchoResponse.decode(from: res)
        #expect(echo.value == "allowed")

        let ctx2 = RpcContext(methodId: listWidgetsId, metadata: [:], deadline: nil)
        await #expect(throws: BebopRpcError.self) {
            _ = try await router.serverStream(
                methodId: listWidgetsId, payload: CountRequest(n: 1).encode(), ctx: ctx2
            )
        }
    }

    private actor Events {
        var values: [String] = []
        func append(_ value: String) {
            values.append(value)
        }
    }

    private struct TracingInterceptor: BebopInterceptor {
        let events: Events
        let name: String

        func intercept<Value: Sendable>(
            methodId _: UInt32, ctx _: RpcContext,
            proceed: @Sendable () async throws -> Value
        ) async throws -> Value {
            await events.append(name + ".before")
            let value = try await proceed()
            await events.append(name + ".after")
            return value
        }
    }

    @Test func interceptorsWrapActualHandlerDispatch() async throws {
        let events = Events()
        let builder = BebopRouterBuilder()
        builder.addInterceptor(TracingInterceptor(events: events, name: "outer"))
        builder.addInterceptor(TracingInterceptor(events: events, name: "inner"))
        builder.register(
            WidgetService.self,
            unary: { _, _, bytes in
                await events.append("handler")
                return bytes
            },
            serverStream: { _, _, _ in
                throw BebopRpcError(code: .unimplemented)
            },
            clientStream: { _, _ in
                throw BebopRpcError(code: .unimplemented)
            },
            duplexStream: { _, _ in
                throw BebopRpcError(code: .unimplemented)
            })
        let router = builder.build()
        let context = RpcContext(methodId: getWidgetId, metadata: [:], deadline: nil)
        let bytes = try await router.unary(
            methodId: getWidgetId, payload: EchoRequest(value: "trace").encode(), ctx: context)
        #expect(try EchoResponse.decode(from: bytes).value == "trace")
        #expect(
            await events.values == [
                "outer.before", "inner.before", "handler", "inner.after", "outer.after",
            ])
    }

    private struct AuthenticationInterceptor: BebopInterceptor {
        func intercept<Value: Sendable>(
            methodId _: UInt32, ctx: RpcContext,
            proceed: @Sendable () async throws -> Value
        ) async throws -> Value {
            ctx[PeerInfoKey.self] = PeerInfo(authInfo: LoopbackAuthInfo(identity: "alice"))
            return try await proceed()
        }
    }

    @Test func authenticationRunsBeforeFutureOwnershipChecks() async throws {
        let router = buildRouter(interceptors: [AuthenticationInterceptor()], futuresEnabled: true)
        let context = RpcContext(methodId: 2, metadata: [:], deadline: nil)
        let request = FutureDispatchRequest(
            methodId: getWidgetId, payload: EchoRequest(value: "authenticated").encode())
        let bytes = try await router.unary(methodId: 2, payload: request.encode(), ctx: context)
        let handle = try FutureHandle.decode(from: bytes)
        context.cancel()
        let resolve = RpcContext(methodId: 3, metadata: [:], deadline: nil)
        let stream = try await router.serverStream(
            methodId: 3, payload: FutureResolveRequest(ids: [handle.id]).encode(), ctx: resolve)
        var iterator = stream.makeAsyncIterator()
        let result = try #require(try await iterator.next())
        #expect(try FutureResult.decode(from: result.bytes).id == handle.id)
    }

}
