import BebopPlugin

enum GenerateService {
  nonisolated(unsafe) static var definitionMap: [String: DefinitionDescriptor] = [:]

  static func generate(
    _ def: DefinitionDescriptor, options: GeneratorOptions
  ) throws -> [String] {
    guard options.services != .none else { return [] }

    guard let defName = def.name else {
      throw CodegenError.malformedDefinition("service missing name")
    }
    guard let serviceDef = def.serviceDef else {
      throw CodegenError.malformedDefinition("service '\(defName)' missing body")
    }
    let methods = serviceDef.methods ?? []
    guard !methods.isEmpty else { return [] }

    let vis = effectiveVisibility(for: def, options: options)
    let prefix = declPrefix(doc: def.documentation, decorators: def.decorators)

    let methodInfos = try methods.compactMap { m -> MethodInfo? in
      try resolveMethod(m, serviceName: defName)
    }

    var result: [String] = []

    result.append(
      try generateServiceEnum(
        defName, methods: methodInfos, prefix: prefix, vis: vis))

    if options.services == .server || options.services == .both {
      result.append(
        try generateHandlerProtocol(
          defName, methods: methodInfos, vis: vis))
      result.append(
        try generateHandlerRegistration(
          defName, methods: methodInfos, vis: vis))
    }

    if options.services == .client || options.services == .both {
      result.append(
        try generateClientStub(
          defName, methods: methodInfos, vis: vis))
      result.append(
        try generateBatchAccessor(
          defName, methods: methodInfos, vis: vis))
      result.append(
        try generateDispatchAccessor(
          defName, methods: methodInfos, vis: vis))
    }

    return result
  }

  // MARK: - Method resolution

  struct MethodInfo {
    let name: String
    let swiftName: String
    let doc: String?
    let methodType: RuntimeMethodType
    let methodId: UInt32
    let requestTypeName: String
    let responseTypeName: String
    let requestFqn: String
    let responseFqn: String
    let deconstructedParams: [(swiftName: String, swiftType: String, isOptional: Bool)]?
    let decorators: [DecoratorUsage]?
  }

  enum RuntimeMethodType: String {
    case unary, serverStream, clientStream, duplexStream
  }

  private static func resolveMethod(
    _ m: MethodDescriptor, serviceName: String
  ) throws -> MethodInfo {
    guard let name = m.name else {
      throw CodegenError.malformedDefinition("service '\(serviceName)' method missing name")
    }
    guard let descriptorMethodType = m.methodType else {
      throw CodegenError.malformedDefinition(
        "service '\(serviceName)' method '\(name)' missing method type")
    }
    guard let id = m.id else {
      throw CodegenError.malformedDefinition(
        "service '\(serviceName)' method '\(name)' missing id")
    }
    guard let reqType = m.requestType else {
      throw CodegenError.malformedDefinition(
        "service '\(serviceName)' method '\(name)' missing request type")
    }
    guard let resType = m.responseType else {
      throw CodegenError.malformedDefinition(
        "service '\(serviceName)' method '\(name)' missing response type")
    }

    let runtimeType = mapMethodType(descriptorMethodType)
    let reqTypeName = try TypeMapper.swiftType(for: reqType)
    let resTypeName = try TypeMapper.swiftType(for: resType)
    let reqFqn = reqType.definedFqn ?? ""
    let resFqn = resType.definedFqn ?? ""

    let deconstructed = resolveDeconstructedParams(for: reqType)

    return MethodInfo(
      name: name,
      swiftName: NamingPolicy.fieldName(name),
      doc: m.documentation,
      methodType: runtimeType,
      methodId: id,
      requestTypeName: reqTypeName,
      responseTypeName: resTypeName,
      requestFqn: reqFqn,
      responseFqn: resFqn,
      deconstructedParams: deconstructed,
      decorators: m.decorators
    )
  }

  /// Map descriptor MethodType (1-4) to runtime MethodType (0-3).
  private static func mapMethodType(_ dt: BebopPlugin.MethodType) -> RuntimeMethodType {
    switch dt {
    case .unary: return .unary
    case .serverStream: return .serverStream
    case .clientStream: return .clientStream
    case .duplexStream: return .duplexStream
    default: return .unary
    }
  }

