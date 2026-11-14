/// Server-side middleware. Call `proceed` to continue, throw to reject.
public protocol BebopInterceptor: Sendable {
    func intercept<Value: Sendable>(
        methodId: UInt32,
        ctx: RpcContext,
        proceed: @Sendable () async throws -> Value
    ) async throws -> Value
}
