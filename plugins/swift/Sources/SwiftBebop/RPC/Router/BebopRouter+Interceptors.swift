extension BebopRouter {
  func checkCallViability(_ ctx: RpcContext) throws {
    guard !Task.isCancelled else {
      throw CancellationError()
    }
    guard !ctx.isCancelled else {
      throw BebopRpcError(code: .cancelled)
    }
    if let deadline = ctx.deadline {
      guard !deadline.isPast else {
        throw BebopRpcError(code: .deadlineExceeded)
      }
    }
  }

  func runInterceptors(
    methodId: UInt32,
    ctx: RpcContext
  ) async throws {
    try checkCallViability(ctx)

    guard !interceptors.isEmpty else { return }

    var next: @Sendable () async throws -> Void = {}
    for i in stride(from: interceptors.count - 1, through: 0, by: -1) {
      let interceptor = interceptors[i]
      let proceed = next
      next = { [methodId] in
        try Task.checkCancellation()
        try await interceptor.intercept(
          methodId: methodId,
          ctx: ctx,
          proceed: proceed
        )
      }
    }

    try await next()
  }
}
