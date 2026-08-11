import { readFileSync } from "node:fs";

const version = readFileSync(new URL("../../../VERSION", import.meta.url), "utf8").trim();
const packageNames = ["runtime", "plugin", "generator"];

for (const name of packageNames) {
  const path = new URL(`../packages/${name}/package.json`, import.meta.url);
  const manifest = JSON.parse(readFileSync(path, "utf8"));
  if (manifest.version !== version) {
    throw new Error(`${manifest.name} version ${manifest.version} does not match VERSION ${version}`);
  }
  for (const [dependency, range] of Object.entries(manifest.dependencies ?? {})) {
    if (dependency.startsWith("@bebop/") && range !== `^${version}`) {
      throw new Error(`${manifest.name} dependency ${dependency}@${range} must be ^${version}`);
    }
  }
}
