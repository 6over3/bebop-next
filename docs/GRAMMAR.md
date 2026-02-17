# Bebop schema language

Bebop is a binary serialization format with a schema language for defining data structures. This document covers the complete grammar and usage rules.

## Source encoding

Bebop schema files must be valid UTF-8. The compiler rejects files containing invalid UTF-8 byte sequences.

String literals within schemas must also contain valid UTF-8. Invalid byte sequences in string literals produce a compile-time error.

## File structure

A Bebop schema file (`.bop`) has three sections, in this order:

1. **Header** - edition and package declarations (both optional)
2. **Imports** - import statements
3. **Definitions** - types, constants, services

```bebop
edition = "2026"
package my.app

import "bebop/decorators.bop"
import "shared/types.bop"

struct User {
    name: string;
}
```

### Edition

Optional. Specifies the schema language version.

```bebop
edition = "2026"
```

If present, must be the first non-comment line.

### Package

Optional. Declares a namespace for all definitions in the file.

```bebop
package my.app.models
```

- Dot-separated identifiers
- Types accessible from other files as `my.app.models.TypeName`
- Must come after edition (if present), before imports

### Imports

Import other schema files to use their definitions.

```bebop
import "bebop/decorators.bop"
import "shared/types.bop"
```

Import paths are resolved against include directories. Parent directory traversal (`../`) is not allowed. Imported types are accessible by simple name (if unambiguous) or qualified name.

Include directories are configured via:
- CLI: `bebopc build -I <DIR>` or `--include <DIR>`
- Config: `include:` array in `bebop.yml`

Imports must come after package, before definitions.

## Comments

Three styles:

```bebop
// Line comment

/* Block comment
   spans multiple lines */

/// Documentation comment
```

Use `///` for documentation comments. Comments immediately before a definition are captured as metadata and appear in generated code.

## Primitive types

| Type | Size | Description |
|------|------|-------------|
| `bool` | 1 byte | Boolean |
| `byte` | 1 byte | Unsigned 8-bit integer |
| `int8` | 1 byte | Signed 8-bit integer |
| `int16` | 2 bytes | Signed 16-bit integer |
| `uint16` | 2 bytes | Unsigned 16-bit integer |
| `int32` | 4 bytes | Signed 32-bit integer |
| `uint32` | 4 bytes | Unsigned 32-bit integer |
| `int64` | 8 bytes | Signed 64-bit integer |
| `uint64` | 8 bytes | Unsigned 64-bit integer |
| `int128` | 16 bytes | Signed 128-bit integer |
| `uint128` | 16 bytes | Unsigned 128-bit integer |
| `float16` | 2 bytes | IEEE 754 half precision |
| `float32` | 4 bytes | IEEE 754 single precision |
| `float64` | 8 bytes | IEEE 754 double precision |
| `bfloat16` | 2 bytes | Brain floating point |
| `string` | variable | UTF-8 text, length-prefixed |
| `uuid` | 16 bytes | UUID/GUID identifier |
| `timestamp` | 12 bytes | Point in time (seconds + nanoseconds since Unix epoch) |
| `duration` | 12 bytes | Time span (seconds + nanoseconds) |

### Type aliases

Shorthand aliases exist for convenience:

| Alias | Canonical |
|-------|-----------|
| `uint8` | `byte` |
| `guid` | `uuid` |
| `half` | `float16` |
| `bf16` | `bfloat16` |

Prefer canonical names (`uuid`, `byte`, `float16`, `bfloat16`) in schemas. Aliases are not guaranteed to remain in future editions.

### timestamp vs duration

`timestamp` represents an absolute point in time: seconds and nanoseconds since the Unix epoch (1970-01-01 00:00:00 UTC).

`duration` represents a time span: a signed number of seconds and nanoseconds. Use for intervals, timeouts, or relative offsets.

## Naming conventions

