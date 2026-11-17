import Foundation
import Testing

@testable import SwiftBebop

private struct RpcConformance: Decodable, Sendable {
    struct FrameCase: Decodable, Sendable {
        let name: String
        let hex: String
        let valid: Bool
        let flags: UInt8
    }

    struct BatchCase: Decodable, Sendable {
        struct Call: Decodable, Sendable {
            let id: Int32
            let inputFrom: Int32
        }
        let name: String
        let calls: [Call]
        let valid: Bool
    }

    let frames: [FrameCase]
    let batches: [BatchCase]

    static func load() throws -> Self {
        var root = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 {
            root.deleteLastPathComponent()
        }
        let url = root.appendingPathComponent("tests/fixtures/rpc/conformance.json")
        return try JSONDecoder().decode(Self.self, from: Data(contentsOf: url))
    }
}

@Suite struct RpcConformanceTests {
    @Test func framesMatchSharedFixtures() throws {
        for fixture in try RpcConformance.load().frames {
            let hex = Array(fixture.hex)
            let bytes = try stride(from: 0, to: hex.count, by: 2).map { index in
                try #require(UInt8(String(hex[index...index + 1]), radix: 16))
            }
            if fixture.valid {
                let frame = try Frame.decode(from: bytes)
                #expect(
                    frame.header.flags.rawValue == fixture.flags, Comment(rawValue: fixture.name))
            } else {
                #expect(throws: BebopRpcError.self, Comment(rawValue: fixture.name)) {
                    try Frame.decode(from: bytes)
                }
            }
        }
    }

    @Test func batchesMatchSharedFixtures() async throws {
        let router = buildRouter()
        for fixture in try RpcConformance.load().batches {
            let context = RpcContext(methodId: 1, metadata: [:], deadline: nil)
            let calls = fixture.calls.map { call in
                BatchCall(
                    callId: call.id, methodId: getWidgetId,
                    payload: EchoRequest(value: "shared").encode(), inputFrom: call.inputFrom)
            }
            let payload = BatchRequest(calls: calls).encode()
            if fixture.valid {
                let bytes = try await router.unary(methodId: 1, payload: payload, ctx: context)
                let response = try BatchResponse.decode(from: bytes)
                #expect(response.results.map(\.callId) == fixture.calls.map(\.id))
                for result in response.results {
                    guard case .success = result.outcome else {
                        Issue.record("Expected success for \(fixture.name)")
                        continue
                    }
                }
            } else {
                await #expect(throws: BebopRpcError.self, Comment(rawValue: fixture.name)) {
                    try await router.unary(methodId: 1, payload: payload, ctx: context)
                }
            }
        }
    }

    @Test func cancellationFlowsToChildrenOnly() {
        let parent = RpcContext()
        let child = parent.deriving(appending: [:])
        let sibling = parent.makeBatchContext(methodId: 1024)
        child.cancel()
        #expect(!parent.isCancelled && !sibling.isCancelled)
        parent.cancel()
        #expect(sibling.isCancelled && parent.forwarding().isCancelled)
    }
}
