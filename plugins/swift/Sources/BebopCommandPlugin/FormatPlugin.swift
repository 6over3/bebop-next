import Foundation
import PackagePlugin

@main
struct BebopCommandPlugin: CommandPlugin {
  func performCommand(context: PluginContext, arguments: [String]) async throws {
    let bebopc = try findBebopc()

    var argExtractor = ArgumentExtractor(arguments)
    let targetNames = argExtractor.extractOption(named: "target")

    var targets = context.package.targets
    if !targetNames.isEmpty {
      targets = targets.filter { targetNames.contains($0.name) }
    }

    let bopFiles =
      targets
      .compactMap { $0 as? SourceModuleTarget }
      .flatMap { $0.sourceFiles(withSuffix: "bop") }
      .map(\.url)

    guard !bopFiles.isEmpty else {
      print("No .bop files found")
      return
    }

    try format(bebopc: bebopc, files: bopFiles)
  }

  private func format(bebopc: URL, files: [URL]) throws {
    var arguments = ["fmt"]
    arguments += files.map { $0.path() }

    let process = Process()
    process.executableURL = bebopc
    process.arguments = arguments
    try process.run()
    process.waitUntilExit()

    guard process.terminationStatus == 0 else {
      throw BebopCommandError.formatFailed(process.terminationStatus)
    }

    print("Formatted \(files.count) Bebop schema\(files.count == 1 ? "" : "s")")
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

    // Xcode sandboxes plugins, stripping user dirs from PATH.
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

    throw BebopCommandError.bebopcNotFound
  }
}

#if canImport(XcodeProjectPlugin)
  import XcodeProjectPlugin

  extension BebopCommandPlugin: XcodeCommandPlugin {
    func performCommand(context: XcodePluginContext, arguments: [String]) throws {
      let bebopc = try findBebopc()

      var argExtractor = ArgumentExtractor(arguments)
      let targetNames = argExtractor.extractOption(named: "target")

      var targets = context.xcodeProject.targets
      if !targetNames.isEmpty {
        targets = targets.filter { targetNames.contains($0.displayName) }
      }

      let bopFiles =
        targets
        .flatMap(\.inputFiles)
        .filter { $0.url.pathExtension == "bop" }
        .map(\.url)

      guard !bopFiles.isEmpty else {
        print("No .bop files found")
        return
      }

      try format(bebopc: bebopc, files: bopFiles)
    }
  }
#endif

enum BebopCommandError: Error, CustomStringConvertible {
  case bebopcNotFound
  case formatFailed(Int32)

  var description: String {
    switch self {
    case .bebopcNotFound: "bebopc not found in PATH"
    case .formatFailed(let code): "bebopc fmt exited with code \(code)"
    }
  }
}