  // MARK: - Deconstructed params

  private static func resolveDeconstructedParams(
    for type: TypeDescriptor
  ) -> [(swiftName: String, swiftType: String, isOptional: Bool)]? {
    guard type.kind == .defined, let fqn = type.definedFqn else { return nil }
    guard let def = definitionMap[fqn] else { return nil }

    let fields: [FieldDescriptor]?
    let isMessage: Bool

    if let structDef = def.structDef {
      fields = structDef.fields
      isMessage = false
    } else if let messageDef = def.messageDef {
      fields = messageDef.fields
      isMessage = true
    } else {
      return nil
    }

    let fieldList = fields ?? []
    guard fieldList.count <= 4 else { return nil }

    return fieldList.compactMap { f -> (String, String, Bool)? in
      guard let name = f.name, let fieldType = f.type else { return nil }
      guard let swiftType = try? TypeMapper.swiftType(for: fieldType) else { return nil }
      return (NamingPolicy.fieldName(name), swiftType, isMessage)
    }
  }

  // MARK: - Service enum generation

  private static func generateServiceEnum(
    _ serviceName: String, methods: [MethodInfo], prefix: String, vis: String
  ) throws -> String {
    let name = NamingPolicy.typeName(serviceName)

    var methodCases: [String] = []
    var nameCases: [String] = []
    var typeCases: [String] = []
    var reqUrlCases: [String] = []
    var resUrlCases: [String] = []

    for m in methods {
      methodCases.append("case \(m.swiftName) = 0x\(hex(m.methodId))")

      nameCases.append("case .\(m.swiftName): return \(quoted(m.name))")

      typeCases.append("case .\(m.swiftName): return .\(m.methodType.rawValue)")

      let reqUrl = typeUrl(m.requestFqn)
      reqUrlCases.append("case .\(m.swiftName): return \(quoted(reqUrl))")

      let resUrl = typeUrl(m.responseFqn)
      resUrlCases.append("case .\(m.swiftName): return \(quoted(resUrl))")
    }

    let methodCasesStr = methodCases.map { indent($0, 2) }.joined(separator: "\n")
    let nameBody = nameCases.map { indent($0, 4) }.joined(separator: "\n")
    let typeBody = typeCases.map { indent($0, 4) }.joined(separator: "\n")
    let reqUrlBody = reqUrlCases.map { indent($0, 4) }.joined(separator: "\n")
    let resUrlBody = resUrlCases.map { indent($0, 4) }.joined(separator: "\n")

    let methodInfoEntries = methods.map { m in
      let reqUrl = typeUrl(m.requestFqn)
      let resUrl = typeUrl(m.responseFqn)
      return """
        MethodInfo(
            name: \(quoted(m.name)),
            methodId: 0x\(hex(m.methodId)),
            methodType: .\(m.methodType.rawValue),
            requestTypeUrl: \(quoted(reqUrl)),
            responseTypeUrl: \(quoted(resUrl))
        )
        """
    }.joined(separator: ",\n")

    return """
      \(prefix)\(vis)enum \(name): BebopServiceDefinition {
          \(vis)enum Method: UInt32, BebopServiceMethod, CaseIterable {
      \(methodCasesStr)

              \(vis)var name: String {
                  switch self {
      \(nameBody)
                  }
              }

              \(vis)var methodType: MethodType {
                  switch self {
      \(typeBody)
                  }
              }

              \(vis)var requestTypeUrl: String {
                  switch self {
      \(reqUrlBody)
                  }
              }

              \(vis)var responseTypeUrl: String {
                  switch self {
      \(resUrlBody)
                  }
              }
          }

          \(vis)static let serviceName = \(quoted(serviceName))
          \(vis)static let serviceInfo = ServiceInfo(
              name: \(quoted(serviceName)),
              methods: [
      \(indent(methodInfoEntries, 3))
              ]
          )
          \(vis)static func method(for id: UInt32) -> Method? { Method(rawValue: id) }
          // @@bebop_insertion_point(service_scope:\(serviceName))
      }
      """
  }

