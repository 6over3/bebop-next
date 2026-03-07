/// Handle to a server-stream call's result inside a `Batch`.
public struct StreamRef<Response: BebopRecord>: Sendable {
    public let callId: Int32

    @usableFromInline
    init(callId: Int32) {
        self.callId = callId
    }
}
