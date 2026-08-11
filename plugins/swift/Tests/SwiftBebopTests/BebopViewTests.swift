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
    #expect(view.value == "héllo 🌍")
}

@Test func structViewUsesNaturalProperties() throws {
    let value = BatchSuccess(payloads: [[1, 2, 3], [4]], metadata: ["trace": "abc"])
    let view = try BatchSuccess.View(value.serializedData())

    #expect(view.payloads.map(Array.init) == [[1, 2, 3], [4]])
    let metadata = Dictionary(uniqueKeysWithValues: view.metadata.map { ($0.key.value, $0.value.value) })
    #expect(metadata == ["trace": "abc"])
    #expect(try view.decoded() == value)
}

@Test func messageViewProvidesTypedOptionalFields() throws {
    let value = RpcError(code: .invalidArgument, detail: "bad widget", metadata: ["field": "name"])
    let view = try RpcError.View(value.serializedData())

    #expect(view.code == .invalidArgument)
    #expect(view.detail?.value == "bad widget")
    let metadata = try #require(view.metadata)
    #expect(metadata["field"]?.value == "name")
    #expect(try view.decoded() == value)
}

@Test func unionViewExposesTypedContentWithoutDecoding() throws {
    let value = BatchOutcome.success(
        BatchSuccess(payloads: [[9, 8, 7]], metadata: ["request": "42"]))
    let view = try BatchOutcome.View(value.serializedData())

    #expect(view.discriminator == 1)
    guard case .success(let success) = view.value else {
        Issue.record("expected success view")
        return
    }
    #expect(success.payloads.first.map(Array.init) == [9, 8, 7])
    #expect(success.metadata["request"]?.value == "42")
    #expect(try view.decoded() == value)
}

@Test func unionViewPreservesUnknownPayloadAsAView() throws {
    let value = BatchOutcome.unknown(discriminator: 99, data: [1, 2, 3])
    let view = try BatchOutcome.View(value.serializedData())

    guard case .unknown(let discriminator, let data) = view.value else {
        Issue.record("expected unknown view")
        return
    }
    #expect(discriminator == 99)
    #expect(Array(data) == [1, 2, 3])
}
