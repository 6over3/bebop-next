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
        try Data(readBytes(count))
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
        guard !data.isEmpty else { return }
        data.withUnsafeBytes { buf in
            writeBytes(Array(buf.bindMemory(to: UInt8.self)))
        }
    }
}
