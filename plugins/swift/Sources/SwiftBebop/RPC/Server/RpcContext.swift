import Synchronization

public protocol AttachmentKey {
    associatedtype Value: Sendable
}

public final class RpcContext: Sendable {
    public let methodId: UInt32
    public let metadata: [String: String]
    public let deadline: BebopTimestamp?
    public let cursor: UInt64

    private struct MutableState: Sendable {
        var cancelled = false
        var decodeLimits = BebopDecodeLimits.default
        var responseMetadata: [String: String] = [:]
        var attachments: [ObjectIdentifier: any Sendable] = [:]
        var cursorQueue: [UInt64] = []
        var cursorIndex = 0
    }

    private let _state = Mutex(MutableState())
    private let cancellationParent: RpcContext?

    public convenience init(
        metadata: [String: String] = [:], deadline: BebopTimestamp? = nil, cursor: UInt64 = 0
    ) {
        self.init(methodId: 0, metadata: metadata, deadline: deadline, cursor: cursor, parent: nil)
    }

    public convenience init(metadata: [String: String] = [:], timeout: Duration, cursor: UInt64 = 0)
    {
        self.init(
            methodId: 0, metadata: metadata, deadline: BebopTimestamp(fromNow: timeout),
            cursor: cursor, parent: nil)
    }

    public convenience init(
        methodId: UInt32, metadata: [String: String], deadline: BebopTimestamp?, cursor: UInt64 = 0
    ) {
        self.init(
            methodId: methodId, metadata: metadata, deadline: deadline, cursor: cursor, parent: nil)
    }

    private init(
        methodId: UInt32, metadata: [String: String], deadline: BebopTimestamp?, cursor: UInt64,
        parent: RpcContext?
    ) {
        self.methodId = methodId
        self.metadata = metadata
        self.deadline = deadline
        self.cursor = cursor
        cancellationParent = parent
    }

    // MARK: - Cancellation

    public var isCancelled: Bool {
        var current: RpcContext? = self
        while let context = current {
            if context._state.withLock({ $0.cancelled }) {
                return true
            }
            current = context.cancellationParent
        }
        return false
    }
    public func cancel() { _state.withLock { $0.cancelled = true } }
    public var decodeLimits: BebopDecodeLimits { _state.withLock { $0.decodeLimits } }

    func setDecodeLimits(_ limits: BebopDecodeLimits) {
        _state.withLock { $0.decodeLimits = limits }
    }

    // MARK: - Response metadata (handler -> transport)

    public func setResponseMetadata(_ key: String, _ value: String) {
        _state.withLock { $0.responseMetadata[key] = value }
    }

    public var responseMetadata: [String: String] {
        _state.withLock { $0.responseMetadata }
    }

    // MARK: - Cursor queue

    public func emitCursor(_ value: UInt64) {
        _state.withLock { $0.cursorQueue.append(value) }
    }

    public func dequeueCursor() -> UInt64? {
        _state.withLock { state in
            guard state.cursorIndex < state.cursorQueue.count else { return nil }
            let cursor = state.cursorQueue[state.cursorIndex]
            state.cursorIndex += 1
            if state.cursorIndex >= 64, state.cursorIndex >= state.cursorQueue.count / 2 {
                state.cursorQueue.removeFirst(state.cursorIndex)
                state.cursorIndex = 0
            }
            return cursor
        }
    }

    // MARK: - Attachments (transport-specific data)

    public subscript<K: AttachmentKey>(key: K.Type) -> K.Value? {
        get { _state.withLock { $0.attachments[ObjectIdentifier(key)] as? K.Value } }
        set { _state.withLock { $0.attachments[ObjectIdentifier(key)] = newValue } }
    }

    // MARK: - Derivation

    public func deriving(appending extra: [String: String]) -> RpcContext {
        let context = RpcContext(
            methodId: methodId,
            metadata: metadata.merging(extra) { _, new in new },
            deadline: deadline,
            cursor: cursor,
            parent: self
        )
        context.setDecodeLimits(decodeLimits)
        return context
    }

    public func forwarding() -> RpcContext {
        let context = RpcContext(
            methodId: 0, metadata: metadata, deadline: deadline, cursor: cursor, parent: self)
        context.setDecodeLimits(decodeLimits)
        return context
    }

    // MARK: - Transport binding

    func binding(to methodId: UInt32) -> RpcContext {
        let context = RpcContext(
            methodId: methodId,
            metadata: metadata,
            deadline: deadline,
            cursor: cursor,
            parent: self
        )
        context.setDecodeLimits(decodeLimits)
        return context
    }

    // MARK: - Batch

    func makeBatchContext(
        methodId: UInt32,
        upstreamMetadata: [String: String] = [:]
    ) -> RpcContext {
        let context = RpcContext(
            methodId: methodId,
            metadata: metadata.merging(upstreamMetadata) { _, new in new },
            deadline: deadline,
            cursor: 0,
            parent: self
        )
        context.setDecodeLimits(decodeLimits)
        context[PeerInfoKey.self] = self[PeerInfoKey.self]
        return context
    }
}
