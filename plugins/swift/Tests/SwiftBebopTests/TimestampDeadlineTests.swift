import Testing

@testable import SwiftBebop

@Suite struct TimestampDeadlineTests {
    @Test func nowIsReasonable() {
        let now = BebopTimestamp.now
        #expect(now.seconds > 1_700_000_000)
        #expect(now.nanoseconds >= 0)
        #expect(now.nanoseconds < 1_000_000_000)
    }

    @Test func fromNowPositive() {
        let deadline = BebopTimestamp(fromNow: .seconds(60))
        let now = BebopTimestamp.now
        #expect(deadline.seconds >= now.seconds + 59)
    }

    @Test func isPastForOldTimestamp() {
        let old = BebopTimestamp(seconds: 0, nanoseconds: 0)
        #expect(old.isPast)
    }

    @Test func isPastForFutureTimestamp() {
        let future = BebopTimestamp(fromNow: .seconds(300))
        #expect(!future.isPast)
    }

    @Test func timeRemainingForFuture() {
        let future = BebopTimestamp(fromNow: .seconds(10))
        let remaining = future.timeRemaining
        #expect(remaining != nil)
    }

    @Test func timeRemainingForPast() {
        let past = BebopTimestamp(seconds: 0, nanoseconds: 0)
        #expect(past.timeRemaining == nil)
    }

    @Test func withDeadlineCompletesBeforeTimeout() async throws {
        let deadline = BebopTimestamp(fromNow: .seconds(5))
        let result = try await withDeadline(deadline) {
            42
        }
        #expect(result == 42)
    }

    @Test func withDeadlineAlreadyPastThrows() async {
        let past = BebopTimestamp(seconds: 0, nanoseconds: 0)
        await #expect(throws: BebopRpcError.self) {
            _ = try await withDeadline(past) { 42 }
        }
    }
}
