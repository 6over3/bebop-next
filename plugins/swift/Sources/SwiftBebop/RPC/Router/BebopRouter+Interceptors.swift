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

    func runInterceptors<Value: Sendable>(
        methodId: UInt32,
        ctx: RpcContext,
        operation: @escaping @Sendable () async throws -> Value
    ) async throws -> Value {
        try checkCallViability(ctx)
        var next = operation
        for interceptor in interceptors.reversed() {
            let proceed = next
            next = {
                try self.checkCallViability(ctx)
                return try await interceptor.intercept(
                    methodId: methodId,
                    ctx: ctx,
                    proceed: proceed
                )
            }
        }
        let result = try await next()
        try checkCallViability(ctx)
        return result
    }
}
