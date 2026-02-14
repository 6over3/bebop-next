#if canImport(Accelerate)
  import Accelerate

  @available(macOS 12.0, iOS 15.0, tvOS 15.0, watchOS 8.0, visionOS 1.0, *)
  extension BFloat16: BNNSScalar {
    @inlinable @inline(__always)
    public static var bnnsDataType: BNNSDataType { .bfloat16 }
  }

  @available(macOS 12.0, iOS 15.0, tvOS 15.0, watchOS 8.0, visionOS 1.0, *)
  extension BNNSDataType {
    @inlinable @inline(__always)
    public static var bfloat16: BNNSDataType { BNNSDataTypeBFloat16 }
  }

  @available(macOS 26.0, iOS 26.0, tvOS 26.0, watchOS 26.0, *)
  extension BFloat16: BNNSGraph.Builder.OperationParameter {
    @inlinable
    public func graphBuilderTensor(_ builder: BNNSGraph.Builder)
      -> BNNSGraph.Builder.Tensor<BFloat16>
    {
      builder.constant(value: self)
    }
  }
#endif
