/// Handle to a unary call's result inside a `Batch`.
public struct CallRef<Response: BebopRecord>: Sendable {
  public let callId: Int32

  @usableFromInline
  init(callId: Int32) {
    self.callId = callId
  }
}
