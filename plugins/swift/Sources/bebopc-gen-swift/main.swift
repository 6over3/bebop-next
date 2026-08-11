import BebopPlugin
import Foundation
import SwiftBebop

func readAllStdin() throws -> Data {
    try FileHandle.standardInput.readToEnd() ?? Data()
}

func decodeRequest(_ input: Data) throws -> PluginRequest {
    try input.withUnsafeBytes { bytes in
        var reader = BebopReader(data: bytes)
        let request = try PluginRequest.decode(from: &reader)
        guard reader.remaining == 0 else { throw BebopDecodingError.trailingData }
        return request
    }
}

func parseOptions(_ hostOptions: [String: String]?) throws -> GeneratorOptions {
    var opts = GeneratorOptions()
    guard let hostOptions else { return opts }
    for (key, value) in hostOptions {
        switch key.lowercased() {
        case "visibility":
            switch value.lowercased() {
            case "internal": opts.visibility = "internal "
            case "package": opts.visibility = "package "
            case "public": opts.visibility = "public "
            default:
                throw CodegenError.invalidOption(
                    "Visibility must be internal, package, or public; got '\(value)'")
            }
        case "services":
            guard let mode = ServiceGenMode(rawValue: value.lowercased()) else {
                throw CodegenError.invalidOption(
                    "Services must be none, client, server, or both; got '\(value)'")
            }
            opts.services = mode
        default:
            throw CodegenError.invalidOption(
                "unknown Swift generator option '\(key)'")
        }
    }
    return opts
}

func run() throws {
    let input = try readAllStdin()
    guard !input.isEmpty else {
        throw GeneratorError.emptyInput
    }

    let request = try decodeRequest(input)

    guard let filesToGenerate = request.filesToGenerate else {
        throw CodegenError.malformedDefinition("request missing filesToGenerate")
    }
    guard let schemas = request.schemas else {
        throw CodegenError.malformedDefinition("request missing schemas")
    }

    let fileSet = Set(filesToGenerate)
    let fqnMap = try NamingPolicy.makeFQNMap(schemas: schemas, localFiles: fileSet)
    let definitionMap = try buildDefinitionMap(schemas)
    let options = try parseOptions(request.hostOptions)
    let generator = SwiftGenerator(options: options, compilerVersion: request.compilerVersion)
    let response = ResponseBuilder()

    try NamingPolicy.$fqnMap.withValue(fqnMap) {
        try GenerateService.$definitionMap.withValue(definitionMap) {
            for schema in schemas {
                guard let path = schema.path, fileSet.contains(path) else {
                    continue
                }

                let fileName =
                    URL(fileURLWithPath: path)
                        .deletingPathExtension()
                        .lastPathComponent + ".bb.swift"

                let code = try generator.generate(schema: schema)
                response.addFile(name: fileName, content: code)
            }
        }
    }

    let output = response.encode()
    try FileHandle.standardOutput.write(contentsOf: output)
}

do {
    try run()
} catch {
    let response = ResponseBuilder()
    response.setError("bebopc-gen-swift: \(error)")
    FileHandle.standardOutput.write(Data(response.encode()))
}

enum GeneratorError: Error {
    case emptyInput
}

func buildDefinitionMap(_ schemas: [SchemaDescriptor]) throws -> [String: DefinitionDescriptor] {
    var map = [String: DefinitionDescriptor]()
    for schema in schemas {
        if let defs = schema.definitions {
            for def in defs {
                try collectDefinitions(def, into: &map)
            }
        }
    }
    return map
}

private func collectDefinitions(
    _ def: DefinitionDescriptor, into map: inout [String: DefinitionDescriptor]
) throws {
    if let fqn = def.fqn {
        guard map[fqn] == nil else {
            throw CodegenError.malformedDefinition("duplicate definition fqn '\(fqn)'")
        }
        map[fqn] = def
    }
    if let nested = def.nested {
        for child in nested {
            try collectDefinitions(child, into: &map)
        }
    }
}
