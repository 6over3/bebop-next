/// A typed client-streaming request sink with one terminal response.
public struct ClientStream<Request: Sendable, Reply: Sendable, Metadata: Sendable>: Sendable {
    private let sendRequest: @Sendable (Request) async throws -> Void
    private let finishRequests: @Sendable () async throws -> Response<Reply, Metadata>

    /// Creates a stream from transport-provided send and finish operations.
    public init(
        send: @escaping @Sendable (Request) async throws -> Void,
        finish: @escaping @Sendable () async throws -> Response<Reply, Metadata>
    ) {
        sendRequest = send
        finishRequests = finish
    }

    /// Sends one request, respecting transport backpressure.
    public func send(_ request: Request) async throws {
        try await sendRequest(request)
    }

    /// Closes the request stream and returns the server's response.
    public func finish() async throws -> Response<Reply, Metadata> {
        try await finishRequests()
    }

    /// Sends an asynchronous request sequence, closes the stream, and returns the response.
    public func send<Requests: AsyncSequence>(
        _ requests: Requests
    ) async throws -> Response<Reply, Metadata> where Requests.Element == Request {
        for try await request in requests {
            try Task.checkCancellation()
            try await sendRequest(request)
        }
        return try await finishRequests()
    }

    /// Sends a request sequence, closes the stream, and returns the response.
    public func send<Requests: Sequence>(
        _ requests: Requests
    ) async throws -> Response<Reply, Metadata> where Requests.Element == Request {
        for request in requests {
            try Task.checkCancellation()
            try await sendRequest(request)
        }
        return try await finishRequests()
    }

    /// Adapts the transport's request and response values without buffering them.
    public func map<MappedRequest: Sendable, MappedReply: Sendable>(
        request encode: @escaping @Sendable (MappedRequest) throws -> Request,
        response decode: @escaping @Sendable (Reply) throws -> MappedReply
    ) -> ClientStream<MappedRequest, MappedReply, Metadata> {
        ClientStream<MappedRequest, MappedReply, Metadata>(
            send: { try await sendRequest(encode($0)) },
            finish: { try await finishRequests().map(decode) }
        )
    }
}

/// A typed bidirectional RPC stream with independent request and response flows.
public struct DuplexStream<Request: Sendable, Reply: Sendable, Metadata: Sendable>:
    AsyncSequence, Sendable
{
    public typealias Element = Reply
    public typealias Failure = any Error

    private let sendRequest: @Sendable (Request) async throws -> Void
    private let finishRequests: @Sendable () async throws -> Void

    /// Responses arrive independently of request production.
    public let responses: StreamResponse<Reply, Metadata>

    /// Trailing response metadata, available when the response stream completes.
    public var metadata: Metadata {
        get async { await responses.metadata }
    }

    /// Creates a duplex stream from transport-provided operations.
    public init(
        send: @escaping @Sendable (Request) async throws -> Void,
        finish: @escaping @Sendable () async throws -> Void,
        responses: StreamResponse<Reply, Metadata>
    ) {
        sendRequest = send
        finishRequests = finish
        self.responses = responses
    }

    /// Sends one request, respecting transport backpressure.
    public func send(_ request: Request) async throws {
        try await sendRequest(request)
    }

    /// Closes the request side while leaving responses available for consumption.
    public func finish() async throws {
        try await finishRequests()
    }

    /// Iterates responses from the server.
    public func makeAsyncIterator() -> StreamResponse<Reply, Metadata>.Iterator {
        responses.makeAsyncIterator()
    }

    /// Sends an asynchronous request sequence and closes the request side when it ends.
    public func send<Requests: AsyncSequence>(
        _ requests: Requests
    ) async throws where Requests.Element == Request {
        for try await request in requests {
            try Task.checkCancellation()
            try await sendRequest(request)
        }
        try await finishRequests()
    }

    /// Sends a request sequence and closes the request side when it ends.
    public func send<Requests: Sequence>(
        _ requests: Requests
    ) async throws where Requests.Element == Request {
        for request in requests {
            try Task.checkCancellation()
            try await sendRequest(request)
        }
        try await finishRequests()
    }

    /// Sends an asynchronous sequence while `receive` consumes responses.
    public func exchange<Requests: AsyncSequence & Sendable, Result: Sendable>(
        _ requests: Requests,
        receive: @escaping @Sendable (StreamResponse<Reply, Metadata>) async throws -> Result
    ) async throws -> Result where Requests.Element == Request {
        try await runExchange(
            sending: { try await send(requests) },
            receive: receive
        )
    }

    /// Sends a sequence while `receive` consumes responses.
    public func exchange<Requests: Sequence & Sendable, Result: Sendable>(
        _ requests: Requests,
        receive: @escaping @Sendable (StreamResponse<Reply, Metadata>) async throws -> Result
    ) async throws -> Result where Requests.Element == Request {
        try await runExchange(
            sending: { try await send(requests) },
            receive: receive
        )
    }

    private func runExchange<Result: Sendable>(
        sending: @escaping @Sendable () async throws -> Void,
        receive: @escaping @Sendable (StreamResponse<Reply, Metadata>) async throws -> Result
    ) async throws -> Result {
        try await withThrowingTaskGroup(of: ExchangeTaskResult<Result>.self) { group in
            group.addTask {
                try await sending()
                return .sendingFinished
            }
            group.addTask {
                .received(try await receive(responses))
            }

            while let outcome = try await group.next() {
                if case .received(let result) = outcome {
                    group.cancelAll()
                    return result
                }
            }
            throw CancellationError()
        }
    }

    /// Adapts both stream directions without buffering either one.
    public func map<MappedRequest: Sendable, MappedReply: Sendable>(
        request encode: @escaping @Sendable (MappedRequest) throws -> Request,
        response decode: @escaping @Sendable (Reply) throws -> MappedReply
    ) -> DuplexStream<MappedRequest, MappedReply, Metadata> {
        DuplexStream<MappedRequest, MappedReply, Metadata>(
            send: { try await sendRequest(encode($0)) },
            finish: finishRequests,
            responses: responses.map(decode)
        )
    }
}

private enum ExchangeTaskResult<Result: Sendable>: Sendable {
    case sendingFinished
    case received(Result)
}
