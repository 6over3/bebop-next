import Testing

@testable import SwiftBebop

@Suite struct BebopEmptyTests {
    @Test func encodesToZeroBytes() {
        let empty = BebopEmpty()
        #expect(empty.encodedSize == 0)
        #expect(empty.serializedData().isEmpty)
    }

    @Test func decodesFromEmptyInput() throws {
        let decoded = try BebopEmpty.decode(from: [])
        #expect(decoded.encodedSize == 0)
    }
}