  // MARK: - Handler protocol

  private static func generateHandlerProtocol(
    _ serviceName: String, methods: [MethodInfo], vis: String
  ) throws -> String {
    let name = NamingPolicy.typeName(serviceName)
    let protocolName = "\(name)Handler"

    var protocolMethods: [String] = []
    for m in methods {
      protocolMethods.append(handlerSignature(m))
    }

    let body = protocolMethods.map { indent($0) }.joined(separator: "\n\n")
    return "\(vis)protocol \(protocolName): BebopHandler {\n\(body)\n}"
  }

  private static func handlerSignature(_ m: MethodInfo) -> String {
    let prefix = declPrefix(doc: m.doc, decorators: m.decorators)
    switch m.methodType {
    case .unary:
      return """
        \(prefix)func \(m.swiftName)(
            _ request: \(m.requestTypeName),
            context: RpcContext
        ) async throws -> \(m.responseTypeName)
        """
    case .serverStream:
      return """
        \(prefix)func \(m.swiftName)(
            _ request: \(m.requestTypeName),
            context: RpcContext
        ) async throws -> AsyncThrowingStream<\(m.responseTypeName), Error>
        """
    case .clientStream:
      return """
        \(prefix)func \(m.swiftName)(
            _ requests: AsyncThrowingStream<\(m.requestTypeName), Error>,
            context: RpcContext
        ) async throws -> \(m.responseTypeName)
        """
    case .duplexStream:
      return """
        \(prefix)func \(m.swiftName)(
            _ requests: AsyncThrowingStream<\(m.requestTypeName), Error>,
            context: RpcContext
        ) async throws -> AsyncThrowingStream<\(m.responseTypeName), Error>
        """
    }
  }

  // MARK: - Handler registration

