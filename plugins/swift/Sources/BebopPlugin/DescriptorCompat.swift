import SwiftBebop

@_exported import enum SwiftBebop.BebopDecodingError

/// The request payload sent by `bebopc` to a code-generation plugin.
public typealias PluginRequest = CodeGeneratorRequest

/// Semantic version of the `bebopc` compiler that issued the request.
public typealias CompilerVersion = Version

/// Errors raised during plugin request/response serialization.
public enum PluginError: Error, Sendable {
    /// The incoming request bytes could not be decoded.
    case decodeFailed(String)
    /// The outgoing response could not be encoded.
    case encodeFailed(String)
}

/// Accumulate generated files (or an error) and encode the plugin response.
///
/// Create a builder, call ``addFile(name:content:)`` for each output file,
/// then call ``encode()`` to produce the wire-format bytes that `bebopc` expects.
public final class ResponseBuilder: @unchecked Sendable {
    private var response: CodeGeneratorResponse

    public init() { response = CodeGeneratorResponse() }

    /// Record a fatal error message. `bebopc` will report it and discard any files.
    public func setError(_ message: String) {
        response.error = message
    }

    /// Append a generated file to the response.
    public func addFile(name: String, content: String) {
        let file = GeneratedFile(name: name, content: content)
        if var files = response.files {
            files.append(file)
            response.files = files
        } else {
            response.files = [file]
        }
    }

    /// Serialize the accumulated response to Bebop wire format.
    public func encode() throws -> [UInt8] {
        response.serializedData()
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
