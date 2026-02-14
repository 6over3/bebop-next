/// Client-side call configuration: metadata and optional deadline.
public struct CallOptions: Sendable {
  public var metadata: [String: String]
  public var deadline: BebopTimestamp?

  public init(metadata: [String: String] = [:], deadline: BebopTimestamp? = nil) {
    self.metadata = metadata
    self.deadline = deadline
  }

  public init(metadata: [String: String] = [:], timeout: Duration) {
    self.metadata = metadata
    self.deadline = BebopTimestamp(fromNow: timeout)
  }

  public static let `default` = CallOptions()
}
