import Testing

@testable import SwiftBebop

@Suite struct CallOptionsTests {
  @Test func defaultOptions() {
    let opts = CallOptions.default
    #expect(opts.metadata.isEmpty)
    #expect(opts.deadline == nil)
  }

  @Test func withMetadata() {
    let opts = CallOptions(metadata: ["auth": "token"])
    #expect(opts.metadata["auth"] == "token")
  }

  @Test func withDeadline() {
    let deadline = BebopTimestamp(seconds: 999, nanoseconds: 0)
    let opts = CallOptions(deadline: deadline)
    #expect(opts.deadline?.seconds == 999)
  }

  @Test func withTimeout() {
    let opts = CallOptions(timeout: .seconds(5))
    #expect(opts.deadline != nil)
  }
}
