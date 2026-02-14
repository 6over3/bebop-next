extension BebopRouter {
  func checkCallViability(_ ctx: C) throws {
    try Task.checkCancellation()
    if let deadline = ctx.deadline, deadline.isPast {
      throw BebopRpcError(code: .deadlineExceeded)
    }
  }

  func runInterceptors(
    methodId: UInt32,
    ctx: C
  ) async throws {
    try checkCallViability(ctx)

    guard !interceptors.isEmpty else { return }

    // Innermost first so first-added interceptor is outermost
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