  private static func generateHandlerRegistration(
    _ serviceName: String, methods: [MethodInfo], vis: String
  ) throws -> String {
    let name = NamingPolicy.typeName(serviceName)
    let protocolName = "\(name)Handler"

    var unaryBody: [String] = []
    var serverStreamBody: [String] = []
    var clientStreamBody: [String] = []
    var duplexStreamBody: [String] = []

    for m in methods {
      switch m.methodType {
      case .unary:
        unaryBody.append("case .\(m.swiftName):")
        unaryBody.append("    let req = try \(m.requestTypeName).decode(from: payload)")
        unaryBody.append("    let res = try await handler.\(m.swiftName)(req, context: context)")
        unaryBody.append("    return res.serializedData()")
      case .serverStream:
        serverStreamBody.append("case .\(m.swiftName):")
        serverStreamBody.append("    let req = try \(m.requestTypeName).decode(from: payload)")
        serverStreamBody.append(
          "    let typed = try await handler.\(m.swiftName)(req, context: context)")
        serverStreamBody.append("    return AsyncThrowingStream<StreamElement, Error> { c in")
        serverStreamBody.append("        let task = Task {")
        serverStreamBody.append("            do {")
        serverStreamBody.append("                for try await item in typed {")
        serverStreamBody.append("                    try Task.checkCancellation()")
        serverStreamBody.append(
          "                    if let d = context.deadline, d.isPast {")
        serverStreamBody.append(
          "                        throw BebopRpcError(code: .deadlineExceeded)")
        serverStreamBody.append(
          "                    }")
        serverStreamBody.append(
          "                    c.yield(StreamElement(bytes: item.serializedData(), cursor: context.dequeueCursor()))"
        )
        serverStreamBody.append("                }")
        serverStreamBody.append("                c.finish()")
        serverStreamBody.append("            } catch {")
        serverStreamBody.append("                c.finish(throwing: error)")
        serverStreamBody.append("            }")
        serverStreamBody.append("        }")
        serverStreamBody.append("        c.onTermination = { _ in task.cancel() }")
        serverStreamBody.append("    }")
      case .clientStream:
        clientStreamBody.append("case .\(m.swiftName):")
        clientStreamBody.append(
          "    let (stream, continuation) = AsyncThrowingStream.makeStream(of: \(m.requestTypeName).self)"
        )
        clientStreamBody.append(
          "    let task = Task {")
        clientStreamBody.append(
          "        defer { continuation.finish() }")
        clientStreamBody.append(
          "        return try await handler.\(m.swiftName)(stream, context: context)")
        clientStreamBody.append(
          "    }")
        clientStreamBody.append("    return (")
        clientStreamBody.append("        send: { bytes in")
        clientStreamBody.append(
          "            try Task.checkCancellation()")
        clientStreamBody.append(
          "            if let d = context.deadline, d.isPast {")
        clientStreamBody.append(
          "                throw BebopRpcError(code: .deadlineExceeded)")
        clientStreamBody.append(
          "            }")
        clientStreamBody.append(
          "            let req = try \(m.requestTypeName).decode(from: bytes)")
        clientStreamBody.append(
          "            if case .terminated = continuation.yield(req) {")
        clientStreamBody.append(
          "                throw BebopRpcError(code: .cancelled, detail: \"stream terminated\")")
        clientStreamBody.append(
          "            }")
        clientStreamBody.append("        },")
        clientStreamBody.append("        finish: {")
        clientStreamBody.append("            continuation.finish()")
        clientStreamBody.append("            return try await task.value.serializedData()")
        clientStreamBody.append("        }")
        clientStreamBody.append("    )")
      case .duplexStream:
        duplexStreamBody.append("case .\(m.swiftName):")
        duplexStreamBody.append(
          "    let (stream, continuation) = AsyncThrowingStream.makeStream(of: \(m.requestTypeName).self)"
        )
        duplexStreamBody.append(
          "    let typedResponses = try await handler.\(m.swiftName)(stream, context: context)")
        duplexStreamBody.append(
          "    let rawResponses = AsyncThrowingStream<StreamElement, Error> { c in")
        duplexStreamBody.append("        let task = Task {")
        duplexStreamBody.append("            do {")
        duplexStreamBody.append("                for try await item in typedResponses {")
        duplexStreamBody.append("                    try Task.checkCancellation()")
        duplexStreamBody.append(
          "                    if let d = context.deadline, d.isPast {")
        duplexStreamBody.append(
          "                        throw BebopRpcError(code: .deadlineExceeded)")
        duplexStreamBody.append(
          "                    }")
        duplexStreamBody.append(
          "                    c.yield(StreamElement(bytes: item.serializedData(), cursor: context.dequeueCursor()))"
        )
        duplexStreamBody.append("                }")
        duplexStreamBody.append("                c.finish()")
        duplexStreamBody.append("            } catch {")
        duplexStreamBody.append("                c.finish(throwing: error)")
        duplexStreamBody.append("            }")
        duplexStreamBody.append("        }")
        duplexStreamBody.append("        c.onTermination = { _ in")
        duplexStreamBody.append("            task.cancel()")
        duplexStreamBody.append("            continuation.finish()")
        duplexStreamBody.append("        }")
        duplexStreamBody.append("    }")
        duplexStreamBody.append("    return (")
        duplexStreamBody.append("        send: { bytes in")
        duplexStreamBody.append(
          "            try Task.checkCancellation()")
        duplexStreamBody.append(
          "            if let d = context.deadline, d.isPast {")
        duplexStreamBody.append(
          "                throw BebopRpcError(code: .deadlineExceeded)")
        duplexStreamBody.append(
          "            }")
        duplexStreamBody.append(
          "            let req = try \(m.requestTypeName).decode(from: bytes)")
        duplexStreamBody.append(
          "            if case .terminated = continuation.yield(req) {")
        duplexStreamBody.append(
          "                throw BebopRpcError(code: .cancelled, detail: \"stream terminated\")")
        duplexStreamBody.append(
          "            }")
        duplexStreamBody.append("        },")
        duplexStreamBody.append("        finish: { continuation.finish() },")
        duplexStreamBody.append("        responses: rawResponses")
        duplexStreamBody.append("    )")
      }
    }

    let unarySwitch = buildRouterSwitch(unaryBody)
    let serverStreamSwitch = buildRouterSwitch(serverStreamBody)
    let clientStreamSwitch = buildRouterSwitch(clientStreamBody)
    let duplexStreamSwitch = buildRouterSwitch(duplexStreamBody)

    return """
      extension BebopRouterBuilder {
          \(vis)func register(\(NamingPolicy.fieldName(serviceName)) handler: some \(protocolName)) {
              register(\(name).self, unary: { method, context, payload in
      \(indent(unarySwitch, 3))
              }, serverStream: { method, context, payload in
      \(indent(serverStreamSwitch, 3))
              }, clientStream: { method, context in
      \(indent(clientStreamSwitch, 3))
              }, duplexStream: { method, context in
      \(indent(duplexStreamSwitch, 3))
              })
          }
      }
      """
  }

