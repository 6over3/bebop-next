/// Wire-protocol reserved method IDs.
public enum BebopReservedMethod {
  public static let discovery: UInt32 = 0
  public static let batch: UInt32 = 1
  public static let dispatch: UInt32 = 2
  public static let resolve: UInt32 = 3
  public static let cancel: UInt32 = 4
}

public struct BebopRouterConfig: Sendable {
  public var discoveryEnabled: Bool = true
  public var futuresEnabled: Bool = false
  public var maxBatchSize: UInt = .max
  public var maxBatchStreamElements: UInt = .max
  public var maxPendingFutures: UInt = .max
  public var maxCompletedFutures: UInt = 10_000

  public init() {}
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
    guard case .unary(let dispatch) = reg else {
      throw BebopRpcError(code: .unimplemented, detail: "method \(methodId) is not unary")
    }

    try await runInterceptors(methodId: methodId, ctx: ctx)
    return try await dispatch(payload, ctx)
  }

  public func serverStream(
    methodId: UInt32, payload: [UInt8], ctx: RpcContext
  ) async throws -> AsyncThrowingStream<StreamElement, Error> {
    switch methodId {
    case BebopReservedMethod.resolve:
      return try await handleResolve(payload: payload, ctx: ctx)
    default:
      break
    }

    guard let reg = methods[methodId] else {
      throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
    }
    guard case .serverStream(let dispatch) = reg else {
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
    guard let reg = methods[methodId] else {
      throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
    }
    guard case .clientStream(let dispatch) = reg else {
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
    guard let reg = methods[methodId] else {
      throw BebopRpcError(code: .notFound, detail: "method \(methodId)")
    }
    guard case .duplexStream(let dispatch) = reg else {
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
    return DiscoveryResponse(services: serviceInfos).serializedData()
  }
}
