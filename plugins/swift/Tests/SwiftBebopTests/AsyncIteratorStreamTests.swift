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
        let response = try await client.uploadWidgets { send in
            for value in ["one", "two", "three"] {
                try await send(EchoRequest(value: value))
            }
        }
        #expect(response.value.value == "one,two,three")
    }

    @Test func clientStreamSendNothing() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let response = try await client.uploadWidgets { _ in }
        #expect(response.value.value == "")
    }

    @Test func clientStreamSendSingleItem() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let response = try await client.uploadWidgets { send in
            try await send(EchoRequest(value: "solo"))
        }
        #expect(response.value.value == "solo")
    }

    @Test func duplexInterleavedSendAndReceive() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        try await client.syncWidgets { send, finish, responses in
            var iterator = responses.makeAsyncIterator()

            try await send(EchoRequest(value: "a"))
            let r1 = try await iterator.next()
            #expect(r1?.value == "a")

            try await send(EchoRequest(value: "b"))
            let r2 = try await iterator.next()
            #expect(r2?.value == "b")

            try await finish()
            let done = try await iterator.next()
            #expect(done == nil)
        }
    }

    @Test func duplexCollectAllAfterFinish() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        try await client.syncWidgets { send, finish, responses in
            try await send(EchoRequest(value: "x"))
            try await send(EchoRequest(value: "y"))
            try await send(EchoRequest(value: "z"))
            try await finish()

            var results: [String] = []
            for try await item in responses {
                results.append(item.value)
            }
            #expect(results == ["x", "y", "z"])
        }
    }

    @Test func duplexEarlyBreakOnResponses() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        try await client.syncWidgets { send, finish, responses in
            for i in 0 ..< 10 {
                try await send(EchoRequest(value: "msg\(i)"))
            }
            try await finish()

            var collected: [String] = []
            for try await item in responses {
                collected.append(item.value)
                if collected.count == 3 { break }
            }
            #expect(collected == ["msg0", "msg1", "msg2"])
        }
    }

    @Test func duplexEmptyStream() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        try await client.syncWidgets { _, finish, responses in
            try await finish()
            var count = 0
            for try await _ in responses {
                count += 1
            }
            #expect(count == 0)
        }
    }

    @Test func clientStreamFromAsyncGenerator() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let response = try await client.uploadWidgets { send in
            let source = AsyncStream<EchoRequest> { c in
                for word in ["alpha", "bravo", "charlie"] {
                    c.yield(EchoRequest(value: word))
                }
                c.finish()
            }
            for await request in source {
                try await send(request)
            }
        }
        #expect(response.value.value == "alpha,bravo,charlie")
    }

    @Test func duplexSendFromAsyncGeneratorReadInLoop() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        try await client.syncWidgets { send, finish, responses in
            let source = AsyncStream<String> { c in
                for i in 0 ..< 5 {
                    c.yield("item-\(i)")
                }
                c.finish()
            }

            for await value in source {
                try await send(EchoRequest(value: value))
            }
            try await finish()

            var received: [String] = []
            for try await response in responses {
                received.append(response.value)
            }
            #expect(received == ["item-0", "item-1", "item-2", "item-3", "item-4"])
        }
    }

    @Test func duplexConcurrentSendAndReceive() async throws {
        let client = WidgetServiceClient(channel: buildChannel())
        let received = Counter()
        try await client.syncWidgets { send, finish, responses in
            let reader = Task {
                var values: [String] = []
                for try await response in responses {
                    values.append(response.value)
                    await received.increment()
                }
                return values
            }

            let source = AsyncStream<String> { c in
                for i in 0 ..< 4 {
                    c.yield("msg-\(i)")
                }
                c.finish()
            }
            for await value in source {
                try await send(EchoRequest(value: value))
            }
            try await finish()

            let values = try await reader.value
            #expect(values == ["msg-0", "msg-1", "msg-2", "msg-3"])
        }
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
        try await client.syncWidgets { send, finish, responses in
            for word in ["hello", "world"] {
                try await send(EchoRequest(value: word))
            }
            try await finish()

            var uppercased: [String] = []
            for try await response in responses {
                uppercased.append(response.value.uppercased())
            }
            #expect(uppercased == ["HELLO", "WORLD"])
        }
    }
}
