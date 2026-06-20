#!/usr/bin/env bun

import { CodeGeneratorResponse } from "@bebop/plugin";
import { encode } from "@bebop/runtime";
import { runGenerator } from "./generator";

void main();

async function main(): Promise<void> {
  try {
    const input = new Uint8Array(await Bun.stdin.arrayBuffer());
    Bun.stdout.write(runGenerator(input));
  } catch (error) {
    Bun.stdout.write(encode(CodeGeneratorResponse, {
      error: `bebopc-gen-typescript: ${error instanceof Error ? error.message : String(error)}`,
    }));
  }
}
