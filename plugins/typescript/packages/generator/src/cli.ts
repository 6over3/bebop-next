#!/usr/bin/env node

import { readFileSync, writeFileSync } from "node:fs";
import { runGenerator } from "./generator.js";

writeFileSync(1, runGenerator(readFileSync(0)));
