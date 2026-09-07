# Bebop for SQLite

Store ordinary Bebop wire values in SQLite and query them with SQL. The value
contains its rooted descriptor, so a query never needs generated record code,
an application schema registry, or a repeated type-name argument.

## Build

```sh
make -C lab/sqlite
```

The Makefile uses `zig cc` and selects the native library suffix (`.so`,
`.dylib`, or `.dll`). Set `SQLITE_PREFIX` when SQLite is installed elsewhere:

```sh
make -C lab/sqlite SQLITE_PREFIX=/usr
```

Load the library by basename; SQLite resolves the platform suffix:

```sql
.load ./lab/sqlite/bebopsqlite
```

## Wire values

Assume `:descriptor` is a serialized Bebop `DescriptorSet` and `:wire` is an
ordinary serialized `example.Person` value produced by the application.

```sql
CREATE TABLE people (
  id INTEGER PRIMARY KEY,
  data BLOB NOT NULL CHECK (bebop_valid(data)),
  city TEXT GENERATED ALWAYS AS (bebop_extract(data, '$.home.city')) VIRTUAL
);
CREATE INDEX people_city ON people(city);

INSERT INTO people(data)
VALUES (bebop_pack(:wire, bebop_type(:descriptor, 'example.Person')));

SELECT id, bebop_extract(data, '$.name') AS name, city
FROM people WHERE city = 'Tokyo';

UPDATE people
SET data = bebop_set(data, '$.home.city', 'Osaka')
WHERE id = 1;

SELECT bebop_raw(data), bebop_spec(data), bebop_typeof(data)
FROM people;
```

`bebop_pack` validates and wraps the existing wire bytes. It does not decode and
re-encode them. `bebop_raw` returns those bytes unchanged. The descriptor and
root are needed only when creating the specification at the ingestion boundary.

## Functions

Paths follow SQLite's JSON path syntax: `$.field`, `$[0]`, `$[#-1]`, and quoted
names such as `$.'field name'` using SQLite's double-quoted path form.

### Function reference

#### `bebop_type(descriptor, root)`

Validates a serialized descriptor set and returns
a canonical rooted specification. Call it at ingestion, once per root type.
#### `bebop_pack(wire, spec)`

Validates ordinary Bebop wire bytes and wraps them in a
self-contained value without re-encoding the payload.

#### `bebop_raw(value)`

Returns the original root wire bytes.

#### `bebop_spec(value)`

Returns the embedded rooted specification.

#### `bebop_typeof(value [, path])`

Returns
the precise type expression at the root or selected path.

#### `bebop_extract(value, path)`

Follows a path and returns a SQLite scalar for
primitive values, raw BLOB bytes for byte arrays, and a typed BLOB for
containers.

#### `bebop_get(value, path)`

Always returns a typed BLOB, preserving the
selected value's exact wire representation.

#### `bebop_kind(value [, path])`

Returns the broad kind (`message`, `array`, `map`,
`integer`, `string`, or `bytes`).

#### `bebop_exists(value [, path])`

Returns 1 when the path exists and 0 when it does not.

#### `bebop_length(value [, path])`

Returns string/byte length or the count of an array, map, or record.

#### `bebop_valid(value)`

Performs complete validation and returns 1 or 0.

#### `bebop_error(value)`

Returns NULL when valid or a diagnostic string when invalid.

#### `bebop_branch(value [, path])`

Returns the active union branch name, or `#N` for an
unknown discriminator.

#### `bebop_enum_name(value [, path])`

Returns the declared enum member name, or NULL for an unnamed numeric value.

#### `bebop_map_get(value, key)`

Looks up a root map using its declared key type and
returns the result using extraction rules.

#### `bebop_map_has(value, key)`

Returns 1 when that key exists, including when its value is empty or false.

#### `bebop_from_json(json_text, spec)`

Performs strict, schema-directed JSON import.

#### `bebop_to_json(value)`

Emits the corresponding schema-directed JSON form. JSON
is an interchange format; existing wire values should use `bebop_pack`.

#### `bebop_set(value, path, replacement)`

Replaces a value or creates a declared
message field/map entry.

#### `bebop_insert(value, path, replacement)`

Creates an absent target and shifts later array elements.

#### `bebop_replace(value, path, replacement)`

Changes only a present target.

#### `bebop_remove(value, path)`

Removes an optional message field, map entry, or dynamic-array element.

#### `bebop_append(value, path, replacement)`

Appends to a dynamic array.

#### `bebop_map_set(value, key, replacement)`

Sets a root map entry using its declared key and value types.

#### `bebop_map_remove(value, key)`

Removes a root map entry.

All mutators return a new typed value and reject incompatible replacements,
required-field removal, fixed-array resizing, and missing parents.

#### `bebop_group_array(value, array_spec)`

Aggregates rows into a typed dynamic array.

#### `bebop_group_map(key, value, map_spec)`

Aggregates rows into a typed map
and rejects duplicate keys. The specification must be identical for every row.

Examples:

```sql
SELECT bebop_kind(data), bebop_length(data, '$.tags'),
       bebop_exists(data, '$.nickname'), bebop_error(data)
FROM people;

SELECT bebop_to_json(bebop_set(data, '$.home.city', 'Osaka')) FROM people;
SELECT bebop_group_array(tag, :tags_spec) FROM tags;
```

Containers returned by `bebop_get` or `bebop_extract` are complete typed values:

```sql
CREATE TABLE homes AS
SELECT bebop_get(data, '$.home') AS data FROM people;
SELECT bebop_extract(data, '$.city') FROM homes;
```

### `bebop_each(value [, path])`

Emits the direct children of a value.

### `bebop_tree(value [, path])`

Emits the root
and then walks descendants depth-first. Both expose `key`, `value`, `atom`,
`kind`, `type`, `id`, `parent`, `fullkey`, and `path`:

```sql
SELECT key, value, kind, fullkey
FROM people, bebop_each(data, '$.tags');

SELECT id, parent, fullkey, atom
FROM people, bebop_tree(data);
```

## Operators

The normal entry point does not replace SQLite's JSON operators. Connections
that want Bebop operators can load `sqlite3_bebopsqlite_operators_init`:

```sql
SELECT data -> 'home' ->> 'city' FROM people;
SELECT data ->> '$.home.city' FROM people;
```

`->` is `bebop_get`; `->>` is `bebop_extract`. Use SQLite's `json_extract` and
`jsonb_extract` for JSON values.

The default build links Bebop's wire runtime and descriptor decoder directly.
It has no compiler or Lua dependency. The storage envelope is experimental; the
implementation is contained in `bebop.c` and the build is defined by the single
Makefile.
