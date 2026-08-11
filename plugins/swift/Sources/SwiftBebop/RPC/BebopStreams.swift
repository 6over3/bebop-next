private actor PullingMapState<Element: Sendable> {
    private enum Status {
        case open
        case finished(Result<Element?, any Error>)
    }

    private var status = Status.open
    private var demand: CheckedContinuation<Void, any Error>?
    private var consumer: CheckedContinuation<Element?, any Error>?
    private var termination: (@Sendable () async -> Void)?

    init(termination: @escaping @Sendable () async -> Void) {
        self.termination = termination
    }

    func next() async throws -> Element? {
        try Task.checkCancellation()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation {
                (continuation: CheckedContinuation<Element?, any Error>) in
                guard case .open = status else {
                    if case let .finished(result) = status {
                        continuation.resume(with: result)
                    }
                    return
                }
                guard consumer == nil else {
                    continuation.resume(
                        throwing: BebopRpcError(
                            code: .invalidArgument, detail: "stream supports one consumer"))
                    return
                }
                consumer = continuation
                demand?.resume()
                demand = nil
            }
        } onCancel: {
            Task { await self.finish(.failure(CancellationError())) }
        }
    }

    func waitForDemand() async throws -> Bool {
        guard case .open = status else { return false }
        if consumer != nil { return true }
        try await withCheckedThrowingContinuation {
            (continuation: CheckedContinuation<Void, any Error>) in
            guard case .open = status else {
                continuation.resume(returning: ())
                return
            }
            demand = continuation
        }
        if case .open = status { return true }
        return false
    }

    func yield(_ element: Element) {
        guard case .open = status, let consumer else { return }
        self.consumer = nil
        consumer.resume(returning: element)
    }

    func finish(_ result: Result<Element?, any Error>) async {
        guard case .open = status else { return }
        status = .finished(result)

        demand?.resume(returning: ())
        demand = nil
        if let consumer {
            self.consumer = nil
            status = .finished(.success(nil))
            switch result {
            case .success(let element): consumer.resume(returning: element)
            case .failure(let error): consumer.resume(throwing: error)
            }
        }

        let termination = termination
        self.termination = nil
        await termination?()
    }
}

private final class PullingMapLifetime<Element: Sendable>: Sendable {
    let state: PullingMapState<Element>
    let worker: Task<Void, Never>

    init(state: PullingMapState<Element>, worker: Task<Void, Never>) {
        self.state = state
        self.worker = worker
    }

    deinit {
        worker.cancel()
        let state = state
        Task { await state.finish(.failure(CancellationError())) }
    }
}

public enum BebopStreams {
    public static func map<Input: Sendable, Output: Sendable>(
        _ source: AsyncThrowingStream<Input, Error>,
        onCancel: @escaping @Sendable () async -> Void = {},
        _ transform: @escaping @Sendable (Input) throws -> Output
    ) -> AsyncThrowingStream<Output, Error> {
        map([], then: { source }, onCancel: onCancel, transform)
    }

    public static func map<Input: Sendable, Output: Sendable>(
        _ source: @escaping @Sendable () async throws -> AsyncThrowingStream<Input, Error>,
        onCancel: @escaping @Sendable () async -> Void = {},
        _ transform: @escaping @Sendable (Input) throws -> Output
    ) -> AsyncThrowingStream<Output, Error> {
        map([], then: source, onCancel: onCancel, transform)
    }

    static func map<Input: Sendable, Output: Sendable>(
        _ leading: [Input],
        then source: AsyncThrowingStream<Input, Error>,
        onCancel: @escaping @Sendable () async -> Void = {},
        _ transform: @escaping @Sendable (Input) throws -> Output
    ) -> AsyncThrowingStream<Output, Error> {
        map(leading, then: { source }, onCancel: onCancel, transform)
    }

