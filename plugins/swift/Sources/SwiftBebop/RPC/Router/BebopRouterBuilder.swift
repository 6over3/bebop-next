/// Mutable builder for `BebopRouter`.
public final class BebopRouterBuilder {
    public var config = BebopRouterConfig()

    private static let reservedMethodIds: Set<UInt32> = [
        BebopReservedMethod.discovery,
        BebopReservedMethod.batch,
        BebopReservedMethod.dispatch,
        BebopReservedMethod.resolve,
        BebopReservedMethod.cancel,
    ]

    private var methods: [UInt32: MethodRegistration] = [:]
    private var serviceInfos: [ServiceInfo] = []
    private var interceptors: [any BebopInterceptor] = []

    public init() {}

    public func addInterceptor(_ interceptor: some BebopInterceptor) {
        interceptors.append(interceptor)
    }

    public func register<S: BebopServiceDefinition>(
        _: S.Type,
        unary: @escaping @Sendable (S.Method, RpcContext, [UInt8]) async throws -> [UInt8],
        serverStream:
        @escaping @Sendable (S.Method, RpcContext, [UInt8]) async throws -> AsyncThrowingStream<
            StreamElement, Error
        >,
        clientStream:
        @escaping @Sendable (S.Method, RpcContext) async throws -> (
            send: @Sendable ([UInt8]) async throws -> Void,
            finish: @Sendable () async throws -> [UInt8]
        ),
        duplexStream:
        @escaping @Sendable (S.Method, RpcContext) async throws -> (
            send: @Sendable ([UInt8]) async throws -> Void,
            finish: @Sendable () async throws -> Void,
            responses: AsyncThrowingStream<StreamElement, Error>
        )
    ) {
        serviceInfos.append(S.serviceInfo)

        for method in S.Method.allCases {
            let m = method
            precondition(
                !Self.reservedMethodIds.contains(m.rawValue),
                "method '\(m.name)' uses reserved ID \(m.rawValue)"
            )
            precondition(
                methods[m.rawValue] == nil,
                "duplicate method ID 0x\(String(m.rawValue, radix: 16, uppercase: true))"
            )
            let reg: MethodRegistration
            switch m.methodType {
            case .unary:
                reg = .unary { payload, ctx in try await unary(m, ctx, payload) }
            case .serverStream:
                reg = .serverStream { payload, ctx in try await serverStream(m, ctx, payload) }
            case .clientStream:
                reg = .clientStream { ctx in try await clientStream(m, ctx) }
            case .duplexStream:
                reg = .duplexStream { ctx in try await duplexStream(m, ctx) }
            default:
                continue
            }
            methods[m.rawValue] = reg
        }
    }

    public func build() -> BebopRouter<FutureStore> {
        var store: FutureStore?
        if config.futuresEnabled {
            store = FutureStore(
                maxPendingFutures: config.maxPendingFutures,
                maxCompletedFutures: config.maxCompletedFutures
            )
        }
        return BebopRouter(
            methods: methods,
            serviceInfos: serviceInfos,
            interceptors: interceptors,
            config: config,
            futureStore: store
        )
    }

    public func build<Store: FutureStorage>(futureStore: Store) -> BebopRouter<Store> {
        BebopRouter(
            methods: methods,
            serviceInfos: serviceInfos,
            interceptors: interceptors,
            config: config,
            futureStore: futureStore
        )
    }
}