| Element | Convention | Example |
|---------|------------|---------|
| Definitions (struct, message, enum, union, service) | PascalCase | `UserProfile`, `MessageType` |
| Fields | snake_case | `user_id`, `created_at` |
| Enum members | SCREAMING_CASE | `NOT_FOUND`, `READ_ONLY` |
| Constants | SCREAMING_CASE | `MAX_SIZE`, `DEFAULT_PORT` |

## Collection types

All collection elements must be present and valid. Bebop has no null type. Empty values (zero-length strings, empty structs) are valid collection elements; null or missing values are not.

### Dynamic arrays

Variable-length, count-prefixed.

```bebop
values: int32[];
names: string[];
users: User[];
matrix: int32[][];  // nested
```

### Fixed arrays

Compile-time constant size. Maximum 65535 elements.

```bebop
hash: byte[32];
coords: float32[3];
grid: int32[3][2];           // 3x2 matrix
mixed: byte[10][];           // dynamic array of fixed arrays
also_mixed: int32[][3];      // fixed array of dynamic arrays
```

Changing the size of a fixed array is a breaking change. To resize, deprecate the message field containing the old array and add a new field with the new size.

### Maps

Associative collections. Keys must be primitive types that support equality comparison.

```bebop
lookup: map[string, int32];
cache: map[uuid, User];
nested: map[string, map[string, int32]];
```

**Valid key types**: `bool`, `byte`, `int8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `int128`, `uint128`, `string`, `uuid`.

**Invalid key types**: floats (equality is problematic), `timestamp`, `duration`, arrays, maps, structs, messages, unions, enums.

## Enums

Named integer constants.

```bebop
enum Status {
    UNKNOWN = 0;
    ACTIVE = 1;
    INACTIVE = 2;
}
```

### Base type

Default is `uint32`. Override with a colon:

```bebop
enum Flags : uint8 {
    NONE = 0;
    READ = 1;
    WRITE = 2;
}
```

Valid base types: `byte`, `uint8`, `int8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`.

### Value expressions

Enum values support expressions:

```bebop
import "bebop/decorators.bop"

@flags
enum Permissions : uint32 {
    NONE = 0;
    READ = 0x0001;
    WRITE = 1 << 1;
    EXECUTE = 1 << 2;
    READ_WRITE = READ | WRITE;
    ALL = READ | WRITE | EXECUTE;
}
```

Supported operators: `|` (or), `&` (and), `~` (not), `<<` (left shift), `>>` (right shift), `+`, `-`.

### Zero requirement

Every enum must have a member with value 0. This is the default/unset state.

### Decorators on members

```bebop
import "bebop/decorators.bop"

enum Letters {
    @deprecated("use B instead")
    A = 0;
    B = 1;
}
```

## Structs

Fixed-layout aggregate types. Fields are positionally encoded (no tags).

```bebop
struct Point {
    x: float32;
    y: float32;
}
```

### Mutable structs

By default, structs are immutable in the generated code. Use `mut` to indicate mutability:

```bebop
mut struct MutablePoint {
    x: float32;
    y: float32;
}
```

Whether mutability is enforced depends on the target language and its support for immutable data structures. Languages without immutability constructs may ignore this modifier.

### Field syntax

```bebop
field_name: Type;
```

No field tags. Order matters for wire encoding.

### Schema evolution

Structs are not forward-compatible. Once a struct is used in your schema, any modification is a breaking change:

- Adding fields changes wire size
- Removing fields changes wire size
- Reordering fields changes encoding

To evolve a struct, create a new versioned type:

```bebop
struct UserV1 { name: string; }
struct UserV2 { name: string; email: string; }
```

If you need field-level evolution, use a message instead.

### What structs can contain

- Primitives
- Enums
- Other structs
- Arrays (dynamic and fixed)
- Maps
- References to messages (not inline definitions)

### What structs cannot contain

- Inline message definitions
- Inline union definitions (use references)

### Empty structs

Empty structs encode as zero bytes. The standard library provides `bebop.Empty` for RPC methods that take no input or return no data:

```bebop
import "bebop/empty.bop"

