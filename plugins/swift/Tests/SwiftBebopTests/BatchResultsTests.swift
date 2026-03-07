import Testing

@testable import SwiftBebop

@Suite struct BatchResultsTests {
    @Test func callRefSuccess() throws {
        let response = BatchResponse(results: [
            BatchResult(
                callId: 0,
                outcome: .success(
                    BatchSuccess(payloads: [EchoResponse(value: "ok").serializedData()]))
            ),
        ])
        let results = BatchResults(response)
        let ref = CallRef<EchoResponse>(callId: 0)
        let echo = try results[ref]
        #expect(echo.value == "ok")
    }

    @Test func callRefError() {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .error(RpcError(code: .notFound, detail: "gone"))),
        ])
        let results = BatchResults(response)
        let ref = CallRef<EchoResponse>(callId: 0)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func callRefMissing() {
        let results = BatchResults(BatchResponse(results: []))
        let ref = CallRef<EchoResponse>(callId: 42)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func callRefEmptyPayloads() {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .success(BatchSuccess(payloads: []))),
        ])
        let results = BatchResults(response)
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
                        CountResponse(i: 0).serializedData(),
                        CountResponse(i: 1).serializedData(),
                        CountResponse(i: 2).serializedData(),
                    ]))
            ),
        ])
        let results = BatchResults(response)
        let ref = StreamRef<CountResponse>(callId: 0)
        let items = try results[ref]
        #expect(items.map(\.i) == [0, 1, 2])
    }

    @Test func streamRefError() {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .error(RpcError(code: .internal))),
        ])
        let results = BatchResults(response)
        let ref = StreamRef<CountResponse>(callId: 0)
        #expect(throws: BebopRpcError.self) {
            _ = try results[ref]
        }
    }

    @Test func streamRefEmptyPayloads() throws {
        let response = BatchResponse(results: [
            BatchResult(callId: 0, outcome: .success(BatchSuccess(payloads: []))),
        ])
        let results = BatchResults(response)
        let ref = StreamRef<CountResponse>(callId: 0)
        let items = try results[ref]
        #expect(items.isEmpty)
    }
}
