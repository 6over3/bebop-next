public extension AsyncThrowingStream where Element == [UInt8], Failure == Error {
    func decode<T: BebopRecord>(_: T.Type) -> AsyncThrowingStream<T, Error> {
        AsyncThrowingStream<T, Error> { continuation in
            let task = Task {
                do {
                    for try await bytes in self {
                        try Task.checkCancellation()
                        try continuation.yield(T.decode(from: bytes))
                    }
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }
}
