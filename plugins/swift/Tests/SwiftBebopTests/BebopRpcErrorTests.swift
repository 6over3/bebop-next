import Testing

@testable import SwiftBebop

@Suite struct BebopRpcErrorTests {
  @Test func basicConstruction() {
    let err = BebopRpcError(code: .notFound, detail: "method 42")
    #expect(err.code == .notFound)
    #expect(err.detail == "method 42")
    #expect(err.metadata == nil)
  }

  @Test func withMetadata() {
    let err = BebopRpcError(code: .internal, metadata: ["key": "val"])
    #expect(err.metadata?["key"] == "val")
  }

  @Test func wireRoundTrip() {
    let err = BebopRpcError(code: .permissionDenied, detail: "forbidden", metadata: ["x": "y"])
    let wire = err.toWire()
    #expect(wire.code == .permissionDenied)
    #expect(wire.detail == "forbidden")
    #expect(wire.metadata?["x"] == "y")

    let back = BebopRpcError(from: wire)
    #expect(back.code == .permissionDenied)
    #expect(back.detail == "forbidden")
    #expect(back.metadata?["x"] == "y")
  }

  @Test func wireWithNilCodeDefaultsToUnknown() {
    let wire = RpcError(code: nil, detail: "oops")
    let err = BebopRpcError(from: wire)
    #expect(err.code == .unknown)
  }

  @Test func descriptionWithDetail() {
    let err = BebopRpcError(code: .notFound, detail: "method 42")
    #expect(err.description == "BebopRpcError(NOT_FOUND): method 42")
  }

  @Test func descriptionWithoutDetail() {
    let err = BebopRpcError(code: .cancelled)
    #expect(err.description == "BebopRpcError(CANCELLED)")
  }

  @Test func descriptionUnknownCode() {
    let err = BebopRpcError(code: StatusCode(rawValue: 99))
    #expect(err.description == "BebopRpcError(99)")
  }
}
