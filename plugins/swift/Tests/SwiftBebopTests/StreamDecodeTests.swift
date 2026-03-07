import Testing

@testable import SwiftBebop

@Suite struct StreamDecodeTests {
    @Test func decodesTypedStream() async throws {
        let rawStream = AsyncThrowingStream<[UInt8], Error> { c in
            c.yield(EchoResponse(value: "a").serializedData())
            c.yield(EchoResponse(value: "b").serializedData())
            c.finish()
        }

        let typed = rawStream.decode(EchoResponse.self)
        var values: [String] = []
        for try await item in typed {
            values.append(item.value)
        }
        #expect(values == ["a", "b"])
    }

    @Test func emptyStreamDecodes() async throws {
        let rawStream = AsyncThrowingStream<[UInt8], Error> { c in
            c.finish()
        }
        let typed = rawStream.decode(EchoResponse.self)
        var count = 0
        for try await _ in typed {
            count += 1
        }
        #expect(count == 0)
    }

    @Test func errorPropagates() async {
        let rawStream = AsyncThrowingStream<[UInt8], Error> { c in
            c.yield(EchoResponse(value: "ok").serializedData())
            c.finish(throwing: BebopRpcError(code: .internal))
        }
        let typed = rawStream.decode(EchoResponse.self)
        var got: [String] = []
        do {
            for try await item in typed {
                got.append(item.value)
            }
            Issue.record("should have thrown")
        } catch {
            #expect(got == ["ok"])
        }
    }

    @Test func invalidBytesThrows() async {
        let rawStream = AsyncThrowingStream<[UInt8], Error> { c in
            c.yield([0xFF])
            c.finish()
        }
        let typed = rawStream.decode(EchoResponse.self)
        do {
            for try await _ in typed {}
            Issue.record("should have thrown")
        } catch {}
    }
}
