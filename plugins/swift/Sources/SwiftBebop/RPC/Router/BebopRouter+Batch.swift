extension BatchCall {
  /// -1 means use own payload, no dependency.
  fileprivate var hasDependency: Bool { inputFrom >= 0 }
}

extension BebopRouter {
  func handleBatch(payload: [UInt8], ctx: RpcContext) async throws -> [UInt8] {
    let request = try BatchRequest.decode(from: payload)
    let calls = request.calls

    guard !calls.isEmpty else {
      return BatchResponse(results: []).serializedData()
    }

    guard UInt(calls.count) <= config.maxBatchSize else {
      throw BebopRpcError(code: .resourceExhausted, detail: "batch too large")
    }

    try validateBatchCalls(calls)

    let layers = buildExecutionLayers(calls)
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

    let results = calls.map { call in
      BatchResult(
        callId: call.callId,
        outcome: outcomes[call.callId]
          ?? .error(RpcError(code: .internal, detail: "missing outcome"))
      )
    }
    return BatchResponse(results: results).serializedData()
  }

  // MARK: - Validation

  private func validateBatchCalls(_ calls: [BatchCall]) throws {
    var seenIds = Set<Int32>(minimumCapacity: calls.count)
    for call in calls {
      guard call.callId >= 0 else {
        throw BebopRpcError(code: .invalidArgument, detail: "batch call_id must be >= 0")
      }
      guard seenIds.insert(call.callId).inserted else {
        throw BebopRpcError(code: .invalidArgument, detail: "duplicate call_id \(call.callId)")
      }
      if call.hasDependency {
        guard call.inputFrom < call.callId, seenIds.contains(call.inputFrom) else {
          throw BebopRpcError(
            code: .invalidArgument,
            detail: "call \(call.callId) references invalid input_from \(call.inputFrom)"
          )
        }
      }
    }
  }

  // MARK: - Dependency graph

  private func buildExecutionLayers(_ calls: [BatchCall]) -> [[Int]] {
    let rootDepth = 0
    var callDepth: [Int32: Int] = [:]
    for call in calls {
      if call.hasDependency {
        callDepth[call.callId] = (callDepth[call.inputFrom] ?? rootDepth) + 1
      } else {
        callDepth[call.callId] = rootDepth
      }
    }

    var depthBuckets: [Int: [Int]] = [:]
    for (index, call) in calls.enumerated() {
      let depth = callDepth[call.callId] ?? rootDepth
      depthBuckets[depth, default: []].append(index)
    }

    guard let maxDepth = depthBuckets.keys.max() else { return [] }
    return (rootDepth...maxDepth).compactMap { depthBuckets[$0] }
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
      case .success(let success):
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

    let callCtx = ctx.makeBatchContext(upstreamMetadata: upstreamMeta)

    do {
      try await runInterceptors(methodId: call.methodId, ctx: callCtx)

      switch reg {
      case .unary(let dispatch):
        let result = try await dispatch(resolvedPayload, callCtx)
        return .success(BatchSuccess(payloads: [result], metadata: callCtx.responseMetadata))

      case .serverStream(let dispatch):
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
