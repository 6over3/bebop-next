import { execFileSync } from "node:child_process";
import { mkdirSync, rmSync } from "node:fs";
import { fileURLToPath } from "node:url";

const workspace = fileURLToPath(new URL("../", import.meta.url));
const output = fileURLToPath(new URL("../dist/npm/", import.meta.url));
const packages = ["runtime", "plugin", "generator"];

rmSync(output, { recursive: true, force: true });
mkdirSync(output, { recursive: true });

for (const name of packages) {
  execFileSync(
    "npm",
    ["pack", `./packages/${name}`, "--pack-destination", output],
    { cwd: workspace, stdio: "inherit" },
  );
}