    private static func map<Input: Sendable, Output: Sendable>(
        _ leading: [Input],
        then source: @escaping @Sendable () async throws -> AsyncThrowingStream<Input, Error>,
        onCancel: @escaping @Sendable () async -> Void,
        _ transform: @escaping @Sendable (Input) throws -> Output
    ) -> AsyncThrowingStream<Output, Error> {
        let state = PullingMapState<Output>(termination: onCancel)
        let worker = Task {
            do {
                var iterator = try await source().makeAsyncIterator()
                for input in leading {
                    guard try await state.waitForDemand() else { return }
                    try Task.checkCancellation()
                    await state.yield(try transform(input))
                }
                while try await state.waitForDemand() {
                    try Task.checkCancellation()
                    guard let input = try await iterator.next() else {
                        await state.finish(.success(nil))
                        return
                    }
                    await state.yield(try transform(input))
                }
            } catch {
                await state.finish(.failure(error))
            }
        }
        let lifetime = PullingMapLifetime(state: state, worker: worker)
        return AsyncThrowingStream(unfolding: {
            _ = lifetime
            return try await withTaskCancellationHandler {
                try await state.next()
            } onCancel: {
                worker.cancel()
                Task { await state.finish(.failure(CancellationError())) }
            }
        })
    }
}

private actor RpcInboundState<Element: Sendable> {
    private enum Status {
        case open
        case finished((any Error)?)
    }

    private var status = Status.open
    private var receiver: CheckedContinuation<Element?, any Error>?
    private var sender: (Element, CheckedContinuation<Void, any Error>)?

    func send(_ element: Element) async throws {
        try Task.checkCancellation()
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation {
                (continuation: CheckedContinuation<Void, any Error>) in
                switch status {
                case .finished(let error):
                    if let error {
                        continuation.resume(throwing: error)
                    } else {
                        continuation.resume(
                            throwing: BebopRpcError(
                                code: .cancelled, detail: "request stream is closed"))
                    }
                case .open:
                    if let receiver {
                        self.receiver = nil
                        receiver.resume(returning: element)
                        continuation.resume()
                    } else if sender == nil {
                        sender = (element, continuation)
                    } else {
                        continuation.resume(
                            throwing: BebopRpcError(
                                code: .invalidArgument,
                                detail: "concurrent sends are not supported"))
                    }
                }
            }
        } onCancel: {
            Task { await self.cancel() }
        }
    }

    func next() async throws -> Element? {
        try Task.checkCancellation()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation {
                (continuation: CheckedContinuation<Element?, any Error>) in
                switch status {
                case .finished(let error):
                    if let error {
                        continuation.resume(throwing: error)
                    } else {
                        continuation.resume(returning: nil)
                    }
                case .open:
                    if let (element, sender) = sender {
                        self.sender = nil
                        sender.resume()
                        continuation.resume(returning: element)
                    } else if receiver == nil {
                        receiver = continuation
                    } else {
                        continuation.resume(
                            throwing: BebopRpcError(
                                code: .invalidArgument,
                                detail: "request stream supports one consumer"))
                    }
                }
            }
        } onCancel: {
            Task { await self.cancel() }
        }
    }

    func finish(throwing error: (any Error)? = nil) {
        guard case .open = status else { return }
        status = .finished(error)

        if let receiver {
            self.receiver = nil
            if let error {
                receiver.resume(throwing: error)
            } else {
                receiver.resume(returning: nil)
            }
        }
        if let (_, sender) = sender {
            self.sender = nil
            if let error {
                sender.resume(throwing: error)
            } else {
                sender.resume(
                    throwing: BebopRpcError(code: .cancelled, detail: "request stream is closed"))
            }
        }
    }

    private func cancel() {
        finish(throwing: CancellationError())
    }
}

public final class RpcInboundStream<Element: Sendable>: Sendable {
    private let state: RpcInboundState<Element>
    public let stream: AsyncThrowingStream<Element, Error>

    public init() {
        let state = RpcInboundState<Element>()
        self.state = state
        stream = AsyncThrowingStream(unfolding: { try await state.next() })
    }

    public func send(_ element: Element) async throws {
        try await state.send(element)
    }

    public func finish(throwing error: (any Error)? = nil) async {
        await state.finish(throwing: error)
    }
}
