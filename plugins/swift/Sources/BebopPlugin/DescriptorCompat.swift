import Synchronization
import SwiftBebop

@_exported import enum SwiftBebop.BebopDecodingError

/// The request payload sent by `bebopc` to a code-generation plugin.
public typealias PluginRequest = CodeGeneratorRequest

/// Semantic version of the `bebopc` compiler that issued the request.
public typealias CompilerVersion = Version

/// Accumulate generated files (or an error) and encode the plugin response.
///
/// Create a builder, call ``addFile(name:content:)`` for each output file,
/// then call ``encode()`` to produce the wire-format bytes that `bebopc` expects.
public final class ResponseBuilder: Sendable {
    private struct State: Sendable {
        var files: [GeneratedFile] = []
        var diagnostics: [Diagnostic] = []
        var error: String?
    }

    private let state = Mutex(State())

    public init() {}

    /// Record a fatal error message. `bebopc` will report it and discard any files.
    public func setError(_ message: String) {
        state.withLock { $0.error = message }
    }

    /// Append a generated file to the response.
    public func addFile(name: String, content: String) {
        state.withLock { $0.files.append(GeneratedFile(name: name, content: content)) }
    }

    public func addDiagnostic(_ diagnostic: Diagnostic) {
        state.withLock { $0.diagnostics.append(diagnostic) }
    }

    /// Serialize the accumulated response to Bebop wire format.
    public func encode() -> [UInt8] {
        state.withLock { state in
            CodeGeneratorResponse(
                error: state.error,
                files: state.error == nil && !state.files.isEmpty ? state.files : nil,
                diagnostics: state.diagnostics.isEmpty ? nil : state.diagnostics
            ).serializedData()
        }
    }
}

public extension FieldDescriptor {
    /// Whether this field carries the `@deprecated` decorator.
    var isDeprecated: Bool {
        guard let decorators else { return false }
        return decorators.contains { $0.fqn == "deprecated" }
    }
}

public extension TypeKind {
    /// Whether this kind represents a scalar (non-aggregate) Bebop type.
    var isScalar: Bool {
        rawValue >= 1 && rawValue <= 19
    }
}