  private static func buildRouterSwitch(_ cases: [String]) -> String {
    guard !cases.isEmpty else {
      return "throw BebopRpcError(code: .unimplemented)"
    }
    var lines = ["switch method {"]
    lines.append(contentsOf: cases.map { indent($0) })
    lines.append(indent("default: throw BebopRpcError(code: .unimplemented)"))
    lines.append("}")
    return lines.joined(separator: "\n")
  }

  // MARK: - Client stub

  private static func generateClientStub(
    _ serviceName: String, methods: [MethodInfo], vis: String
  ) throws -> String {
    let name = NamingPolicy.typeName(serviceName)
    let clientName = "\(name)Client"

    var body: [String] = []
    body.append("\(vis)let channel: C")
    body.append("\(vis)init(channel: C) { self.channel = channel }")

    for m in methods {
      let prefix = declPrefix(doc: m.doc, decorators: m.decorators)

      switch m.methodType {
      case .unary:
        body.append(
          """
          \(prefix)\(vis)func \(m.swiftName)(
              _ request: \(m.requestTypeName),
              context: RpcContext = RpcContext()
          ) async throws -> Response<\(m.responseTypeName), C.Metadata> {
              try await channel.unary(
                  method: 0x\(hex(m.methodId)),
                  request: request.serializedData(),
                  context: context
              ).map { try \(m.responseTypeName).decode(from: $0) }
          }
          """)
        if let params = m.deconstructedParams {
          body.append(
            deconstructedClientMethod(
              m, params: params, vis: vis, isStream: false))
        }

      case .serverStream:
        body.append(
          """
          \(prefix)\(vis)func \(m.swiftName)(
              _ request: \(m.requestTypeName),
              context: RpcContext = RpcContext()
          ) async throws -> StreamResponse<\(m.responseTypeName), C.Metadata> {
              try await channel.serverStream(
                  method: 0x\(hex(m.methodId)),
                  request: request.serializedData(),
                  context: context
              ).map { try \(m.responseTypeName).decode(from: $0) }
          }
          """)
        if let params = m.deconstructedParams {
          body.append(
            deconstructedClientMethod(
              m, params: params, vis: vis, isStream: true))
        }

      case .clientStream:
        body.append(
          """
          \(prefix)\(vis)func \(m.swiftName)(
              context: RpcContext = RpcContext(),
              body: (
                  _ send: @Sendable (\(m.requestTypeName)) async throws -> Void
              ) async throws -> Void
          ) async throws -> Response<\(m.responseTypeName), C.Metadata> {
              let (rawSend, rawFinish) = try await channel.clientStream(
                  method: 0x\(hex(m.methodId)),
                  context: context
              )
              try await body({ try await rawSend($0.serializedData()) })
              return try await rawFinish().map { try \(m.responseTypeName).decode(from: $0) }
          }
          """)

      case .duplexStream:
        body.append(
          """
          \(prefix)\(vis)func \(m.swiftName)(
              context: RpcContext = RpcContext(),
              body: (
                  _ send: @Sendable (\(m.requestTypeName)) async throws -> Void,
                  _ finish: @Sendable () async throws -> Void,
                  _ responses: StreamResponse<\(m.responseTypeName), C.Metadata>
              ) async throws -> Void
          ) async throws {
              let (rawSend, rawFinish, rawResponses) = try await channel.duplexStream(
                  method: 0x\(hex(m.methodId)),
                  context: context
              )
              do {
                  try await body(
                      { try await rawSend($0.serializedData()) },
                      rawFinish,
                      rawResponses.map { try \(m.responseTypeName).decode(from: $0) }
                  )
              } catch {
                  try? await rawFinish()
                  throw error
              }
          }
          """)
      }
    }

    let bodyStr = body.map { indent($0) }.joined(separator: "\n\n")
    return "\(vis)struct \(clientName)<C: BebopChannel>: Sendable {\n\(bodyStr)\n}"
  }

