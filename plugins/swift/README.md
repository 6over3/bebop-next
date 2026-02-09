# Bebop Swift Plugin

Swift code generator and runtime library for [Bebop](https://bebop.sh) schemas.

## Components

- **SwiftBebop** - Runtime library. Provides `BebopReader`, `BebopWriter`, and the `BebopRecord` protocol that all generated types conform to.
- **BebopPlugin** - Shared protocol types for communicating with the `bebopc` compiler.
- **bebopc-gen-swift** - Code generator plugin invoked by `bebopc` to emit `.bb.swift` files from `.bop` schemas.

## Building

Requires Swift 6.2+.

```
swift build
```

Or through the top-level CMake build, which invokes `swift build` automatically on systems where Swift is available:

```
make debug
```

The resulting `bebopc-gen-swift` binary is placed alongside `bebopc` in the output `bin/` directory.

## Testing

```
swift test
```

## Usage

The `bebopc-gen-swift` plugin is discovered automatically when it's in your `PATH` or next to the `bebopc` binary.

```
bebopc build schema.bop --swift_out=./generated
```

You can also point to the plugin explicitly:

```
bebopc build schema.bop --plugin=swift=/path/to/bebopc-gen-swift --swift_out=./generated
```

Add `SwiftBebop` as a dependency in your project's `Package.swift` to use the generated types at runtime.

### Generator Options

| Option       | Values                          | Default  |
|--------------|---------------------------------|----------|
| `Visibility` | `public`, `package`, `internal` | `public` |

```
bebopc build schema.bop --swift_out=./generated -D Visibility=package
```
