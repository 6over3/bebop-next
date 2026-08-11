/// Wire-protocol reserved method IDs.
public enum BebopReservedMethod {
    public static let discovery: UInt32 = 0
    public static let batch: UInt32 = 1
    public static let dispatch: UInt32 = 2
    public static let resolve: UInt32 = 3
    public static let cancel: UInt32 = 4
}

public struct BebopRouterConfig: Sendable {
    public var discoveryEnabled: Bool
    public var futuresEnabled: Bool
    public var maxBatchSize: UInt
    public var maxBatchStreamElements: UInt
    public var maxPendingFutures: UInt
    public var maxCompletedFutures: UInt
    public var futureSubscriberBufferCapacity: Int
    public var decodeLimits: BebopDecodeLimits
    public var allowUnauthenticatedFutureOwners: Bool

    public init(
        discoveryEnabled: Bool = true,
        futuresEnabled: Bool = false,
        maxBatchSize: UInt = 1_024,
        maxBatchStreamElements: UInt = 10_000,
        maxPendingFutures: UInt = 10_000,
        maxCompletedFutures: UInt = 10_000,
        futureSubscriberBufferCapacity: Int = 256,
        decodeLimits: BebopDecodeLimits = .default,
        allowUnauthenticatedFutureOwners: Bool = false
    ) {
        self.discoveryEnabled = discoveryEnabled
        self.futuresEnabled = futuresEnabled
        self.maxBatchSize = maxBatchSize
        self.maxBatchStreamElements = maxBatchStreamElements
        self.maxPendingFutures = maxPendingFutures
        self.maxCompletedFutures = maxCompletedFutures
        self.futureSubscriberBufferCapacity = futureSubscriberBufferCapacity
        self.decodeLimits = decodeLimits
        self.allowUnauthenticatedFutureOwners = allowUnauthenticatedFutureOwners
    }
}

public struct BebopRouter<Store: FutureStorage>: Sendable {
    public let config: BebopRouterConfig

    let methods: [UInt32: MethodRegistration]
    let serviceInfos: [ServiceInfo]
    let interceptors: [any BebopInterceptor]
    let futureStore: Store?

    init(
        methods: [UInt32: MethodRegistration],
        serviceInfos: [ServiceInfo],
        interceptors: [any BebopInterceptor],
        config: BebopRouterConfig,
        futureStore: Store?
    ) {
        self.methods = methods
        self.serviceInfos = serviceInfos
        self.interceptors = interceptors
        self.config = config
        self.futureStore = futureStore
    }

    // MARK: - Dispatch

    public func unary(
        methodId: UInt32, payload: [UInt8], ctx: RpcContext
    ) async throws -> [UInt8] {
        guard ctx.methodId == methodId else {
            throw BebopRpcError(code: .invalidArgument, detail: "context method ID mismatch")
        }
        ctx.setDecodeLimits(config.decodeLimits)
        switch methodId {
        case BebopReservedMethod.discovery:
            return try handleDiscovery()
        case BebopReservedMethod.batch:
            return try await handleBatch(payload: payload, ctx: ctx)
        case BebopReservedMethod.dispatch:
            return try await handleDispatch(payload: payload, ctx: ctx)
        case BebopReservedMethod.cancel:
            return try await handleCancel(payload: payload, ctx: ctx)
        default:
            break
        }

        guard let reg = methods[methodId] else {
            throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
        }
        guard case let .unary(dispatch) = reg else {
            throw BebopRpcError(code: .unimplemented, detail: "method \(methodId) is not unary")
        }

        try await runInterceptors(methodId: methodId, ctx: ctx)
        return try await dispatch(payload, ctx)
    }

    public func serverStream(
        methodId: UInt32, payload: [UInt8], ctx: RpcContext
    ) async throws -> AsyncThrowingStream<StreamElement, Error> {
        guard ctx.methodId == methodId else {
            throw BebopRpcError(code: .invalidArgument, detail: "context method ID mismatch")
        }
        ctx.setDecodeLimits(config.decodeLimits)
        switch methodId {
        case BebopReservedMethod.resolve:
            return try await handleResolve(payload: payload, ctx: ctx)
        default:
            break
        }

        guard let reg = methods[methodId] else {
            throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
        }
        guard case let .serverStream(dispatch) = reg else {
            throw BebopRpcError(code: .unimplemented, detail: "method \(methodId) is not server-stream")
        }

        try await runInterceptors(methodId: methodId, ctx: ctx)
        return try await dispatch(payload, ctx)
    }

    public func clientStream(
        methodId: UInt32, ctx: RpcContext
    ) async throws -> (
        send: @Sendable ([UInt8]) async throws -> Void,
        finish: @Sendable () async throws -> [UInt8]
    ) {
        guard ctx.methodId == methodId else {
            throw BebopRpcError(code: .invalidArgument, detail: "context method ID mismatch")
        }
        ctx.setDecodeLimits(config.decodeLimits)
        guard let reg = methods[methodId] else {
            throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
        }
        guard case let .clientStream(dispatch) = reg else {
            throw BebopRpcError(code: .unimplemented, detail: "method \(methodId) is not client-stream")
        }

        try await runInterceptors(methodId: methodId, ctx: ctx)
        return try await dispatch(ctx)
    }

    public func duplexStream(
        methodId: UInt32, ctx: RpcContext
    ) async throws -> (
        send: @Sendable ([UInt8]) async throws -> Void,
        finish: @Sendable () async throws -> Void,
        responses: AsyncThrowingStream<StreamElement, Error>
    ) {
        guard ctx.methodId == methodId else {
            throw BebopRpcError(code: .invalidArgument, detail: "context method ID mismatch")
        }
        ctx.setDecodeLimits(config.decodeLimits)
        guard let reg = methods[methodId] else {
            throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
        }
        guard case let .duplexStream(dispatch) = reg else {
            throw BebopRpcError(code: .unimplemented, detail: "method \(methodId) is not duplex-stream")
        }

        try await runInterceptors(methodId: methodId, ctx: ctx)
        return try await dispatch(ctx)
    }

    public func methodType(for methodId: UInt32) -> MethodType? {
        methods[methodId]?.methodType
    }

    // MARK: - Discovery

    private func handleDiscovery() throws -> [UInt8] {
        guard config.discoveryEnabled else {
            throw BebopRpcError(code: .unimplemented, detail: "discovery disabled")
        }
        return DiscoveryResponse(services: serviceInfos).encode()
    }
}
