/// Errors thrown during Bebop wire format decoding.
public enum BebopDecodingError: Error, Sendable, Equatable {
    /// The buffer ended before the expected number of bytes could be read.
    case unexpectedEndOfData
    /// A decoded integer did not match any known enum member.
    case invalidEnumValue
    /// A string field contained invalid UTF-8.
    case invalidUTF8
    /// A union's discriminator byte did not match any known branch.
    case unknownUnionDiscriminator(UInt8)
    /// A `BebopAny` unpack found a different type URL than expected.
    case typeMismatch(expected: String, actual: String)
}
