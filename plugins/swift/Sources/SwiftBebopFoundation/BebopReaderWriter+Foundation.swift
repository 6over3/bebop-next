import Foundation
import SwiftBebop

public extension BebopReader {
    /// Read 16 bytes as a Foundation `UUID`.
    @inlinable
    mutating func readFoundationUUID() throws -> UUID {
        try UUID(readUUID())
    }

    /// Read `count` bytes as Foundation `Data`.
    @inlinable
    mutating func readData(_ count: Int) throws -> Data {
        guard count >= 0 else { throw BebopDecodingError.invalidLength }
        var data = Data(count: count)
        try data.withUnsafeMutableBytes { try readBytes(into: $0) }
        return data
    }
}

public extension BebopWriter {
    /// Write a Foundation `UUID` as 16 bytes.
    @inlinable
    mutating func writeUUID(_ value: UUID) {
        writeUUID(BebopUUID(value))
    }

    /// Write Foundation `Data` bytes.
    @inlinable
    mutating func writeData(_ data: Data) {
        data.withUnsafeBytes { writeBytes($0) }
    }
}
