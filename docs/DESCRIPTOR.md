# Bebop descriptor format

Compiled representation of parsed, linked, and validated Bebop schemas. Plugins consume descriptors to emit target-language types. Runtimes use them for reflection.

Descriptors serialize using Bebop's own wire format. Any runtime that decodes Bebop records can decode a descriptor.

## Terminology

| Term | Meaning |
|------|---------|
| FQN | Fully-qualified name. Package and parent scopes, dot-separated: `mypackage.Outer.Inner` |
| Wire tag | Field index byte in a message (1-255). Tag 0 terminates the message. |
| Discriminator | Byte identifying a union branch on the wire (1-255). |
| Topological order | Definitions sorted so each appears after all types it references. |
| Fixed size | Struct with compile-time-known byte size. No length prefix on wire. |

## Structure

```
DescriptorSet
└── SchemaDescriptor[]         one per .bop file
    ├── path                   file path as given to compiler
    ├── package                package name (if declared)
    ├── edition                schema edition (EDITION_2026, etc.)
    ├── imports[]              import paths
    ├── definitions[]          all definitions, topologically sorted
    └── source_code_info       optional source positions and comments
```

Schemas are ordered so imports appear before the schemas that import them. Process `schemas[0..N]` in order and every dependency type appears before its first use.

## DescriptorSet

Root container. Passed to plugins and available for runtime reflection.

```bebop
message DescriptorSet {
    schemas(1): SchemaDescriptor[];
}
```

## SchemaDescriptor

One source file. Contains metadata and all definitions from that file.

```bebop
message SchemaDescriptor {
    path(1): string;
    package(2): string;
    edition(3): Edition;
    imports(4): string[];
    definitions(5): DefinitionDescriptor[];
    source_code_info(6): SourceCodeInfo;
}
```

### Fields

**path** - File path as provided to the compiler. May be relative or absolute.

**package** - Package declaration from source. Absent when no package was declared.

**edition** - Schema edition. Current value: `EDITION_2026` (1000).

**imports** - Import paths in source declaration order.

**definitions** - All definitions in topological dependency order. Process sequentially for single-pass code generation.

**source_code_info** - Source positions and comments. Only present when `BEBOP_DESC_FLAG_SOURCE_INFO` is set during compilation.

## DefinitionDescriptor

A named definition: enum, struct, message, union, service, const, or decorator.

```bebop
message DefinitionDescriptor {
    kind(1): DefinitionKind;
    name(2): string;
    fqn(3): string;
    documentation(4): string;
    visibility(5): Visibility;
    decorators(6): DecoratorUsage[];
    nested(7): DefinitionDescriptor[];

    enum_def(8): EnumDef;
    struct_def(9): StructDef;
    message_def(10): MessageDef;
    union_def(11): UnionDef;
    service_def(12): ServiceDef;
    const_def(13): ConstDef;
    decorator_def(14): DecoratorDef;
}
```

### Fields

**kind** - Which definition type. Determines which body field is populated.

| DefinitionKind | Value | Body field |
|----------------|-------|------------|
| ENUM | 1 | `enum_def` |
| STRUCT | 2 | `struct_def` |
| MESSAGE | 3 | `message_def` |
| UNION | 4 | `union_def` |
| SERVICE | 5 | `service_def` |
| CONST | 6 | `const_def` |
| DECORATOR | 7 | `decorator_def` |

**name** - Simple identifier from source: `ChatMessage`.

**fqn** - Fully-qualified name: `myapp.Outer.ChatMessage`.

**documentation** - Text from preceding `///` comments.

**visibility** - Access modifier.

| Visibility | Value | Meaning |
|------------|-------|---------|
| DEFAULT | 0 | Use scope default |
| EXPORT | 1 | Accessible from other files |
| LOCAL | 2 | File-private or parent-scope-private |

Top-level definitions default to EXPORT. Nested definitions default to LOCAL.

**decorators** - Decorators applied to this definition.