service HealthService {
    Ping(bebop.Empty): bebop.Empty;
}
```

## Messages

Fields identified by tag, not position. Wire-compatible across schema evolution.

```bebop
message UserRequest {
    user_id(1): int32;
    include_profile(2): bool;
    max_results(3): int32;
}
```

### Field syntax

```bebop
field_name(tag): Type;
```

- Tag must be a positive integer (1-255)
- Tags must be unique within the message
- Tags need not be sequential; gaps are allowed

```bebop
message Sparse {
    first(1): string;
    tenth(10): string;
    last(255): string;
}
```

### Wire compatibility

Messages support forward/backward compatibility:

- New fields can be added with new tags
- Removed fields leave their tags unused (do not reuse)
- Field order in source does not affect wire format, but order them logically for humans

### What messages can contain

- Primitives
- Enums
- Structs
- Other messages (including self-references)
- Arrays
- Maps

Messages can reference themselves or their parent types for recursive structures:

```bebop
message TreeNode {
    value(1): int32;
    children(2): TreeNode[];
}
```

## Unions

Discriminated variant types. Exactly one branch is active at a time.

```bebop
union Shape {
    Circle(1): {
        radius: float32;
    };
    Rectangle(2): {
        width: float32;
        height: float32;
    };
}
```

### Branch syntax

Four forms:

**Inline struct** (default):

```bebop
BranchName(discriminator): {
    field_name: Type;
};
```

**Inline mutable struct**:

```bebop
BranchName(discriminator): mut {
    field_name: Type;
};
```

**Inline message**:

```bebop
BranchName(discriminator): message {
    field_name(1): Type;
};
```

**Type reference**:

```bebop
BranchName(discriminator): ExistingType;
```

### Discriminator

- Unsigned 8-bit value (0-255)
- Must be unique within the union
- Can appear in any order in source, but keep them sequential for readability

```bebop
// Valid but hard to read
union WeirdOrder {
    Two(2): mut {};
    Four(4): mut {};
    One(1): mut {};
}

// Prefer this
union BetterOrder {
    One(1): mut {};
    Two(2): mut {};
    Four(4): mut {};
}
```

### Empty branches

Valid. Useful for signaling states without data:

```bebop
union Result {
    Success(1): mut { value: string; };
    Empty(2): mut {};
    Error(3): mut { code: int32; };
}
```

### Type references

Branches can reference existing structs or messages:

```bebop
struct Point {
    x: float32;
    y: float32;
}

message Notification {
    id(1): int64;
    text(2): string;
}

union Payload {
    Position(1): Point;
    Alert(2): Notification;
}
```

### What unions cannot contain

Unions cannot be nested. This is invalid:

```bebop
// INVALID
union Outer {
    Inner(1): union {
        A(1): mut {};
    };
}
```

## Services

RPC interface definitions.

```bebop
service UserService {
    GetUser(UserRequest): UserResponse;
}
```

### Method types

**Unary** - single request, single response:

```bebop
GetUser(Request): Response;
```

**Server streaming** - single request, stream of responses:

```bebop
Watch(Request): stream Response;
```

**Client streaming** - stream of requests, single response:

```bebop
Upload(stream Request): Response;
```

**Bidirectional streaming** - streams both directions:

```bebop
Chat(stream Request): stream Response;
```

### Method constraints

- Request and response types must be named struct, message, or union definitions
- Inline type definitions are not allowed
- Primitives, arrays, and maps cannot be used directly

### Service composition

Services can include methods from other services using the `with` keyword:

```bebop
service BaseService {
    GetStatus(StatusRequest): StatusResponse;
}

