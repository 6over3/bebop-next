/// Errors thrown during Bebop wire format decoding.
public enum BebopDecodingError: Error, Sendable, Equatable {
    /// The buffer ended before the expected number of bytes could be read.
    case unexpectedEndOfData
    /// A decoded byte count overflowed the host representation.
    case invalidLength
    /// A decode finished before consuming all bytes in the value.
    case trailingData
    /// A decoded integer did not match any known enum member.
    case invalidEnumValue
    /// A string field contained invalid UTF-8.
    case invalidUTF8
    /// A string field was not followed by the required NUL terminator.
    case invalidStringTerminator
    /// An indexed message contained an invalid directory or field boundary.
    case malformedMessage
    /// A union's discriminator byte did not match any known branch.
    case unknownUnionDiscriminator(UInt8)
    /// A `BebopAny` unpack found a different type URL than expected.
    case typeMismatch(expected: String, actual: String)
}
