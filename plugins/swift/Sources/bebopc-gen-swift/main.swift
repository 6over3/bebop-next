import BebopPlugin
import Foundation

func readAllStdin() -> [UInt8] {
    var buf = [UInt8]()
    let chunk = 65536
    let handle = FileHandle.standardInput
    while true {
        let data = handle.readData(ofLength: chunk)
        if data.isEmpty { break }
        buf.append(contentsOf: data)
    }
    return buf
}

func parseOptions(_ hostOptions: [String: String]?) -> GeneratorOptions {
    var opts = GeneratorOptions()
    guard let hostOptions else { return opts }
    if let vis = hostOptions["Visibility"] {
        switch vis.lowercased() {
        case "internal": opts.visibility = "internal "
        case "package": opts.visibility = "package "
        case "public": opts.visibility = "public "
        default: break
        }
    }
    return opts
}

func run() throws {
    let input = readAllStdin()
    guard !input.isEmpty else {
        throw GeneratorError.emptyInput
    }

    let request = try PluginRequest.decode(from: input)

    guard let filesToGenerate = request.filesToGenerate else {
        throw CodegenError.malformedDefinition("request missing filesToGenerate")
    }
    guard let schemas = request.schemas else {
        throw CodegenError.malformedDefinition("request missing schemas")
    }

    let fileSet = Set(filesToGenerate)
    try NamingPolicy.registerSchemas(schemas)
    let options = parseOptions(request.hostOptions)
    let generator = SwiftGenerator(options: options)
    let response = ResponseBuilder()

    for schema in schemas {
        guard let path = schema.path, fileSet.contains(path) else {
            continue
        }

        let fileName = URL(fileURLWithPath: path)
            .deletingPathExtension()
            .lastPathComponent + ".bb.swift"

        let code = try generator.generate(schema: schema)
        response.addFile(name: fileName, content: code)
    }

    let output = try response.encode()
    FileHandle.standardOutput.write(Data(output))
}

do {
    try run()
} catch {
    let response = ResponseBuilder()
    response.setError("bebopc-gen-swift: \(error)")
    if let output = try? response.encode() {
        FileHandle.standardOutput.write(Data(output))
    } else {
        FileHandle.standardError.write(
            Data("bebopc-gen-swift: \(error)\n".utf8)
        )
        exit(1)
    }
}

enum GeneratorError: Error {
    case emptyInput
}
