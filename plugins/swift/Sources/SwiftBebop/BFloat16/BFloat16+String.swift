extension BFloat16: CustomStringConvertible {
    public var description: String { Float(self).description }
}

extension BFloat16: CustomDebugStringConvertible {
    public var debugDescription: String { Float(self).debugDescription }
}

extension BFloat16: TextOutputStreamable {
    public func write<Target: TextOutputStream>(to target: inout Target) {
        Float(self).write(to: &target)
    }
}

extension BFloat16: LosslessStringConvertible {
    @inlinable
    public init?<S: StringProtocol>(_ description: S) {
        guard let float = Float(description) else { return nil }
        self = BFloat16(float)
    }
}
