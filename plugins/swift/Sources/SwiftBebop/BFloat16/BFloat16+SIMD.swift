extension BFloat16: SIMDScalar {
  public typealias SIMDMaskScalar = Int16

  @frozen @_alignment(4)
  public struct SIMD2Storage: SIMDStorage, Sendable {
    public var _value: UInt16.SIMD2Storage

    @inlinable
    public var scalarCount: Int { _value.scalarCount }

    @inlinable
    public init() { _value = .init() }

    public subscript(index: Int) -> BFloat16 {
      @inlinable get { BFloat16(bitPattern: _value[index]) }
      @inlinable set { _value[index] = newValue._value }
    }

    public typealias Scalar = BFloat16
  }

  @frozen @_alignment(8)
  public struct SIMD4Storage: SIMDStorage, Sendable {
    public var _value: UInt16.SIMD4Storage

    @inlinable
    public var scalarCount: Int { _value.scalarCount }

    @inlinable
    public init() { _value = .init() }

    public subscript(index: Int) -> BFloat16 {
      @inlinable get { BFloat16(bitPattern: _value[index]) }
      @inlinable set { _value[index] = newValue._value }
    }

    public typealias Scalar = BFloat16
  }

  @frozen @_alignment(16)
  public struct SIMD8Storage: SIMDStorage, Sendable {
    public var _value: UInt16.SIMD8Storage

    @inlinable
    public var scalarCount: Int { _value.scalarCount }

    @inlinable
    public init() { _value = .init() }

    public subscript(index: Int) -> BFloat16 {
      @inlinable get { BFloat16(bitPattern: _value[index]) }
      @inlinable set { _value[index] = newValue._value }
    }

    public typealias Scalar = BFloat16
  }

  @frozen @_alignment(16)
  public struct SIMD16Storage: SIMDStorage, Sendable {
    public var _value: UInt16.SIMD16Storage

    @inlinable
    public var scalarCount: Int { _value.scalarCount }

    @inlinable
    public init() { _value = .init() }

    public subscript(index: Int) -> BFloat16 {
      @inlinable get { BFloat16(bitPattern: _value[index]) }
      @inlinable set { _value[index] = newValue._value }
    }

    public typealias Scalar = BFloat16
  }

  @frozen @_alignment(16)
  public struct SIMD32Storage: SIMDStorage, Sendable {
    public var _value: UInt16.SIMD32Storage

    @inlinable
    public var scalarCount: Int { _value.scalarCount }

    @inlinable
    public init() { _value = .init() }

    public subscript(index: Int) -> BFloat16 {
      @inlinable get { BFloat16(bitPattern: _value[index]) }
      @inlinable set { _value[index] = newValue._value }
    }

    public typealias Scalar = BFloat16
  }

  @frozen @_alignment(16)
  public struct SIMD64Storage: SIMDStorage, Sendable {
    public var _value: UInt16.SIMD64Storage

    @inlinable
    public var scalarCount: Int { _value.scalarCount }

    @inlinable
    public init() { _value = .init() }

    public subscript(index: Int) -> BFloat16 {
      @inlinable get { BFloat16(bitPattern: _value[index]) }
      @inlinable set { _value[index] = newValue._value }
    }

    public typealias Scalar = BFloat16
  }
}