service ExtendedService with BaseService {
    GetDetails(DetailsRequest): DetailsResponse;
}
```

`ExtendedService` includes all methods from `BaseService` plus its own methods.

Multiple services can be composed:

```bebop
service Combined with ServiceA, ServiceB, ServiceC {
    OwnMethod(Request): Response;
}
```

Constraints:
- Mixins must be service types (not structs, messages, or other definitions)
- Method names must be unique across all composed services

## Constants

Named compile-time values.

```bebop
const int32 MAX_SIZE = 1024;
const string DEFAULT_HOST = "localhost";
const bool ENABLED = true;
const uuid NAMESPACE_ID = "550e8400-e29b-41d4-a716-446655440000";
```

### Allowed types

`bool`, `byte`, `int8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `int128`, `uint128`, `float16`, `float32`, `float64`, `bfloat16`, `string`, `uuid`, `timestamp`, `duration`.

### Numeric formats

```bebop
const int32 DECIMAL = 123;
const int32 HEX = 0xFF;
const int32 NEGATIVE = -42;
const float64 SCIENTIFIC = 1.23e10;
```

### Special float values

```bebop
const float64 POS_INF = inf;
const float64 NEG_INF = -inf;
const float64 NOT_A_NUMBER = nan;
```

### String literals

String literals are delimited by double quotes (`"`) or single quotes (`'`). They must contain valid UTF-8.

```bebop
const string GREETING = "Hello, world!";
const string ALSO_VALID = 'single quotes work too';
```

Literal newlines are allowed within strings:

```bebop
const string MULTILINE = "Line one
Line two
Line three";
```

### Escape sequences

| Escape | Output |
|--------|--------|
| `\\` | Backslash |
| `\n` | Newline (LF) |
| `\r` | Carriage return (CR) |
| `\t` | Tab |
| `\0` | Null byte |
| `\"` | Double quote |
| `\'` | Single quote |
| `\u{XXXX}` | Unicode codepoint (1-6 hex digits) |

```bebop
const string PATH = "C:\\Users\\name";
const string EMOJI = "Grinning face: \u{1F600}";
const string TABS = "col1\tcol2\tcol3";
const string LINES = "line1\nline2";
```

Unicode escapes accept 1 to 6 hexadecimal digits and produce UTF-8 output:

```bebop
const string EURO = "\u{20AC}";      // €
const string EMOJI = "\u{1F600}";    // 😀
const string ASCII = "\u{41}";       // A
```

Invalid codepoints are rejected:

- Surrogate range (U+D800 to U+DFFF)
- Values above U+10FFFF

To include a literal backslash followed by `u`, escape the backslash:

```bebop
const string LITERAL = "\\u{1F600}";  // outputs: \u{1F600}
```

### Quote escaping

Within double-quoted strings, use `""` to include a literal double quote:

```bebop
const string QUOTED = "He said ""hello""";  // outputs: He said "hello"
```

Within single-quoted strings, use `''` for a literal single quote:

```bebop
const string APOSTROPHE = 'It''s fine';  // outputs: It's fine
```

### Environment variable substitution

String constants support environment variable substitution using `$(VAR_NAME)` syntax:

```bebop
const string API_HOST = "$(API_HOST)";
const string CONNECTION = "host=$(DB_HOST);port=$(DB_PORT)";
```

Variables are resolved at compile time from the environment passed to the compiler. If a variable is not found, the compiler emits an error. The original string (before substitution) is preserved in the schema descriptor for tooling.

### Timestamp literals

Timestamps are specified as ISO 8601 formatted strings:

```bebop
const timestamp EPOCH = "1970-01-01T00:00:00Z";
const timestamp WITH_OFFSET = "2024-01-15T10:30:00+05:00";
const timestamp WITH_NANOS = "2024-01-15T10:30:00.123456789Z";
```

