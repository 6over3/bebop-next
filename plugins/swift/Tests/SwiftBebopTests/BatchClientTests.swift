import Testing

@testable import SwiftBebop

@Suite struct BatchClientTests {
  @Test func singleUnaryBatch() async throws {
    let batch = buildChannel().makeBatch()
    let ref: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "batched"))
    let results = try await batch.execute()
    let echo = try results[ref]
    #expect(echo.value == "batched")
  }

  @Test func multipleUnaryBatch() async throws {
    let batch = buildChannel().makeBatch()
    let r1: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "one"))
    let r2: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "two"))
    let results = try await batch.execute()
    #expect(try results[r1].value == "one")
    #expect(try results[r2].value == "two")
  }

  @Test func serverStreamBatch() async throws {
    let batch = buildChannel().makeBatch()
    let ref: StreamRef<CountResponse> = batch.addServerStream(
      methodId: listWidgetsId, request: CountRequest(n: 3))
    let results = try await batch.execute()
    let items = try results[ref]
    #expect(items.map(\.i) == [0, 1, 2])
  }

  @Test func forwardingBetweenCalls() async throws {
    let batch = buildChannel().makeBatch()
    let first: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "chain"))
    let _: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, forwardingFrom: first.callId)
    let results = try await batch.execute()
    let echo = try results[first]
    #expect(echo.value == "chain")
  }

  @Test func callRefIds() {
    let batch = buildChannel().makeBatch()
    let r1: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "a"))
    let r2: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "b"))
    #expect(r1.callId == 0)
    #expect(r2.callId == 1)
  }

  @Test func streamRefIds() {
    let batch = buildChannel().makeBatch()
    let s1: StreamRef<CountResponse> = batch.addServerStream(
      methodId: listWidgetsId, request: CountRequest(n: 1))
    let s2: StreamRef<CountResponse> = batch.addServerStream(
      methodId: listWidgetsId, request: CountRequest(n: 2))
    #expect(s1.callId == 0)
    #expect(s2.callId == 1)
  }

  @Test func batchWithMetadata() async throws {
    let batch = buildChannel().makeBatch(metadata: ["trace-id": "abc"])
    let ref: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "meta"))
    let results = try await batch.execute()
    #expect(try results[ref].value == "meta")
  }

  @Test func missingCallRefThrows() async throws {
    let batch = buildChannel().makeBatch()
    let _: CallRef<EchoResponse> = batch.addUnary(
      methodId: getWidgetId, request: EchoRequest(value: "x"))
    let results = try await batch.execute()
    let bogus = CallRef<EchoResponse>(callId: 999)
    #expect(throws: BebopRpcError.self) {
      _ = try results[bogus]
    }
  }
}
