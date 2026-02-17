/// Mutable builder for `BebopRouter`.
public final class BebopRouterBuilder<C: CallContext> {
  public var discoveryEnabled: Bool = true
  public var maxBatchSize: UInt = UInt.max
  public var maxBatchStreamElements: UInt = UInt.max

  private var methods: [UInt32: MethodRegistration<C>] = [:]
  private var serviceInfos: [ServiceInfo] = []
  private var interceptors: [any BebopInterceptor] = []

  public init() {}

  public func addInterceptor(_ interceptor: some BebopInterceptor) {
    interceptors.append(interceptor)
  }

  public func register<S: BebopServiceDefinition>(
    _ service: S.Type,
    unary: @escaping @Sendable (S.Method, C, [UInt8]) async throws -> [UInt8],
    serverStream:
      @escaping @Sendable (S.Method, C, [UInt8]) async throws -> AsyncThrowingStream<
        [UInt8], Error
      >,
    clientStream:
      @escaping @Sendable (S.Method, C) async throws -> (
        send: @Sendable ([UInt8]) async throws -> Void,
        finish: @Sendable () async throws -> [UInt8]
      ),
    duplexStream:
      @escaping @Sendable (S.Method, C) async throws -> (
        send: @Sendable ([UInt8]) async throws -> Void,
        finish: @Sendable () async throws -> Void,
        responses: AsyncThrowingStream<[UInt8], Error>
      )
  ) {
    serviceInfos.append(S.serviceInfo)

    for method in S.Method.allCases {
      let m = method
      precondition(
        m.rawValue != BebopReservedMethod.discovery
          && m.rawValue != BebopReservedMethod.batch,
        "method '\(m.name)' uses reserved ID \(m.rawValue)")
      precondition(
        methods[m.rawValue] == nil,
        "duplicate method ID 0x\(String(m.rawValue, radix: 16, uppercase: true))")
      let reg: MethodRegistration<C>
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

  public func build() -> BebopRouter<C> {
    BebopRouter<C>(
      methods: methods,
      serviceInfos: serviceInfos,
      interceptors: interceptors,
      discoveryEnabled: discoveryEnabled,
      maxBatchSize: maxBatchSize,
      maxBatchStreamElements: maxBatchStreamElements
    )
  }
}
