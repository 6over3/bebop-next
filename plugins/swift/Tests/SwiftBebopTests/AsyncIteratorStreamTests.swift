import Testing

@testable import SwiftBebop

@Suite struct AsyncIteratorStreamTests {
    @Test func serverStreamReduceToSum() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(n: 5)
        var sum: UInt32 = 0
        for try await item in stream {
            sum += item.i
        }
        #expect(sum == 0 + 1 + 2 + 3 + 4)
    }

    @Test func serverStreamEarlyBreak() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(n: 100)
        var collected: [UInt32] = []
        for try await item in stream {
            collected.append(item.i)
            if collected.count == 3 { break }
        }
        #expect(collected == [0, 1, 2])
    }

    @Test func serverStreamManualIterator() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(n: 4)
        var iterator = stream.makeAsyncIterator()

        let first = try await iterator.next()
        #expect(first?.i == 0)

        let second = try await iterator.next()
        #expect(second?.i == 1)

        let third = try await iterator.next()
        #expect(third?.i == 2)

        let fourth = try await iterator.next()
        #expect(fourth?.i == 3)

        let done = try await iterator.next()
        #expect(done == nil)
    }

    @Test func serverStreamCollectToArray() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(n: 3)
        var items: [CountResponse] = []
        for try await item in stream {
            items.append(item)
        }
        #expect(items.map(\.i) == [0, 1, 2])
    }

    @Test func clientStreamFeedFromArray() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let upload = try await client.uploadWidgets()
        let requests = ["one", "two", "three"].map(EchoRequest.init)
        let response = try await upload.send(requests)
        #expect(response.value == "one,two,three")
    }

    @Test func clientStreamSendNothing() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let upload = try await client.uploadWidgets()
        let response = try await upload.finish()
        #expect(response.value == "")
    }

    @Test func clientStreamSendSingleItem() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let upload = try await client.uploadWidgets()
        try await upload.send(EchoRequest(value: "solo"))
        let response = try await upload.finish()
        #expect(response.value == "solo")
    }

    @Test func duplexInterleavedSendAndReceive() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        var responses = sync.responses.makeAsyncIterator()

        try await sync.send(EchoRequest(value: "a"))
        let first = try await responses.next()
        #expect(first?.value == "a")

        try await sync.send(EchoRequest(value: "b"))
        let second = try await responses.next()
        #expect(second?.value == "b")

        try await sync.finish()
        #expect(try await responses.next() == nil)
    }

    @Test func duplexCollectAllAfterFinish() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        try await sync.send(["x", "y", "z"].map(EchoRequest.init))

        var results: [String] = []
        for try await item in sync {
            results.append(item.value)
        }
        #expect(results == ["x", "y", "z"])
    }

    @Test func duplexEarlyBreakOnResponses() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        try await sync.send((0 ..< 10).map { EchoRequest(value: "msg\($0)") })

        var collected: [String] = []
        for try await item in sync.responses {
            collected.append(item.value)
            if collected.count == 3 { break }
        }
        #expect(collected == ["msg0", "msg1", "msg2"])
    }

    @Test func duplexEmptyStream() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        try await sync.finish()
        var count = 0
        for try await _ in sync.responses {
            count += 1
        }
        #expect(count == 0)
    }

    @Test func clientStreamFromAsyncGenerator() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let upload = try await client.uploadWidgets()
        let source = AsyncStream<EchoRequest> { continuation in
            for word in ["alpha", "bravo", "charlie"] {
                continuation.yield(EchoRequest(value: word))
            }
            continuation.finish()
        }
        let response = try await upload.send(source)
        #expect(response.value == "alpha,bravo,charlie")
    }

    @Test func duplexSendFromAsyncGeneratorReadInLoop() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        let source = AsyncStream<EchoRequest> { continuation in
            for i in 0 ..< 5 {
                continuation.yield(EchoRequest(value: "item-\(i)"))
            }
            continuation.finish()
        }

        async let sending: Void = sync.send(source)
        var received: [String] = []
        for try await response in sync {
            received.append(response.value)
        }
        try await sending
        #expect(received == ["item-0", "item-1", "item-2", "item-3", "item-4"])
    }

    @Test func duplexConcurrentSendAndReceive() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let received = Counter()
        let sync = try await client.syncWidgets()
        let source = AsyncStream<EchoRequest> { continuation in
            for i in 0 ..< 4 {
                continuation.yield(EchoRequest(value: "msg-\(i)"))
            }
            continuation.finish()
        }

        async let sending: Void = sync.send(source)
        var values: [String] = []
        for try await response in sync {
            values.append(response.value)
            await received.increment()
        }
        try await sending
        #expect(values == ["msg-0", "msg-1", "msg-2", "msg-3"])
        #expect(await received.value == 4)
    }

    @Test func serverStreamConsumedByAsyncForLoop() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let stream = try await client.listWidgets(n: 6)

        var sum: UInt32 = 0
        var count = 0
        for try await item in stream {
            sum += item.i
            count += 1
        }
        #expect(count == 6)
        #expect(sum == 0 + 1 + 2 + 3 + 4 + 5)
    }

    @Test func duplexSendFromSequenceReadWithTransform() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let sync = try await client.syncWidgets()
        try await sync.send(["hello", "world"].map(EchoRequest.init))

        var uppercased: [String] = []
        for try await response in sync {
            uppercased.append(response.value.uppercased())
        }
        #expect(uppercased == ["HELLO", "WORLD"])
    }
}