  private static func deconstructedClientMethod(
    _ m: MethodInfo,
    params: [(swiftName: String, swiftType: String, isOptional: Bool)],
    vis: String,
    isStream: Bool
  ) -> String {
    let returnType =
      isStream
      ? "StreamResponse<\(m.responseTypeName), C.Metadata>"
      : "Response<\(m.responseTypeName), C.Metadata>"

    if params.isEmpty {
      return """
        \(vis)func \(m.swiftName)(
            context: RpcContext = RpcContext()
        ) async throws -> \(returnType) {
            try await \(m.swiftName)(\(m.requestTypeName)(), context: context)
        }
        """
    }

    let paramList = params.map { p in
      if p.isOptional {
        return "\(p.swiftName): \(p.swiftType)? = nil"
      }
      return "\(p.swiftName): \(p.swiftType)"
    }.joined(separator: ", ")

    let constructArgs = params.map { "\($0.swiftName): \($0.swiftName)" }
      .joined(separator: ", ")

    return """
      \(vis)func \(m.swiftName)(
          \(paramList),
          context: RpcContext = RpcContext()
      ) async throws -> \(returnType) {
          try await \(m.swiftName)(\(m.requestTypeName)(\(constructArgs)), context: context)
      }
      """
  }

  // MARK: - Batch accessor

  private static func generateBatchAccessor(
    _ serviceName: String, methods: [MethodInfo], vis: String
  ) throws -> String {
    let name = NamingPolicy.typeName(serviceName)
    let batchStructName = "\(name)_Batch"
    let accessorName = NamingPolicy.fieldName(serviceName)

    var body: [String] = []
    body.append("\(vis)let batch: Batch<C>")

    for m in methods {
      switch m.methodType {
      case .unary:
        body.append(
          """
          @discardableResult
          \(vis)func \(m.swiftName)(_ request: \(m.requestTypeName)) -> CallRef<\(m.responseTypeName)> {
              batch.addUnary(methodId: 0x\(hex(m.methodId)), request: request)
          }
          """)
        if let params = m.deconstructedParams {
          body.append(
            deconstructedBatchMethod(
              m, params: params, vis: vis, isStream: false))
        }
        body.append(
          """
          @discardableResult
          \(vis)func \(m.swiftName)<T: BebopRecord>(forwarding ref: CallRef<T>) -> CallRef<\(m.responseTypeName)> {
              batch.addUnary(methodId: 0x\(hex(m.methodId)), forwardingFrom: ref.callId)
          }
          """)

      case .serverStream:
        body.append(
          """
          @discardableResult
          \(vis)func \(m.swiftName)(_ request: \(m.requestTypeName)) -> StreamRef<\(m.responseTypeName)> {
              batch.addServerStream(methodId: 0x\(hex(m.methodId)), request: request)
          }
          """)
        if let params = m.deconstructedParams {
          body.append(
            deconstructedBatchMethod(
              m, params: params, vis: vis, isStream: true))
        }
        body.append(
          """
          @discardableResult
          \(vis)func \(m.swiftName)<T: BebopRecord>(forwarding ref: CallRef<T>) -> StreamRef<\(m.responseTypeName)> {
              batch.addServerStream(methodId: 0x\(hex(m.methodId)), forwardingFrom: ref.callId)
          }
          """)

      case .clientStream, .duplexStream:
        break
      }
    }

    let bodyStr = body.map { indent($0) }.joined(separator: "\n\n")

    return """
      \(vis)struct \(batchStructName)<C: BebopChannel> {
      \(bodyStr)
      }

      extension Batch {
          \(vis)var \(accessorName): \(batchStructName)<Channel> { \(batchStructName)(batch: self) }
      }
      """
  }

