# Bebop wire format

Binary encoding specification for Bebop types. All multi-byte integers use little-endian byte order.

## Primitive types

| Type | Wire size | Encoding |
|------|-----------|----------|
| `bool` | 1 byte | `0x00` = false, any non-zero = true |
| `byte` | 1 byte | Unsigned 8-bit integer |
| `int8` | 1 byte | Signed 8-bit integer, two's complement |
| `int16` | 2 bytes | Signed 16-bit integer, little-endian |
| `uint16` | 2 bytes | Unsigned 16-bit integer, little-endian |
| `int32` | 4 bytes | Signed 32-bit integer, little-endian |
| `uint32` | 4 bytes | Unsigned 32-bit integer, little-endian |
| `int64` | 8 bytes | Signed 64-bit integer, little-endian |
| `uint64` | 8 bytes | Unsigned 64-bit integer, little-endian |
| `int128` | 16 bytes | Signed 128-bit integer, low 8 bytes first |
| `uint128` | 16 bytes | Unsigned 128-bit integer, low 8 bytes first |
| `float16` | 2 bytes | IEEE 754 binary16, little-endian |
| `float32` | 4 bytes | IEEE 754 binary32, little-endian |
| `float64` | 8 bytes | IEEE 754 binary64, little-endian |
| `bfloat16` | 2 bytes | Brain floating point format, little-endian |
| `uuid` | 16 bytes | Raw bytes, RFC 4122 byte order |
| `timestamp` | 12 bytes | See below |
| `duration` | 12 bytes | See below |

### 128-bit integers

128-bit integers are stored as two 64-bit halves:

```
offset 0: low 64 bits (uint64, little-endian)
offset 8: high 64 bits (uint64/int64, little-endian)
```

### timestamp

12 bytes representing a point in time since the Unix epoch (1970-01-01 00:00:00 UTC):

```
offset 0: seconds (int64, little-endian)
offset 8: nanoseconds (int32, little-endian)
```

The nanoseconds field should be in the range [0, 999999999].

### duration

12 bytes representing a signed time span:

```
offset 0: seconds (int64, little-endian)
offset 8: nanoseconds (int32, little-endian)
```

For negative durations, both seconds and nanoseconds are negative or zero.

## Strings

```
┌─────────────────┬─────────────────────────┬───────────┐
│ length (uint32) │ UTF-8 content (N bytes) │ NUL (1)   │
└─────────────────┴─────────────────────────┴───────────┘
```

- 4-byte unsigned length prefix (byte count of UTF-8 content, excludes NUL)
- UTF-8 encoded string data
- 1-byte null terminator

Total wire size: 4 + length + 1 bytes.

Example: `"hello"` encodes as:

```
05 00 00 00    // length = 5
68 65 6c 6c 6f // "hello"
00             // NUL terminator
```

## Enums

Enums encode as their underlying integer type. Default base type is `int32`.

```bebop
enum Status : uint8 {
    ACTIVE = 1;
}
```

`Status.ACTIVE` encodes as 1 byte: `01`.

## Dynamic arrays

```
┌─────────────────┬───────────────────────────────────────┐
│ count (uint32)  │ elements (element_size × count bytes) │
└─────────────────┴───────────────────────────────────────┘
```

- 4-byte unsigned count prefix (number of elements)
- Elements in sequence, no padding
- Every element must be present and valid (no null values)

An array of 3 elements always contains exactly 3 encoded values. Empty values (zero-length strings, empty structs) are valid; null/missing values are not.

Example: `int32[] = [1, 2, 3]` encodes as:

```
03 00 00 00    // count = 3
01 00 00 00    // elements[0] = 1
02 00 00 00    // elements[1] = 2
03 00 00 00    // elements[2] = 3
```

## Fixed arrays

```
┌───────────────────────────────────────┐
│ elements (element_size × count bytes) │
└───────────────────────────────────────┘
```

No length prefix. Element count is known at compile time.

Example: `byte[4] = [0xDE, 0xAD, 0xBE, 0xEF]` encodes as:

```
de ad be ef    // 4 bytes, no prefix
```

## Maps

