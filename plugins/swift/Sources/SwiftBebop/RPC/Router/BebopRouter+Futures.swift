extension BebopRouter {
    private func requireOwner(_ ctx: RpcContext) throws -> String {
        let peer = ctx[PeerInfoKey.self]
        if let identity = peer?.authInfo?.identity { return identity }
        if let addr = peer?.remoteAddress { return addr }
        throw BebopRpcError(code: .unauthenticated)
    }

    // MARK: - Dispatch (method ID 2)

    func handleDispatch(payload: [UInt8], ctx: RpcContext) async throws -> [UInt8] {
        guard let store = futureStore else {
            throw BebopRpcError(code: .unimplemented, detail: "futures disabled")
        }

        let owner = try requireOwner(ctx)
        try await runInterceptors(methodId: BebopReservedMethod.dispatch, ctx: ctx)

        let req = try FutureDispatchRequest.decode(from: payload)

        guard let methodId = req.methodId else {
            throw BebopRpcError(code: .invalidArgument, detail: "missing method_id")
        }
        guard let innerPayload = req.payload else {
            throw BebopRpcError(code: .invalidArgument, detail: "missing payload")
        }

        switch methodId {
        case BebopReservedMethod.dispatch, BebopReservedMethod.resolve, BebopReservedMethod.cancel:
            throw BebopRpcError(
                code: .invalidArgument, detail: "cannot dispatch reserved method"
            )
        default:
            break
        }

        switch methods[methodId] {
        case .unary?:
            break
        case nil
            where methodId == BebopReservedMethod.discovery
            || methodId == BebopReservedMethod.batch:
            break
        case .serverStream?, .clientStream?, .duplexStream?:
            throw BebopRpcError(
                code: .invalidArgument,
                detail: "only unary methods can be dispatched"
            )
        case nil:
            throw BebopRpcError(code: .notFound, detail: "unknown method")
        }

        let deadline = req.deadline

        let innerCtx = RpcContext(
            metadata: ctx.metadata.merging(req.metadata ?? [:]) { _, new in new },
            deadline: deadline
        )
        innerCtx[PeerInfoKey.self] = ctx[PeerInfoKey.self]

        let id = try await store.register(
            ctx: innerCtx, idempotencyKey: req.idempotencyKey, owner: owner,
            discardResult: req.discardResult ?? false
        ) {
            [self] futureId in
            do {
                let result: [UInt8] = if let deadline {
                    try await withDeadline(deadline) {
                        try await self.unary(methodId: methodId, payload: innerPayload, ctx: innerCtx)
                    }
                } else {
                    try await unary(methodId: methodId, payload: innerPayload, ctx: innerCtx)
                }
                return FutureResult(
                    id: futureId,
                    outcome: .success(FutureSuccess(payload: result, metadata: innerCtx.responseMetadata))
                )
            } catch is CancellationError {
                return FutureResult(
                    id: futureId,
                    outcome: .error(RpcError(code: .cancelled, detail: "future cancelled"))
                )
            } catch let error as BebopRpcError {
                return FutureResult(id: futureId, outcome: .error(error.toWire()))
            } catch {
                return FutureResult(
                    id: futureId,
                    outcome: .error(RpcError(code: .internal))
                )
            }
        }

        return FutureHandle(id: id).serializedData()
    }

    // MARK: - Resolve (method ID 3)

    func handleResolve(payload: [UInt8], ctx: RpcContext) async throws -> AsyncThrowingStream<
        StreamElement, Error
    > {
        guard let store = futureStore else {
            throw BebopRpcError(code: .unimplemented, detail: "futures disabled")
        }

        let owner = try requireOwner(ctx)
        try await runInterceptors(methodId: BebopReservedMethod.resolve, ctx: ctx)

        let req = try FutureResolveRequest.decode(from: payload)
        let requestedIds = req.ids
        let (immediate, stream) = await store.subscribe(futureIds: requestedIds, owner: owner)

        return AsyncThrowingStream { continuation in
            let task = Task {
                for result in immediate {
                    continuation.yield(StreamElement(bytes: result.serializedData()))
                }

                if let requestedIds {
                    let target = Set(requestedIds)
                    var resolved = Set(immediate.map(\.id))

                    if target.isSubset(of: resolved) {
                        continuation.finish()
                        return
                    }

                    for await result in stream {
                        if Task.isCancelled { break }
                        continuation.yield(StreamElement(bytes: result.serializedData()))
                        resolved.insert(result.id)
                        if target.isSubset(of: resolved) { break }
                    }
                } else {
                    for await result in stream {
                        if Task.isCancelled { break }
                        continuation.yield(StreamElement(bytes: result.serializedData()))
                    }
                }

                continuation.finish()
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    // MARK: - Cancel (method ID 4)

    func handleCancel(payload: [UInt8], ctx: RpcContext) async throws -> [UInt8] {
        guard let store = futureStore else {
            throw BebopRpcError(code: .unimplemented, detail: "futures disabled")
        }

        let owner = try requireOwner(ctx)
        try await runInterceptors(methodId: BebopReservedMethod.cancel, ctx: ctx)

        let req = try FutureCancelRequest.decode(from: payload)
        await store.cancel(id: req.id, owner: owner)
        return BebopEmpty().serializedData()
    }
}
