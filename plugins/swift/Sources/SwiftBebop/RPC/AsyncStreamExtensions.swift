extension AsyncThrowingStream where Element == [UInt8], Failure == Error {
  public func decode<T: BebopRecord>(_ type: T.Type) -> AsyncThrowingStream<T, Error> {
    AsyncThrowingStream<T, Error> { continuation in
      let task = Task {
        do {
          for try await bytes in self {
            try Task.checkCancellation()
            continuation.yield(try T.decode(from: bytes))
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
