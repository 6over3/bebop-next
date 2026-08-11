import Testing

@testable import SwiftBebop

@Suite struct BatchResultsTests {
    @Test func callRefSuccess() throws {
        let response = BatchResponse(results: [
            BatchResult(
                callId: 0,
                outcome: .success(
                    BatchSuccess(payloads: [EchoResponse(value: "ok").encode()]))
            ),
        ])
        let results = try BatchResults(response)
        let ref = CallRef<EchoResponse>(callId: 0)
        let echo = try results[ref]
        #expect(echo.value == "ok")
    }

    @Test func callRefError() throws {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .error(RpcError(code: .notFound, detail: "gone"))),
        ])
        let results = try BatchResults(response)
        let ref = CallRef<EchoResponse>(callId: 0)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func callRefMissing() throws {
        let results = try BatchResults(BatchResponse(results: []))
        let ref = CallRef<EchoResponse>(callId: 42)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func callRefEmptyPayloads() throws {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .success(BatchSuccess(payloads: []))),
        ])
        let results = try BatchResults(response)
        let ref = CallRef<EchoResponse>(callId: 0)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func streamRefSuccess() throws {
        let response = BatchResponse(results: [
            BatchResult(
                callId: 0,
                outcome: .success(
                    BatchSuccess(payloads: [
                        CountResponse(i: 0).encode(),
                        CountResponse(i: 1).encode(),
                        CountResponse(i: 2).encode(),
                    ]))
            ),
        ])
        let results = try BatchResults(response)
        let ref = StreamRef<CountResponse>(callId: 0)
        let items = try results[ref]
        #expect(items.map(\.i) == [0, 1, 2])
    }

    @Test func streamRefError() throws {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .error(RpcError(code: .internal))),
        ])
        let results = try BatchResults(response)
        let ref = StreamRef<CountResponse>(callId: 0)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func streamRefEmptyPayloads() throws {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .success(BatchSuccess(payloads: []))),
        ])
        let results = try BatchResults(response)
        let ref = StreamRef<CountResponse>(callId: 0)
        let items = try results[ref]
        #expect(items.isEmpty)
    }

    @Test func duplicateResultIdsAreRejected() {
        let response = BatchResponse(results: [
            BatchResult(callId: 1, outcome: .success(BatchSuccess(payloads: []))),
            BatchResult(callId: 1, outcome: .success(BatchSuccess(payloads: []))),
        ])

        #expect(throws: BebopRpcError.self) {
            try BatchResults(response)
        }
    }
}
