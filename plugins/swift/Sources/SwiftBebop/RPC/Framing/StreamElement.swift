public struct StreamElement: Sendable {
    public let bytes: [UInt8]
    public let cursor: UInt64?

    public init(bytes: [UInt8], cursor: UInt64? = nil) {
        self.bytes = bytes
        self.cursor = cursor
    }
}
