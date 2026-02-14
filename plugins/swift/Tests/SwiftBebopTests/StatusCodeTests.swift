import Testing

@testable import SwiftBebop

@Suite struct StatusCodeTests {
  @Test func knownCodes() {
    #expect(StatusCode.ok.rawValue == 0)
    #expect(StatusCode.cancelled.rawValue == 1)
    #expect(StatusCode.unknown.rawValue == 2)
    #expect(StatusCode.invalidArgument.rawValue == 3)
    #expect(StatusCode.deadlineExceeded.rawValue == 4)
    #expect(StatusCode.notFound.rawValue == 5)
    #expect(StatusCode.permissionDenied.rawValue == 7)
    #expect(StatusCode.resourceExhausted.rawValue == 8)
    #expect(StatusCode.unimplemented.rawValue == 12)
    #expect(StatusCode.internal.rawValue == 13)
    #expect(StatusCode.unavailable.rawValue == 14)
    #expect(StatusCode.unauthenticated.rawValue == 16)
  }

  @Test func names() {
    #expect(StatusCode.ok.name == "OK")
    #expect(StatusCode.cancelled.name == "CANCELLED")
    #expect(StatusCode.notFound.name == "NOT_FOUND")
    #expect(StatusCode.internal.name == "INTERNAL")
  }

  @Test func unknownCodeHasNoName() {
    #expect(StatusCode(rawValue: 99).name == nil)
  }

  @Test func description() {
    #expect(StatusCode.ok.description == "OK")
    #expect(StatusCode(rawValue: 99).description == "STATUS_99")
  }

  @Test func roundTrip() throws {
    for code in [StatusCode.ok, .cancelled, .notFound, .internal] {
      let bytes = code.serializedData()
      let decoded = try StatusCode.decode(from: bytes)
      #expect(decoded == code)
    }
  }
}
