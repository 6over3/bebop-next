import pathlib
import sqlite3
import subprocess
import struct


root = pathlib.Path(__file__).parent
subprocess.run(["make", "-C", str(root)], check=True)
library = next(root.glob("bebopsqlite.*"))

descriptor = bytes.fromhex(
    "E000000001000000D60000000A00000073716C6974652E626F7000E803000002000000"
    "480000000204000000486F6D650004000000486F6D650000290000000100000018000000"
    "040000006369747900030000001001100000000009100D10000000000020210710010A"
    "13141701146B0000000306000000506572736F6E0006000000506572736F6E000048000000"
    "0200000018000000040000006E616D6500030000001001100100000009100D102200000004"
    "000000686F6D65000D0000001704000000486F6D650001411002000000091A0D100110010C"
    "17181702140F1315100110"
)


def string(value):
    raw = value.encode()
    return struct.pack("<I", len(raw)) + raw + b"\0"


def message(fields):
    fields = sorted(fields.items())
    payload = b"".join(value for _, value in fields)
    offsets = []
    position = 0
    for _, value in fields[:-1]:
        position += len(value)
        offsets.append(position)
    body = payload + bytes(offsets) + bytes(tag for tag, _ in fields) + bytes([len(fields) << 2])
    return struct.pack("<I", len(body)) + body

db = sqlite3.connect(":memory:")
db.enable_load_extension(True)
db.load_extension(str(library))
db.enable_load_extension(False)

functions = {
    row[0]
    for row in db.execute(
        "SELECT name FROM pragma_function_list WHERE name LIKE 'bebop_%'"
    )
}
expected = {
    "bebop_type", "bebop_pack", "bebop_raw", "bebop_spec", "bebop_typeof",
    "bebop_extract", "bebop_get", "bebop_kind", "bebop_exists", "bebop_length",
    "bebop_valid", "bebop_error", "bebop_branch", "bebop_enum_name",
    "bebop_map_get", "bebop_map_has", "bebop_from_json", "bebop_to_json",
    "bebop_set", "bebop_insert", "bebop_replace", "bebop_remove", "bebop_append",
    "bebop_map_set", "bebop_map_remove", "bebop_group_array", "bebop_group_map",
}
assert expected <= functions
assert db.execute("SELECT bebop_valid(NULL), bebop_error(NULL)").fetchone() == (None, None)
assert db.execute("SELECT bebop_extract(NULL, '$.name')").fetchone() == (None,)
assert db.execute("SELECT bebop_exists(NULL, '$.name')").fetchone() == (None,)
assert db.execute("SELECT bebop_valid(?)", (b"not a Bebop value",)).fetchone() == (0,)

spec = db.execute("SELECT bebop_type(?, 'Person')", (descriptor,)).fetchone()[0]
wire = message({1: string("Andrew"), 2: string("Tokyo")})
value = db.execute("SELECT bebop_pack(?, ?)", (wire, spec)).fetchone()[0]
assert db.execute("SELECT bebop_extract(?, '$.name')", (value,)).fetchone() == ("Andrew",)
assert db.execute("SELECT bebop_extract(?, '$.home.city')", (value,)).fetchone() == ("Tokyo",)
updated = db.execute("SELECT bebop_set(?, '$.home.city', 'Osaka')", (value,)).fetchone()[0]
assert db.execute("SELECT bebop_extract(?, '$.home.city')", (updated,)).fetchone() == ("Osaka",)
assert db.execute("SELECT bebop_raw(?)", (value,)).fetchone()[0] == wire
assert db.execute("SELECT count(*) FROM bebop_each(?)", (value,)).fetchone() == (2,)
assert db.execute("SELECT count(*) FROM bebop_tree(?)", (value,)).fetchone() == (4,)

for table in ("bebop_each", "bebop_tree"):
    assert db.execute(f"SELECT count(*) FROM {table}(NULL)").fetchone() == (0,)

try:
    db.execute("SELECT bebop_extract(?, '$.name')", (b"not a Bebop value",)).fetchone()
except sqlite3.DatabaseError:
    pass
else:
    raise AssertionError("malformed typed value was accepted")

print("PASS: extension loads, functions register, NULL/malformed handling works")
