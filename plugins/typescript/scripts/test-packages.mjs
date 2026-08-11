import { spawnSync } from "node:child_process";
import { mkdtempDisposableSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

if (Number.parseInt(process.versions.node, 10) < 26) {
  throw new Error(`package smoke tests require Node 26 or newer; received ${process.version}`);
}

const npm = process.platform === "win32" ? "npm.cmd" : "npm";
const workspace = fileURLToPath(new URL("..", import.meta.url));
using temporary = mkdtempDisposableSync(join(tmpdir(), "bebop-typescript-packages-"));

const archives = ["runtime", "plugin", "generator"].map((name) => {
  const output = run(npm, [
    "pack",
    "--json",
    "--pack-destination",
    temporary.path,
    join(workspace, "packages", name),
  ]);
  const result = JSON.parse(output);
  const filename = result[0]?.filename;
  if (typeof filename !== "string") throw new Error(`npm pack did not return a filename for ${name}`);
  return join(temporary.path, filename);
});

const consumer = join(temporary.path, "consumer");
mkdirSync(consumer);
writeFileSync(join(consumer, "package.json"), JSON.stringify({ private: true, type: "module" }));
run(npm, ["install", "--ignore-scripts", "--silent", ...archives], consumer);
run(process.execPath, [
  "--input-type=module",
  "--eval",
  `
    import { BebopWriter, utf8ByteLength } from "@bebop/runtime/wire";
    import { ResponseBuilder } from "@bebop/plugin/sdk";
    import { generate } from "@bebop/generator-typescript";
    await Promise.all([
      "any", "bfloat16", "codec", "empty", "error", "json", "message",
      "reflection", "rpc", "rpc/protocol", "rpc/batch", "rpc/buffered-payload",
      "rpc/channel", "rpc/context", "rpc/error", "rpc/frame", "rpc/future",
      "rpc/future-storage", "rpc/payload", "rpc/router", "rpc/service",
      "temporal", "uuid", "view", "wire",
    ].map((path) => import(\`@bebop/runtime/\${path}\`)));
    await Promise.all([
      "descriptor", "node", "protocol", "sdk",
    ].map((path) => import(\`@bebop/plugin/\${path}\`)));
    if (new BebopWriter().length !== 0) throw new Error("runtime export failed");
    if (utf8ByteLength("😀") !== 4) throw new Error("runtime subpath failed");
    if (new ResponseBuilder().build() === undefined) throw new Error("plugin SDK export failed");
    if (typeof generate !== "function") throw new Error("generator export failed");
  `,
], consumer);

const entry = join(consumer, "tree-shaking.js");
const bundle = join(consumer, "tree-shaking.bundle.js");
writeFileSync(entry, `
  import { BebopWriter } from "@bebop/runtime";
  export function encodeByte(value) {
    const writer = new BebopWriter(1);
    writer.writeByte(value);
    return writer.toArrayView();
  }
`);
run("bun", ["build", entry, "--outfile", bundle, "--minify"], consumer);
const bundled = readFileSync(bundle, "utf8");
for (const excluded of ["BebopRouter", "InMemoryFutureStorage"]) {
  if (bundled.includes(excluded)) throw new Error(`runtime root export retained unused ${excluded}`);
}
if (bundled.length > 16 * 1024) {
  throw new Error(`tree-shaken BebopWriter bundle is unexpectedly large: ${bundled.length} bytes`);
}

function run(command, args, cwd = workspace) {
  const result = spawnSync(command, args, { cwd, encoding: "utf8" });
  if (result.status !== 0) {
    throw new Error([
      `${command} ${args.join(" ")} exited with ${result.status ?? "no status"}`,
      result.stdout,
      result.stderr,
    ].filter(Boolean).join("\n"));
  }
  return result.stdout;
}
