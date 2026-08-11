@dynamicMemberLookup
public struct Response<Message: Sendable, Metadata: Sendable>: Sendable {
    public let message: Message
    public let metadata: Metadata

    public init(message: Message, metadata: Metadata) {
        self.message = message
        self.metadata = metadata
    }

    @inlinable
    public subscript<Member>(dynamicMember keyPath: KeyPath<Message, Member>) -> Member {
        message[keyPath: keyPath]
    }

    public func map<T: Sendable>(
        _ transform: (Message) throws -> T
    ) rethrows -> Response<T, Metadata> {
        try Response<T, Metadata>(message: transform(message), metadata: metadata)
    }
}
