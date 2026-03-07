/// Server-side middleware. Call `proceed` to continue, throw to reject.
public protocol BebopInterceptor: Sendable {
    func intercept(
        methodId: UInt32,
        ctx: RpcContext,
        proceed: @Sendable () async throws -> Void
    ) async throws
}