**nested** - Types declared inside this definition's body. Includes inline union branch definitions.

## TypeDescriptor

Describes a type reference. Used for field types, const types, method parameters, array elements, and map keys/values.

```bebop
message TypeDescriptor {
    kind(1): TypeKind;
    array_element(2): TypeDescriptor;
    fixed_array_element(3): TypeDescriptor;
    fixed_array_size(4): uint32;
    map_key(5): TypeDescriptor;
    map_value(6): TypeDescriptor;
    defined_fqn(7): string;
}
```

### TypeKind values

Scalars (1-19) have fixed wire sizes:

| TypeKind | Value | Wire size |
|----------|-------|-----------|
| BOOL | 1 | 1 byte |
| BYTE | 2 | 1 byte |
| INT8 | 3 | 1 byte |
| INT16 | 4 | 2 bytes |
| UINT16 | 5 | 2 bytes |
| INT32 | 6 | 4 bytes |
| UINT32 | 7 | 4 bytes |
| INT64 | 8 | 8 bytes |
| UINT64 | 9 | 8 bytes |
| INT128 | 10 | 16 bytes |
| UINT128 | 11 | 16 bytes |
| FLOAT16 | 12 | 2 bytes |
| FLOAT32 | 13 | 4 bytes |
| FLOAT64 | 14 | 8 bytes |
| BFLOAT16 | 15 | 2 bytes |
| STRING | 16 | variable |
| UUID | 17 | 16 bytes |
| TIMESTAMP | 18 | 12 bytes |
| DURATION | 19 | 12 bytes |

Compound types (20-23) carry additional structure:

| TypeKind | Value | Additional fields |
|----------|-------|-------------------|
| ARRAY | 20 | `array_element` |
| FIXED_ARRAY | 21 | `fixed_array_element`, `fixed_array_size` |
| MAP | 22 | `map_key`, `map_value` |
| DEFINED | 23 | `defined_fqn` |

### Recursive structure

TypeDescriptor is recursive. An `int32[][]` (array of arrays) encodes as:

```
kind = ARRAY
array_element.kind = ARRAY
array_element.array_element.kind = INT32
```

A `map[string, User]` encodes as:

```
kind = MAP
map_key.kind = STRING
map_value.kind = DEFINED
map_value.defined_fqn = "mypackage.User"
```

## Definition bodies

### EnumDef

```bebop
message EnumDef {
    base_type(1): TypeKind;
    members(2): EnumMemberDescriptor[];
    is_flags(3): bool;
}
```

**base_type** - Integer width. Valid values: BYTE, INT8, INT16, UINT16, INT32, UINT32, INT64, UINT64. Defaults to UINT32 when not specified in source.

**is_flags** - True when `@flags` decorator is applied.

Member values are stored as `uint64` regardless of base type. For signed base types, sign-extend from the base type's bit width. Value `0xFFFFFFFFFFFFFFFF` in an `int8` enum means -1.

### EnumMemberDescriptor

```bebop
message EnumMemberDescriptor {
    name(1): string;
    documentation(2): string;
    value(3): uint64;
    decorators(4): DecoratorUsage[];
    value_expr(5): string;
}
```

**value_expr** - Original expression text (e.g. `1 << 3`). Absent when value was a simple literal.

### StructDef

```bebop
message StructDef {
    fields(1): FieldDescriptor[];
    is_mutable(2): bool;
    fixed_size(3): uint32;
}
```

**is_mutable** - True when declared with `mut`. Controls readonly vs mutable fields in generated code.

**fixed_size** - Total wire byte size when all fields are fixed-size scalars or fixed arrays of scalars. Zero when any field is variable-size (string, dynamic array, map, message). Plugins use this to pre-allocate buffers.

### MessageDef

```bebop
message MessageDef {
    fields(1): FieldDescriptor[];
}
```

### FieldDescriptor

Used for both struct and message fields.

```bebop
message FieldDescriptor {
    name(1): string;
    documentation(2): string;
    type(3): TypeDescriptor;
    index(4): uint32;
    decorators(5): DecoratorUsage[];
}
```