```
┌─────────────────┬─────────────────────────────────────────────┐
│ count (uint32)  │ entries (key₀, value₀, key₁, value₁, ...)   │
└─────────────────┴─────────────────────────────────────────────┘
```

- 4-byte unsigned count prefix (number of entries)
- Key-value pairs in sequence: key followed immediately by value
- No padding between entries

Example: `map[uint8, int32] = {1: 100, 2: 200}` encodes as:

```
02 00 00 00    // count = 2
01             // key[0] = 1
64 00 00 00    // value[0] = 100
02             // key[1] = 2
c8 00 00 00    // value[1] = 200
```

## Structs

```
┌─────────┬─────────┬─────────┬─────┐
│ field₀  │ field₁  │ field₂  │ ... │
└─────────┴─────────┴─────────┴─────┘
```

Fields encoded in definition order. No tags, no length prefix, no padding.

```bebop
struct Point {
    x: float32;
    y: float32;
}
```

`Point { x: 1.0, y: 2.0 }` encodes as:

```
00 00 80 3f    // x = 1.0 (IEEE 754)
00 00 00 40    // y = 2.0 (IEEE 754)
```

Empty structs encode as zero bytes.

## Messages

```
┌─────────────────┬───────────────────────────────────────────────────┐
│ length (uint32) │ content (tag₀, value₀, tag₁, value₁, ..., 0x00)   │
└─────────────────┴───────────────────────────────────────────────────┘
```

- 4-byte unsigned length prefix (byte count of content)
- Fields as tag-value pairs:
  - 1-byte tag (field index, 1-255)
  - Value encoded according to field type
- 0x00 byte terminates the field sequence

Fields may appear in any order. Absent fields are not encoded. Unknown tags should be skipped by decoders.

```bebop
message UserRequest {
    user_id(1): int32;
    include_profile(2): bool;
}
```

`UserRequest { user_id: 42, include_profile: true }` encodes as:

```
08 00 00 00    // length = 8 bytes
01             // tag = 1 (user_id)
2a 00 00 00    // value = 42
02             // tag = 2 (include_profile)
01             // value = true
00             // end marker
```

`UserRequest { user_id: 42 }` (include_profile absent) encodes as:

```
06 00 00 00    // length = 6 bytes
01             // tag = 1 (user_id)
2a 00 00 00    // value = 42
00             // end marker
```

Empty message encodes as:

```
01 00 00 00    // length = 1
00             // end marker
```

## Unions

```
┌─────────────────┬───────────────────────┬──────────────────┐
│ length (uint32) │ discriminator (uint8) │ branch content   │
└─────────────────┴───────────────────────┴──────────────────┘
```

- 4-byte unsigned length prefix (byte count of discriminator + content)
- 1-byte discriminator (branch index, 0-255)
- Branch content follows

```bebop
union Shape {
    Circle(1): { radius: float32; };
    Rectangle(2): { width: float32; height: float32; };
}
```

`Shape.Circle { radius: 5.0 }` encodes as:

```
05 00 00 00    // length = 5 bytes
01             // discriminator = 1 (Circle)
00 00 a0 40    // radius = 5.0
```

`Shape.Rectangle { width: 3.0, height: 4.0 }` encodes as:

```
09 00 00 00    // length = 9 bytes
02             // discriminator = 2 (Rectangle)
00 00 40 40    // width = 3.0
00 00 80 40    // height = 4.0
```

### Inline message branches

Union branches can use inline message syntax:

```bebop
union Event {
    Request(1): message {
        method(1): string;
        path(2): string;
    };
    Ack(2): { code: int32; };
}
```

The `Request` branch is an inline message. Its content encodes as a message with its own length prefix and tag-value pairs:

```
05 00 00 00    // union length = 5 bytes
01             // discriminator = 1 (Request)
...            // message content (length + tag-value pairs + end marker)
```

The `Ack` branch is an inline struct. Its content encodes as a struct (fields in order, no tags):

```
05 00 00 00    // union length = 5 bytes
02             // discriminator = 2 (Ack)
00 00 00 00    // code = 0
```

## Nested types

Collection types nest recursively:

### Array of arrays

`int32[][]` - each inner array has its own count prefix:

