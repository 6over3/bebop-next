/// Run `operation` with a deadline and request cancellation on expiry.
public func withDeadline<T: Sendable>(
    _ deadline: BebopTimestamp,
    operation: @escaping @Sendable () async throws -> T
) async throws -> T {
    guard let remaining = deadline.timeRemaining else {
        throw BebopRpcError(code: .deadlineExceeded)
    }
    return try await withThrowingTaskGroup(of: T.self) { group in
        group.addTask { try await operation() }
        group.addTask {
            try await Task.sleep(for: remaining)
            throw BebopRpcError(code: .deadlineExceeded)
        }
        guard let result = try await group.next() else {
            throw BebopRpcError(code: .deadlineExceeded)
        }
        group.cancelAll()
        return result
    }
}