**index** - Wire tag. Zero for struct fields (positional encoding). 1-255 for message fields (tagged encoding).

### UnionDef

```bebop
message UnionDef {
    branches(1): UnionBranchDescriptor[];
}
```

### UnionBranchDescriptor

```bebop
message UnionBranchDescriptor {
    discriminator(1): byte;
    documentation(2): string;
    inline_fqn(3): string;
    type_ref_fqn(4): string;
    name(5): string;
    decorators(6): DecoratorUsage[];
}
```

Two mutually exclusive modes:

**Inline branch** - Body declared in the union:
```bebop
union Shape { Circle(1): { radius: float32; }; }
```
Sets `inline_fqn = "mypackage.Shape.Circle"`. The inline definition lives in the parent DefinitionDescriptor's `nested` array.

**Type-reference branch** - References an existing type:
```bebop
union Shape { rect(2): Rect; }
```
Sets `type_ref_fqn = "mypackage.Rect"` and `name = "rect"`.

### ServiceDef

```bebop
message ServiceDef {
    methods(1): MethodDescriptor[];
}
```

### MethodDescriptor

```bebop
message MethodDescriptor {
    name(1): string;
    documentation(2): string;
    request_type(3): TypeDescriptor;
    response_type(4): TypeDescriptor;
    method_type(5): MethodType;
    id(6): uint32;
    decorators(7): DecoratorUsage[];
}
```

**method_type** - Streaming mode:

| MethodType | Value | Source syntax |
|------------|-------|---------------|
| UNARY | 1 | `Method(Req): Res` |
| SERVER_STREAM | 2 | `Method(Req): stream Res` |
| CLIENT_STREAM | 3 | `Method(stream Req): Res` |
| DUPLEX_STREAM | 4 | `Method(stream Req): stream Res` |

**id** - MurmurHash3 of `/ServiceName/MethodName`. Stable 32-bit routing key.

### ConstDef

```bebop
message ConstDef {
    type(1): TypeDescriptor;
    value(2): LiteralValue;
}
```

### DecoratorDef

```bebop
message DecoratorDef {
    targets(1): DecoratorTarget;
    allow_multiple(2): bool;
    params(3): DecoratorParamDef[];
    validate_source(4): string;
    export_source(5): string;
}
```

**targets** - Bitmask of element kinds this decorator can decorate:

| DecoratorTarget | Value |
|-----------------|-------|
| NONE | 0 |
| ENUM | 1 |
| STRUCT | 2 |
| MESSAGE | 4 |
| UNION | 8 |
| FIELD | 16 |
| SERVICE | 32 |
| METHOD | 64 |
| BRANCH | 128 |
| ALL | 255 |

**validate_source** - Lua source for compile-time validation.

**export_source** - Lua source that produces metadata for plugins.

## LiteralValue

Concrete value for constants, decorator arguments, and decorator parameter defaults.

```bebop
message LiteralValue {
    kind(1): LiteralKind;
    bool_value(2): bool;
    int_value(3): int64;
    float_value(4): float64;
    string_value(5): string;
    uuid_value(6): uuid;
    raw_value(7): string;
}
```

**kind** - Discriminates which value field is set.

| LiteralKind | Value |
|-------------|-------|
| BOOL | 1 |
| INT | 2 |
| FLOAT | 3 |
| STRING | 4 |
| UUID | 5 |

**raw_value** - Original text before `$(...)` expansion. Only set for string literals containing environment variable references.

## DecoratorUsage

A decorator applied to a definition, field, member, branch, or method.

```bebop
message DecoratorUsage {
    fqn(1): string;
    args(2): DecoratorArg[];
    export_data(3): map[string, LiteralValue];
}
```

**fqn** - Fully-qualified name of the decorator definition.

**args** - Arguments in declaration order.

**export_data** - Key-value pairs from the decorator's Lua export block. Plugins read these directly without re-running Lua.

