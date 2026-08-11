import BebopPlugin
import SwiftBebop

enum GenerateService {
    @TaskLocal static var definitionMap: [String: DefinitionDescriptor] = [:]

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

        let methodInfos = try methods.map { m in
            try resolveMethod(m, serviceName: defName)
        }
        try validateUniqueGeneratedNames(
            methodInfos.map(\.swiftName),
            in: "service '\(defName)'"
        )
        var methodIds: Set<UInt32> = []
        for method in methodInfos where !methodIds.insert(method.methodId).inserted {
            throw CodegenError.malformedDefinition(
                "service '\(defName)' has duplicate method ID \(method.methodId)"
            )
        }

        var result: [String] = []

        try result.append(
            generateServiceEnum(
                defName, methods: methodInfos, prefix: prefix, vis: vis
            ))

        if options.services == .server || options.services == .both {
            try result.append(
                generateHandlerProtocol(
                    defName, methods: methodInfos, vis: vis
                ))
            try result.append(
                generateHandlerRegistration(
                    defName, methods: methodInfos, vis: vis
                ))
        }

        if options.services == .client || options.services == .both {
            try result.append(
                generateClientStub(
                    defName, methods: methodInfos, vis: vis
                ))
            try result.append(
                generateBatchAccessor(
                    defName, methods: methodInfos, vis: vis
                ))
            try result.append(
                generateDispatchAccessor(
                    defName, methods: methodInfos, vis: vis
                ))
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
        guard id > BebopReservedMethod.cancel else {
            throw CodegenError.malformedDefinition(
                "service '\(serviceName)' method '\(name)' uses reserved method ID \(id)"
            )
        }
        guard let reqType = m.requestType else {
            throw CodegenError.malformedDefinition(
                "service '\(serviceName)' method '\(name)' missing request type")
        }
        guard let resType = m.responseType else {
            throw CodegenError.malformedDefinition(
                "service '\(serviceName)' method '\(name)' missing response type")
        }

        let runtimeType = try mapMethodType(
            descriptorMethodType, serviceName: serviceName, methodName: name)
        let reqTypeName = try TypeMapper.swiftType(for: reqType)
        let resTypeName = try TypeMapper.swiftType(for: resType)
        guard reqType.kind == .defined, let reqFqn = reqType.definedFqn, !reqFqn.isEmpty else {
            throw CodegenError.malformedDefinition(
                "service '\(serviceName)' method '\(name)' request must be a defined record")
        }
        guard resType.kind == .defined, let resFqn = resType.definedFqn, !resFqn.isEmpty else {
            throw CodegenError.malformedDefinition(
                "service '\(serviceName)' method '\(name)' response must be a defined record")
        }

        let deconstructed = try resolveDeconstructedParams(for: reqType)

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

    private static func mapMethodType(
        _ dt: BebopPlugin.MethodType, serviceName: String, methodName: String
    ) throws -> RuntimeMethodType {
        switch dt {
        case .unary: .unary
        case .serverStream: .serverStream
        case .clientStream: .clientStream
        case .duplexStream: .duplexStream
        default:
            throw CodegenError.malformedDefinition(
                "service '\(serviceName)' method '\(methodName)' has unsupported method type")
        }
    }

    // MARK: - Deconstructed params

    private static func resolveDeconstructedParams(
        for type: TypeDescriptor
    ) throws -> [(swiftName: String, swiftType: String, isOptional: Bool)]? {
        guard type.kind == .defined, let fqn = type.definedFqn else { return nil }
        guard let def = definitionMap[fqn] else {
            throw CodegenError.malformedDefinition("request type '\(fqn)' is not in the schema")
        }

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

        return try fieldList.map { f in
            guard let name = f.name else {
                throw CodegenError.malformedDefinition("request type '\(fqn)' has an unnamed field")
            }
            guard let fieldType = f.type else {
                throw CodegenError.malformedDefinition(
                    "request type '\(fqn)' field '\(name)' has no type")
            }
            let swiftType = try TypeMapper.swiftType(for: fieldType)
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
        let prefix = docComment(m.doc)
        switch m.methodType {
        case .unary:
            return """
            \(prefix)func \(m.swiftName)(
                _ request: \(m.requestTypeName).View,
                context: RpcContext
            ) async throws -> \(m.responseTypeName)
            """
        case .serverStream:
            return """
            \(prefix)func \(m.swiftName)(
                _ request: \(m.requestTypeName).View,
                context: RpcContext
            ) async throws -> AsyncThrowingStream<\(m.responseTypeName), Error>
            """
        case .clientStream:
            return """
            \(prefix)func \(m.swiftName)(
                _ requests: AsyncThrowingStream<\(m.requestTypeName).View, Error>,
                context: RpcContext
            ) async throws -> \(m.responseTypeName)
            """
        case .duplexStream:
            return """
            \(prefix)func \(m.swiftName)(
                _ requests: AsyncThrowingStream<\(m.requestTypeName).View, Error>,
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
                unaryBody.append("    let req = try \(m.requestTypeName).View(payload, limits: context.decodeLimits)")
                unaryBody.append("    let res = try await handler.\(m.swiftName)(req, context: context)")
                unaryBody.append("    return res.encode()")
            case .serverStream:
                serverStreamBody.append("case .\(m.swiftName):")
                serverStreamBody.append("    let req = try \(m.requestTypeName).View(payload, limits: context.decodeLimits)")
                serverStreamBody.append(
                    "    let typed = try await handler.\(m.swiftName)(req, context: context)")
                serverStreamBody.append("    return BebopStreams.map(typed) { item in")
                serverStreamBody.append("        try Task.checkCancellation()")
                serverStreamBody.append(
                    "        if let d = context.deadline, d.isPast {")
                serverStreamBody.append(
                    "            throw BebopRpcError(code: .deadlineExceeded)")
                serverStreamBody.append(
                    "        }")
                serverStreamBody.append(
                    "        return StreamElement(bytes: item.encode(), cursor: context.dequeueCursor())"
                )
                serverStreamBody.append("    }")
            case .clientStream:
                clientStreamBody.append("case .\(m.swiftName):")
                clientStreamBody.append(
                    "    let inbound = RpcInboundStream<\(m.requestTypeName).View>()"
                )
                clientStreamBody.append(
                    "    let task = Task {")
                clientStreamBody.append(
                    "        do {")
                clientStreamBody.append(
                    "            let response = try await handler.\(m.swiftName)(inbound.stream, context: context)")
                clientStreamBody.append(
                    "            await inbound.finish()")
                clientStreamBody.append(
                    "            return response")
                clientStreamBody.append(
                    "        } catch {")
                clientStreamBody.append(
                    "            await inbound.finish(throwing: error)")
                clientStreamBody.append(
                    "            throw error")
                clientStreamBody.append(
                    "        }")
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
                    "            let req = try \(m.requestTypeName).View(bytes, limits: context.decodeLimits)")
                clientStreamBody.append(
                    "            try await inbound.send(req)")
                clientStreamBody.append("        },")
                clientStreamBody.append("        finish: {")
                clientStreamBody.append("            await inbound.finish()")
                clientStreamBody.append("            return try await task.value.encode()")
                clientStreamBody.append("        }")
                clientStreamBody.append("    )")
            case .duplexStream:
                duplexStreamBody.append("case .\(m.swiftName):")
                duplexStreamBody.append(
                    "    let inbound = RpcInboundStream<\(m.requestTypeName).View>()"
                )
                duplexStreamBody.append("    let rawResponses = BebopStreams.map(")
                duplexStreamBody.append(
                    "        { try await handler.\(m.swiftName)(inbound.stream, context: context) },"
                )
                duplexStreamBody.append("        onCancel: { await inbound.finish() }")
                duplexStreamBody.append("    ) { item in")
                duplexStreamBody.append("        try Task.checkCancellation()")
                duplexStreamBody.append(
                    "        if let d = context.deadline, d.isPast {")
                duplexStreamBody.append(
                    "            throw BebopRpcError(code: .deadlineExceeded)")
                duplexStreamBody.append(
                    "        }")
                duplexStreamBody.append(
                    "        return StreamElement(bytes: item.encode(), cursor: context.dequeueCursor())"
                )
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
                    "            let req = try \(m.requestTypeName).View(bytes, limits: context.decodeLimits)")
                duplexStreamBody.append(
                    "            try await inbound.send(req)")
                duplexStreamBody.append("        },")
                duplexStreamBody.append("        finish: { await inbound.finish() },")
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
                            request: request.encode(),
                            context: context
                        ).map { try \(m.responseTypeName).decode(from: $0) }
                    }
                    """)
                if let params = m.deconstructedParams {
                    body.append(
                        deconstructedClientMethod(
                            m, params: params, vis: vis, isStream: false
                        ))
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
                            request: request.encode(),
                            context: context
                        ).map { try \(m.responseTypeName).decode(from: $0) }
                    }
                    """)
                if let params = m.deconstructedParams {
                    body.append(
                        deconstructedClientMethod(
                            m, params: params, vis: vis, isStream: true
                        ))
                }

            case .clientStream:
                body.append(
                    """
                    \(prefix)\(vis)func \(m.swiftName)(
                        context: RpcContext = RpcContext()
                    ) async throws -> ClientStream<\(m.requestTypeName), \(m.responseTypeName), C.Metadata> {
                        try await channel.clientStream(
                            method: 0x\(hex(m.methodId)),
                            context: context
                        ).map(
                            request: { $0.encode() },
                            response: { try \(m.responseTypeName).decode(from: $0) }
                        )
                    }
                    """)

            case .duplexStream:
                body.append(
                    """
                    \(prefix)\(vis)func \(m.swiftName)(
                        context: RpcContext = RpcContext()
                    ) async throws -> DuplexStream<\(m.requestTypeName), \(m.responseTypeName), C.Metadata> {
                        try await channel.duplexStream(
                            method: 0x\(hex(m.methodId)),
                            context: context
                        ).map(
                            request: { $0.encode() },
                            response: { try \(m.responseTypeName).decode(from: $0) }
                        )
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
        let batchStructName = "\(name)Batch"
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
                            m, params: params, vis: vis, isStream: false
                        ))
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
                            m, params: params, vis: vis, isStream: true
                        ))
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
        let dispatchStructName = "\(name)Dispatch"
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
