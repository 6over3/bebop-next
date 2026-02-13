import Foundation
import SwiftBebop

extension BebopReader {
    /// Read 16 bytes as a Foundation `UUID`.
    @inlinable
    public mutating func readFoundationUUID() throws -> UUID {
        UUID(try readUUID())
    }

    /// Read `count` bytes as Foundation `Data`.
    @inlinable
    public mutating func readData(_ count: Int) throws -> Data {
        Data(try readBytes(count))
    }
}

extension BebopWriter {
    /// Write a Foundation `UUID` as 16 bytes.
    @inlinable
    public mutating func writeUUID(_ value: UUID) {
        writeUUID(BebopUUID(value))
    }

    /// Write Foundation `Data` bytes.
    @inlinable
    public mutating func writeData(_ data: Data) {
        guard !data.isEmpty else { return }
        data.withUnsafeBytes { buf in
            writeBytes(Array(buf.bindMemory(to: UInt8.self)))
        }
    }
}
