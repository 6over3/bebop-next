/// A 16-byte UUID value that does not depend on Foundation.
///
/// Wire format: 16 bytes in RFC 4122 byte order. Layout-compatible with
/// `Foundation.UUID`, enabling zero-cost bridging via `SwiftBebopFoundation`.
public struct BebopUUID: Sendable {
  // swiftlint:disable:next large_tuple
  public var uuid:
    (
      UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8,
      UInt8, UInt8, UInt8
    ) = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

  @inlinable @inline(__always)
  public init() {}

  // swiftlint:disable:next large_tuple
  @inlinable @inline(__always)
  public init(
    uuid: (
      UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8, UInt8,
      UInt8, UInt8, UInt8
    )
  ) {
    self.uuid = uuid
  }
}

// MARK: - Equatable / Hashable

extension BebopUUID: Equatable {
  @inlinable
  public static func == (lhs: BebopUUID, rhs: BebopUUID) -> Bool {
    Swift.withUnsafeBytes(of: lhs) { a in
      Swift.withUnsafeBytes(of: rhs) { b in
        let al = a.loadUnaligned(as: (UInt64, UInt64).self)
        let bl = b.loadUnaligned(as: (UInt64, UInt64).self)
        return (al.0 ^ bl.0) | (al.1 ^ bl.1) == 0
      }
    }
  }
}

extension BebopUUID: Hashable {
  @inlinable
  public func hash(into hasher: inout Hasher) {
    Swift.withUnsafeBytes(of: uuid) { hasher.combine(bytes: $0) }
  }
}

// MARK: - String conversion

extension BebopUUID: CustomStringConvertible {
  public var description: String { uuidString }

  /// The UUID formatted as `XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX` (uppercase).
  public var uuidString: String {
    Swift.withUnsafeBytes(of: uuid) { bytes in
      let h = Self.hexTable
      func hex(_ b: UInt8) -> (UInt8, UInt8) {
        (h[Int(b >> 4)], h[Int(b & 0x0F)])
      }
      let dash = UInt8(ascii: "-")
      let offsets: [Int] = [0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34]
      return String(unsafeUninitializedCapacity: 36) { buf in
        for i in 0..<16 {
          let (hi, lo) = hex(bytes[i])
          buf[offsets[i]] = hi
          buf[offsets[i] + 1] = lo
        }
        buf[8] = dash
        buf[13] = dash
        buf[18] = dash
        buf[23] = dash
        return 36
      }
    }
  }

  private static let hexTable: [UInt8] = Array("0123456789ABCDEF".utf8)
}

extension BebopUUID: LosslessStringConvertible {
  /// Parse a UUID string in `XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX` format.
  public init?(_ string: String) {
    self.init(uuidString: string)
  }

  /// Parse a UUID string in `XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX` format.
  public init?(uuidString string: String) {
    var string = string
    guard string.utf8.count == 36 else { return nil }
    let parsed: BebopUUID? = string.withUTF8 { buf -> BebopUUID? in
      guard buf[8] == UInt8(ascii: "-"),
        buf[13] == UInt8(ascii: "-"),
        buf[18] == UInt8(ascii: "-"),
        buf[23] == UInt8(ascii: "-")
      else { return nil }

      func nibble(_ c: UInt8) -> UInt8? {
        switch c {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): return c &- UInt8(ascii: "0")
        case UInt8(ascii: "a")...UInt8(ascii: "f"): return c &- UInt8(ascii: "a") &+ 10
        case UInt8(ascii: "A")...UInt8(ascii: "F"): return c &- UInt8(ascii: "A") &+ 10
        default: return nil
        }
      }
      func byte(at i: Int) -> UInt8? {
        guard let hi = nibble(buf[i]), let lo = nibble(buf[i + 1]) else { return nil }
        return hi << 4 | lo
      }
      let offsets: [Int] = [0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34]
      var raw = BebopUUID()
      let ok = Swift.withUnsafeMutablePointer(to: &raw.uuid) { ptr in
        ptr.withMemoryRebound(to: UInt8.self, capacity: 16) { bytes in
          for i in 0..<16 {
            guard let b = byte(at: offsets[i]) else { return false }
            bytes[i] = b
          }
          return true
        }
      }
      return ok ? raw : nil
    }
    guard let parsed else { return nil }
    self = parsed
  }
}

// MARK: - Codable

extension BebopUUID: Codable {
  public func encode(to encoder: any Encoder) throws {
    var container = encoder.singleValueContainer()
    try container.encode(uuidString)
  }

  public init(from decoder: any Decoder) throws {
    let container = try decoder.singleValueContainer()
    let string = try container.decode(String.self)
    guard let uuid = BebopUUID(uuidString: string) else {
      throw DecodingError.dataCorruptedError(
        in: container, debugDescription: "Invalid UUID string: \(string)")
    }
    self = uuid
  }
}
