import PackagePlugin
import Foundation

@main
struct BebopBuildToolPlugin {
    func createBuildCommands(
        outputDir: URL,
        inputFiles: FileList
    ) throws -> [Command] {
        let bopFiles = inputFiles.filter { $0.url.pathExtension == "bop" }
        guard !bopFiles.isEmpty else {
            return []
        }

        let bebopc = try findBebopc()
        let inputs = bopFiles.map(\.url)
        let outputs = inputs.map { url in
            outputDir.appending(path: url.deletingPathExtension().lastPathComponent + ".bb.swift")
        }

        let sourceDir = inputs[0].deletingLastPathComponent()

        var arguments: [String] = ["build"]
        for input in inputs {
            arguments.append(input.path())
        }

        var trackedInputs = inputs
        let configURL = sourceDir.appending(path: "bebop.yml")
        if FileManager.default.fileExists(atPath: configURL.path()) {
            arguments += ["--config", configURL.path()]
            trackedInputs.append(configURL)
        }

        arguments += ["-I", sourceDir.path()]
        arguments += ["--format", "xcode"]
        arguments += ["--color", "never"]
        arguments.append("--swift_out=\(outputDir.path())")

        return [
            .buildCommand(
                displayName: "Compile \(inputs.count) Bebop schema\(inputs.count == 1 ? "" : "s")",
                executable: bebopc,
                arguments: arguments,
                inputFiles: trackedInputs,
                outputFiles: outputs
            )
        ]
    }

    private func findBebopc() throws -> URL {
        if let pathEnv = ProcessInfo.processInfo.environment["PATH"] {
            for dir in pathEnv.split(separator: ":") {
                let candidate = URL(filePath: String(dir)).appending(path: "bebopc")
                if FileManager.default.isExecutableFile(atPath: candidate.path()) {
                    return candidate
                }
            }
        }

        // Xcode sandboxes build plugins, stripping user dirs from PATH.
        let home = FileManager.default.homeDirectoryForCurrentUser
        let fallbacks = [
            home.appending(path: ".local/bin/bebopc"),
            home.appending(path: "bin/bebopc"),
            URL(filePath: "/opt/homebrew/bin/bebopc"),
            URL(filePath: "/usr/local/bin/bebopc"),
        ]
        for candidate in fallbacks {
            if FileManager.default.isExecutableFile(atPath: candidate.path()) {
                return candidate
            }
        }

        throw BebopPluginError.bebopcNotFound
    }
}

extension BebopBuildToolPlugin: BuildToolPlugin {
    func createBuildCommands(context: PluginContext, target: Target) async throws -> [Command] {
        guard let sourceTarget = target as? SourceModuleTarget else {
            return []
        }
        return try createBuildCommands(
            outputDir: context.pluginWorkDirectoryURL,
            inputFiles: sourceTarget.sourceFiles
        )
    }
}

#if canImport(XcodeProjectPlugin)
import XcodeProjectPlugin

extension BebopBuildToolPlugin: XcodeBuildToolPlugin {
    func createBuildCommands(context: XcodePluginContext, target: XcodeTarget) throws -> [Command] {
        return try createBuildCommands(
            outputDir: context.pluginWorkDirectoryURL,
            inputFiles: target.inputFiles
        )
    }
}
#endif

enum BebopPluginError: Error, CustomStringConvertible {
    case bebopcNotFound
    var description: String {
        "bebopc not found in PATH"
    }
}
