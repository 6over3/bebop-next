public struct StreamReply<Element: Sendable, Metadata: Sendable>: AsyncSequence, Sendable {
  public typealias Failure = any Error

  private let _stream: AsyncThrowingStream<Element, any Error>
  private let _trailing: @Sendable () async -> Metadata

  public var metadata: Metadata {
    get async { await _trailing() }
  }

  public init(
    stream: AsyncThrowingStream<Element, any Error>,
    trailing: @escaping @Sendable () async -> Metadata
  ) {
    self._stream = stream
    self._trailing = trailing
  }

  public struct Iterator: AsyncIteratorProtocol {
    var base: AsyncThrowingStream<Element, any Error>.AsyncIterator
    public mutating func next() async throws -> Element? {
      try await base.next()
    }
  }

  public func makeAsyncIterator() -> Iterator {
    Iterator(base: _stream.makeAsyncIterator())
  }

  public func map<T: Sendable>(
    _ transform: @escaping @Sendable (Element) throws -> T
  ) -> StreamReply<T, Metadata> {
    let mapped = AsyncThrowingStream<T, any Error> { continuation in
      let task = Task {
        do {
          for try await element in self._stream {
            try Task.checkCancellation()
            continuation.yield(try transform(element))
          }
          continuation.finish()
        } catch {
          continuation.finish(throwing: error)
        }
      }
      continuation.onTermination = { _ in task.cancel() }
    }
    return StreamReply<T, Metadata>(stream: mapped, trailing: self._trailing)
  }
}
