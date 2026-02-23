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
    var responseMetadata: [String: String] = [:]
    var attachments: [ObjectIdentifier: any Sendable] = [:]
    var cursorQueue: [UInt64] = []
    var cursorIndex = 0
  }

  private let _state = Mutex(MutableState())

  public init(metadata: [String: String] = [:], deadline: BebopTimestamp? = nil, cursor: UInt64 = 0)
  {
    self.methodId = 0
    self.metadata = metadata
    self.deadline = deadline
    self.cursor = cursor
  }

  public init(metadata: [String: String] = [:], timeout: Duration, cursor: UInt64 = 0) {
    self.methodId = 0
    self.metadata = metadata
    self.deadline = BebopTimestamp(fromNow: timeout)
    self.cursor = cursor
  }

  public init(
    methodId: UInt32, metadata: [String: String], deadline: BebopTimestamp?, cursor: UInt64 = 0
  ) {
    self.methodId = methodId
    self.metadata = metadata
    self.deadline = deadline
    self.cursor = cursor
  }

  // MARK: - Cancellation

  public var isCancelled: Bool { _state.withLock { $0.cancelled } }
  public func cancel() { _state.withLock { $0.cancelled = true } }

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
      defer { state.cursorIndex += 1 }
      return state.cursorQueue[state.cursorIndex]
    }
  }

  // MARK: - Attachments (transport-specific data)

  public subscript<K: AttachmentKey>(key: K.Type) -> K.Value? {
    get { _state.withLock { $0.attachments[ObjectIdentifier(key)] as? K.Value } }
    set { _state.withLock { $0.attachments[ObjectIdentifier(key)] = newValue } }
  }

  // MARK: - Derivation

  public func deriving(appending extra: [String: String]) -> RpcContext {
    RpcContext(
      metadata: metadata.merging(extra) { _, new in new },
      deadline: deadline,
      cursor: cursor)
  }

  public func forwarding() -> RpcContext {
    RpcContext(metadata: metadata, deadline: deadline, cursor: cursor)
  }

  // MARK: - Transport binding

  func binding(to methodId: UInt32) -> RpcContext {
    RpcContext(methodId: methodId, metadata: metadata, deadline: deadline, cursor: cursor)
  }

  // MARK: - Batch

  func makeBatchContext(upstreamMetadata: [String: String] = [:]) -> RpcContext {
    RpcContext(
      methodId: methodId,
      metadata: metadata.merging(upstreamMetadata) { _, new in new },
      deadline: deadline)
  }
}