Supported formats:
- `YYYY-MM-DDTHH:MM:SSZ` - UTC time
- `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` - with nanoseconds (up to 9 digits)
- `YYYY-MM-DDTHH:MM:SS+HH:MM` - with timezone offset
- `YYYY-MM-DDTHH:MM:SS-HH:MM` - with negative timezone offset

The `T` separator can also be a space. Timezone offsets are applied during parsing to convert to UTC.

### Duration literals

Durations are specified as strings with time unit suffixes:

```bebop
const duration ONE_HOUR = "1h";
const duration NINETY_MINUTES = "1h30m";
const duration TEN_SECONDS = "10s";
const duration HALF_SECOND = "500ms";
const duration PRECISE = "1h30m45s500ms";
```

Supported units:
- `h` - hours
- `m` - minutes (without `s` suffix)
- `s` - seconds
- `ms` - milliseconds
- `us` - microseconds
- `ns` - nanoseconds

Fractional values are supported for seconds: `"1.5s"` equals `"1s500ms"`.

### Byte array literals

Byte arrays use a `b` prefix with string syntax. This is the only array type allowed in const declarations:

```bebop
const byte[] MAGIC = b"\x89PNG\r\n\x1a\n";
const byte[] HELLO = b"hello";
const byte[] BINARY = b"\x00\x01\x02\x03";
```

The `b` prefix indicates binary data, allowing arbitrary byte values including those that aren't valid UTF-8.

Escape sequences supported:
- `\xHH` - hexadecimal byte value (e.g., `\x0a` = newline)
- `\n`, `\r`, `\t`, `\0` - common escape characters
- `\\` - backslash

## Decorators

Metadata annotations on definitions, fields, and members.

### Usage

Decorators require importing their definitions:

```bebop
import "bebop/decorators.bop"

@deprecated
struct OldThing {}

@deprecated("use NewThing instead")
struct OlderThing {}

@flags
enum Perms : uint8 {
    NONE = 0;
    READ = 1;
}

message Request {
    @deprecated("no longer used")
    legacy_field(1): string;
}
```

### Standard decorators

Import the standard library:

```bebop
import "bebop/decorators.bop"
```

**`@deprecated`** - marks definitions, fields, enum members, branches, or methods as deprecated. Optional `reason` parameter.

```bebop
@deprecated
@deprecated("use X instead")
```

Deprecated message fields are skipped during encoding and decoding. This allows removing fields from the wire format without changing tags:

```bebop
message Request {
    @deprecated("no longer used")
    old_field(1): string;   // not encoded, not decoded
    new_field(2): string;   // active
}
```

Cannot be applied to struct fields. Struct fields are positional, so any change breaks wire compatibility.

**`@flags`** - marks an enum as a bit flags enum where values can be combined with bitwise OR.

```bebop
@flags
enum Permissions : uint8 {
    READ = 1;
    WRITE = 2;
    EXECUTE = 4;
}
```

### Custom decorators

Define decorators with `#decorator`:

```bebop
#decorator(range) {
    targets = FIELD
    param min!: int32
    param max!: int32

    validate [[
        if min >= max then
            error("min must be less than max")
        end
    ]]

    export [[
        return {
            constraint = "range",
            range_min = min,
            range_max = max,
            width = max - min
        }
    ]]
}
```

**Syntax elements**:

- `targets` - where the decorator can be applied. Combine with `|`: `FIELD | MEMBER`
  - `ENUM`, `STRUCT`, `MESSAGE`, `UNION`, `SERVICE` - definition types
  - `FIELD` - struct or message fields
  - `MEMBER` - enum members
  - `BRANCH` - union branches
  - `METHOD` - service methods
  - `ALL` - any target
- `param name!: Type` - required parameter (note the `!`)
- `param name?: Type` - optional parameter (note the `?`)
- `validate [[ ... ]]` - Lua code run at compile time to validate usage
- `export [[ ... ]]` - Lua code that returns data for plugins

### Validate blocks

