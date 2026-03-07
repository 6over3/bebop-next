import Testing

@testable import SwiftBebop

private enum StringKey: AttachmentKey {
    typealias Value = String
}

private enum IntKey: AttachmentKey {
    typealias Value = Int
}

private enum BoolKey: AttachmentKey {
    typealias Value = Bool
}

private enum BytesKey: AttachmentKey {
    typealias Value = [UInt8]
}

@Suite struct RpcContextTests {
    // MARK: - Attachments

    @Test func setAndGetAttachment() {
        let ctx = RpcContext()
        ctx[StringKey.self] = "hello"
        #expect(ctx[StringKey.self] == "hello")
    }

    @Test func attachmentMissing() {
        let ctx = RpcContext()
        #expect(ctx[StringKey.self] == nil)
    }

    @Test func attachmentOverwrite() {
        let ctx = RpcContext()
        ctx[StringKey.self] = "first"
        ctx[StringKey.self] = "second"
        #expect(ctx[StringKey.self] == "second")
    }

    @Test func attachmentRemove() {
        let ctx = RpcContext()
        ctx[IntKey.self] = 42
        ctx[IntKey.self] = nil
        #expect(ctx[IntKey.self] == nil)
    }

    @Test func attachmentDifferentTypes() {
        let ctx = RpcContext()
        ctx[StringKey.self] = "hello"
        ctx[IntKey.self] = 42
        ctx[BoolKey.self] = true
        ctx[BytesKey.self] = [UInt8](repeating: 0xFF, count: 4)

        #expect(ctx[StringKey.self] == "hello")
        #expect(ctx[IntKey.self] == 42)
        #expect(ctx[BoolKey.self] == true)
        #expect(ctx[BytesKey.self] == [0xFF, 0xFF, 0xFF, 0xFF])
    }

    @Test func attachmentsNotCopiedByDeriving() {
        let ctx = RpcContext(metadata: ["a": "1"])
        ctx[StringKey.self] = "original"
        let derived = ctx.deriving(appending: ["b": "2"])
        #expect(derived[StringKey.self] == nil)
    }

    @Test func attachmentsNotCopiedByForwarding() {
        let ctx = RpcContext(metadata: ["a": "1"])
        ctx[StringKey.self] = "original"
        let forwarded = ctx.forwarding()
        #expect(forwarded[StringKey.self] == nil)
    }

    // MARK: - Response metadata

    @Test func setAndGetReplyMetadata() {
        let ctx = RpcContext()
        ctx.setResponseMetadata("x-request-id", "abc")
        #expect(ctx.responseMetadata["x-request-id"] == "abc")
    }

    @Test func responseMetadataStartsEmpty() {
        let ctx = RpcContext()
        #expect(ctx.responseMetadata.isEmpty)
    }

    // MARK: - Derivation

    @Test func derivingAppendsMetadata() {
        let ctx = RpcContext(metadata: ["a": "1", "b": "2"])
        let derived = ctx.deriving(appending: ["b": "override", "c": "3"])
        #expect(derived.metadata == ["a": "1", "b": "override", "c": "3"])
    }

    @Test func derivingPreservesDeadline() {
        let deadline = BebopTimestamp(fromNow: .seconds(30))
        let ctx = RpcContext(metadata: [:], deadline: deadline)
        let derived = ctx.deriving(appending: ["key": "value"])
        #expect(derived.deadline == deadline)
    }

    @Test func forwardingCopiesMetadataAndDeadline() {
        let deadline = BebopTimestamp(fromNow: .seconds(10))
        let ctx = RpcContext(metadata: ["auth": "token"], deadline: deadline)
        let forwarded = ctx.forwarding()
        #expect(forwarded.metadata == ["auth": "token"])
        #expect(forwarded.deadline == deadline)
    }

    @Test func forwardingDoesNotCarryReplyMetadata() {
        let ctx = RpcContext(metadata: ["a": "1"])
        ctx.setResponseMetadata("reply-key", "reply-value")
        let forwarded = ctx.forwarding()
        #expect(forwarded.responseMetadata.isEmpty)
    }

    @Test func freshContextHasNoMetadata() {
        let ctx = RpcContext()
        #expect(ctx.metadata.isEmpty)
        #expect(ctx.deadline == nil)
        #expect(ctx.methodId == 0)
    }

    // MARK: - Cancellation

    @Test func cancellationStartsFalse() {
        let ctx = RpcContext()
        #expect(!ctx.isCancelled)
    }

    @Test func cancelSetsCancelledTrue() {
        let ctx = RpcContext()
        ctx.cancel()
        #expect(ctx.isCancelled)
    }

    // MARK: - Batch context

    @Test func batchContextMergesUpstreamMetadata() {
        let ctx = RpcContext(
            methodId: 1, metadata: ["auth": "token"], deadline: nil
        )
        let batch = ctx.makeBatchContext(upstreamMetadata: ["widget-id": "42"])
        #expect(batch.metadata["auth"] == "token")
        #expect(batch.metadata["widget-id"] == "42")
    }

    @Test func batchContextUpstreamOverridesBatch() {
        let ctx = RpcContext(
            methodId: 1, metadata: ["key": "batch-value"], deadline: nil
        )
        let batch = ctx.makeBatchContext(upstreamMetadata: ["key": "upstream-value"])
        #expect(batch.metadata["key"] == "upstream-value")
    }

    // MARK: - Cursor

    @Test func cursorDefaultsToZero() {
        let ctx = RpcContext()
        #expect(ctx.cursor == 0)
    }

    @Test func cursorSetViaInit() {
        let ctx = RpcContext(metadata: [:], cursor: 500)
        #expect(ctx.cursor == 500)
    }

    @Test func derivingPreservesCursor() {
        let ctx = RpcContext(metadata: ["a": "1"], cursor: 42)
        let derived = ctx.deriving(appending: ["b": "2"])
        #expect(derived.cursor == 42)
    }

    @Test func forwardingPreservesCursor() {
        let ctx = RpcContext(metadata: ["a": "1"], cursor: 99)
        let forwarded = ctx.forwarding()
        #expect(forwarded.cursor == 99)
    }

    @Test func batchContextDoesNotPropagateCursor() {
        let ctx = RpcContext(
            methodId: 1, metadata: [:], deadline: nil, cursor: 42
        )
        let batch = ctx.makeBatchContext()
        #expect(batch.cursor == 0)
    }
}
