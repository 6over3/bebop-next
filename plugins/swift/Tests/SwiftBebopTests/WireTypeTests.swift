import Testing

@testable import SwiftBebop

@Suite struct WireTypeTests {
  @Test func frameHeaderRoundTrip() throws {
    let header = FrameHeader(length: 42, flags: FrameFlags(rawValue: 3), streamId: 7)
    let bytes = header.serializedData()
    let decoded = try FrameHeader.decode(from: bytes)
    #expect(decoded.length == 42)
    #expect(decoded.flags == FrameFlags(rawValue: 3))
    #expect(decoded.streamId == 7)
  }

  @Test func callHeaderRoundTrip() throws {
    let header = CallHeader(
      methodId: 0xAABB, deadline: BebopTimestamp(seconds: 100, nanoseconds: 500),
      metadata: ["auth": "bearer xyz"])
    let bytes = header.serializedData()
    let decoded = try CallHeader.decode(from: bytes)
    #expect(decoded.methodId == 0xAABB)
    #expect(decoded.deadline?.seconds == 100)
    #expect(decoded.deadline?.nanoseconds == 500)
    #expect(decoded.metadata?["auth"] == "bearer xyz")
  }

  @Test func callHeaderNilFields() throws {
    let header = CallHeader()
    let bytes = header.serializedData()
    let decoded = try CallHeader.decode(from: bytes)
    #expect(decoded.methodId == nil)
    #expect(decoded.deadline == nil)
    #expect(decoded.metadata == nil)
  }

  @Test func rpcErrorRoundTrip() throws {
    let err = RpcError(code: .notFound, detail: "missing", metadata: ["retry": "true"])
    let bytes = err.serializedData()
    let decoded = try RpcError.decode(from: bytes)
    #expect(decoded.code == .notFound)
    #expect(decoded.detail == "missing")
    #expect(decoded.metadata?["retry"] == "true")
  }

  @Test func trailingMetadataRoundTrip() throws {
    let meta = TrailingMetadata(metadata: ["x-trace": "abc123"])
    let bytes = meta.serializedData()
    let decoded = try TrailingMetadata.decode(from: bytes)
    #expect(decoded.metadata?["x-trace"] == "abc123")
  }

  @Test func methodTypeValues() {
    #expect(MethodType.unary.rawValue == 0)
    #expect(MethodType.serverStream.rawValue == 1)
    #expect(MethodType.clientStream.rawValue == 2)
    #expect(MethodType.duplexStream.rawValue == 3)
  }

  @Test func frameFlagsValues() {
    #expect(FrameFlags.endStream.rawValue == 1)
    #expect(FrameFlags.error.rawValue == 2)
    #expect(FrameFlags.compressed.rawValue == 4)
    #expect(FrameFlags.trailer.rawValue == 8)
  }

  @Test func frameFlagsCombine() {
    let flags: FrameFlags = [.endStream, .error]
    #expect(flags.contains(.endStream))
    #expect(flags.contains(.error))
    #expect(!flags.contains(.compressed))
    #expect(flags.rawValue == 3)
  }

  @Test func serviceInfoRoundTrip() throws {
    let info = ServiceInfo(
      name: "Foo",
      methods: [
        MethodInfo(
          name: "Bar", methodId: 0x1234, methodType: .unary,
          requestTypeUrl: "type.bebop.sh/Req", responseTypeUrl: "type.bebop.sh/Res"
        )
      ])
    let bytes = info.serializedData()
    let decoded = try ServiceInfo.decode(from: bytes)
    #expect(decoded.name == "Foo")
    #expect(decoded.methods.count == 1)
    #expect(decoded.methods[0].name == "Bar")
    #expect(decoded.methods[0].methodId == 0x1234)
    #expect(decoded.methods[0].methodType == .unary)
  }

  @Test func discoveryResponseRoundTrip() throws {
    let response = DiscoveryResponse(services: [
      ServiceInfo(name: "A", methods: []),
      ServiceInfo(name: "B", methods: []),
    ])
    let bytes = response.serializedData()
    let decoded = try DiscoveryResponse.decode(from: bytes)
    #expect(decoded.services.count == 2)
    #expect(decoded.services[0].name == "A")
    #expect(decoded.services[1].name == "B")
  }

  @Test func batchCallRoundTrip() throws {
    let call = BatchCall(callId: 0, methodId: 0x1000, payload: [1, 2, 3], inputFrom: -1)
    let bytes = call.serializedData()
    let decoded = try BatchCall.decode(from: bytes)
    #expect(decoded.callId == 0)
    #expect(decoded.methodId == 0x1000)
    #expect(decoded.payload == [1, 2, 3])
    #expect(decoded.inputFrom == -1)
  }

  @Test func batchRequestRoundTrip() throws {
    let req = BatchRequest(
      calls: [BatchCall(callId: 0, methodId: 0x1000, payload: [0xFF], inputFrom: -1)],
      metadata: ["trace": "abc"])
    let bytes = req.serializedData()
    let decoded = try BatchRequest.decode(from: bytes)
    #expect(decoded.calls.count == 1)
    #expect(decoded.metadata["trace"] == "abc")
  }

  @Test func batchOutcomeSuccess() throws {
    let outcome = BatchOutcome.success(BatchSuccess(payloads: [[1, 2], [3, 4]]))
    let bytes = outcome.serializedData()
    let decoded = try BatchOutcome.decode(from: bytes)
    guard case .success(let s) = decoded else {
      Issue.record("expected success")
      return
    }
    #expect(s.payloads.count == 2)
    #expect(s.payloads[0] == [1, 2])
    #expect(s.payloads[1] == [3, 4])
  }

  @Test func batchOutcomeError() throws {
    let outcome = BatchOutcome.error(RpcError(code: .notFound, detail: "nope"))
    let bytes = outcome.serializedData()
    let decoded = try BatchOutcome.decode(from: bytes)
    guard case .error(let e) = decoded else {
      Issue.record("expected error")
      return
    }
    #expect(e.code == .notFound)
    #expect(e.detail == "nope")
  }

  @Test func batchResponseRoundTrip() throws {
    let response = BatchResponse(results: [
      BatchResult(callId: 0, outcome: .success(BatchSuccess(payloads: [[42]]))),
      BatchResult(callId: 1, outcome: .error(RpcError(code: .internal))),
    ])
    let bytes = response.serializedData()
    let decoded = try BatchResponse.decode(from: bytes)
    #expect(decoded.results.count == 2)
    #expect(decoded.results[0].callId == 0)
    #expect(decoded.results[1].callId == 1)
  }
}