  private static func deconstructedBatchMethod(
    _ m: MethodInfo,
    params: [(swiftName: String, swiftType: String, isOptional: Bool)],
    vis: String,
    isStream: Bool
  ) -> String {
    let refType =
      isStream
      ? "StreamRef<\(m.responseTypeName)>"
      : "CallRef<\(m.responseTypeName)>"

    if params.isEmpty {
      return """
        @discardableResult
        \(vis)func \(m.swiftName)() -> \(refType) {
            \(m.swiftName)(\(m.requestTypeName)())
        }
        """
    }

    let paramList = params.map { p in
      if p.isOptional {
        return "\(p.swiftName): \(p.swiftType)? = nil"
      }
      return "\(p.swiftName): \(p.swiftType)"
    }.joined(separator: ", ")

    let constructArgs = params.map { "\($0.swiftName): \($0.swiftName)" }
      .joined(separator: ", ")

    return """
      @discardableResult
      \(vis)func \(m.swiftName)(\(paramList)) -> \(refType) {
          \(m.swiftName)(\(m.requestTypeName)(\(constructArgs)))
      }
      """
  }

  // MARK: - Dispatch accessor

  private static func generateDispatchAccessor(
    _ serviceName: String, methods: [MethodInfo], vis: String
  ) throws -> String {
    let name = NamingPolicy.typeName(serviceName)
    let dispatchStructName = "\(name)_Dispatch"
    let accessorName = NamingPolicy.fieldName(serviceName)

    let unaryMethods = methods.filter { $0.methodType == .unary }

    var body: [String] = []
    body.append("\(vis)let dispatcher: FutureDispatcher<C>")

    for m in unaryMethods {
      body.append(
        """
        \(vis)func \(m.swiftName)(
            _ request: \(m.requestTypeName),
            options: DispatchOptions = .init(),
            context: RpcContext = RpcContext()
        ) async throws -> BebopFuture<\(m.responseTypeName)> {
            try await dispatcher.dispatch(
                methodId: 0x\(hex(m.methodId)),
                request: request,
                options: options,
                context: context)
        }
        """)
      if let params = m.deconstructedParams {
        body.append(
          deconstructedDispatchMethod(m, params: params, vis: vis))
      }
    }

    let bodyStr = body.map { indent($0) }.joined(separator: "\n\n")

    return """
      \(vis)struct \(dispatchStructName)<C: BebopChannel> {
      \(bodyStr)
      }

      extension FutureDispatcher {
          \(vis)var \(accessorName): \(dispatchStructName)<Channel> { \(dispatchStructName)(dispatcher: self) }
      }
      """
  }

  private static func deconstructedDispatchMethod(
    _ m: MethodInfo,
    params: [(swiftName: String, swiftType: String, isOptional: Bool)],
    vis: String
  ) -> String {
    let returnType = "BebopFuture<\(m.responseTypeName)>"

    if params.isEmpty {
      return """
        \(vis)func \(m.swiftName)(
            options: DispatchOptions = .init(),
            context: RpcContext = RpcContext()
        ) async throws -> \(returnType) {
            try await \(m.swiftName)(\(m.requestTypeName)(), options: options, context: context)
        }
        """
    }

    let paramList = params.map { p in
      if p.isOptional {
        return "\(p.swiftName): \(p.swiftType)? = nil"
      }
      return "\(p.swiftName): \(p.swiftType)"
    }.joined(separator: ", ")

    let constructArgs = params.map { "\($0.swiftName): \($0.swiftName)" }
      .joined(separator: ", ")

    return """
      \(vis)func \(m.swiftName)(
          \(paramList),
          options: DispatchOptions = .init(),
          context: RpcContext = RpcContext()
      ) async throws -> \(returnType) {
          try await \(m.swiftName)(\(m.requestTypeName)(\(constructArgs)), options: options, context: context)
      }
      """
  }

  // MARK: - Helpers

  private static func hex(_ value: UInt32) -> String {
    String(value, radix: 16, uppercase: true)
  }

  private static func typeUrl(_ fqn: String) -> String {
    guard !fqn.isEmpty else { return "" }
    return "type.bebop.sh/\(fqn)"
  }
}
