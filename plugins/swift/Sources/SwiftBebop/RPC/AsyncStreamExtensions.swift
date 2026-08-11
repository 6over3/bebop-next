public extension AsyncThrowingStream where Element == [UInt8], Failure == Error {
    func decode<T: BebopRecord>(_: T.Type) -> AsyncThrowingStream<T, Error> {
        BebopStreams.map(self) { try T.decode(from: $0) }
    }
}
