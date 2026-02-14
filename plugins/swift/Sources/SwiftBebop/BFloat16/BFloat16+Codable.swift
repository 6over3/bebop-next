extension BFloat16: Codable {
  @inlinable
  public func encode(to encoder: Encoder) throws {
    var container = encoder.singleValueContainer()
    try container.encode(Float(self))
  }

  @inlinable
  public init(from decoder: Decoder) throws {
    let container = try decoder.singleValueContainer()
    let float = try container.decode(Float.self)
    self.init(float)
  }
}
