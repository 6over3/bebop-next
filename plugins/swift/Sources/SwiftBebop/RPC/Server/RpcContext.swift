import Synchronization

public protocol AttachmentKey {
  associatedtype Value: Sendable
}

public final class RpcContext: @unchecked Sendable {
  public let methodId: UInt32
  public let metadata: [String: String]
  public let deadline: BebopTimestamp?
  public let cursor: UInt64

  private let _cancelled = Mutex(false)
  private let _responseMetadata = Mutex<[String: String]>([:])
  private let _attachments = Mutex<[ObjectIdentifier: any Sendable]>([:])

  public init(metadata: [String: String] = [:], deadline: BebopTimestamp? = nil, cursor: UInt64 = 0) {
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

  init(methodId: UInt32, metadata: [String: String], deadline: BebopTimestamp?, cursor: UInt64 = 0) {
    self.methodId = methodId
    self.metadata = metadata
    self.deadline = deadline
    self.cursor = cursor
  }

  // MARK: - Cancellation

  public var isCancelled: Bool { _cancelled.withLock { $0 } }
  func cancel() { _cancelled.withLock { $0 = true } }

  // MARK: - Response metadata (handler -> transport)

  public func setResponseMetadata(_ key: String, _ value: String) {
    _responseMetadata.withLock { $0[key] = value }
  }

  public var responseMetadata: [String: String] {
    _responseMetadata.withLock { $0 }
  }

  // MARK: - Attachments (transport-specific data)

  public subscript<K: AttachmentKey>(key: K.Type) -> K.Value? {
    get { _attachments.withLock { $0[ObjectIdentifier(key)] as? K.Value } }
    set { _attachments.withLock { $0[ObjectIdentifier(key)] = newValue } }
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
