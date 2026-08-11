private extension BatchCall {
    /// -1 means use own payload, no dependency.
    var hasDependency: Bool { inputFrom >= 0 }
}

extension BebopRouter {
    func handleBatch(payload: [UInt8], ctx: RpcContext) async throws -> [UInt8] {
        let request = try BatchRequest.decode(from: payload, limits: config.decodeLimits)
        let calls = request.calls

        guard !calls.isEmpty else {
            return BatchResponse(results: []).encode()
        }

        guard UInt(calls.count) <= config.maxBatchSize else {
            throw BebopRpcError(code: .resourceExhausted, detail: "batch too large")
        }

        let layers = try buildExecutionLayers(calls)
        var outcomes: [Int32: BatchOutcome] = [:]

        for layer in layers {
            if let deadline = ctx.deadline, deadline.isPast {
                for index in layer {
                    let call = calls[index]
                    if outcomes[call.callId] == nil {
                        outcomes[call.callId] = .error(
                            RpcError(
                                code: .deadlineExceeded,
                                detail: "batch deadline exceeded"
                            ))
                    }
                }
                continue
            }

            let snapshot = outcomes
            let layerResults = await withTaskGroup(
                of: (Int32, BatchOutcome).self,
                returning: [Int32: BatchOutcome].self
            ) { group in
                for index in layer {
                    let call = calls[index]
                    group.addTask {
                        await (call.callId, self.executeBatchCall(call, outcomes: snapshot, ctx: ctx))
                    }
                }
                return await group.reduce(into: [:]) { $0[$1.0] = $1.1 }
            }
            outcomes.merge(layerResults) { _, new in new }
        }

        var results: [BatchResult] = []
        results.reserveCapacity(calls.count)
        for call in calls {
            guard let outcome = outcomes[call.callId] else {
                throw BebopRpcError(
                    code: .internal,
                    detail: "batch execution did not produce outcome for call \(call.callId)"
                )
            }
            results.append(BatchResult(callId: call.callId, outcome: outcome))
        }
        return BatchResponse(results: results).encode()
    }

    // MARK: - Dependency graph

    private func buildExecutionLayers(_ calls: [BatchCall]) throws -> [[Int]] {
        var seenIds = Set<Int32>(minimumCapacity: calls.count)
        var callDepth: [Int32: Int] = [:]
        var layers: [[Int]] = []

        for (index, call) in calls.enumerated() {
            guard call.callId >= 0 else {
                throw BebopRpcError(code: .invalidArgument, detail: "batch call_id must be >= 0")
            }
            guard seenIds.insert(call.callId).inserted else {
                throw BebopRpcError(code: .invalidArgument, detail: "duplicate call_id \(call.callId)")
            }
            let depth: Int
            if call.hasDependency {
                guard call.inputFrom < call.callId,
                      let dependencyDepth = callDepth[call.inputFrom]
                else {
                    throw BebopRpcError(
                        code: .invalidArgument,
                        detail: "call \(call.callId) references invalid input_from \(call.inputFrom)"
                    )
                }
                depth = dependencyDepth + 1
            } else {
                depth = 0
            }
            callDepth[call.callId] = depth
            while layers.count <= depth { layers.append([]) }
            layers[depth].append(index)
        }
        return layers
    }

    // MARK: - Single call execution

    private func executeBatchCall(
        _ call: BatchCall,
        outcomes: [Int32: BatchOutcome],
        ctx: RpcContext
    ) async -> BatchOutcome {
        let resolvedPayload: [UInt8]
        var upstreamMeta: [String: String] = [:]

        if call.hasDependency {
            guard let depOutcome = outcomes[call.inputFrom] else {
                return .error(
                    RpcError(code: .invalidArgument, detail: "dependency \(call.inputFrom) not resolved"))
            }
            switch depOutcome {
            case let .success(success):
                guard let first = success.payloads.first else {
                    return .error(
                        RpcError(code: .invalidArgument, detail: "dependency \(call.inputFrom) has no payload"))
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
            try await runInterceptors(methodId: call.methodId, ctx: callCtx)

            switch reg {
            case let .unary(dispatch):
                let result = try await dispatch(resolvedPayload, callCtx)
                return .success(BatchSuccess(payloads: [result], metadata: callCtx.responseMetadata))

            case let .serverStream(dispatch):
                let stream = try await dispatch(resolvedPayload, callCtx)
                var payloads: [[UInt8]] = []
                for try await element in stream {
                    guard UInt(payloads.count) < config.maxBatchStreamElements else {
                        throw BebopRpcError(code: .resourceExhausted, detail: "batch stream too large")
                    }
                    payloads.append(element.bytes)
                }
                return .success(BatchSuccess(payloads: payloads, metadata: callCtx.responseMetadata))

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