## Source code info

Optional metadata mapping descriptor elements to source positions and comments. Increases descriptor size; omit unless needed for tooling.

### SourceCodeInfo

```bebop
message SourceCodeInfo {
    locations(1): Location[];
}
```

### Location

```bebop
message Location {
    path(1): int32[];
    span(2): int32[4];
    leading_comments(3): string;
    trailing_comments(4): string;
    detached_comments(5): string[];
}
```

**path** - Identifies which descriptor element this location describes. Encoded as pairs of (field_tag, index):

| Path | Element |
|------|---------|
| `[5, 0]` | `definitions[0]` |
| `[5, 0, 1, 2]` | `definitions[0].fields[2]` |
| `[5, 1, 2, 0]` | `definitions[1].members[0]` |

Field tag values:
- `5` = SchemaDescriptor.definitions
- `1` = StructDef.fields, MessageDef.fields, UnionDef.branches, ServiceDef.methods
- `2` = EnumDef.members

**span** - `[start_line, start_col, end_line, end_col]`. 1-based. Tabs advance columns to next multiple of 4.

**leading_comments** - Comments on preceding lines with no blank line separation.

**trailing_comments** - Comment on same line after the element.

**detached_comments** - Comment groups separated by blank lines.

## C API

### Build and serialize

```c
bebop_parse_result_t* result;
// ... parse schemas ...

bebop_descriptor_t* desc;
bebop_desc_flags_t flags = BEBOP_DESC_FLAG_SOURCE_INFO;
bebop_descriptor_build(result, flags, &desc);

const uint8_t* buf;
size_t len;
bebop_descriptor_encode(desc, &buf, &len);
// write buf to file

bebop_descriptor_free(desc);
```

### Decode and inspect

```c
bebop_context_t* ctx = bebop_context_new(NULL);

bebop_descriptor_t* desc;
bebop_descriptor_decode(ctx, buf, len, &desc);

uint32_t schema_count = bebop_descriptor_schema_count(desc);
for (uint32_t i = 0; i < schema_count; i++) {
    const bebop_descriptor_schema_t* s = bebop_descriptor_schema_at(desc, i);
    const char* path = bebop_descriptor_schema_path(s);
    const char* pkg = bebop_descriptor_schema_package(s);

    uint32_t def_count = bebop_descriptor_schema_def_count(s);
    for (uint32_t j = 0; j < def_count; j++) {
        const bebop_descriptor_def_t* d = bebop_descriptor_schema_def_at(s, j);
        bebop_def_kind_t kind = bebop_descriptor_def_kind(d);
        const char* name = bebop_descriptor_def_name(d);
        const char* fqn = bebop_descriptor_def_fqn(d);
        // ...
    }
}

bebop_descriptor_free(desc);
bebop_context_free(ctx);
```

### Accessor functions

**Schema level:**
- `bebop_descriptor_schema_count(desc)` - Number of schemas
- `bebop_descriptor_schema_at(desc, idx)` - Get schema by index
- `bebop_descriptor_schema_path(s)` - File path
- `bebop_descriptor_schema_package(s)` - Package name (NULL if none)
- `bebop_descriptor_schema_edition(s)` - Edition enum value
- `bebop_descriptor_schema_import_count(s)` - Number of imports
- `bebop_descriptor_schema_import_at(s, idx)` - Import path
- `bebop_descriptor_schema_def_count(s)` - Number of definitions
- `bebop_descriptor_schema_def_at(s, idx)` - Get definition

**Definition level:**
- `bebop_descriptor_def_kind(d)` - Definition kind
- `bebop_descriptor_def_name(d)` - Simple name
- `bebop_descriptor_def_fqn(d)` - Fully-qualified name
- `bebop_descriptor_def_documentation(d)` - Doc comment text
- `bebop_descriptor_def_visibility(d)` - Visibility modifier
- `bebop_descriptor_def_decorator_count(d)` - Decorator count
- `bebop_descriptor_def_decorator_at(d, idx)` - Get decorator
- `bebop_descriptor_def_nested_count(d)` - Nested definition count
- `bebop_descriptor_def_nested_at(d, idx)` - Get nested definition

