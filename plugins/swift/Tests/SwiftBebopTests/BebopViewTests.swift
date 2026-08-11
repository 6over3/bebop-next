import Testing

@testable import SwiftBebop

@Test func stringViewRejectsInvalidUTF8WithoutMaterializingAString() {
    var reader = BebopViewReader(BebopView([2, 0, 0, 0, 0xC0, 0x80, 0]))

    #expect(throws: BebopDecodingError.invalidUTF8) {
        try reader.readStringView()
    }
}

@Test func stringViewBorrowsANativeUTF8Span() throws {
    var writer = BebopWriter()
    writer.writeString("héllo 🌍")
    let encoded = writer.toBytes()
    var reader = BebopViewReader(BebopView(encoded))
    let view = try reader.readStringView()

    let inspection = view.withUTF8Span { span in
        (
            matchesBytes: span.bytesEqual(to: "héllo 🌍".utf8),
            copiedValue: String(copying: span),
            byteCount: span.count
        )
    }

    #expect(inspection.matchesBytes)
    #expect(inspection.copiedValue == "héllo 🌍")
    #expect(inspection.byteCount == "héllo 🌍".utf8.count)
    #expect(view.string == "héllo 🌍")
}

@Test func structViewUsesNaturalProperties() throws {
    let value = BatchSuccess(payloads: [[1, 2, 3], [4]], metadata: ["trace": "abc"])
    let view = try BatchSuccess.View(value.encode())

    #expect(view.payloads.map(Array.init) == [[1, 2, 3], [4]])
    #expect(view.payloads.first?.bytes.elementsEqual([1, 2, 3]) == true)
    let metadata = Dictionary(
        uniqueKeysWithValues: view.metadata.map { ($0.key.string, $0.value.string) })
    #expect(metadata == ["trace": "abc"])
    #expect(try view.decoded() == value)
}

@Test func recordViewsApplyCustomCollectionLimits() {
    let value = BatchSuccess(payloads: [[1], [2]])

    #expect(throws: BebopDecodingError.limitExceeded) {
        try BatchSuccess.View(
            value.encode(),
            limits: BebopDecodeLimits(maxCollectionElements: 1)
        )
    }
}

@Test func messageViewProvidesTypedOptionalFields() throws {
    let value = RpcError(code: .invalidArgument, detail: "bad widget", metadata: ["field": "name"])
    let view = try RpcError.View(value.encode())

    #expect(try view.code == .invalidArgument)
    #expect(try view.detail?.string == "bad widget")
    let metadata = try #require(try view.metadata)
    #expect(metadata["field"]?.string == "name")
    #expect(try view.decoded() == value)
}

@Test func unionViewExposesTypedContentWithoutDecoding() throws {
    let value = BatchOutcome.success(
        BatchSuccess(payloads: [[9, 8, 7]], metadata: ["request": "42"]))
    let view = try BatchOutcome.View(value.encode())

    #expect(view.discriminator == 1)
    guard case .success(let success) = view.value else {
        Issue.record("expected success view")
        return
    }
    #expect(success.payloads.first.map(Array.init) == [9, 8, 7])
    #expect(success.metadata["request"]?.string == "42")
    #expect(try view.decoded() == value)
}

@Test func unionViewPreservesUnknownPayloadAsAView() throws {
    let value = BatchOutcome.unknown(discriminator: 99, data: [1, 2, 3])
    let view = try BatchOutcome.View(value.encode())

    guard case .unknown(let discriminator, let data) = view.value else {
        Issue.record("expected unknown view")
        return
    }
    #expect(discriminator == 99)
    #expect(Array(data) == [1, 2, 3])
}

@Test func lengthPrefixedViewsPreserveDecodeDepthLimits() {
    var reader = BebopViewReader(
        BebopView([1, 0, 0, 0, 0]),
        limits: BebopDecodeLimits(maxDepth: 0)
    )

    #expect(throws: BebopDecodingError.limitExceeded) {
        try reader.readLengthPrefixedValue { body in
            try body.readNested { nested in try nested.readByte() }
        }
    }
}

@Test func contiguousByteArrayViewsBorrowTheirPayload() throws {
    let encoded: [UInt8] = [3, 0, 0, 0, 10, 20, 30]
    var reader = BebopViewReader(BebopView(encoded))
    let view = try reader.readContiguousArrayView(elementSize: 1) { reader in
        try reader.readByte()
    }

    #expect(view.count == 3)
    #expect(view[1] == 20)
    #expect(view.bytes.elementsEqual([10, 20, 30]))
    #expect(Array(view) == [10, 20, 30])
    try reader.finish()
}

@Test func contiguousArrayViewsRejectTruncatedPayloads() {
    var reader = BebopViewReader(BebopView([2, 0, 0, 0, 1, 0, 0, 0]))

    #expect(throws: BebopDecodingError.unexpectedEndOfData) {
        try reader.readContiguousArrayView(elementSize: 4) { reader in
            try reader.readUInt32()
        }
    }
}

@Test func mapViewsDoNotMaterializeADictionaryToValidateStructure() throws {
    var writer = BebopWriter()
    writer.writeMapLength(2)
    writer.writeString("same")
    writer.writeUInt32(1)
    writer.writeString("same")
    writer.writeUInt32(2)
    let encoded = writer.toBytes()

    var viewReader = BebopViewReader(BebopView(encoded))
    let view = try viewReader.readMapView(
        key: { try $0.readStringView() },
        value: { try $0.readUInt32() }
    )
    #expect(view.map { ($0.key.string, $0.value) }.count == 2)

    #expect(throws: BebopDecodingError.duplicateMapKey) {
        try encoded.withUnsafeBytes { bytes in
            var reader = BebopReader(data: bytes)
            return try reader.readDynamicMap { reader in
                (try reader.readString(), try reader.readUInt32())
            }
        }
    }
}
