/// Handler's view of the current RPC call. Transport implementations
/// provide concrete conformances.
public protocol CallContext: Sendable {
  var methodId: UInt32 { get }
  var requestMetadata: [String: String] { get }
  var deadline: BebopTimestamp? { get }
  var isCancelled: Bool { get }
  func setResponseMetadata(_ key: String, _ value: String)
}