**Struct/Message:**
- `bebop_descriptor_def_field_count(d)` - Field count
- `bebop_descriptor_def_field_at(d, idx)` - Get field
- `bebop_descriptor_def_is_mutable(d)` - True if mutable struct
- `bebop_descriptor_def_fixed_size(d)` - Fixed wire size (0 if variable)

**Enum:**
- `bebop_descriptor_def_member_count(d)` - Member count
- `bebop_descriptor_def_member_at(d, idx)` - Get member
- `bebop_descriptor_def_base_type(d)` - Integer base type
- `bebop_descriptor_def_is_flags(d)` - True if flags enum

**Union:**
- `bebop_descriptor_def_branch_count(d)` - Branch count
- `bebop_descriptor_def_branch_at(d, idx)` - Get branch

**Service:**
- `bebop_descriptor_def_method_count(d)` - Method count
- `bebop_descriptor_def_method_at(d, idx)` - Get method

**Const:**
- `bebop_descriptor_def_const_type(d)` - Type descriptor
- `bebop_descriptor_def_const_value(d)` - Literal value

**Decorator definition:**
- `bebop_descriptor_def_targets(d)` - Target bitmask
- `bebop_descriptor_def_allow_multiple(d)` - Allow multiple flag
- `bebop_descriptor_def_param_count(d)` - Parameter count
- `bebop_descriptor_def_param_at(d, idx)` - Get parameter
- `bebop_descriptor_def_validate_source(d)` - Validate Lua source
- `bebop_descriptor_def_export_source(d)` - Export Lua source

**Field:**
- `bebop_descriptor_field_name(f)` - Field name
- `bebop_descriptor_field_documentation(f)` - Doc comment
- `bebop_descriptor_field_type(f)` - Type descriptor
- `bebop_descriptor_field_index(f)` - Wire tag (0 for struct fields)
- `bebop_descriptor_field_decorator_count(f)` - Decorator count
- `bebop_descriptor_field_decorator_at(f, idx)` - Get decorator

**Enum member:**
- `bebop_descriptor_member_name(m)` - Member name
- `bebop_descriptor_member_documentation(m)` - Doc comment
- `bebop_descriptor_member_value(m)` - Value as uint64
- `bebop_descriptor_member_decorator_count(m)` - Decorator count
- `bebop_descriptor_member_decorator_at(m, idx)` - Get decorator

**Union branch:**
- `bebop_descriptor_branch_discriminator(b)` - Discriminator byte
- `bebop_descriptor_branch_documentation(b)` - Doc comment
- `bebop_descriptor_branch_inline_fqn(b)` - FQN if inline (NULL otherwise)
- `bebop_descriptor_branch_type_ref_fqn(b)` - FQN if type ref (NULL otherwise)
- `bebop_descriptor_branch_name(b)` - Branch name (for type refs)
- `bebop_descriptor_branch_decorator_count(b)` - Decorator count
- `bebop_descriptor_branch_decorator_at(b, idx)` - Get decorator

**Method:**
- `bebop_descriptor_method_name(m)` - Method name
- `bebop_descriptor_method_documentation(m)` - Doc comment
- `bebop_descriptor_method_request(m)` - Request type descriptor
- `bebop_descriptor_method_response(m)` - Response type descriptor
- `bebop_descriptor_method_type(m)` - Streaming mode
- `bebop_descriptor_method_id(m)` - MurmurHash3 routing ID
- `bebop_descriptor_method_decorator_count(m)` - Decorator count
- `bebop_descriptor_method_decorator_at(m, idx)` - Get decorator

**Type:**
- `bebop_descriptor_type_kind(t)` - Type kind
- `bebop_descriptor_type_element(t)` - Element type (arrays)
- `bebop_descriptor_type_fixed_size(t)` - Fixed array size
- `bebop_descriptor_type_key(t)` - Map key type
- `bebop_descriptor_type_value(t)` - Map value type
- `bebop_descriptor_type_fqn(t)` - FQN for DEFINED types

