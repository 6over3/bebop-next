public protocol AuthInfo: Sendable {
  var authType: String { get }
  var identity: String { get }
}

public struct PeerInfo: Sendable {
  public let remoteAddress: String?
  public let localAddress: String?
  public let authInfo: (any AuthInfo)?

  public init(
    remoteAddress: String? = nil,
    localAddress: String? = nil,
    authInfo: (any AuthInfo)? = nil
  ) {
    self.remoteAddress = remoteAddress
    self.localAddress = localAddress
    self.authInfo = authInfo
  }
}

public enum PeerInfoKey: AttachmentKey {
  public typealias Value = PeerInfo
}
