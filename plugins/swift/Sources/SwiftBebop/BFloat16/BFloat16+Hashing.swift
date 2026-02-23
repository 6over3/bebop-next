extension BFloat16: Hashable {
  @inlinable
  public func hash(into hasher: inout Hasher) {
    let v = isZero ? 0 : self
    hasher.combine(v.bitPattern)
  }
}