Validate blocks contain Lua code that runs when the decorator is used. Use `error()` to reject invalid usage or `warn()` for non-fatal issues.

**Error and warning functions**:

```lua
error("message")                   -- error at decorator usage site
error("message", self.span)        -- error at decorator usage site (explicit)
error("message", self.min.span)    -- error at parameter location
warn("message")                    -- warning at decorator usage site
warn("message", self.max.span)     -- warning at parameter location
```

**Function arguments**:

Validate and export blocks receive these arguments:

1. `self` - metadata table with:
   - `self.span` - decorator usage location
   - `self.<param>.span` - each parameter's source location
2. `target` - information about what the decorator is applied to
3. Parameter values directly by name (`min`, `max`, etc.)

**Target fields**:

- `target.kind` - `"field"`, `"struct"`, `"enum"`, `"message"`, `"union"`, `"service"`, `"method"`, `"member"`, `"branch"`
- `target.name` - name of the decorated element
- `target.parent_kind` - containing definition kind (for fields, members, branches)
- `target.fqn` - fully qualified name (for definitions)
- `target.def_kind` - definition kind (for definitions)

**Span table fields**: `off` (byte offset), `len` (length), `start_line`, `start_col`

```bebop
#decorator(deprecated) {
    targets = ALL
    param reason?: string

    validate [[
        if target.kind == "field" and target.parent_kind == "struct" then
            error("Cannot deprecate struct fields")
        end
    ]]
}

#decorator(positive) {
    targets = FIELD
    param value!: int32

    validate [[
        if value <= 0 then
            error("value must be positive", self.value.span)
        end
    ]]
}
```

### Export blocks

Export blocks return structured data that plugins can access. Return a Lua table with the values you want to expose:

```bebop
#decorator(indexed) {
    targets = FIELD
    param key!: string
    param unique?: bool

    validate [[
        local valid_keys = { id = true, name = true, timestamp = true }
        if not valid_keys[key] then
            error("invalid index key: " .. key)
        end
    ]]

    export [[
        return {
            index_key = key,
            is_unique = unique or false
        }
    ]]
}
```

Plugins receive the exported table and can use it to generate appropriate code.

### Qualified decorator names

Reference decorators from imported packages:

```bebop
import "validators.bop"

struct Bounded {
    @validators.range(min: 0, max: 100)
    percentage: float32;
}
```

## Visibility

### Top-level visibility

Top-level definitions are exported by default. Use `local` to make them file-private:

```bebop
struct PublicType {}           // accessible from other files

local struct PrivateType {}    // only accessible in this file

const int32 PUBLIC_VALUE = 1;        // exported
local const int32 PRIVATE_VALUE = 2; // file-private
```

### Nested visibility

Nested definitions are local by default. Use `export` to make them accessible:

```bebop
struct Outer {
    struct LocalInner {}           // not accessible outside Outer

    export struct PublicInner {}   // accessible as Outer.PublicInner
}
```

### Union branch visibility

```bebop
union Result {
    Success(1): { value: string; };    // local by default

    export Error(2): {                 // accessible as Result.Error
        code: int32;
    };
}
```

## Nested types

Structs, messages, and unions can contain nested type definitions.

```bebop
struct Container {
    id: int32;

    struct Metadata {
        key: string;
        value: string;
    }

    enum Status {
        UNKNOWN = 0;
        ACTIVE = 1;
    }

    export struct Point {
        x: float32;
        y: float32;
    }
}
```

### Accessing nested types

Use qualified names:

```bebop
struct UsesNested {
    point: Container.Point;
    status: Container.Status;
}
```

### Nesting depth

No limit on nesting depth:

```bebop
struct Level1 {
    struct Level2 {
        struct Level3 {
            value: int32;
        }
    }
}
```

## Reserved words

Keywords: `enum`, `struct`, `message`, `union`, `service`, `const`, `mut`, `export`, `local`, `map`, `array`, `stream`, `import`, `edition`, `package`, `true`, `false`, `with`

