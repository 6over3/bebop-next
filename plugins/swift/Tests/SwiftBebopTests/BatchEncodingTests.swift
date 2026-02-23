import Testing

@testable import SwiftBebop

@Suite struct BatchEncodingTests {
  @Test func payloadEncodedOnceInBatchCall() {
    let request = EchoRequest(value: "hello")
    let expectedBytes = request.serializedData()

    let batch = buildChannel().makeBatch()
    let _: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: request)

    let batchRequest = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: expectedBytes, inputFrom: -1)
      ], metadata: [:])
    let wireBytes = batchRequest.serializedData()
    let decoded = try! BatchRequest.decode(from: wireBytes)

    #expect(decoded.calls[0].payload == expectedBytes)
  }

  @Test func payloadSurvivesFullRoundTrip() async throws {
    let request = EchoRequest(value: "round-trip")
    let requestBytes = request.serializedData()

    let router = buildRouter()
    let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

    let batchReq = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: requestBytes, inputFrom: -1)
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: batchReq.serializedData(), ctx: ctx)
    let batchResp = try BatchResponse.decode(from: responseBytes)

    guard case .success(let s) = batchResp.results[0].outcome else {
      Issue.record("expected success")
      return
    }

    let responsePayload = s.payloads[0]
    let decoded = try EchoResponse.decode(from: responsePayload)
    #expect(decoded.value == "round-trip")

    #expect(decoded.serializedData() == responsePayload)
  }

  @Test func pipelinedCallUsesResponseBytesDirectly() async throws {
    let router = buildRouter()
    let ctx = RpcContext(methodId: 1, metadata: [:], deadline: nil)

    let batchReq = BatchRequest(
      calls: [
        BatchCall(
          callId: 0, methodId: getWidgetId,
          payload: EchoRequest(value: "piped").serializedData(), inputFrom: -1),
        BatchCall(callId: 1, methodId: getWidgetId, payload: [], inputFrom: 0),
      ],
      metadata: [:])

    let responseBytes = try await router.unary(
      methodId: 1, payload: batchReq.serializedData(), ctx: ctx)
    let batchResp = try BatchResponse.decode(from: responseBytes)

    guard case .success(let s1) = batchResp.results[0].outcome,
      case .success(let s2) = batchResp.results[1].outcome
    else {
      Issue.record("expected both calls to succeed")
      return
    }

    let r1 = try EchoResponse.decode(from: s1.payloads[0])
    let r2 = try EchoResponse.decode(from: s2.payloads[0])
    #expect(r1.value == "piped")
    #expect(r2.value == "piped")

    #expect(s1.payloads[0] == s2.payloads[0])
  }

  @Test func batchCallPayloadIsNotReEncodedByFraming() {

    let innerPayload: [UInt8] = EchoRequest(value: "test").serializedData()

    let call = BatchCall(
      callId: 0, methodId: getWidgetId,
      payload: innerPayload, inputFrom: -1)

    let wireBytes = call.serializedData()
    let decoded = try! BatchCall.decode(from: wireBytes)

    #expect(decoded.payload == innerPayload)
    #expect(decoded.callId == 0)
    #expect(decoded.methodId == getWidgetId)
    #expect(decoded.inputFrom == -1)
  }

  @Test func batchResponsePayloadsAreOpaqueBytes() {

    let p1 = EchoResponse(value: "a").serializedData()
    let p2 = EchoResponse(value: "b").serializedData()
    let success = BatchSuccess(payloads: [p1, p2])

    let wireBytes = success.serializedData()
    let decoded = try! BatchSuccess.decode(from: wireBytes)

    #expect(decoded.payloads.count == 2)
    #expect(decoded.payloads[0] == p1)
    #expect(decoded.payloads[1] == p2)
  }
}
