extension BatchCall {
    /// -1 means use own payload, no dependency.
    fileprivate var hasDependency: Bool { inputFrom >= 0 }
}

extension BebopRouter {
    func handleBatch(payload: [UInt8], ctx: RpcContext) async throws -> [UInt8] {
        let request = try BatchRequest.decode(from: payload, limits: config.decodeLimits)
        let calls = request.calls
        let batchContext = ctx.makeBatchContext(
            methodId: ctx.methodId, upstreamMetadata: request.metadata)

        guard !calls.isEmpty else {
            return BatchResponse(results: []).encode()
        }

        guard UInt(calls.count) <= config.maxBatchSize else {
            throw BebopRpcError(code: .resourceExhausted, detail: "batch too large")
        }

        let dependents = try validateBatch(calls)
        var outcomes = [BatchOutcome?](repeating: nil, count: calls.count)
        await withTaskGroup(of: (Int, BatchOutcome).self) { group in
            for (index, call) in calls.enumerated() where !call.hasDependency {
                group.addTask {
                    (index, await self.executeBatchCall(call, dependency: nil, ctx: batchContext))
                }
            }
            while let (index, outcome) = await group.next() {
                outcomes[index] = outcome
                for dependent in dependents[index] {
                    let call = calls[dependent]
                    group.addTask {
                        (
                            dependent,
                            await self.executeBatchCall(
                                call, dependency: outcome, ctx: batchContext)
                        )
                    }
                }
            }
        }
        let results = try calls.enumerated().map { index, call in
            guard let outcome = outcomes[index] else {
                throw BebopRpcError(code: .internal, detail: "missing batch outcome")
            }
            return BatchResult(callId: call.callId, outcome: outcome)
        }
        return BatchResponse(results: results).encode()
    }

    private func validateBatch(_ calls: [BatchCall]) throws -> [[Int]] {
        var earlier: [Int32: Int] = [:]
        var dependents = [[Int]](repeating: [], count: calls.count)
        for (index, call) in calls.enumerated() {
            guard call.callId >= 0, earlier[call.callId] == nil else {
                throw BebopRpcError(
                    code: .invalidArgument, detail: "invalid or duplicate batch call ID")
            }
            guard call.inputFrom >= -1 else {
                throw BebopRpcError(
                    code: .invalidArgument, detail: "input_from must be -1 or an earlier call ID")
            }
            if call.hasDependency {
                guard let dependency = earlier[call.inputFrom] else {
                    throw BebopRpcError(code: .invalidArgument, detail: "invalid batch dependency")
                }
                dependents[dependency].append(index)
            }
            earlier[call.callId] = index
        }
        return dependents
    }

    // MARK: - Single call execution

    private func executeBatchCall(
        _ call: BatchCall,
        dependency: BatchOutcome?,
        ctx: RpcContext
    ) async -> BatchOutcome {
        let resolvedPayload: [UInt8]
        var upstreamMeta: [String: String] = [:]

        if call.hasDependency {
            guard let depOutcome = dependency else {
                return .error(
                    RpcError(
                        code: .invalidArgument, detail: "dependency \(call.inputFrom) not resolved")
                )
            }
            switch depOutcome {
            case .success(let success):
                guard let first = success.payloads.first else {
                    return .error(
                        RpcError(
                            code: .invalidArgument,
                            detail: "dependency \(call.inputFrom) has no payload"))
                }
                resolvedPayload = first
                upstreamMeta = success.metadata
            case .error, .unknown:
                return .error(
                    RpcError(code: .invalidArgument, detail: "dependency \(call.inputFrom) failed"))
            }
        } else {
            resolvedPayload = call.payload
        }

        guard let reg = methods[call.methodId] else {
            return .error(RpcError(code: .notFound, detail: "method \(call.methodId)"))
        }

        let callCtx = ctx.makeBatchContext(
            methodId: call.methodId,
            upstreamMetadata: upstreamMeta
        )

        do {
            switch reg {
            case .unary(let dispatch):
                let result = try await runInterceptors(methodId: call.methodId, ctx: callCtx) {
                    try await dispatch(resolvedPayload, callCtx)
                }
                return .success(
                    BatchSuccess(payloads: [result], metadata: callCtx.responseMetadata))

            case .serverStream(let dispatch):
                let stream = try await runInterceptors(methodId: call.methodId, ctx: callCtx) {
                    try await dispatch(resolvedPayload, callCtx)
                }
                var payloads: [[UInt8]] = []
                for try await element in stream {
                    guard UInt(payloads.count) < config.maxBatchStreamElements else {
                        throw BebopRpcError(
                            code: .resourceExhausted, detail: "batch stream too large")
                    }
                    payloads.append(element.bytes)
                }
                return .success(
                    BatchSuccess(payloads: payloads, metadata: callCtx.responseMetadata))

            case .clientStream, .duplexStream:
                return .error(
                    RpcError(
                        code: .invalidArgument,
                        detail: "batch does not support \(reg.methodType.rawValue) methods"
                    ))
            }
        } catch let error as BebopRpcError {
            return .error(error.toWire())
        } catch {
            return .error(RpcError(code: .internal))
        }
    }
}
