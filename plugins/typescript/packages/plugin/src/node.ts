import { readFileSync, writeFileSync } from "node:fs";
import type { BebopPlugin } from "./sdk.js";
import { executePlugin } from "./sdk.js";

/** Run a Bebop plugin over stdin/stdout using Node's portable file-descriptor APIs. */
export async function runPlugin(plugin: BebopPlugin): Promise<void> {
  writeFileSync(1, await executePlugin(plugin, readFileSync(0)));
}
