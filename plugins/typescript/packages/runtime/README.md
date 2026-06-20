# @bebop/runtime

Tree-shakeable runtime primitives for Bebop-generated TypeScript.

The runtime is plain browser-safe ESM. It does not use Bun, Node, `Buffer`,
`process`, or `require`. The generator executable may use Bun; this package
must not.

## Requirements

This is a modern JavaScript runtime package. Missing platform features are hard
errors rather than polyfilled behavior.

- `DataView.getFloat16` / `DataView.setFloat16` are required for scalar
  `float16`.
- `Float16Array` is required for `float16[]`.
- Bulk typed-array paths assume a little-endian host, matching Bebop's wire
  format and target platforms.

## Arrays

Bebop scalar arrays use typed arrays where JavaScript has a native type:

- `byte[]` -> `Uint8Array`
- `int32[]` -> `Int32Array`
- `uint64[]` -> `BigUint64Array`
- `float16[]` -> `Float16Array`
- `float32[]` -> `Float32Array`
- `bfloat16[]` -> `BFloat16Array`

`BFloat16Array` follows native typed-array constructor behavior:

```ts
new BFloat16Array(8);              // allocate 8 values
new BFloat16Array(buffer);         // live view over ArrayBuffer
new BFloat16Array([1, 2, 3]);      // copy and convert numeric values
BFloat16Array.from(values);        // copy and convert values
```

Raw bfloat16 bit patterns are explicit:

```ts
BFloat16Array.from(bits, { as: "bit-patterns" });              // copy bits
BFloat16Array.from(bits, { as: "bit-patterns", copy: false }); // live view
```

Use `values()` or `getBitPattern()` in hot loops. `get()` and the default
iterator allocate `BFloat16` wrapper objects.
