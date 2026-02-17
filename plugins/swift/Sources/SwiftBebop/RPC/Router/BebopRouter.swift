/// Wire-protocol reserved method IDs.
public enum BebopReservedMethod {
  public static let discovery: UInt32 = 0
  public static let batch: UInt32 = 1
}

public struct BebopRouter<C: CallContext>: Sendable {
  public let discoveryEnabled: Bool
  public let maxBatchSize: UInt
  public let maxBatchStreamElements: UInt

  let methods: [UInt32: MethodRegistration<C>]
  let serviceInfos: [ServiceInfo]
  let interceptors: [any BebopInterceptor]

  init(
    methods: [UInt32: MethodRegistration<C>],
    serviceInfos: [ServiceInfo],
    interceptors: [any BebopInterceptor],
    discoveryEnabled: Bool,
    maxBatchSize: UInt,
    maxBatchStreamElements: UInt
  ) {
    self.methods = methods
    self.serviceInfos = serviceInfos
    self.interceptors = interceptors
    self.discoveryEnabled = discoveryEnabled
    self.maxBatchSize = maxBatchSize
    self.maxBatchStreamElements = maxBatchStreamElements
  }

  // MARK: - Dispatch

  public func unary(
    methodId: UInt32, payload: [UInt8], ctx: C
  ) async throws -> [UInt8] {
    if methodId == BebopReservedMethod.discovery { return try handleDiscovery() }
    if methodId == BebopReservedMethod.batch { return try await handleBatch(payload: payload, ctx: ctx) }

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
    methodId: UInt32, payload: [UInt8], ctx: C
  ) async throws -> AsyncThrowingStream<[UInt8], Error> {
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
    methodId: UInt32, ctx: C
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
    methodId: UInt32, ctx: C
  ) async throws -> (
    send: @Sendable ([UInt8]) async throws -> Void,
    finish: @Sendable () async throws -> Void,
    responses: AsyncThrowingStream<[UInt8], Error>
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
    guard discoveryEnabled else {
      throw BebopRpcError(code: .unimplemented, detail: "discovery disabled")
    }
    return DiscoveryResponse(services: serviceInfos).serializedData()
  }
}
