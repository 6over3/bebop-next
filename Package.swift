// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "swift-bebop",
    platforms: [.macOS(.v26), .iOS(.v26), .tvOS(.v26), .watchOS(.v26), .visionOS(.v26)],
    products: [
        .library(name: "SwiftBebop", targets: ["SwiftBebop"]),
        .library(name: "SwiftBebopFoundation", targets: ["SwiftBebopFoundation"]),
        .executable(name: "bebopc-gen-swift", targets: ["bebopc-gen-swift"]),
        .plugin(name: "BebopBuildToolPlugin", targets: ["BebopBuildToolPlugin"]),
        .plugin(name: "BebopCommandPlugin", targets: ["BebopCommandPlugin"])
    ],
    targets: [
        .target(name: "CBFloat16", path: "plugins/swift/Sources/CBFloat16"),
        .target(name: "SwiftBebop", dependencies: ["CBFloat16"], path: "plugins/swift/Sources/SwiftBebop"),
        .target(name: "SwiftBebopFoundation", dependencies: ["SwiftBebop"], path: "plugins/swift/Sources/SwiftBebopFoundation"),
        .target(name: "BebopPlugin", dependencies: ["SwiftBebop"], path: "plugins/swift/Sources/BebopPlugin"),
        .executableTarget(
            name: "bebopc-gen-swift",
            dependencies: [
                "BebopPlugin"
            ],
            path: "plugins/swift/Sources/bebopc-gen-swift"
        ),
        .plugin(
            name: "BebopBuildToolPlugin",
            capability: .buildTool(),
            path: "plugins/swift/Sources/BebopBuildToolPlugin"
        ),
        .plugin(
            name: "BebopCommandPlugin",
            capability: .command(
                intent: .custom(verb: "format-bop", description: "Format Bebop schema files"),
                permissions: [.writeToPackageDirectory(reason: "Format Bebop schema files in place")]
            ),
            path: "plugins/swift/Sources/BebopCommandPlugin"
        ),
        .testTarget(name: "SwiftBebopTests", dependencies: ["SwiftBebop", "SwiftBebopFoundation"], path: "plugins/swift/Tests/SwiftBebopTests"),
        .testTarget(name: "BebopPluginTests", dependencies: ["BebopPlugin"], path: "plugins/swift/Tests/BebopPluginTests"),
        .testTarget(name: "CodegenTests", dependencies: [
            "bebopc-gen-swift",
            "BebopPlugin"
        ], path: "plugins/swift/Tests/CodegenTests")
    ]
)