**Literal:**
- `bebop_descriptor_literal_kind(l)` - Literal kind
- `bebop_descriptor_literal_as_bool(l)` - Boolean value
- `bebop_descriptor_literal_as_int(l)` - Integer value
- `bebop_descriptor_literal_as_float(l)` - Float value
- `bebop_descriptor_literal_as_string(l)` - String value
- `bebop_descriptor_literal_as_uuid(l)` - UUID bytes
- `bebop_descriptor_literal_raw_value(l)` - Pre-expansion text

**Decorator usage:**
- `bebop_descriptor_usage_fqn(u)` - Decorator FQN
- `bebop_descriptor_usage_arg_count(u)` - Argument count
- `bebop_descriptor_usage_arg_name(u, idx)` - Argument name
- `bebop_descriptor_usage_arg_value(u, idx)` - Argument value
- `bebop_descriptor_usage_export_count(u)` - Export data count
- `bebop_descriptor_usage_export_key_at(u, idx)` - Export key
- `bebop_descriptor_usage_export_value_at(u, idx)` - Export value

**Source code info:**
- `bebop_descriptor_schema_source_code_info(s)` - Get source code info
- `bebop_descriptor_location_count(sci)` - Location count
- `bebop_descriptor_location_at(sci, idx)` - Get location
- `bebop_descriptor_location_path(loc, &count)` - Path array
- `bebop_descriptor_location_span(loc)` - Span array (4 int32s)
- `bebop_descriptor_location_leading(loc)` - Leading comment
- `bebop_descriptor_location_trailing(loc)` - Trailing comment
- `bebop_descriptor_location_detached_count(loc)` - Detached comment count
- `bebop_descriptor_location_detached_at(loc, idx)` - Detached comment

## Example

Source file `chat.bop`:

```bebop
edition = "2026"
package myapp

enum Role : uint8 {
    USER = 1;
    ADMIN = 2;
}

struct ChatMessage {
    sender: string;
    text: string;
    timestamp: uint64;
}
```

Produces a DescriptorSet with one SchemaDescriptor:

```
schemas[0].path = "chat.bop"
schemas[0].package = "myapp"
schemas[0].edition = EDITION_2026

schemas[0].definitions[0].kind = ENUM
schemas[0].definitions[0].name = "Role"
schemas[0].definitions[0].fqn = "myapp.Role"
schemas[0].definitions[0].enum_def.base_type = BYTE
schemas[0].definitions[0].enum_def.members[0].name = "USER"
schemas[0].definitions[0].enum_def.members[0].value = 1
schemas[0].definitions[0].enum_def.members[1].name = "ADMIN"
schemas[0].definitions[0].enum_def.members[1].value = 2

schemas[0].definitions[1].kind = STRUCT
schemas[0].definitions[1].name = "ChatMessage"
schemas[0].definitions[1].fqn = "myapp.ChatMessage"
schemas[0].definitions[1].struct_def.fixed_size = 0
schemas[0].definitions[1].struct_def.fields[0].name = "sender"
schemas[0].definitions[1].struct_def.fields[0].type.kind = STRING
schemas[0].definitions[1].struct_def.fields[0].index = 0
schemas[0].definitions[1].struct_def.fields[1].name = "text"
schemas[0].definitions[1].struct_def.fields[1].type.kind = STRING
schemas[0].definitions[1].struct_def.fields[1].index = 0
schemas[0].definitions[1].struct_def.fields[2].name = "timestamp"
schemas[0].definitions[1].struct_def.fields[2].type.kind = UINT64
schemas[0].definitions[1].struct_def.fields[2].index = 0
```

Role appears before ChatMessage because definitions are topologically sorted. Role has no dependencies. If ChatMessage had a `Role` field, the ordering would still be correct.