These cannot be used as definition names. Using them as field names is allowed but discouraged; plugins may rename them to avoid conflicts with target language keywords.

## Identifiers

- Start with letter or underscore
- Contain letters, digits, underscores
- Case-sensitive

```bebop
struct User {}
struct _Internal {}
struct User2 {}
```

## Qualified names

Dot-separated identifiers for packages and nested types:

```bebop
my.app.models.User
Container.InnerType
package.Outer.Inner
```

## Common patterns

### Request/response

```bebop
message GetUserRequest {
    user_id(1): string;
}

message GetUserResponse {
    user(1): User;
    error(2): string;
}

service UserService {
    GetUser(GetUserRequest): GetUserResponse;
}
```

### Error handling with unions

```bebop
union Result {
    Success(1): {
        data: string;
    };
    Error(2): {
        code: int32;
        error_message: string;
    };
}
```

### Bit flags

```bebop
import "bebop/decorators.bop"

@flags
enum FileMode : uint8 {
    NONE = 0;
    READ = 1;
    WRITE = 2;
    EXECUTE = 4;
    READ_WRITE = READ | WRITE;
    ALL = READ | WRITE | EXECUTE;
}
```

### Optional fields

Messages inherently support optional fields. Unset fields are simply not present in the wire format:

```bebop
message UserProfile {
    name(1): string;
    bio(2): string;
    avatar(3): byte[];
}
```

Unlike Protocol Buffers, Bebop's optional fields are detectable: you can distinguish "field not set" from "field set to default value." Generated code provides `has_value` or equivalent checks for each message field.

### Collections of complex types

```bebop
struct Team {
    members: User[];
    lookup: map[string, User];
    matrix: User[][];
}
```

## Validation rules

The compiler enforces these rules:

1. **Enum zero value** - every enum must have a member with value 0
2. **Unique enum values** - no duplicate values within an enum
3. **Unique message tags** - field tags must be unique and positive (1-255)
4. **Unique union discriminators** - branch discriminators must be unique (0-255)
5. **Valid map keys** - only supported primitive types (no floats, timestamp, duration, or composite types)
6. **No union nesting** - unions cannot contain unions
7. **File order** - edition, then package, then imports, then definitions
8. **Unique names** - no duplicate field/member names within a container
9. **No cycles** - type references cannot form cycles (except messages referencing themselves)
10. **Service method types** - request/response must be struct, message, or union

## Complete example

```bebop
edition = "2026"
package example.chat

import "bebop/decorators.bop"

const string DEFAULT_ROOM = "general";
const int32 MAX_MESSAGE_LENGTH = 4096;

enum MessageType : uint8 {
    TEXT = 0;
    IMAGE = 1;
    FILE = 2;
    SYSTEM = 3;
}

@flags
enum UserStatus : uint8 {
    NONE = 0;
    ONLINE = 1;
    AWAY = 2;
    DO_NOT_DISTURB = 4;
}

struct User {
    id: uuid;
    name: string;
    status: UserStatus;
}

message ChatMessage {
    id(1): uuid;
    author(2): User;
    content(3): string;
    message_type(4): MessageType;
    sent_at(5): timestamp;
    attachments(6): byte[][];
}

message JoinRoomRequest {
    room_id(1): string;
}

message SubscribeRequest {
    user_id(1): uuid;
}

union Event {
    Message(1): ChatMessage;
    UserJoined(2): {
        user: User;
        room: string;
    };
    UserLeft(3): {
        user_id: uuid;
        room: string;
    };
    RoomCreated(4): message {
        room_id(1): string;
        creator(2): User;
    };
}

service ChatService {
    SendMessage(ChatMessage): ChatMessage;
    JoinRoom(JoinRoomRequest): stream Event;
    Subscribe(stream SubscribeRequest): stream Event;
}
```
