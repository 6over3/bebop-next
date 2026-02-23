public struct Response<Value: Sendable, Metadata: Sendable>: Sendable {
  public let value: Value
  public let metadata: Metadata

  public init(value: Value, metadata: Metadata) {
    self.value = value
    self.metadata = metadata
  }

  public func map<T: Sendable>(
    _ transform: (Value) throws -> T
  ) rethrows -> Response<T, Metadata> {
    Response<T, Metadata>(value: try transform(value), metadata: metadata)
  }
}
