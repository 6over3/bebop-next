import Testing

@testable import SwiftBebop

@Suite struct WidgetServiceBatchTests {
  @Test func batchGetWidget() async throws {
    let batch = buildChannel().makeBatch()
    let ref = batch.widgetService.getWidget(EchoRequest(value: "batched"))
    let results = try await batch.execute()
    #expect(try results[ref].value == "batched")
  }

  @Test func batchGetWidgetDeconstructed() async throws {
    let batch = buildChannel().makeBatch()
    let ref = batch.widgetService.getWidget(value: "decon")
    let results = try await batch.execute()
    #expect(try results[ref].value == "decon")
  }

  @Test func batchListWidgets() async throws {
    let batch = buildChannel().makeBatch()
    let ref = batch.widgetService.listWidgets(CountRequest(n: 3))
    let results = try await batch.execute()
    #expect(try results[ref].map(\.i) == [0, 1, 2])
  }

  @Test func batchListWidgetsDeconstructed() async throws {
    let batch = buildChannel().makeBatch()
    let ref = batch.widgetService.listWidgets(n: 2)
    let results = try await batch.execute()
    #expect(try results[ref].map(\.i) == [0, 1])
  }

  @Test func batchMultipleCalls() async throws {
    let batch = buildChannel().makeBatch()
    let echoRef = batch.widgetService.getWidget(value: "e2e")
    let streamRef = batch.widgetService.listWidgets(n: 2)
    let results = try await batch.execute()
    #expect(try results[echoRef].value == "e2e")
    #expect(try results[streamRef].map(\.i) == [0, 1])
  }

  @Test func forwardingUnary() async throws {
    let batch = buildChannel().makeBatch()
    let first = batch.widgetService.getWidget(value: "chain")
    let second = batch.widgetService.getWidget(forwarding: first)
    let results = try await batch.execute()
    #expect(try results[first].value == "chain")
    #expect(try results[second].value == "chain")
  }

  @Test func forwardingToServerStream() async throws {
    let batch = buildChannel().makeBatch()
    let echo = batch.widgetService.getWidget(value: "piped")
    _ = batch.widgetService.getWidget(forwarding: echo)
    let results = try await batch.execute()
    #expect(try results[echo].value == "piped")
  }

  @Test func forwardingChain() async throws {
    let batch = buildChannel().makeBatch()
    let a = batch.widgetService.getWidget(value: "start")
    let b = batch.widgetService.getWidget(forwarding: a)
    let c = batch.widgetService.getWidget(forwarding: b)
    let results = try await batch.execute()
    #expect(try results[a].value == "start")
    #expect(try results[b].value == "start")
    #expect(try results[c].value == "start")
  }
}
