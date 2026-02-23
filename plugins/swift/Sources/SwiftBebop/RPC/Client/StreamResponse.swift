public struct StreamResponse<Element: Sendable, Metadata: Sendable>: AsyncSequence, Sendable {
  public typealias Failure = any Error

  private let _stream: AsyncThrowingStream<(Element, UInt64?), any Error>
  private let _trailing: @Sendable () async -> Metadata

  public var metadata: Metadata {
    get async { await _trailing() }
  }

  public init(
    stream: AsyncThrowingStream<(Element, UInt64?), any Error>,
    trailing: @escaping @Sendable () async -> Metadata
  ) {
    self._stream = stream
    self._trailing = trailing
  }

  public struct Iterator: AsyncIteratorProtocol {
    var base: AsyncThrowingStream<(Element, UInt64?), any Error>.AsyncIterator
    public private(set) var lastCursor: UInt64?

    public mutating func next() async throws -> Element? {
      guard let (element, cursor) = try await base.next() else {
        return nil
      }
      lastCursor = cursor
      return element
    }
  }

  public func makeAsyncIterator() -> Iterator {
    Iterator(base: _stream.makeAsyncIterator())
  }

  public func map<T: Sendable>(
    _ transform: @escaping @Sendable (Element) throws -> T
  ) -> StreamResponse<T, Metadata> {
    let source = _stream
    let mapped = AsyncThrowingStream<(T, UInt64?), any Error> { continuation in
      let task = Task {
        do {
          for try await (element, cursor) in source {
            try Task.checkCancellation()
            continuation.yield((try transform(element), cursor))
          }
          continuation.finish()
        } catch {
          continuation.finish(throwing: error)
        }
      }
      continuation.onTermination = { _ in task.cancel() }
    }
    return StreamResponse<T, Metadata>(stream: mapped, trailing: self._trailing)
  }
}