```
02 00 00 00    // outer count = 2
02 00 00 00    // inner[0] count = 2
01 00 00 00    // inner[0][0] = 1
02 00 00 00    // inner[0][1] = 2
01 00 00 00    // inner[1] count = 1
03 00 00 00    // inner[1][0] = 3
```

### Array of strings

`string[]` - each string has its own length prefix:

```
02 00 00 00    // count = 2
02 00 00 00    // length of "ab"
61 62 00       // "ab" + NUL
03 00 00 00    // length of "cde"
63 64 65 00    // "cde" + NUL
```

### Fixed array of dynamic arrays

`int32[][3]` - outer dimension fixed (no prefix), inner arrays dynamic:

```
02 00 00 00    // [0] count = 2
01 00 00 00    // [0][0] = 1
02 00 00 00    // [0][1] = 2
00 00 00 00    // [1] count = 0 (empty)
01 00 00 00    // [2] count = 1
03 00 00 00    // [2][0] = 3
```

## Empty vs null

Empty values are valid and encode normally. Null values do not exist in Bebop.

| Type | Empty encoding |
|------|----------------|
| `string` | 4 bytes length (0) + 1 byte NUL = 5 bytes |
| `byte[]` | 4 bytes count (0) = 4 bytes |
| Empty struct | 0 bytes |
| Message (no fields set) | 4 bytes length (1) + 1 byte end marker = 5 bytes |

An empty struct in an array takes zero bytes. An empty string in an array takes 5 bytes. All are valid; there is no way to encode "null" or "missing" for array elements.

## Wire compatibility

### Structs

Structs are not forward-compatible. Any change to a struct's field list is a breaking change:

- Adding fields changes the wire size
- Removing fields changes the wire size
- Reordering fields changes the encoding

If you need to evolve a struct, create a new versioned type (e.g., `UserV2`) and migrate consumers.

### Fixed arrays

Changing the size of a fixed array is a breaking change. A `byte[32]` field encodes as exactly 32 bytes; changing it to `byte[64]` doubles the wire size.

To change a fixed array size, deprecate the message field and add a new field with the new size.

### Messages

Messages are designed for schema evolution:

- Adding fields with new tags is backward compatible
- Removing fields is safe (old tags are ignored by new decoders)
- Reusing a removed tag with a different type breaks compatibility
- Field order in the wire format does not matter

### Unions

Union branches are identified by discriminator:

- Adding new branches is backward compatible
- Removing branches may cause decode failures for old data
- Changing a branch's type breaks compatibility

## Size limits

| Limit | Value |
|-------|-------|
| Fixed array elements | 65535 (2^16 - 1) |
| String length | ~4 GB (2^32 - 1 bytes) |
| Dynamic array count | ~4 billion (2^32 - 1 elements) |
| Map entry count | ~4 billion (2^32 - 1 entries) |
| Message content | ~4 GB (2^32 - 1 bytes) |
| Union content | ~4 GB (2^32 - 1 bytes) |

These are wire format limits. Practical limits depend on available memory and transport constraints.

## Size calculations

Minimum sizes for computing buffer requirements:

| Type | Minimum size |
|------|-------------|
| Primitives | Fixed (see table above) |
| `string` | 5 bytes (4 length + 1 NUL) |
| Dynamic array | 4 bytes (count only) |
| Fixed array | element_size × count |
| Map | 4 bytes (count only) |
| Struct | Sum of field sizes |
| Message | 5 bytes (4 length + 1 end marker) |
| Union | 5 bytes (4 length + 1 discriminator) |

## Design notes

Bebop uses fixed-width integers for all length prefixes and numeric types. There is no variable-length integer encoding (like Protocol Buffers' varints) or built-in compression.

This design enables:

- **Single-pass encoding/decoding** - each byte is visited exactly once, no backtracking
- **Linear time complexity** - O(n) where n is data size, which is optimal for serialization
- **Cache efficiency** - sequential memory access patterns with no random seeks
- **Zero-copy decoding** - strings and byte arrays can reference the input buffer directly

If wire size is a concern, apply a fast compression layer like zstd or lz4 to the encoded output. This typically achieves better compression than varint encoding while maintaining high throughput.
